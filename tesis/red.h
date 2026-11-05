// red.h — Conectividad del wearable: Wi-Fi, hora NTP y MQTT hacia el broker del backend.
//
// Corre en su propia tarea en el núcleo 0: los sensores y las pantallas (loop, núcleo 1) nunca
// esperan a la red. El loop solo entrega lecturas a una cola y consulta el estado.
//
//   · Wi-Fi: escanea, se une a la red conocida con mejor señal y, si se cae, vuelve a buscar
//     (espera creciente de 2 a 30 s entre intentos).
//   · Hora: NTP. Cada lectura guarda el instante en que se midió; si se midió sin hora, se le
//     asigna al sincronizar (el backend exige "ts" real y deduplica por dispositivo + ts).
//   · MQTT: cliente nativo de ESP-IDF (esp-mqtt) con QoS 1, reconexión automática y "last will"
//     (device/<codigo>/estado = "offline"). En desarrollo, el broker se ubica por nombre mDNS y se
//     vuelve a resolver si la IP de la laptop cambió. En producción (MQTT_BROKER_TLS en secretos.h)
//     va al dominio del servidor por el 8883 con TLS, después de tener hora NTP (raices_tls.h).
//   · Búfer: sin red o sin broker, las lecturas se guardan (CAPACIDAD_BUFER_OFFLINE) y se envían
//     en orden al volver. Una lectura sale del búfer solo cuando el broker confirma (PUBACK).
//   · Vinculación (vinculacion.h): se suscribe a device/<codigo>/comandos. Solo publica telemetría
//     vinculado a un paciente; al dejar de estarlo descarta el búfer; pide un QR cuando hace falta.
#pragma once

#include <stdint.h>

#include "telemetria.h"
#include "vinculacion.h"

enum class EstadoWifi : uint8_t { Buscando, Conectando, Conectado };
enum class EstadoBroker : uint8_t { Desconectado, Conectando, Conectado, Rechazado };

struct EstadoRed {
  EstadoWifi wifi = EstadoWifi::Buscando;
  int8_t rssi = 0;               // dBm
  char ssid[33] = "";
  char ip[16] = "";
  EstadoBroker broker = EstadoBroker::Desconectado;
  char brokerIp[16] = "";
  bool horaSincronizada = false;
  uint16_t pendientes = 0;       // lecturas en el búfer sin confirmar
  uint32_t enviadas = 0;         // confirmadas por el broker
  uint32_t perdidas = 0;         // descartadas por búfer lleno
  char aviso[22] = "";           // último problema de conectividad (para la OLED)

  // Vinculación con un paciente (comandos del backend)
  EstadoVinculo vinculo = EstadoVinculo::Desconocido;
  char qr[72] = "";              // contenido del QR que se muestra
  int64_t qrExpiraMs = 0;        // UTC en ms (0 = sin dato)
  char cama[24] = "";
  char iniciales[12] = "";
  char avisoComando[64] = "";    // "Repita: presion" (repetir-medicion)
  uint32_t avisoComandoMs = 0;   // millis() en que llegó
};

void redIniciar();                  // crea la tarea de red
bool redEncolar(const Lectura& l);  // no bloquea; false si la cola está llena
EstadoRed redEstado();              // copia consistente del estado actual
void redPedirQr();                  // el botón: pide un QR nuevo (si se está esperando el escaneo)

// Las lecturas nuevas se guardan solo vinculado a un paciente o mientras no se sabe (recién encendido)
inline bool redAceptaLecturas(const EstadoRed& r) {
  return r.vinculo == EstadoVinculo::Vinculado || r.vinculo == EstadoVinculo::Desconocido;
}
