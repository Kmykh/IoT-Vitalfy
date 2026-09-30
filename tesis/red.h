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
//     (device/<codigo>/estado = "offline"). El broker se ubica por nombre mDNS y se vuelve a
//     resolver si la IP de la laptop cambió.
//   · Búfer: sin red o sin broker, las lecturas se guardan (CAPACIDAD_BUFER_OFFLINE) y se envían
//     en orden al volver. Una lectura sale del búfer solo cuando el broker confirma (PUBACK).
#pragma once

#include <stdint.h>

#include "telemetria.h"

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
};

void redIniciar();                  // crea la tarea de red
bool redEncolar(const Lectura& l);  // no bloquea; false si la cola está llena
EstadoRed redEstado();              // copia consistente del estado actual
