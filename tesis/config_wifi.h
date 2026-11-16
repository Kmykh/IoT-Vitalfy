// config_wifi.h — Modo de configuración del Wi-Fi por Bluetooth, desde la app con la cuenta de administrador.
//
// Sin red el backend no puede mandarle nada al wearable, así que la app le habla directo por Bluetooth con
// el protocolo de aprovisionamiento de Espressif (protocomm, Security 1: Curve25519 + AES-CTR):
//
//   1. Se entra con BOOT apretado 3 s o, solo, si en 1 min desde el encendido no se unió a ninguna red
//      conocida y no está con un paciente.
//   2. La OLED muestra un QR "VTFW:{codigo}:{pop}" con un código de un solo uso (pop): solo quien tiene el
//      wearable en la mano puede configurarlo. La app lo escanea, se conecta por Bluetooth ("VTF-{codigo}")
//      y le pide al wearable las redes que él ve (con señal y candado).
//   3. El administrador elige una y escribe la clave; el wearable la prueba y la app dice si conectó o por
//      qué no (clave incorrecta, red no encontrada) para corregirla ahí mismo.
//   4. Conectado, la guarda en la flash (redes.h, hasta 5) y se reinicia para volver a lo normal.
//
// Mientras dura, la tarea de red se pausa (sin MQTT). Se sale con BOOT (se reinicia) o solo a los 10 min.
// El Bluetooth solo se enciende en este modo: el resto del tiempo no ocupa memoria ni radio.
#pragma once

#include <stdint.h>

#include "redes.h"

enum class EtapaConfigWifi : uint8_t {
  Inactiva,
  Preparando,    // esperando que la tarea de red suelte el Wi-Fi
  Esperando,     // el QR en la OLED, esperando a la app
  AppConectada,  // la app se conectó por Bluetooth (con el código correcto)
  Probando,      // llegaron la red y la clave: se está conectando
  Fallo,         // no conectó: la app puede corregir y reintentar
  Lista,         // conectó y la guardó: se reinicia en unos segundos
};

enum class FalloConfigWifi : uint8_t { Ninguno, Clave, RedNoEncontrada, CodigoIncorrecto };

struct EstadoConfigWifi {
  EtapaConfigWifi etapa = EtapaConfigWifi::Inactiva;
  FalloConfigWifi fallo = FalloConfigWifi::Ninguno;
  char pop[kLargoPop + 1] = "";
  char qr[48] = "";
  char nombreBle[kLargoNombreBle + 1] = "";
  char ssid[33] = "";            // la red que se está probando (nunca la clave)
  uint32_t desdeMs = 0;          // millis() al entrar
};

// Carga las redes guardadas de la flash (la tarea de red las usa al arrancar)
void configWifiCargarRedes(ListaRedes& destino);

void configWifiIniciar(uint32_t ahora, const char* motivo);
void configWifiAtender(uint32_t ahora);  // en el loop: arranca el Bluetooth, guarda, reinicia
void configWifiSalir();                  // BOOT: se reinicia sin guardar nada
bool configWifiActiva();
EstadoConfigWifi configWifiEstado();

const uint32_t kMsConfigWifiMaximo = 10UL * 60 * 1000;  // sin terminar en 10 min, se reinicia
