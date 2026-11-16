// redes.cpp — ver redes.h
#include "redes.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {
const uint8_t kVersionFormato = 1;
}  // namespace

bool ListaRedes::ssidValido(const char* ssid) {
  if (!ssid) return false;
  const size_t n = strlen(ssid);
  return n >= 1 && n <= 32;
}

bool ListaRedes::claveValida(const char* clave) {
  if (!clave) return false;
  const size_t n = strlen(clave);
  return n == 0 || (n >= 8 && n <= 63);
}

bool ListaRedes::agregar(const char* ssid, const char* clave) {
  if (!ssidValido(ssid) || !claveValida(clave)) return false;
  int i = 0;
  while (i < cantidad_ && strcasecmp(redes_[i].ssid, ssid) != 0) i++;
  if (i == cantidad_) {  // nueva: si no hay lugar, sale la más antigua (la última)
    if (cantidad_ < kMax) cantidad_++;
    i = cantidad_ - 1;
  }
  for (int k = i; k > 0; k--) redes_[k] = redes_[k - 1];  // corre las anteriores y deja la nueva primera
  RedGuardada& r = redes_[0];
  memset(&r, 0, sizeof r);
  strncpy(r.ssid, ssid, sizeof r.ssid - 1);
  strncpy(r.clave, clave, sizeof r.clave - 1);
  return true;
}

bool ListaRedes::quitar(const char* ssid) {
  for (int i = 0; i < cantidad_; i++) {
    if (strcasecmp(redes_[i].ssid, ssid) != 0) continue;
    for (int k = i; k + 1 < cantidad_; k++) redes_[k] = redes_[k + 1];
    cantidad_--;
    return true;
  }
  return false;
}

size_t ListaRedes::serializar(uint8_t* buf, size_t tam) const {
  const size_t n = 2 + (size_t)cantidad_ * sizeof(RedGuardada);
  if (tam < n) return 0;
  buf[0] = kVersionFormato;
  buf[1] = (uint8_t)cantidad_;
  memcpy(buf + 2, redes_, (size_t)cantidad_ * sizeof(RedGuardada));
  return n;
}

bool ListaRedes::deserializar(const uint8_t* buf, size_t largo) {
  cantidad_ = 0;
  if (!buf || largo < 2 || buf[0] != kVersionFormato || buf[1] > kMax) return false;
  const int n = buf[1];
  if (largo != 2 + (size_t)n * sizeof(RedGuardada)) return false;
  for (int i = 0; i < n; i++) {
    RedGuardada r;
    memcpy(&r, buf + 2 + i * sizeof(RedGuardada), sizeof r);
    r.ssid[sizeof r.ssid - 1] = 0;  // una flash corrupta no deja un texto sin terminar
    r.clave[sizeof r.clave - 1] = 0;
    if (!ssidValido(r.ssid) || !claveValida(r.clave)) {
      cantidad_ = 0;
      return false;
    }
    redes_[cantidad_++] = r;
  }
  return true;
}

const char kAlfabetoPop[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

void generarPop(char pop[kLargoPop + 1], uint32_t (*azar)()) {
  const uint32_t n = sizeof kAlfabetoPop - 1;
  for (int i = 0; i < kLargoPop; i++) pop[i] = kAlfabetoPop[azar() % n];
  pop[kLargoPop] = 0;
}

size_t textoQrWifi(const char* codigo, const char* pop, char* buf, size_t tam) {
  const int k = snprintf(buf, tam, "VTFW:%s:%s", codigo, pop);
  return k > 0 && (size_t)k < tam ? (size_t)k : 0;
}

bool leerQrWifi(const char* texto, char* codigo, char* pop) {
  if (!texto || strncmp(texto, "VTFW:", 5) != 0) return false;
  const char* c = texto + 5;
  const char* dos = strrchr(c, ':');
  if (!dos || dos == c) return false;
  const size_t largoCodigo = (size_t)(dos - c), largoPop = strlen(dos + 1);
  if (largoCodigo > 32 || largoPop != kLargoPop) return false;
  for (size_t i = 0; i < largoPop; i++)
    if (!strchr(kAlfabetoPop, dos[1 + i])) return false;
  memcpy(codigo, c, largoCodigo);
  codigo[largoCodigo] = 0;
  memcpy(pop, dos + 1, largoPop + 1);
  return true;
}

void nombreBle(const char* codigo, char nombre[kLargoNombreBle + 1]) {
  snprintf(nombre, kLargoNombreBle + 1, "VTF-%s", codigo);
}
