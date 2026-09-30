// tesis.ino — Vitalify: wearable IoMT (ESP32-S3 Zero)
//
// Mide FC y SpO2 (MAX30102), ECG (AD8232), temperatura (DS18B20) y caídas (ADXL345), los muestra
// en dos OLED y los envía al backend por MQTT según el contrato de telemetría.
//
//   Núcleo 1: este loop (sensores, algoritmos y pantallas) y la tarea que muestrea el ECG a 250 Hz.
//   Núcleo 0: la tarea de red (Wi-Fi, NTP, MQTT y búfer offline). La red nunca frena al loop.
//
// Configuración: config.h (dispositivo, red, pines) y secretos.h (redes Wi-Fi y clave MQTT).
// Monitor serie a 115200: cada 5 s imprime el estado; la tecla 'p' activa el trazado del ECG
// para el Serial Plotter.

#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <esp_timer.h>
#include <time.h>

#include "caidas.h"
#include "config.h"
#include "ecg.h"
#include "pantallas.h"
#include "ppg.h"
#include "red.h"
#include "sensores.h"
#include "telemetria.h"

// El último parámetro deja el bus en 400 kHz después de dibujar (la librería lo bajaba a 100 kHz
// y hacía más lentas las lecturas del MAX30102 y del ADXL345, que comparten el bus 0).
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000, 400000);
Adafruit_SSD1306 oledEcg(128, 32, &Wire1, -1, 400000, 400000);

ParametrosPpg parametrosPpg() {
  ParametrosPpg p;
  p.dedoSalto = DEDO_SALTO;
  p.dedoMinimo = DEDO_MINIMO;
  p.dedoSalida = DEDO_SALIDA;
  p.spo2A = SPO2_A;
  p.spo2B = SPO2_B;
  p.spo2C = SPO2_C;
  return p;
}
ParametrosEcg parametrosEcg() {
  ParametrosEcg p;
  p.frecuenciaRed = ECG_FRECUENCIA_RED_HZ;
  return p;
}

ProcesadorPpg ppg(parametrosPpg());
ProcesadorEcg ecg(parametrosEcg());
DetectorCaidas caidas;
BarridoEcg barrido;

bool oledOk = false, oledEcgOk = false, ppgOk = false, acelOk = false;
uint32_t tDatosPpg = 0, tDatosAcel = 0, tReintento = 0;
uint32_t msLatidoPpg = 0, msLatidoEcg = 0, msCaida = 0;
bool caidaPorEnviar = false, trazarEcg = false;
uint32_t seq = 0;
int64_t ultimaMedicionUs = 0;

// FC: la del ECG si los electrodos dan un ritmo confiable; si no, la del oxímetro
int fcActual(bool* delEcg) {
  *delEcg = ecg.resultado().fc > 0;
  return *delEcg ? ecg.resultado().fc : ppg.resultado().fc;
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // con el monitor cerrado, imprimir nunca bloquea el loop
  Serial.printf("\n=== Vitalify %s | %s ===\n", FIRMWARE_VERSION, CODIGO_DISPOSITIVO);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire1.begin(PIN_I2C2_SDA, PIN_I2C2_SCL, 400000);
  // periphBegin = false: los buses ya están iniciados con sus pines
  oledOk = i2cPresente(Wire, 0x3C) && oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  oledEcgOk = i2cPresente(Wire1, 0x3C) && oledEcg.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  ppgOk = ppgIniciar(Wire, PPG_CORRIENTE_LED);
  acelOk = acelIniciar(Wire);
  tempIniciar(PIN_DS18B20);
  tempActualizar(millis());
  ecgIniciar(PIN_ECG_SALIDA, PIN_ECG_LO_MAS, PIN_ECG_LO_MENOS);
  redIniciar();  // empieza a conectarse mientras se muestra el chequeo de arranque

  Serial.printf("OLED %s | OLED ECG %s | MAX30102 %s | ADXL345 %s | DS18B20 %s\n", oledOk ? "OK" : "FALTA",
                oledEcgOk ? "OK" : "FALTA", ppgOk ? "OK" : "FALTA", acelOk ? "OK" : "FALTA",
                tempPresente() ? "OK" : "FALTA");
  if (oledOk) {
    VistaArranque a;
    a.version = FIRMWARE_VERSION;
    a.codigo = CODIGO_DISPOSITIVO;
    a.ppg = ppgOk;
    a.acelerometro = acelOk;
    a.temperatura = tempPresente();
    a.oledEcg = oledEcgOk;
    dibujarArranque(oled, a);
    oled.display();
  }
  delay(1500);
  tDatosPpg = tDatosAcel = millis();
  enableLoopWDT();  // si el loop se colgara más de 5 s, el ESP32 se reinicia solo
}

void atenderPpg(uint32_t ahora) {
  if (!ppgOk) return;
  uint32_t ir[32], rojo[32];
  uint8_t perdidas = 0;
  const int n = ppgLeer(ir, rojo, 32, &perdidas);
  if (perdidas) ppg.saltar(perdidas);
  for (int i = 0; i < n; i++)
    if (ppg.procesar(ir[i], rojo[i])) msLatidoPpg = ahora;
  if (n > 0) {
    tDatosPpg = ahora;
  } else if (ahora - tDatosPpg > 2000) {
    ppgOk = false;
    Serial.println("MAX30102 sin datos: se reintentara (revisa SDA=5/SCL=6)");
  }
}

void atenderAcelerometro(uint32_t ahora) {
  if (!acelOk) return;
  float ax[32], ay[32], az[32];
  const int n = acelLeer(ax, ay, az, 32);
  for (int i = 0; i < n; i++) {
    if (caidas.procesar(ax[i], ay[i], az[i])) {
      msCaida = ahora;
      caidaPorEnviar = true;
      Serial.println("*** CAIDA DETECTADA: se envia al backend de inmediato ***");
    }
  }
  if (n > 0) {
    tDatosAcel = ahora;
  } else if (ahora - tDatosAcel > 2000) {
    acelOk = false;
    Serial.println("ADXL345 sin datos: se reintentara");
  }
}

void atenderEcg(uint32_t ahora) {
  MuestraEcg m[64];
  int n;
  while ((n = ecgLeer(m, 64)) > 0) {
    for (int i = 0; i < n; i++) {
      bool latido = false;
      const float v = ecg.procesar(m[i].adc, m[i].conectados, &latido);
      if (latido) msLatidoEcg = ahora;
      if (m[i].conectados) barrido.agregar(v);
      if (trazarEcg) Serial.printf("ecg:%.0f\n", v);
    }
  }
  static bool conectadosAntes = false;
  const bool conectados = ecg.resultado().electrodos;
  if (conectados != conectadosAntes) {
    conectadosAntes = conectados;
    if (!conectados) barrido.limpiar();
    Serial.printf("ECG: electrodos %s\n", conectados ? "conectados" : "desconectados");
  }
}

// Sensores que no respondieron (cable flojo, conectado en caliente): reintento cada 5 s
void reintentarSensores(uint32_t ahora) {
  if (ahora - tReintento < 5000) return;
  tReintento = ahora;
  if (!ppgOk && (ppgOk = ppgIniciar(Wire, PPG_CORRIENTE_LED))) {
    tDatosPpg = ahora;
    Serial.println("MAX30102 conectado");
  }
  if (!acelOk && (acelOk = acelIniciar(Wire))) {
    tDatosAcel = ahora;
    Serial.println("ADXL345 conectado");
  }
  if (!oledOk && i2cPresente(Wire, 0x3C)) oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  if (!oledEcgOk && i2cPresente(Wire1, 0x3C)) oledEcgOk = oledEcg.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
}

// Una lectura cada INTERVALO_TELEMETRIA_MS y otra en el instante en que se confirma una caída
void emitirTelemetria(uint32_t ahora) {
  static uint32_t proxima = INTERVALO_TELEMETRIA_MS;
  const bool toca = (int32_t)(ahora - proxima) >= 0;
  if (!toca && !caidaPorEnviar) return;
  if (toca) proxima = ahora + INTERVALO_TELEMETRIA_MS;

  Lectura l;
  l.monoUs = esp_timer_get_time();
  if (l.monoUs - ultimaMedicionUs < 1000) l.monoUs = ultimaMedicionUs + 1000;  // nunca dos con el mismo ts
  bool delEcg;
  const int fc = fcActual(&delEcg);
  if (fc > 0) l.fc = (int16_t)fc;
  if (ppg.resultado().spo2 > 0) l.spo2 = (int16_t)ppg.resultado().spo2;
  if (tempValida()) l.tempDecimas = (int16_t)lroundf(tempCelsius() * 10.0f);
  l.caida = caidaPorEnviar;
  caidaPorEnviar = false;
  if (!l.tieneDatos()) return;  // sin paciente conectado no hay nada que enviar
  ultimaMedicionUs = l.monoUs;
  l.seq = seq++;
  if (!redEncolar(l)) Serial.println("Cola de red llena: lectura descartada");
}

int8_t barrasWifi(const EstadoRed& r) {
  if (r.wifi != EstadoWifi::Conectado) return -1;
  if (r.rssi >= -55) return 4;
  if (r.rssi >= -65) return 3;
  if (r.rssi >= -75) return 2;
  return r.rssi >= -85 ? 1 : 0;
}

void actualizarPantallas(uint32_t ahora) {
  static uint32_t tPrincipal = 0, tEcg = 0;
  if (oledEcgOk && ahora - tEcg >= 40) {  // 25 cuadros/s: el barrido se ve fluido
    tEcg = ahora;
    VistaEcg v;
    v.electrodos = ecg.resultado().electrodos;
    v.fc = ecg.resultado().fc;
    v.pulso = ahora - msLatidoEcg < 150;
    v.ms = ahora;
    dibujarEcg(oledEcg, barrido, v);
    oledEcg.display();
  }
  if (oledOk && ahora - tPrincipal >= 200) {  // 5 cuadros/s bastan para números
    tPrincipal = ahora;
    const EstadoRed red = redEstado();
    const ResultadoPpg& p = ppg.resultado();
    VistaPrincipal v;
    v.wifiBarras = barrasWifi(red);
    switch (red.broker) {
      case EstadoBroker::Conectado:
        v.broker = 2;
        break;
      case EstadoBroker::Conectando:
        v.broker = 1;
        break;
      case EstadoBroker::Rechazado:
        v.broker = 3;
        break;
      default:
        v.broker = 0;
    }
    v.pendientes = red.pendientes;
    const time_t t = time(nullptr);
    struct tm local;
    if (red.horaSincronizada && localtime_r(&t, &local)) {
      v.horaValida = true;
      v.hora = (uint8_t)local.tm_hour;
      v.minuto = (uint8_t)local.tm_min;
    }
    v.sensorPpg = ppgOk;
    v.ppg = p.estado;
    v.progreso = p.progreso;
    v.fc = fcActual(&v.fcDelEcg);
    v.spo2 = p.spo2;
    v.pulso = ahora - (v.fcDelEcg ? msLatidoEcg : msLatidoPpg) < 150;
    v.sondaTemp = tempPresente();
    v.tempValida = tempValida();
    v.temp = tempCelsius();
    v.acelerometro = acelOk;
    v.movimiento = caidas.actividad() == Actividad::Movimiento;
    v.alertaCaida = msCaida != 0 && ahora - msCaida < 30000;
    v.codigo = CODIGO_DISPOSITIVO;
    v.avisoRed = red.aviso[0] != 0;
    v.red = v.avisoRed ? red.aviso : (red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando");
    v.ms = ahora;
    dibujarPrincipal(oled, v);
    oled.display();
  }
}

void atenderSerial(uint32_t ahora) {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == 'p' || c == 'P') {
      trazarEcg = !trazarEcg;
      if (!trazarEcg) Serial.println("Trazado del ECG apagado");
    }
  }
  static uint32_t tLog = 0;
  if (trazarEcg || ahora - tLog < 5000) return;
  tLog = ahora;

  const EstadoRed red = redEstado();
  const ResultadoPpg& p = ppg.resultado();
  bool delEcg;
  const int fc = fcActual(&delEcg);
  char temp[12];
  if (tempValida())
    snprintf(temp, sizeof temp, "%.1f C", tempCelsius());
  else
    strcpy(temp, tempPresente() ? "--" : "sin sonda");
  const char* mqtt = red.broker == EstadoBroker::Conectado    ? "OK"
                     : red.broker == EstadoBroker::Rechazado ? "usuario/clave rechazados"
                     : red.broker == EstadoBroker::Conectando ? "conectando"
                                                              : "--";
  Serial.printf("FC %d (%s) | SpO2 %d | dedo %s IR %lu PI %.2f%% | T %s | %s | ECG %s | Wi-Fi %s %d dBm | "
                "MQTT %s | pendientes %u, enviadas %lu\n",
                fc, delEcg ? "ECG" : "PPG", p.spo2, ppg.hayDedo() ? "SI" : "NO", (unsigned long)ppg.ultimoIr(),
                p.perfusion, temp, caidas.actividad() == Actividad::Movimiento ? "movimiento" : "reposo",
                ecg.resultado().electrodos ? "conectado" : "sin electrodos",
                red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando", red.rssi, mqtt, red.pendientes,
                (unsigned long)red.enviadas);
}

void loop() {
  const uint32_t ahora = millis();
  atenderPpg(ahora);
  atenderAcelerometro(ahora);
  atenderEcg(ahora);
  tempActualizar(ahora);
  reintentarSensores(ahora);
  emitirTelemetria(ahora);
  actualizarPantallas(ahora);
  atenderSerial(ahora);
}
