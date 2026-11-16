// tesis.ino — Vitalify: wearable IoMT (ESP32 DevKit o ESP32-S3 Zero)
//
// Mide FC y SpO2 (MAX30102), ECG (AD8232), temperatura (DS18B20), caídas y frecuencia respiratoria
// (MPU6050 sobre el tórax) y la carga de su batería (divisor en VP, bateria.h). Los muestra en dos OLED
// (signos en la principal, ECG en la de 128x32) y los envía al backend por MQTT según el contrato de
// telemetría. Sin vincular a un paciente muestra el QR que
// escanea la enfermera y no envía nada. Un LED verde encendido indica que todo funciona.
//
//   Núcleo 1: este loop (sensores, algoritmos y pantallas) y la tarea que muestrea el ECG a 250 Hz.
//   Núcleo 0: la tarea de red (Wi-Fi, NTP, MQTT y búfer offline). La red nunca frena al loop.
//
// Configuración: config.h (dispositivo, red, pines) y secretos.h (redes Wi-Fi y clave MQTT).
// Monitor serie a 115200: cada 5 s imprime el estado; la tecla 'p' activa el trazado del ECG
// para el Serial Plotter y la 'w', el modo de configuración del Wi-Fi desde la app (como BOOT 3 s).

#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <esp_timer.h>
#include <time.h>

#include "bateria.h"
#include "caidas.h"
#include "config.h"
#include "config_wifi.h"
#include "ecg.h"
#include "fr_cardiaca.h"
#include "pantallas.h"
#include "ppg.h"
#include "red.h"
#include "respiracion.h"
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

ParametrosBateria parametrosBateria() {
  ParametrosBateria p;
  p.divisor = BATERIA_DIVISOR;
  p.correccion = BATERIA_CORRECCION;
  return p;
}

ProcesadorPpg ppg(parametrosPpg());
MedidorBateria bateria(parametrosBateria());
EvaluadorTemperatura temperatura(parametrosTemperatura());
SesionPaciente sesion;
ProcesadorEcg ecg(parametrosEcg());
DetectorCaidas caidas;
MonitorRespiracion respiracion;  // FR con el mismo acelerómetro de las caídas
// FR desde el pulso (fr_cardiaca.h): el ECG (FM y amplitud de la R) y el PPG (FM, amplitud y línea de base).
// Se fusiona con la del MPU cada 2 s: frFusionada es la que se muestra y se envía.
FrCardiaca frEcg(false), frPpg(true);
int frFusionada = -1;
FuenteFr fuenteFr = FuenteFr::Ninguna;
BarridoEcg barrido(8, 128);  // OLED 128x32: el trazo a todo lo ancho (~4 s); la FC va en la principal

constexpr bool kHayBus2 = PIN_I2C2_SDA >= 0;  // segundo bus I2C para la OLED del ECG
bool oledOk = false, oledEcgOk = false, ppgOk = false, acelOk = false;
uint32_t tDatosPpg = 0, tDatosAcel = 0, tReintento = 0;
uint32_t msLatidoPpg = 0, msLatidoEcg = 0, msCaida = 0;
bool caidaPorEnviar = false, trazarEcg = false;
uint32_t seq = 0;
int64_t ultimaMedicionUs = 0;
bool arranqueTerminado = false;

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
  // Lo primero que se ve al encender es la presentación con el logo, antes de iniciar los sensores y la red
  // (hasta el primer display() la OLED muestra lo que quedó en su memoria)
  VistaArranque inicio;
  inicio.version = FIRMWARE_VERSION;
  inicio.codigo = CODIGO_DISPOSITIVO;
  inicio.ms = millis();
  if (oledOk) {
    dibujarArranque(oled, inicio);
    oled.display();
  }
  if (oledEcgOk) {
    dibujarVerificacion(oledEcg, inicio);
    oledEcg.display();
  }
  ppgOk = ppgIniciar(Wire, PPG_CORRIENTE_LED, PPG_RANGO_ADC);
  acelOk = acelIniciar(Wire);
  tempIniciar(PIN_DS18B20);
  tempActualizar(millis());
  bateriaIniciar(PIN_BATERIA);  // antes del ECG: su tarea también lee la batería
  ecgIniciar(PIN_ECG_SALIDA, PIN_ECG_LO_MAS, PIN_ECG_LO_MENOS);
  redIniciar();  // empieza a conectarse mientras se muestra el chequeo de arranque

  char dispositivos[64];
  i2cEscanear(Wire, dispositivos, sizeof dispositivos);
  Serial.printf("I2C bus 0 (SDA=%d, SCL=%d): %s\n", PIN_I2C_SDA, PIN_I2C_SCL, dispositivos);
  if (kHayBus2) {
    i2cEscanear(Wire1, dispositivos, sizeof dispositivos);
    Serial.printf("I2C bus 1 (SDA=%d, SCL=%d): %s\n", PIN_I2C2_SDA, PIN_I2C2_SCL, dispositivos);
  }
  Serial.printf("OLED %s | OLED ECG %s | MAX30102 %s | %s %s | DS18B20 %s\n", oledOk ? "OK" : "FALTA",
                oledEcgOk ? "OK" : "FALTA", ppgOk ? "OK" : "FALTA", acelOk ? acelNombre() : "MPU6050",
                acelOk ? "OK" : "FALTA", tempPresente() ? "OK" : "FALTA");
  if (!acelOk && acelDiagnostico()[0]) Serial.printf("Acelerometro: %s\n", acelDiagnostico());
  tDatosPpg = tDatosAcel = millis();
  enableLoopWDT();  // si el loop se colgara más de 5 s, el ESP32 se reinicia solo
}

void atenderPpg(uint32_t ahora) {
  if (!ppgOk) return;
  uint32_t ir[32], rojo[32];
  uint8_t perdidas = 0;
  const int n = ppgLeer(ir, rojo, 32, &perdidas);
  if (perdidas) ppg.saltar(perdidas);
  for (int i = 0; i < n; i++) {
    if (!ppg.procesar(ir[i], rojo[i])) continue;
    msLatidoPpg = ahora;
    const ResultadoPpg& r = ppg.resultado();
    if (r.estado == EstadoPpg::Valido) frPpg.agregar(r.tLatido, r.acIr, r.dcIr);
  }
  if (!ppg.hayDedo() && frPpg.fr() >= 0) frPpg.reiniciar();
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
    respiracion.procesar(ax[i], ay[i], az[i]);
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
    respiracion.reiniciar();
    Serial.printf("%s sin datos: se reintentara\n", acelNombre());
  }
}

void atenderEcg(uint32_t ahora) {
  MuestraEcg m[64];
  int n;
  while ((n = ecgLeer(m, 64)) > 0) {
    for (int i = 0; i < n; i++) {
      bool latido = false;
      const float v = ecg.procesar(m[i].adc, m[i].conectados, &latido);
      if (latido) {
        msLatidoEcg = ahora;
        if (!ecg.resultado().ruidosa) frEcg.agregar(ecg.resultado().tR, ecg.resultado().amplitudR);
      }
      if (m[i].conectados) barrido.agregar(v);
      if (trazarEcg) Serial.printf("ecg:%.0f crudo:%u\n", v, m[i].adc);
    }
  }
  static bool conectadosAntes = false, ruidosaAntes = false;
  const ResultadoEcg& r = ecg.resultado();
  if (r.electrodos != conectadosAntes) {
    conectadosAntes = r.electrodos;
    if (!r.electrodos) {
      barrido.limpiar();
      frEcg.reiniciar();
    }
    Serial.printf("ECG: electrodos %s\n", r.electrodos ? "conectados" : "desconectados");
  }
  if (r.ruidosa != ruidosaAntes) {
    ruidosaAntes = r.ruidosa;
    if (r.ruidosa) frEcg.reiniciar();  // con ruido, los "latidos" no sirven para la FR
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

// Batería: una lectura por segundo (la toma la tarea del ECG). En el monitor serie se avisa cuando cambia el
// estado; el voltaje va en el estado de cada 5 s, para calibrar BATERIA_CORRECCION con un multímetro.
void atenderBateria() {
  static uint32_t lecturas = 0;
  if (bateriaLecturas() == lecturas) return;
  lecturas = bateriaLecturas();
  const EstadoBateria antes = bateria.estado();
  bateria.agregarMvPin(bateriaMvPin());
  if (bateria.estado() == antes) return;
  switch (bateria.estado()) {
    case EstadoBateria::SinMedicion:
      Serial.println("Bateria: sin medicion (falta el divisor en VP o esta suelto)");
      break;
    case EstadoBateria::Critica:
      Serial.printf("Bateria critica: %d%% (%.2f V): cargue el wearable\n", bateria.porcentaje(), bateria.voltios());
      break;
    case EstadoBateria::Baja:
      Serial.printf("Bateria baja: %d%% (%.2f V)\n", bateria.porcentaje(), bateria.voltios());
      break;
    default:
      Serial.printf("Bateria: %d%% (%.2f V)\n", bateria.porcentaje(), bateria.voltios());
  }
}

// FR: cada 2 s se estima la del pulso (ECG y PPG) y se fusiona con la del MPU (fr_cardiaca.h)
void atenderFr(uint32_t ahora) {
  static uint32_t tFr = 0;
  if (ahora - tFr < 2000) return;
  tFr = ahora;
  frEcg.estimar();
  frPpg.estimar();
  frFusionada = fusionarFr(respiracion.fr(), frEcg.fr(), frPpg.fr(), &fuenteFr);
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
    Serial.printf("%s conectado\n", acelNombre());
  }
  if (!oledOk && i2cPresente(Wire, 0x3C) && (oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)))
    Serial.println("OLED principal conectada");
  if (kHayBus2 && !oledEcgOk && i2cPresente(Wire1, 0x3C) && (oledEcgOk = oledEcg.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)))
    Serial.println("OLED del ECG conectada");
}

// Una lectura cada INTERVALO_TELEMETRIA_MS y otra en el instante en que se confirma una caída. Solo con el
// paciente conectado y el wearable vinculado a él (o recién encendido, sin saberlo todavía: la tarea de red
// las guarda y las descarta si resulta que no lo estaba).
void emitirTelemetria(uint32_t ahora) {
  static uint32_t proxima = INTERVALO_TELEMETRIA_MS;
  // Sin paciente no se envía nada; configurando el Wi-Fi desde la app, la red está en pausa
  if (!sesion.monitoreando() || !redAceptaLecturas(redEstado()) || configWifiActiva()) {
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
  if (frFusionada > 0) l.fr = (int16_t)frFusionada;  // MPU y pulso fusionados (fr_cardiaca.h)
  // Solo una temperatura del cuerpo: con la sonda sobre la piel y ya estable
  if (temperatura.estado() == EstadoTemperatura::Estable) l.tempDecimas = (int16_t)lroundf(temperatura.valor() * 10.0f);
  l.caida = caidaPorEnviar;
  l.bateria = (int8_t)bateria.porcentaje();  // -1 sin divisor: no se envía
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

bool horaLocal(const EstadoRed& red, uint8_t* hora, uint8_t* minuto);
uint8_t estadoBroker(const EstadoRed& red);

VistaEcg vistaEcg(uint32_t ahora, const EstadoRed& red) {
  VistaEcg v;
  v.wifiBarras = barrasWifi(red);
  v.broker = estadoBroker(red);
  v.pendientes = red.pendientes;
  v.horaValida = horaLocal(red, &v.hora, &v.minuto);
  v.bateria = (int8_t)bateria.porcentaje();
  v.estadoBateria = bateria.estado();
  v.electrodos = ecg.resultado().electrodos;
  v.ruido = ecg.resultado().ruidosa;
  v.fc = ecg.resultado().fc;
  v.pulso = ahora - msLatidoEcg < 150;
  v.ms = ahora;
  return v;
}

// Hora local para la barra de estado (solo con hora NTP)
bool horaLocal(const EstadoRed& red, uint8_t* hora, uint8_t* minuto) {
  const time_t t = time(nullptr);
  struct tm local;
  if (!red.horaSincronizada || !localtime_r(&t, &local)) return false;
  *hora = (uint8_t)local.tm_hour;
  *minuto = (uint8_t)local.tm_min;
  return true;
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
  v.horaValida = horaLocal(red, &v.hora, &v.minuto);
  v.bateria = (int8_t)bateria.porcentaje();
  v.estadoBateria = bateria.estado();
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
  v.fr = frFusionada;
  v.estadoFr = respiracion.estado();
  v.fuenteFr = fuenteFr;
  v.cama = red.cama;
  v.iniciales = red.iniciales;
  v.aviso = red.avisoComandoMs && ahora - red.avisoComandoMs < 15000 ? red.avisoComando : "";
  v.codigo = CODIGO_DISPOSITIVO;
  v.avisoRed = red.aviso[0] != 0;
  v.red = v.avisoRed ? red.aviso : (red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando");
  v.ms = ahora;
  return v;
}

uint8_t estadoBroker(const EstadoRed& red) {
  switch (red.broker) {
    case EstadoBroker::Conectado:
      return 2;
    case EstadoBroker::Conectando:
      return 1;
    case EstadoBroker::Rechazado:
      return 3;
    default:
      return 0;
  }
}

// Al cambiar la vinculación, una pantalla de 5 s: la confirmación al vincular (desde el QR o el
// emparejamiento; al reconectarse ya vinculado no se muestra) y el aviso al desvincular.
const uint32_t kMsPantallaCambioVinculo = 5000;
bool hayCambioVinculo = false;
PantallaVinculo pantallaCambioVinculo = PantallaVinculo::Vinculado;
uint32_t msCambioVinculo = 0;

void seguirVinculo(uint32_t ahora, const EstadoRed& red) {
  static EstadoVinculo anterior = EstadoVinculo::Desconocido;
  if (red.vinculo == anterior) return;
  const bool vinculo = red.vinculo == EstadoVinculo::Vinculado &&
                       (anterior == EstadoVinculo::EsperandoQr || anterior == EstadoVinculo::Emparejando);
  const bool desvinculo = anterior == EstadoVinculo::Vinculado && red.vinculo == EstadoVinculo::EsperandoQr;
  hayCambioVinculo = vinculo || desvinculo;
  pantallaCambioVinculo = vinculo ? PantallaVinculo::Vinculado : PantallaVinculo::Desvinculado;
  msCambioVinculo = ahora;
  anterior = red.vinculo;
}

bool mostrandoCambioVinculo(uint32_t ahora) {
  return hayCambioVinculo && ahora - msCambioVinculo < kMsPantallaCambioVinculo;
}

// Sin vincular a un paciente, la OLED principal muestra el QR (o "Emparejando..." mientras la enfermera
// registra al paciente). Recién encendido (sin ningún comando todavía): si el wearable no está en un
// paciente, cómo va la conexión con el servidor; si lo está (pudo reiniciarse vinculado y sin red), sigue
// mostrando los signos.
bool mostrarVinculacion(uint32_t ahora, const EstadoRed& red) {
  if (mostrandoCambioVinculo(ahora)) return true;
  if (red.vinculo == EstadoVinculo::EsperandoQr || red.vinculo == EstadoVinculo::Emparejando) return true;
  return red.vinculo == EstadoVinculo::Desconocido && !sesion.monitoreando();
}

// red debe seguir viva mientras se use la vista (v.red, v.cama y v.iniciales apuntan a sus textos)
VistaVinculacion vistaVinculacion(uint32_t ahora, const EstadoRed& red) {
  VistaVinculacion v;
  v.codigo = CODIGO_DISPOSITIVO;
  v.wifiBarras = barrasWifi(red);
  v.broker = estadoBroker(red);
  v.avisoRed = red.aviso[0] != 0;
  v.red = v.avisoRed ? red.aviso : red.ssid;
  v.cama = red.cama;
  v.iniciales = red.iniciales;
  v.bateria = (int8_t)bateria.porcentaje();
  v.estadoBateria = bateria.estado();
  v.horaValida = horaLocal(red, &v.hora, &v.minuto);
  v.ms = ahora;
  v.qr = red.qr;
  const int64_t ahoraMs = red.horaSincronizada ? (int64_t)time(nullptr) * 1000 : 0;
  // Cuánto le queda al QR o, emparejando, a la app para confirmar
  const int64_t expiraMs = red.vinculo == EstadoVinculo::Emparejando ? red.emparejandoExpiraMs : red.qrExpiraMs;
  if (ahoraMs > 0 && expiraMs > 0) {
    const int64_t falta = (expiraMs - ahoraMs) / 1000;
    v.segundosRestantes = falta > 0 ? (int)falta : 0;
  }
  if (mostrandoCambioVinculo(ahora))
    v.pantalla = pantallaCambioVinculo;
  else if (red.vinculo == EstadoVinculo::Emparejando)
    v.pantalla = PantallaVinculo::Emparejando;
  else if (red.vinculo == EstadoVinculo::Desconocido)
    v.pantalla = PantallaVinculo::Solicitando;
  else if (!red.qr[0])
    v.pantalla = PantallaVinculo::Solicitando;
  else if (ahoraMs > 0 && red.qrExpiraMs > 0 && ahoraMs >= red.qrExpiraMs)
    v.pantalla = PantallaVinculo::Vencido;
  else
    v.pantalla = PantallaVinculo::Qr;
  // Desde cuándo está esta pantalla (para su animación de entrada: el QR se arma, los mensajes entran)
  static PantallaVinculo anterior = PantallaVinculo::Solicitando;
  static uint32_t desde = 0;
  if (v.pantalla != anterior) {
    anterior = v.pantalla;
    desde = ahora;
  }
  v.msPantalla = ahora - desde;
  return v;
}

// Listo para seguir: los cuatro sensores responden. Los electrodos no se esperan: se colocan en el
// paciente después de vincular (la pantalla "Conecte al paciente" los pide).
bool sensoresListos() {
  return ppgOk && acelOk && tempPresente() && ecgModuloPresente();
}

// Arranque: el chequeo no sigue mientras falte un sensor (se puede conectar en caliente: se reintenta
// solo) y espera la conexión al broker hasta 25 s; con todo listo deja 2,5 s "Todo conectado".
// El botón BOOT sigue igual aunque falte algo. Mientras tanto, los sensores ya miden.
bool enArranque(uint32_t ahora, const EstadoRed& red) {
  static bool terminado = false;
  static uint32_t listoDesde = 0;
  if (terminado) return false;
  if (digitalRead(PIN_BOTON) == LOW) {
    terminado = arranqueTerminado = true;
    Serial.println("Arranque: se sigue con el boton BOOT aunque falte algun sensor");
    return false;
  }
  const bool redLista = red.broker == EstadoBroker::Conectado || ahora >= 25000;
  static uint32_t tAviso = 0;
  if (!sensoresListos() && ahora - tAviso >= 5000) {
    tAviso = ahora;
    Serial.printf("Arranque: esperando%s%s%s%s (BOOT = seguir igual)\n", ppgOk ? "" : " MAX30102",
                  acelOk ? "" : " MPU6050", tempPresente() ? "" : " DS18B20",
                  ecgModuloPresente() ? "" : " modulo AD8232");
  }
  if (sensoresListos() && redLista) {
    if (!listoDesde) listoDesde = ahora;
  } else {
    listoDesde = 0;
  }
  terminado = listoDesde && ahora - listoDesde >= 2500;
  if (terminado) Serial.println("Arranque completo: los cuatro sensores responden");
  arranqueTerminado = terminado;
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
  a.wifiBarras = barrasWifi(red);
  a.avisoRed = red.aviso[0] != 0;
  a.red = red.aviso[0] ? red.aviso : (a.wifi ? red.ssid : "");
  a.bateria = (int8_t)bateria.porcentaje();
  a.estadoBateria = bateria.estado();
  a.ms = ahora;
  return a;
}

// Cada OLED tiene lo suyo. Durante el arranque, la principal muestra la lista de chequeos y la de
// 128x32, la verificación animada. Mientras se vincula, la principal muestra el QR (o la conexión) y la
// de 128x32, los tres pasos. Vinculado, la principal muestra los signos y la de 128x32, el ECG.
// Configurando el Wi-Fi desde la app (config_wifi.h). c debe seguir viva mientras se use la vista.
VistaConfigWifi vistaConfigWifi(uint32_t ahora, const EstadoConfigWifi& c) {
  VistaConfigWifi v;
  v.etapa = c.etapa;
  v.fallo = c.fallo;
  v.qr = c.qr;
  v.pop = c.pop;
  v.codigo = CODIGO_DISPOSITIVO;
  v.ssid = c.ssid;
  v.bateria = (int8_t)bateria.porcentaje();
  v.estadoBateria = bateria.estado();
  v.ms = ahora;
  return v;
}

void actualizarPantallas(uint32_t ahora) {
  static uint32_t tPrincipal = 0, tEcg = 0;
  if (configWifiActiva()) {  // el modo de configuración del Wi-Fi ocupa las dos OLED
    const EstadoConfigWifi c = configWifiEstado();
    if (oledEcgOk && ahora - tEcg >= 40) {
      tEcg = ahora;
      dibujarPasosConfigWifi(oledEcg, vistaConfigWifi(ahora, c));
      oledEcg.display();
    }
    if (oledOk && ahora - tPrincipal >= 40) {
      tPrincipal = ahora;
      dibujarConfigWifi(oled, vistaConfigWifi(ahora, c));
      oled.display();
    }
    return;
  }
  const EstadoRed red = redEstado();
  const bool arranque = enArranque(ahora, red);  // en cada vuelta, para no perder un toque de BOOT
  seguirVinculo(ahora, red);
  const bool vinculando = !arranque && mostrarVinculacion(ahora, red);

  if (oledEcgOk && ahora - tEcg >= 40) {  // 25 cuadros/s: el barrido y la animación se ven fluidos
    tEcg = ahora;
    if (arranque)
      dibujarVerificacion(oledEcg, vistaArranque(ahora, red));
    else if (vinculando)
      dibujarPasosVinculo(oledEcg, vistaVinculacion(ahora, red));
    else
      dibujarEcg(oledEcg, barrido, vistaEcg(ahora, red));
    oledEcg.display();
  }

  // Animaciones (arranque y vinculación) a 25 cuadros/s; los signos, a 5 (bastan para números y el bus I2C
  // queda libre para el oxímetro y el acelerómetro)
  if (!oledOk || ahora - tPrincipal < ((arranque || vinculando) ? 40u : 200u)) return;
  tPrincipal = ahora;
  if (arranque)
    dibujarArranque(oled, vistaArranque(ahora, red));
  else if (vinculando)
    dibujarVinculacion(oled, vistaVinculacion(ahora, red));
  else
    dibujarPrincipal(oled, vistaPrincipal(ahora, red));
  oled.display();
}

// Botón BOOT, terminado el arranque (un toque durante el arranque es "seguir igual" y no cuenta):
//   · apretado 3 s: modo de configuración del Wi-Fi desde la app (config_wifi.h);
//   · un toque (al soltarlo, menos de 1 s): en ese modo, salir; si no, mientras se espera el escaneo (o se
//     empareja), pide un QR nuevo al backend. Si la app todavía puede confirmar, el backend responde
//     "emparejando" y no se pierde nada.
void atenderBoton(uint32_t ahora) {
  static bool antes = false, largo = false, ignorar = false;
  static uint32_t tCambio = 0, tApretado = 0;
  const bool pulsado = digitalRead(PIN_BOTON) == LOW;
  if (pulsado && antes && !ignorar && !largo && !configWifiActiva() && ahora - tApretado >= 3000) {
    largo = true;
    configWifiIniciar(ahora, "BOOT 3 s");
  }
  if (pulsado == antes || ahora - tCambio < 50) return;  // antirrebote de 50 ms
  antes = pulsado;
  tCambio = ahora;
  if (pulsado) {
    tApretado = ahora;
    largo = false;
    ignorar = !arranqueTerminado;
    return;
  }
  if (ignorar || largo || ahora - tApretado >= 1000) return;
  if (configWifiActiva()) {
    configWifiSalir();
    return;
  }
  const EstadoVinculo vinculo = redEstado().vinculo;
  if (vinculo == EstadoVinculo::EsperandoQr || vinculo == EstadoVinculo::Emparejando) {
    redPedirQr();
    Serial.println("Boton: se pide un QR nuevo");
  }
}

// Sin ninguna red conocida en 1 min desde el encendido, entra solo al modo de configuración del Wi-Fi (así,
// en una red nueva, basta con abrir la app). No con un paciente: ahí se sigue buscando la red de siempre.
const uint32_t kMsSinRedParaConfigurar = 60000;
void atenderConfigWifiAutomatica(uint32_t ahora) {
  static bool vioWifi = false, hecho = false;
  if (hecho || configWifiActiva()) return;
  const EstadoRed red = redEstado();
  if (red.wifi == EstadoWifi::Conectado) vioWifi = true;
  if (vioWifi || !arranqueTerminado || ahora < kMsSinRedParaConfigurar) return;
  if (sesion.monitoreando() || red.vinculo == EstadoVinculo::Vinculado) return;
  hecho = true;
  configWifiIniciar(ahora, "ninguna red conocida en 1 min");
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
  revisar(acelOk, acelNombre());
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
    } else if ((c == 'w' || c == 'W') && arranqueTerminado) {
      configWifiIniciar(ahora, "tecla w del monitor serie");  // como BOOT 3 s
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
  char fr[64];
  const char* mpu = respiracion.fr() > 0                                  ? ""
                    : respiracion.estado() == EstadoRespiracion::Movimiento ? " en movimiento"
                    : respiracion.estado() == EstadoRespiracion::SinSenal   ? " sin senal"
                                                                            : " midiendo";
  snprintf(fr, sizeof fr, "FR %d [MPU %d%s, ECG %d, PPG %d]", frFusionada, respiracion.fr(), mpu, frEcg.fr(), frPpg.fr());
  char bat[40];
  if (bateria.porcentaje() >= 0)
    snprintf(bat, sizeof bat, "Bateria %d%% %.2f V (pin %d mV)", bateria.porcentaje(), bateria.voltios(), bateriaMvPin());
  else
    snprintf(bat, sizeof bat, "Bateria -- (pin %d mV)", bateriaMvPin());
  const char* vinculo = red.vinculo == EstadoVinculo::Vinculado     ? "vinculado"
                        : red.vinculo == EstadoVinculo::EsperandoQr ? "esperando QR"
                        : red.vinculo == EstadoVinculo::Emparejando ? "emparejando"
                                                                    : "sin datos";
  const char* mqtt = red.broker == EstadoBroker::Conectado    ? "OK"
                     : red.broker == EstadoBroker::Rechazado ? "usuario/clave rechazados"
                     : red.broker == EstadoBroker::Conectando ? "conectando"
                                                              : "--";
  Serial.printf("%s (%s) | FC %d (%s) | SpO2 %d | %s | dedo %s IR %lu PI %.2f%% | T %s | %s | ECG %s | Wi-Fi %s %d dBm | "
                "MQTT %s | pendientes %u, enviadas %lu | %s\n",
                sesion.monitoreando() ? "MONITOREO" : "ESPERA", vinculo, fc, delEcg ? "ECG" : "PPG", p.spo2, fr,
                ppg.hayDedo() ? "SI" : "NO", (unsigned long)ppg.ultimoIr(), p.perfusion, temp, caidas.actividad() == Actividad::Movimiento ? "movimiento" : "reposo",
                !ecgModuloPresente()          ? "sin modulo AD8232"
                : !ecg.resultado().electrodos ? "sin electrodos"
                : ecg.resultado().ruidosa     ? "con ruido"
                                              : "conectado",
                red.wifi == EstadoWifi::Conectado ? red.ssid : "buscando", red.rssi, mqtt, red.pendientes,
                (unsigned long)red.enviadas, bat);
}

void loop() {
  const uint32_t ahora = millis();
  atenderPpg(ahora);
  atenderAcelerometro(ahora);
  atenderEcg(ahora);
  atenderTemperatura(ahora);
  atenderBateria();
  atenderFr(ahora);
  atenderSesion(ahora);
  reintentarSensores(ahora);
  emitirTelemetria(ahora);
  configWifiAtender(ahora);
  atenderConfigWifiAutomatica(ahora);
  actualizarPantallas(ahora);
  atenderBoton(ahora);
  atenderLed(ahora);
  atenderSerial(ahora);
}
