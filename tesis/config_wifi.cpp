// config_wifi.cpp — ver config_wifi.h
#include "config_wifi.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_event.h>
#include <esp_random.h>
#include <protocomm_ble.h>
#include <protocomm_security.h>

#include "config.h"
#include "red.h"

namespace {

portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
EstadoConfigWifi estado;            // lo escriben los eventos y el loop, siempre con el candado
char clavePendiente[65] = "";       // la clave recibida hasta saber si conectó (nunca se imprime)
volatile bool guardarPendiente = false;
uint32_t listaDesde = 0;
bool bluetoothIniciado = false, eventosRegistrados = false;

const char* const kNvsEspacio = "vitalify";
const char* const kNvsRedes = "redes";

void cambiar(EtapaConfigWifi etapa, FalloConfigWifi fallo) {
  portENTER_CRITICAL(&candado);
  estado.etapa = etapa;
  estado.fallo = fallo;
  portEXIT_CRITICAL(&candado);
}

uint32_t azar() { return esp_random(); }

// Eventos del aprovisionamiento (los traduce el core de Arduino) y del Bluetooth (protocomm)
void alEventoProv(arduino_event_id_t id, arduino_event_info_t info) {
  switch (id) {
    case ARDUINO_EVENT_PROV_START:
      cambiar(EtapaConfigWifi::Esperando, FalloConfigWifi::Ninguno);
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV: {
      const char* ssid = (const char*)info.prov_cred_recv.ssid;
      portENTER_CRITICAL(&candado);
      strlcpy(estado.ssid, ssid, sizeof estado.ssid);
      estado.etapa = EtapaConfigWifi::Probando;
      estado.fallo = FalloConfigWifi::Ninguno;
      portEXIT_CRITICAL(&candado);
      strlcpy(clavePendiente, (const char*)info.prov_cred_recv.password, sizeof clavePendiente);
      Serial.printf("[wifi-app] la app mando la red \"%s\": probando\n", ssid);
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_FAIL: {
      const bool clave = info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR;
      cambiar(EtapaConfigWifi::Fallo, clave ? FalloConfigWifi::Clave : FalloConfigWifi::RedNoEncontrada);
      memset(clavePendiente, 0, sizeof clavePendiente);
      Serial.printf("[wifi-app] no conecto: %s\n", clave ? "clave incorrecta" : "red no encontrada");
      network_prov_mgr_reset_wifi_sm_state_on_failure();  // la app puede corregir y volver a mandar
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      cambiar(EtapaConfigWifi::Lista, FalloConfigWifi::Ninguno);
      guardarPendiente = true;  // la flash se escribe en el loop, no en la tarea de eventos
      break;
    default:
      break;
  }
}

void alEventoBle(void*, esp_event_base_t base, int32_t id, void*) {
  const EtapaConfigWifi etapa = configWifiEstado().etapa;
  if (base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
    if (id == PROTOCOMM_TRANSPORT_BLE_CONNECTED && etapa == EtapaConfigWifi::Esperando) {
      Serial.println("[wifi-app] la app se conecto por Bluetooth");
    } else if (id == PROTOCOMM_TRANSPORT_BLE_DISCONNECTED &&
               (etapa == EtapaConfigWifi::AppConectada || etapa == EtapaConfigWifi::Fallo)) {
      cambiar(EtapaConfigWifi::Esperando, FalloConfigWifi::Ninguno);  // la app se fue: vuelve el QR
      Serial.println("[wifi-app] la app se desconecto");
    }
  } else if (base == PROTOCOMM_SECURITY_SESSION_EVENT) {
    if (id == PROTOCOMM_SECURITY_SESSION_SETUP_OK) {
      cambiar(EtapaConfigWifi::AppConectada, FalloConfigWifi::Ninguno);
    } else if (id == PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH) {
      cambiar(EtapaConfigWifi::Esperando, FalloConfigWifi::CodigoIncorrecto);
      Serial.println("[wifi-app] la app uso un codigo que no es el de la OLED");
    }
  }
}

void iniciarBluetooth() {
  if (!eventosRegistrados) {
    WiFi.onEvent(alEventoProv);
    esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, alEventoBle, nullptr);
    esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, alEventoBle, nullptr);
    eventosRegistrados = true;
  }
  const EstadoConfigWifi e = configWifiEstado();
  Serial.printf("[wifi-app] Bluetooth \"%s\" (memoria libre %u bytes)\n", e.nombreBle, (unsigned)ESP.getFreeHeap());
  // Security 1 con el código de un solo uso; true = descarta lo que haya quedado de una configuración anterior.
  // FREE_BTDM libera la memoria del Bluetooth al terminar.
  WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM, NETWORK_PROV_SECURITY_1,
                          e.pop, e.nombreBle, nullptr, nullptr, true);
  bluetoothIniciado = true;
}

void guardarRed() {
  const EstadoConfigWifi e = configWifiEstado();
  ListaRedes lista;
  configWifiCargarRedes(lista);
  if (!lista.agregar(e.ssid, clavePendiente)) {
    Serial.println("[wifi-app] la red no es valida: no se guarda");
  } else {
    uint8_t buf[ListaRedes::kTamSerializado];
    const size_t n = lista.serializar(buf, sizeof buf);
    Preferences nvs;
    if (n && nvs.begin(kNvsEspacio, false)) {
      nvs.putBytes(kNvsRedes, buf, n);
      nvs.end();
      Serial.printf("[wifi-app] conectado a \"%s\": guardada (%d redes de la app)\n", e.ssid, lista.cantidad());
    }
  }
  memset(clavePendiente, 0, sizeof clavePendiente);
}

}  // namespace

void configWifiCargarRedes(ListaRedes& destino) {
  destino.vaciar();
  Preferences nvs;
  if (!nvs.begin(kNvsEspacio, true)) return;  // solo lectura: falla si el espacio todavía no existe
  uint8_t buf[ListaRedes::kTamSerializado];
  const size_t n = nvs.getBytesLength(kNvsRedes);
  if (n > 0 && n <= sizeof buf && nvs.getBytes(kNvsRedes, buf, n) == n) destino.deserializar(buf, n);
  nvs.end();
}

void configWifiIniciar(uint32_t ahora, const char* motivo) {
  if (configWifiActiva()) return;
  EstadoConfigWifi e;
  e.etapa = EtapaConfigWifi::Preparando;
  e.desdeMs = ahora;
  generarPop(e.pop, azar);
  textoQrWifi(CODIGO_DISPOSITIVO, e.pop, e.qr, sizeof e.qr);
  nombreBle(CODIGO_DISPOSITIVO, e.nombreBle);
  portENTER_CRITICAL(&candado);
  estado = e;
  portEXIT_CRITICAL(&candado);
  // El código también va al monitor serie (quien tiene el puerto ya tiene el wearable en la mano): sirve para
  // probar sin cámara. Las claves Wi-Fi nunca se imprimen.
  Serial.printf("[wifi-app] modo configuracion de Wi-Fi (%s): pausando la red. Bluetooth \"%s\", codigo %s\n", motivo,
                e.nombreBle, e.pop);
  redPausar();
}

void configWifiAtender(uint32_t ahora) {
  const EstadoConfigWifi e = configWifiEstado();
  if (e.etapa == EtapaConfigWifi::Inactiva) return;
  if (e.etapa == EtapaConfigWifi::Preparando && !bluetoothIniciado && redPausada()) iniciarBluetooth();
  if (guardarPendiente) {
    guardarPendiente = false;
    guardarRed();
    listaDesde = ahora;
  }
  // Conectado: unos segundos para que la app lea "conectado" y se reinicia limpio (sin Bluetooth)
  if (e.etapa == EtapaConfigWifi::Lista && listaDesde && ahora - listaDesde > 6000) {
    Serial.println("[wifi-app] reiniciando con la red nueva");
    delay(100);
    ESP.restart();
  }
  if (ahora - e.desdeMs > kMsConfigWifiMaximo) {
    Serial.println("[wifi-app] 10 min sin configurar: se reinicia y vuelve a buscar sus redes");
    delay(100);
    ESP.restart();
  }
}

void configWifiSalir() {
  Serial.println("[wifi-app] cancelado con BOOT: reiniciando");
  delay(100);
  ESP.restart();
}

bool configWifiActiva() { return configWifiEstado().etapa != EtapaConfigWifi::Inactiva; }

EstadoConfigWifi configWifiEstado() {
  portENTER_CRITICAL(&candado);
  const EstadoConfigWifi e = estado;
  portEXIT_CRITICAL(&candado);
  return e;
}
