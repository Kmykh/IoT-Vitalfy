// redes.h — Redes Wi-Fi que el administrador configuró desde la app por Bluetooth (config_wifi.h), y los
// textos del modo de configuración: el código de un solo uso, el QR y el nombre Bluetooth del wearable.
//
// Las redes se guardan en la flash (NVS) y se suman a las de secretos.h: el wearable se une a la conocida
// con mejor señal, sea de donde sea. Se guardan hasta 5, la más reciente primero; al agregar una sexta sale
// la más antigua. Las claves nunca se imprimen.
//
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stddef.h>
#include <stdint.h>

struct RedGuardada {
  char ssid[33];   // hasta 32 bytes (802.11)
  char clave[65];  // vacía (red abierta) o de 8 a 63 caracteres (WPA2)
};

class ListaRedes {
 public:
  static const int kMax = 5;

  // Valida el nombre y la clave. Si ya estaba (mismo nombre sin distinguir mayúsculas), actualiza la clave
  // y la pasa al principio; si no, la agrega al principio y, sin lugar, saca la más antigua.
  bool agregar(const char* ssid, const char* clave);
  bool quitar(const char* ssid);
  void vaciar() { cantidad_ = 0; }
  int cantidad() const { return cantidad_; }
  const RedGuardada& red(int i) const { return redes_[i]; }

  static bool ssidValido(const char* ssid);
  static bool claveValida(const char* clave);

  // Formato en la NVS: versión, cantidad y las redes de tamaño fijo. 0 si no cabe.
  static const size_t kTamSerializado = 2 + kMax * sizeof(RedGuardada);
  size_t serializar(uint8_t* buf, size_t tam) const;
  bool deserializar(const uint8_t* buf, size_t largo);  // false (y queda vacía) si el formato no es válido

 private:
  RedGuardada redes_[kMax];
  int cantidad_ = 0;
};

// Código de un solo uso (proof of possession del protocolo de Espressif): 8 caracteres sin los que se
// confunden en la OLED (0/O, 1/I/L). Lo ve solo quien tiene el wearable en la mano.
const int kLargoPop = 8;
extern const char kAlfabetoPop[];
void generarPop(char pop[kLargoPop + 1], uint32_t (*azar)());

// Contenido del QR de configuración: "VTFW:{codigo}:{pop}" (alfanumérico: el QR más chico posible).
// Distinto del QR de vinculación ("VTF1:..."), así la app sabe cuál es cuál.
size_t textoQrWifi(const char* codigo, const char* pop, char* buf, size_t tam);
// Lee un QR de configuración; false si no lo es. codigo y pop deben tener lugar para 33 y 9 bytes.
bool leerQrWifi(const char* texto, char* codigo, char* pop);

// Nombre Bluetooth: "VTF-" y el código, recortado a 20 caracteres (entra en el aviso BLE). La app lo arma
// igual a partir del código del QR.
const int kLargoNombreBle = 20;
void nombreBle(const char* codigo, char nombre[kLargoNombreBle + 1]);
