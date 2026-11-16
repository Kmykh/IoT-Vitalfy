// bateria.cpp — ver bateria.h
#include "bateria.h"

namespace {

struct Punto {
  float voltios;
  int porcentaje;
};

// LiPo de 1 celda: voltaje en reposo o con poca carga y la carga que le queda
const Punto kCurva[] = {
    {3.27f, 0},  {3.61f, 5},  {3.69f, 10}, {3.71f, 15}, {3.73f, 20}, {3.75f, 25}, {3.77f, 30},
    {3.79f, 35}, {3.80f, 40}, {3.82f, 45}, {3.84f, 50}, {3.85f, 55}, {3.87f, 60}, {3.91f, 65},
    {3.95f, 70}, {3.98f, 75}, {4.02f, 80}, {4.08f, 85}, {4.11f, 90}, {4.15f, 95}, {4.20f, 100},
};
const int kPuntos = sizeof kCurva / sizeof kCurva[0];

}  // namespace

MedidorBateria::MedidorBateria(ParametrosBateria p) : p_(p) {}

void MedidorBateria::reiniciar() {
  voltios_ = 0;
  porcentaje_ = -1;
  estado_ = EstadoBateria::SinMedicion;
  invalidas_ = 0;
}

int MedidorBateria::porcentajeDe(float v) {
  if (v <= kCurva[0].voltios) return 0;
  if (v >= kCurva[kPuntos - 1].voltios) return 100;
  for (int i = 1; i < kPuntos; i++) {
    if (v > kCurva[i].voltios) continue;
    const Punto a = kCurva[i - 1], b = kCurva[i];
    const float t = (v - a.voltios) / (b.voltios - a.voltios);
    return (int)(a.porcentaje + t * (b.porcentaje - a.porcentaje) + 0.5f);
  }
  return 100;
}

bool MedidorBateria::agregarMvPin(int mvPin) {
  const int porcentajeAntes = porcentaje_;
  const EstadoBateria estadoAntes = estado_;
  const float v = mvPin / 1000.0f * p_.divisor * p_.correccion;

  if (mvPin < 0 || v < kMinimoValido || v > kMaximoValido) {
    // Tres lecturas fuera de rango seguidas: el divisor no está (o se soltó). Una sola es ruido.
    if (invalidas_ < 3 && ++invalidas_ == 3) {
      reiniciar();
      invalidas_ = 3;
    }
    return porcentaje_ != porcentajeAntes || estado_ != estadoAntes;
  }
  invalidas_ = 0;

  voltios_ = voltios_ <= 0 ? v : voltios_ + (v - voltios_) * kSuavizado;
  const int nuevo = porcentajeDe(voltios_);
  if (porcentaje_ < 0 || nuevo - porcentaje_ >= kHisteresisPct || porcentaje_ - nuevo >= kHisteresisPct ||
      (nuevo == 0) != (porcentaje_ == 0) || (nuevo == 100) != (porcentaje_ == 100))
    porcentaje_ = nuevo;

  estado_ = porcentaje_ <= p_.criticaPct ? EstadoBateria::Critica
            : porcentaje_ <= p_.bajaPct  ? EstadoBateria::Baja
                                         : EstadoBateria::Normal;
  return porcentaje_ != porcentajeAntes || estado_ != estadoAntes;
}
