// tesis.ino — Vitalify: wearable IoMT (ESP32 DevKit o ESP32-S3 Zero)
//
// Mide FC y SpO2 (MAX30102), ECG (AD8232), temperatura (DS18B20) y caídas (ADXL345), los muestra
// en dos OLED (signos en la principal, ECG en la de 128x32) y los envía al backend por MQTT según
// el contrato de telemetría. Un LED verde encendido indica que todo funciona.
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
#include "sesion.h"
#include "telemetria.h"
#include "temperatura.h"

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

ParametrosTemperatura parametrosTemperatura() {
  ParametrosTemperatura p;
  p.minimoContacto = TEMP_MIN_CONTACTO;
  p.correccion = TEMP_CORRECCION_C;
  return p;
}

ProcesadorPpg ppg(parametrosPpg());
EvaluadorTemperatura temperatura(parametrosTemperatura());
SesionPaciente sesion;
ProcesadorEcg ecg(parametrosEcg());
DetectorCaidas caidas;
BarridoEcg barrido(8, 97);  // OLED 128x32: 97 columnas de trazo (~3 s) y el panel de la FC

constexpr bool kHayBus2 = PIN_I2C2_SDA >= 0;  // segundo bus I2C para la OLED del ECG
bool oledOk = false, oledEcgOk = false, ppgOk = false, acelOk = false;
uint32_t tDatosPpg = 0, tDatosAcel = 0, tReintento = 0;
uint32_t msLatidoPpg = 0, msLatidoEcg = 0, msCaida = 0;
bool caidaPorEnviar = false, trazarEcg = false;
uint32_t seq = 0;
int64_t ultimaMedicionUs = 0;

// FC: la del ECG es más precisa, pero los electrodos se mueven y meten artefactos. Si hay dedo en
// el oxímetro y las dos discrepan más del 10 %, se confía en el oxímetro.
int fcActual(bool* delEcg) {
  const int e = ecg.resultado().fc, p = ppg.resultado().fc;
  const int tolerancia = p / 10 > 5 ? p / 10 : 5;
  *delEcg = e > 0 && (p <= 0 || abs(e - p) <= tolerancia);
  return *delEcg ? e : p;
}

// Una línea I2C en bajo en reposo hace que cada consulta al bus espere 50 ms y nada responda: se avisa
// cuál es, para saber qué cable revisar
void revisarLineasI2c(int bus, uint8_t sda, uint8_t scl) {
  const bool sdaAlto = i2cLineaEnAlto(sda), sclAlto = i2cLineaEnAlto(scl);
  if (sdaAlto && sclAlto) return;
  Serial.printf("I2C bus %d: %s%s%s en bajo en reposo (modulo sin alimentacion, cable a GND o en otro pin)\n", bus,
                sdaAlto ? "" : "SDA", !sdaAlto && !sclAlto ? " y " : "", sclAlto ? "" : "SCL");
  if (!sdaAlto) Serial.printf("  SDA = GPIO%d\n", sda);
  if (!sclAlto) Serial.printf("  SCL = GPIO%d\n", scl);
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // USB nativo del S3: con el monitor cerrado, imprimir nunca bloquea el loop
#endif
  pinMode(PIN_BOTON, INPUT_PULLUP);
#if PIN_LED_OK >= 0
  pinMode(PIN_LED_OK, OUTPUT);
  digitalWrite(PIN_LED_OK, LOW);  // se enciende cuando todo responde (atenderLed)
#endif
  Serial.printf("\n=== Vitalify %s | %s ===\n", FIRMWARE_VERSION, CODIGO_DISPOSITIVO);

  revisarLineasI2c(0, PIN_I2C_SDA, PIN_I2C_SCL);
  if (kHayBus2) revisarLineasI2c(1, PIN_I2C2_SDA, PIN_I2C2_SCL);
  i2cLiberarBus(PIN_I2C_SDA, PIN_I2C_SCL);  // por si un reset dejó una transmisión a medias
  if (kHayBus2) i2cLiberarBus(PIN_I2C2_SDA, PIN_I2C2_SCL);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  if (kHayBus2) Wire1.begin(PIN_I2C2_SDA, PIN_I2C2_SCL, 400000);
  // periphBegin = false: los buses ya están iniciados con sus pines
  oledOk = i2cPresente(Wire, 0x3C) && oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  oledEcgOk = kHayBus2 && i2cPresente(Wire1, 0x3C) && oledEcg.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  ppgOk = ppgIniciar(Wire, PPG_CORRIENTE_LED, PPG_RANGO_ADC);
  acelOk = acelIniciar(Wire);
  tempIniciar(PIN_DS18B20);
  tempActualizar(millis());
  ecgIniciar(PIN_ECG_SALIDA, PIN_ECG_LO_MAS, PIN_ECG_LO_MENOS);
  redIniciar();  // empieza a conectarse mientras se muestra el chequeo de arranque

  char dispositivos[64];
  i2cEscanear(Wire, dispositivos, sizeof dispositivos);
  Serial.printf("I2C bus 0 (SDA=%d, SCL=%d): %s\n", PIN_I2C_SDA, PIN_I2C_SCL, dispositivos);
  if (kHayBus2) {
    i2cEscanear(Wire1, dispositivos, sizeof dispositivos);
    Serial.printf("I2C bus 1 (SDA=%d, SCL=%d): %s\n", PIN_I2C2_SDA, PIN_I2C2_SCL, dispositivos);
  }
  Serial.printf("OLED %s | OLED ECG %s | MAX30102 %s | ADXL345 %s | DS18B20 %s\n", oledOk ? "OK" : "FALTA",
                oledEcgOk ? "OK" : "FALTA", ppgOk ? "OK" : "FALTA", acelOk ? "OK" : "FALTA",
                tempPresente() ? "OK" : "FALTA");
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
    Serial.printf("MAX30102 sin datos: se reintentara (revisa SDA=%d/SCL=%d)\n", PIN_I2C_SDA, PIN_I2C_SCL);
  }
}

void atenderAcelerometro(uint32_t ahora) {
  if (!acelOk) return;
  float ax[32], ay[32], az[32];
  const int n = acelLeer(ax, ay, az, 32);
  for (int i = 0; i < n; i++) {
    // Sin paciente no hay caídas: mover o levantar la placa no debe generar alertas
    if (caidas.procesar(ax[i], ay[i], az[i]) && sesion.monitoreando()) {
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
      if (trazarEcg) Serial.printf("ecg:%.0f crudo:%u\n", v, m[i].adc);
    }
  }
  static bool conectadosAntes = false, ruidosaAntes = false;
  const ResultadoEcg& r = ecg.resultado();
  if (r.electrodos != conectadosAntes) {
    conectadosAntes = r.electrodos;
    if (!r.electrodos) barrido.limpiar();
    Serial.printf("ECG: electrodos %s\n", r.electrodos ? "conectados" : "desconectados");
  }
  if (r.ruidosa != ruidosaAntes) {
    ruidosaAntes = r.ruidosa;
    if (r.ruidosa)
      Serial.printf("ECG: senal con ruido (red %.0f RMS, saturada %.0f%%): fije los electrodos y el RL\n", r.ruidoRed,
                    100 * r.saturacion);
    else
      Serial.println("ECG: senal limpia");
  }
}

void atenderTemperatura(uint32_t ahora) {
  static uint32_t lecturas = 0;
  tempActualizar(ahora);
  if (!tempPresente()) {
    temperatura.sinSonda();
  } else if (tempLecturas() != lecturas) {
    lecturas = tempLecturas();
    temperatura.agregar(tempCelsius(), ahora);
  }
}

// El dedo en el oxímetro inicia la sesión; termina tras 60 s sin ninguna señal del paciente
void atenderSesion(uint32_t ahora) {
  SenalesPaciente s;
  s.sensorPpg = ppgOk;
  s.dedo = ppg.hayDedo();
  s.electrodos = ecg.resultado().electrodos;
  s.sondaEnPiel = temperatura.estado() == EstadoTemperatura::Estabilizando ||
                  temperatura.estado() == EstadoTemperatura::Estable;
  if (!sesion.actualizar(s, ahora)) return;
  if (sesion.monitoreando()) {
    Serial.println("Sesion iniciada: el wearable esta en el paciente (se empieza a enviar)");
  } else {
    msCaida = 0;
    Serial.println("Sesion terminada: 60 s sin dedo, electrodos ni sonda (se deja de enviar)");
  }
}

// Sensores que no respondieron (cable flojo, conectado en caliente): reintento cada 5 s
void reintentarSensores(uint32_t ahora) {
  if (ahora - tReintento < 5000) return;
  tReintento = ahora;
  if (!ppgOk && (ppgOk = ppgIniciar(Wire, PPG_CORRIENTE_LED, PPG_RANGO_ADC))) {
    tDatosPpg = ahora;
    Serial.println("MAX30102 conectado");
  }
  if (!acelOk && (acelOk = acelIniciar(Wire))) {
    tDatosAcel = ahora;
    Serial.println("ADXL345 conectado");
  }
  if (!oledOk && i2cPresente(Wire, 0x3C) && (oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)))
    Serial.println("OLED principal conectada");
  if (kHayBus2 && !oledEcgOk && i2cPresente(Wire1, 0x3C) && (oledEcgOk = oledEcg.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)))
    Serial.println("OLED del ECG conectada");
}

// Una lectura cada INTERVALO_TELEMETRIA_MS y otra en el instante en que se confirma una caída
void emitirTelemetria(uint32_t ahora) {
  static uint32_t proxima = INTERVALO_TELEMETRIA_MS;
  if (!sesion.monitoreando()) {  // sin paciente no se envía nada
    caidaPorEnviar = false;
    proxima = ahora + INTERVALO_TELEMETRIA_MS;
    return;
  }
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
  // Solo una temperatura del cuerpo: con la sonda sobre la piel y ya estable
  if (temperatura.estado() == EstadoTemperatura::Estable) l.tempDecimas = (int16_t)lroundf(temperatura.valor() * 10.0f);
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

VistaEcg vistaEcg(uint32_t ahora) {
  VistaEcg v;
  v.electrodos = ecg.resultado().electrodos;
  v.ruido = ecg.resultado().ruidosa;
  v.fc = ecg.resultado().fc;
  v.pulso = ahora - msLatidoEcg < 150;
  v.ms = ahora;
  return v;
}

// red debe seguir viva mientras se use la vista (v.red apunta a su texto)
VistaPrincipal vistaPrincipal(uint32_t ahora, const EstadoRed& red) {
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
  v.estadoTemp = temperatura.estado();
  v.temp = temperatura.valor();
  v.rangoTemp = temperatura.rango();
  v.ecgInstalado = ecgModuloPresente();
  v.electrodos = ecg.resultado().electrodos;
  v.sesionActiva = sesion.monitoreando();
  v.acelerometro = acelOk;
  v.movimiento = caidas.actividad() == Actividad::Movimiento;
  v.alertaCaida = msCaida != 0 && ahora - msCaida < 30000;
  v.codigo = CODIGO_DISPOSITIVO;
  v.avisoRed = red.aviso[0] != 0;
  v.red = v.avisoRed ? red.aviso : (red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando");
  v.ms = ahora;
  return v;
}

// Listo para seguir: los cuatro sensores responden y los electrodos del ECG están conectados
bool sensoresListos() {
  return ppgOk && acelOk && tempPresente() && ecgModuloPresente() && ecg.resultado().electrodos;
}

// Arranque: el chequeo no sigue mientras falte un sensor (se puede conectar en caliente: se reintenta
// solo) y espera la conexión al broker hasta 25 s; con todo listo deja 2,5 s "Todo conectado".
// El botón BOOT sigue igual aunque falte algo. Mientras tanto, los sensores ya miden.
bool enArranque(uint32_t ahora, const EstadoRed& red) {
  static bool terminado = false;
  static uint32_t listoDesde = 0;
  if (terminado) return false;
  if (digitalRead(PIN_BOTON) == LOW) {
    terminado = true;
    Serial.println("Arranque: se sigue con el boton BOOT aunque falte algun sensor");
    return false;
  }
  const bool redLista = red.broker == EstadoBroker::Conectado || ahora >= 25000;
  static uint32_t tAviso = 0;
  if (!sensoresListos() && ahora - tAviso >= 5000) {
    tAviso = ahora;
    Serial.printf("Arranque: esperando%s%s%s%s%s (BOOT = seguir igual)\n", ppgOk ? "" : " MAX30102",
                  acelOk ? "" : " ADXL345", tempPresente() ? "" : " DS18B20",
                  ecgModuloPresente() ? "" : " modulo AD8232",
                  ecgModuloPresente() && !ecg.resultado().electrodos ? " electrodos del ECG" : "");
  }
  if (sensoresListos() && redLista) {
    if (!listoDesde) listoDesde = ahora;
  } else {
    listoDesde = 0;
  }
  terminado = listoDesde && ahora - listoDesde >= 2500;
  if (terminado) Serial.println("Arranque completo: sensores y electrodos conectados");
  return !terminado;
}

// red debe seguir viva mientras se use la vista (a.red apunta a su texto)
VistaArranque vistaArranque(uint32_t ahora, const EstadoRed& red) {
  VistaArranque a;
  a.version = FIRMWARE_VERSION;
  a.codigo = CODIGO_DISPOSITIVO;
  a.ppg = ppgOk;
  a.acelerometro = acelOk;
  a.temperatura = tempPresente();
  a.ecgModulo = ecgModuloPresente();
  a.electrodos = ecg.resultado().electrodos;
  a.wifi = red.wifi == EstadoWifi::Conectado;
  switch (red.broker) {
    case EstadoBroker::Conectado:
      a.broker = 2;
      break;
    case EstadoBroker::Rechazado:
      a.broker = 3;
      break;
    default:
      a.broker = red.broker == EstadoBroker::Conectando ? 1 : 0;
  }
  a.red = red.aviso[0] ? red.aviso : (a.wifi ? red.ssid : "");
  a.ms = ahora;
  return a;
}

// Cada OLED tiene lo suyo. Durante el arranque, la principal muestra la lista de chequeos y la de
// 128x32, la verificación animada; después, la principal muestra los signos y la de 128x32, el ECG.
void actualizarPantallas(uint32_t ahora) {
  static uint32_t tPrincipal = 0, tEcg = 0;
  const EstadoRed red = redEstado();
  const bool arranque = enArranque(ahora, red);  // en cada vuelta, para no perder un toque de BOOT

  if (oledEcgOk && ahora - tEcg >= 40) {  // 25 cuadros/s: el barrido y la animación se ven fluidos
    tEcg = ahora;
    if (arranque)
      dibujarVerificacion(oledEcg, vistaArranque(ahora, red));
    else
      dibujarEcg(oledEcg, barrido, vistaEcg(ahora));
    oledEcg.display();
  }

  if (!oledOk || ahora - tPrincipal < 200) return;  // 5 cuadros/s bastan para números
  tPrincipal = ahora;
  if (arranque)
    dibujarArranque(oled, vistaArranque(ahora, red));
  else
    dibujarPrincipal(oled, vistaPrincipal(ahora, red));
  oled.display();
}

// LED verde: encendido = todo funciona. Responden los cuatro sensores y las dos OLED, hay Wi-Fi y hora
// NTP, y el broker MQTT está conectado (las lecturas llegan al backend). Apagado = algo falla, y el
// monitor serie dice qué. No depende del paciente: sin dedo ni electrodos el equipo igual está bien.
void atenderLed(uint32_t ahora) {
#if PIN_LED_OK >= 0
  static uint32_t tLed = 0;
  static char antes[96] = "?";
  if (ahora - tLed < 250) return;
  tLed = ahora;

  const EstadoRed red = redEstado();
  char falta[96] = "";
  const auto revisar = [&falta](bool ok, const char* nombre) {
    if (ok) return;
    strlcat(falta, " ", sizeof falta);
    strlcat(falta, nombre, sizeof falta);
  };
  revisar(oledOk, "OLED");
  revisar(oledEcgOk || !kHayBus2, "OLED-ECG");
  revisar(ppgOk, "MAX30102");
  revisar(acelOk, "ADXL345");
  revisar(tempPresente(), "DS18B20");
  revisar(ecgModuloPresente(), "AD8232");
  revisar(red.wifi == EstadoWifi::Conectado, "Wi-Fi");
  revisar(red.horaSincronizada, "hora NTP");
  revisar(red.broker == EstadoBroker::Conectado, "MQTT");
  digitalWrite(PIN_LED_OK, falta[0] ? LOW : HIGH);

  if (strcmp(falta, antes) == 0) return;
  strcpy(antes, falta);
  if (falta[0])
    Serial.printf("LED verde apagado: falta%s\n", falta);
  else
    Serial.println("LED verde encendido: todo funciona");
#else
  (void)ahora;
#endif
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
  char temp[40];
  switch (temperatura.estado()) {
    case EstadoTemperatura::SinSonda:
      strcpy(temp, "sin sonda");
      break;
    case EstadoTemperatura::SinContacto:
      snprintf(temp, sizeof temp, "%.1f C al aire (no se envia)", temperatura.valor());
      break;
    case EstadoTemperatura::Estabilizando:
      snprintf(temp, sizeof temp, "%.1f C estabilizando", temperatura.valor());
      break;
    default:
      snprintf(temp, sizeof temp, "%.1f C %s%s", temperatura.valor(), EvaluadorTemperatura::nombre(temperatura.rango()),
               temperatura.predicha() ? " (predicha)" : "");
  }
  const char* mqtt = red.broker == EstadoBroker::Conectado    ? "OK"
                     : red.broker == EstadoBroker::Rechazado ? "usuario/clave rechazados"
                     : red.broker == EstadoBroker::Conectando ? "conectando"
                                                              : "--";
  Serial.printf("%s | FC %d (%s) | SpO2 %d | dedo %s IR %lu PI %.2f%% | T %s | %s | ECG %s | Wi-Fi %s %d dBm | "
                "MQTT %s | pendientes %u, enviadas %lu\n",
                sesion.monitoreando() ? "MONITOREO" : "ESPERA", fc, delEcg ? "ECG" : "PPG", p.spo2,
                ppg.hayDedo() ? "SI" : "NO", (unsigned long)ppg.ultimoIr(), p.perfusion, temp, caidas.actividad() == Actividad::Movimiento ? "movimiento" : "reposo",
                !ecgModuloPresente()          ? "sin modulo AD8232"
                : !ecg.resultado().electrodos ? "sin electrodos"
                : ecg.resultado().ruidosa     ? "con ruido"
                                              : "conectado",
                red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando", red.rssi, mqtt, red.pendientes,
                (unsigned long)red.enviadas);
}

void loop() {
  const uint32_t ahora = millis();
  atenderPpg(ahora);
  atenderAcelerometro(ahora);
  atenderEcg(ahora);
  atenderTemperatura(ahora);
  atenderSesion(ahora);
  reintentarSensores(ahora);
  emitirTelemetria(ahora);
  actualizarPantallas(ahora);
  atenderLed(ahora);
  atenderSerial(ahora);
}
