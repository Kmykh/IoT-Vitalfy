// red.cpp — ver red.h
#include "red.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <mqtt_client.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>

#include "config.h"

struct RedWifi {
  const char* ssid;
  const char* clave;
  const char* broker;  // nullptr = MQTT_BROKER_POR_DEFECTO
};
#if __has_include("secretos.h")
#include "secretos.h"
#else
#error "Falta secretos.h: copia secretos.example.h como secretos.h y completa tus redes Wi-Fi y la clave MQTT."
#endif

namespace {

const int kRedes = sizeof(REDES_WIFI) / sizeof(REDES_WIFI[0]);
const int kMaxCandidatos = 8;
const uint32_t kTiempoConexionMs = 12000;
const uint32_t kTiempoPubackMs = 10000;
const uint32_t kBackoffMaxMs = 30000;
const time_t kHoraValida = 1735689600;  // 2025-01-01: antes de esto el reloj no viene del NTP

// ---------- Comunicación con el loop ----------
QueueHandle_t colaLecturas = nullptr;
portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
EstadoRed compartido;  // copia que lee el loop
EstadoRed estado;      // solo lo escribe la tarea de red

// ---------- Búfer de lecturas pendientes (solo lo usa la tarea de red) ----------
BuferLecturas<CAPACIDAD_BUFER_OFFLINE> bufer;

// ---------- Wi-Fi ----------
enum class Fase : uint8_t { Inicio, Escaneando, Conectando, Conectado, Espera };
Fase fase = Fase::Inicio;
struct Candidato {
  int red;
  int32_t canal;
  uint8_t bssid[6];
  bool visto;  // apareció en el escaneo (si no, puede ser una red oculta)
  int32_t rssi;
};
Candidato candidatos[kMaxCandidatos];
int nCandidatos = 0, iCandidato = 0, redActual = -1;
uint32_t tFase = 0, esperaMs = 0, backoffMs = 2000, tRssi = 0;
std::atomic<int> razonDesconexion{0};
char nombreHost[40];

// ---------- Broker y MQTT ----------
esp_mqtt_client_handle_t cliente = nullptr;
QueueHandle_t colaAcks = nullptr;  // msg_id confirmados por el broker (PUBACK)
bool mqttIniciado = false, brokerResuelto = false, mdnsIniciado = false, ntpIniciado = false;
char brokerIp[16] = "";
uint32_t proximoIntentoBroker = 0;
std::atomic<bool> mqttConectado{false}, mqttRechazado{false};
std::atomic<int> erroresTransporte{0};
bool mqttConectadoAntes = false;
bool enVuelo = false;
int idEnVuelo = -1;
uint32_t enviadoEn = 0;

void avisar(const char* texto) { strlcpy(estado.aviso, texto, sizeof estado.aviso); }

const char* razonTexto(int r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
      return "red no encontrada";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "clave incorrecta?";
    case WIFI_REASON_ASSOC_FAIL:
      return "el router rechazo la asociacion";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "senal perdida";
    case 0:
      return "sin respuesta";
    default: {
      static char otro[24];
      snprintf(otro, sizeof otro, "codigo %d", r);
      return otro;
    }
  }
}

const char* hostBroker() {
  const char* h = redActual >= 0 ? REDES_WIFI[redActual].broker : nullptr;
  return (h && *h) ? h : MQTT_BROKER_POR_DEFECTO;
}

// ---------- Hora ----------
int64_t ahoraEpochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void atenderHora() {
  if (estado.horaSincronizada || time(nullptr) < kHoraValida) return;
  estado.horaSincronizada = true;
  bufer.asignarHoras(ahoraEpochMs(), esp_timer_get_time());
  char iso[32];
  formatearIso8601(ahoraEpochMs(), iso, sizeof iso);
  Serial.printf("[red] Hora NTP sincronizada: %s (%d lecturas del bufer ya tienen hora)\n", iso, bufer.cantidad());
}

// ---------- Búfer ----------
void recibirLecturas() {
  Lectura l;
  while (xQueueReceive(colaLecturas, &l, 0) == pdTRUE) {
    if (estado.horaSincronizada) asignarHora(l, ahoraEpochMs(), esp_timer_get_time());
    if (!bufer.agregar(l)) enVuelo = false;  // lleno: salió la más antigua, que era la que estaba en vuelo
  }
  estado.perdidas = bufer.perdidas();
}

// ---------- MQTT ----------
void manejadorMqtt(void*, esp_event_base_t, int32_t id, void* datos) {
  const auto* e = static_cast<esp_mqtt_event_handle_t>(datos);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      mqttConectado = true;
      mqttRechazado = false;
      erroresTransporte = 0;
      esp_mqtt_client_enqueue(e->client, TOPICO_ESTADO, "online", 0, 1, 1, true);
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConectado = false;
      break;
    case MQTT_EVENT_PUBLISHED:
      xQueueSend(colaAcks, &e->msg_id, 0);
      break;
    case MQTT_EVENT_ERROR:
      if (e->error_handle == nullptr) break;
      if (e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        const auto rc = e->error_handle->connect_return_code;
        if (rc == MQTT_CONNECTION_REFUSE_BAD_USERNAME || rc == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)
          mqttRechazado = true;
      } else if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        erroresTransporte++;
      }
      break;
    default:
      break;
  }
}

void iniciarMqtt() {
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.hostname = brokerIp;
  cfg.broker.address.port = MQTT_PUERTO;
  cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  cfg.credentials.client_id = CODIGO_DISPOSITIVO;
  cfg.credentials.username = CODIGO_DISPOSITIVO;
  if (MQTT_CLAVE[0]) cfg.credentials.authentication.password = MQTT_CLAVE;
  cfg.session.keepalive = MQTT_KEEPALIVE_S;
  cfg.session.last_will.topic = TOPICO_ESTADO;  // si el wearable desaparece, el broker avisa
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = 3000;
  cfg.network.timeout_ms = 5000;
  cfg.buffer.size = 512;
  cfg.outbox.limit = 8 * 1024;
  if (cliente == nullptr) {
    cliente = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(cliente, MQTT_EVENT_ANY, manejadorMqtt, nullptr);
  } else {
    esp_mqtt_set_config(cliente, &cfg);
  }
  mqttRechazado = false;
  erroresTransporte = 0;
  mqttIniciado = esp_mqtt_client_start(cliente) == ESP_OK;
  Serial.printf("[red] MQTT: conectando a %s:%d como %s\n", brokerIp, MQTT_PUERTO, CODIGO_DISPOSITIVO);
}

void detenerMqtt() {
  if (mqttIniciado) esp_mqtt_client_stop(cliente);
  mqttIniciado = false;
  mqttConectado = false;
  enVuelo = false;
  estado.broker = EstadoBroker::Desconectado;
}

// Nombre -> IP: IP literal, DNS del router, o mDNS para los nombres .local
bool resolverBroker(const char* host) {
  IPAddress ip;
  bool ok = ip.fromString(host);
  if (!ok) ok = WiFi.hostByName(host, ip) == 1 && ip != IPAddress((uint32_t)0);
  const size_t n = strlen(host);
  if (!ok && n > 6 && strcasecmp(host + n - 6, ".local") == 0) {
    if (!mdnsIniciado) mdnsIniciado = MDNS.begin(nombreHost);
    char nombre[64];
    strlcpy(nombre, host, n - 6 + 1 < sizeof nombre ? n - 6 + 1 : sizeof nombre);
    ip = MDNS.queryHost(nombre, 3000);
    ok = ip != IPAddress((uint32_t)0);
  }
  if (!ok) return false;
  strlcpy(brokerIp, ip.toString().c_str(), sizeof brokerIp);
  strlcpy(estado.brokerIp, brokerIp, sizeof estado.brokerIp);
  Serial.printf("[red] Broker %s -> %s\n", host, brokerIp);
  return true;
}

void atenderBroker(uint32_t ahora) {
  if (fase != Fase::Conectado) return;
  if (!brokerResuelto) {
    if ((int32_t)(ahora - proximoIntentoBroker) < 0) return;
    if (resolverBroker(hostBroker())) {
      brokerResuelto = true;
      iniciarMqtt();
    } else {
      proximoIntentoBroker = ahora + 5000;
      avisar("broker no encontrado");
      Serial.printf("[red] No se encontro el broker \"%s\" (reintento en 5 s). Revisa que Mosquitto este "
                    "corriendo y que la laptop este en la misma red.\n",
                    hostBroker());
    }
    return;
  }

  const bool conectado = mqttConectado;
  if (conectado != mqttConectadoAntes) {
    mqttConectadoAntes = conectado;
    Serial.printf(conectado ? "[red] MQTT conectado a %s:%d\n" : "[red] MQTT desconectado de %s:%d\n", brokerIp,
                  MQTT_PUERTO);
    if (conectado) avisar("");
  }
  if (mqttRechazado) {
    estado.broker = EstadoBroker::Rechazado;
    avisar("MQTT: usuario/clave?");
  } else {
    estado.broker = conectado ? EstadoBroker::Conectado : EstadoBroker::Conectando;
  }

  // La laptop pudo recibir otra IP del DHCP: tras varios fallos de conexión se vuelve a resolver
  IPAddress literal;
  if (!conectado && erroresTransporte >= 4 && !literal.fromString(hostBroker())) {
    Serial.println("[red] El broker no responde: se vuelve a resolver su nombre");
    avisar("broker no responde");
    detenerMqtt();
    brokerResuelto = false;
    proximoIntentoBroker = ahora;
  }
}

void publicarPendientes(uint32_t ahora) {
  int id;
  while (xQueueReceive(colaAcks, &id, 0) == pdTRUE) {
    if (enVuelo && id == idEnVuelo) {  // el broker confirmó: la lectura sale del búfer
      bufer.quitarFrente();
      enVuelo = false;
      estado.enviadas++;
    }
  }
  if (!mqttConectado) {
    enVuelo = false;
    return;
  }
  if (enVuelo && ahora - enviadoEn < kTiempoPubackMs) return;
  enVuelo = false;  // sin confirmación a tiempo: se reenvía (el backend descarta duplicados por ts)
  if (bufer.cantidad() == 0 || !estado.horaSincronizada) return;

  Lectura& l = bufer.frente();
  asignarHora(l, ahoraEpochMs(), esp_timer_get_time());
  char json[200];
  const size_t n = construirJsonTelemetria(CODIGO_DISPOSITIVO, l, json, sizeof json);
  if (n == 0) {  // no debería pasar: se descarta para no trabar la cola
    bufer.quitarFrente();
    return;
  }
  // publish envía en el acto desde esta tarea (enqueue esperaría la próxima vuelta de esp-mqtt, hasta
  // ~1 s): así un búfer de cientos de lecturas se vacía en segundos. Bloquear aquí no frena a nadie.
  const int msg = esp_mqtt_client_publish(cliente, TOPICO_TELEMETRIA, json, (int)n, 1, 0);
  if (msg <= 0) return;  // sin conexión en este instante: se reintenta en la próxima vuelta
  enVuelo = true;
  idEnVuelo = msg;
  enviadoEn = ahora;
  Serial.printf("[mqtt] %s %s\n", TOPICO_TELEMETRIA, json);
}

// ---------- Wi-Fi ----------
void iniciarEscaneo(uint32_t ahora) {
  WiFi.disconnect();  // un intento de conexión colgado impide escanear
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  fase = Fase::Escaneando;
  tFase = ahora;
  estado.wifi = EstadoWifi::Buscando;
}

void esperarYReintentar(uint32_t ahora) {
  fase = Fase::Espera;
  tFase = ahora;
  esperaMs = backoffMs;
  backoffMs = backoffMs * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoffMs * 2;
  estado.wifi = EstadoWifi::Buscando;
}

// Redes conocidas visibles (la mejor señal primero); al final las no vistas, por si son ocultas
void armarCandidatos(int encontradas) {
  nCandidatos = 0;
  for (int i = 0; i < encontradas; i++) {
    const String ssid = WiFi.SSID(i);
    for (int r = 0; r < kRedes; r++) {
      if (ssid != REDES_WIFI[r].ssid) continue;
      const int32_t rssi = WiFi.RSSI(i);
      int k = 0;
      while (k < nCandidatos && candidatos[k].red != r) k++;
      if (k < nCandidatos && candidatos[k].rssi >= rssi) break;  // ya hay un AP mejor de esa red
      if (k == nCandidatos) {
        if (nCandidatos == kMaxCandidatos) break;
        nCandidatos++;
      }
      Candidato& c = candidatos[k];
      c = {r, WiFi.channel(i), {}, true, rssi};
      memcpy(c.bssid, WiFi.BSSID(i), 6);
    }
  }
  for (int a = 1; a < nCandidatos; a++)  // de mejor a peor señal
    for (int b = a; b > 0 && candidatos[b].rssi > candidatos[b - 1].rssi; b--) {
      const Candidato t = candidatos[b];
      candidatos[b] = candidatos[b - 1];
      candidatos[b - 1] = t;
    }
  for (int r = 0; r < kRedes && nCandidatos < kMaxCandidatos; r++) {
    bool esta = false;
    for (int k = 0; k < nCandidatos; k++) esta |= candidatos[k].red == r;
    if (!esta) candidatos[nCandidatos++] = {r, 0, {}, false, -127};
  }
}

void conectar(uint32_t ahora) {
  const Candidato& c = candidatos[iCandidato];
  const RedWifi& r = REDES_WIFI[c.red];
  if (c.visto)
    Serial.printf("[red] Wi-Fi: conectando a \"%s\" (%d dBm, canal %d)\n", r.ssid, (int)c.rssi, (int)c.canal);
  else
    Serial.printf("[red] Wi-Fi: \"%s\" no aparecio en el escaneo; se intenta igual\n", r.ssid);
  WiFi.disconnect();  // corta el intento anterior: con uno en curso, begin() falla ("sta is connecting")
  razonDesconexion = 0;
  if (c.visto)
    WiFi.begin(r.ssid, r.clave, c.canal, c.bssid);
  else
    WiFi.begin(r.ssid, r.clave);
  fase = Fase::Conectando;
  tFase = ahora;
  estado.wifi = EstadoWifi::Conectando;
  strlcpy(estado.ssid, r.ssid, sizeof estado.ssid);
}

void alConectar(uint32_t ahora) {
  fase = Fase::Conectado;
  backoffMs = 2000;
  redActual = candidatos[iCandidato].red;
  estado.wifi = EstadoWifi::Conectado;
  estado.rssi = (int8_t)WiFi.RSSI();
  strlcpy(estado.ip, WiFi.localIP().toString().c_str(), sizeof estado.ip);
  tRssi = ahora;
  avisar("");
  Serial.printf("[red] Wi-Fi conectado a \"%s\" | IP %s | %d dBm\n", estado.ssid, estado.ip, estado.rssi);
  if (!ntpIniciado) {
    configTzTime(ZONA_HORARIA, NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);
    ntpIniciado = true;
  }
  brokerResuelto = false;
  proximoIntentoBroker = ahora;
}

void atenderWifi(uint32_t ahora) {
  switch (fase) {
    case Fase::Inicio:
      iniciarEscaneo(ahora);
      break;

    case Fase::Escaneando: {
      const int16_t n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING && ahora - tFase < 15000) return;
      armarCandidatos(n > 0 ? n : 0);
      WiFi.scanDelete();
      iCandidato = 0;
      if (nCandidatos == 0) {
        avisar("sin redes en secretos");
        esperarYReintentar(ahora);
      } else {
        conectar(ahora);
      }
      break;
    }

    case Fase::Conectando: {
      if (WiFi.status() == WL_CONNECTED) {
        alConectar(ahora);
        break;
      }
      const int razon = razonDesconexion;
      // el router ya respondió que no (ASSOC_LEAVE y STA_LEAVING los genera el propio disconnect())
      const bool propia = razon == WIFI_REASON_ASSOC_LEAVE || razon == WIFI_REASON_STA_LEAVING;
      const bool fallo = razon != 0 && !propia && ahora - tFase > 2000;
      if (!fallo && ahora - tFase < kTiempoConexionMs) break;
      Serial.printf("[red] Wi-Fi: no se pudo conectar a \"%s\" (%s)\n", REDES_WIFI[candidatos[iCandidato].red].ssid,
                    razonTexto(razon));
      if (razon == WIFI_REASON_AUTH_FAIL || razon == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
          razon == WIFI_REASON_HANDSHAKE_TIMEOUT)
        avisar("clave Wi-Fi?");
      else
        avisar("sin Wi-Fi");
      if (++iCandidato < nCandidatos)
        conectar(ahora);
      else
        esperarYReintentar(ahora);
      break;
    }

    case Fase::Conectado:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[red] Wi-Fi: se perdio la conexion con \"%s\" (%s)\n", estado.ssid,
                      razonTexto(razonDesconexion));
        detenerMqtt();
        mqttConectadoAntes = false;
        estado.ip[0] = 0;
        estado.rssi = 0;
        avisar("sin Wi-Fi");
        fase = Fase::Espera;
        tFase = ahora;
        esperaMs = 500;
        estado.wifi = EstadoWifi::Buscando;
      } else if (ahora - tRssi > 2000) {
        tRssi = ahora;
        estado.rssi = (int8_t)WiFi.RSSI();
      }
      break;

    case Fase::Espera:
      if (ahora - tFase >= esperaMs) iniciarEscaneo(ahora);
      break;
  }
}

void publicarEstado() {
  estado.pendientes = (uint16_t)bufer.cantidad();
  portENTER_CRITICAL(&candado);
  compartido = estado;
  portEXIT_CRITICAL(&candado);
}

void tareaRed(void*) {
  snprintf(nombreHost, sizeof nombreHost, "vitalify-%s", CODIGO_DISPOSITIVO);
  for (char* c = nombreHost; *c; c++) *c = (char)tolower(*c);

  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    razonDesconexion = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);       // no escribe las credenciales en la flash en cada intento
  WiFi.setHostname(nombreHost);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false); // la reconexión la maneja esta tarea (elige la mejor red)
  WiFi.setSleep(false);         // sin ahorro de energía del módem: menos latencia y cortes de MQTT
  WiFi.setTxPower(WIFI_POTENCIA_TX);

  for (;;) {
    const uint32_t ahora = millis();
    recibirLecturas();
    atenderWifi(ahora);
    atenderHora();
    atenderBroker(ahora);
    publicarPendientes(ahora);
    publicarEstado();
    // Con cola pendiente y broker conectado se vacía rápido; si no, se revisa cada 50 ms
    vTaskDelay(pdMS_TO_TICKS((bufer.cantidad() > 0 && mqttConectado) ? 5 : 50));
  }
}

}  // namespace

void redIniciar() {
  colaLecturas = xQueueCreate(16, sizeof(Lectura));
  colaAcks = xQueueCreate(8, sizeof(int));
  xTaskCreatePinnedToCore(tareaRed, "red", 8192, nullptr, 2, nullptr, 0);
}

bool redEncolar(const Lectura& l) { return colaLecturas && xQueueSend(colaLecturas, &l, 0) == pdTRUE; }

EstadoRed redEstado() {
  EstadoRed e;
  portENTER_CRITICAL(&candado);
  e = compartido;
  portEXIT_CRITICAL(&candado);
  return e;
}
