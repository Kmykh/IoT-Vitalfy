// temperatura.cpp — ver temperatura.h
#include "temperatura.h"

#include <math.h>

EvaluadorTemperatura::EvaluadorTemperatura(const ParametrosTemperatura& p) : p_(p) {}

void EvaluadorTemperatura::sinSonda() {
  estado_ = EstadoTemperatura::SinSonda;
  n_ = pos_ = nPred_ = posPred_ = 0;
  predicha_ = false;
}

// Ajuste por mínimos cuadrados de T(t) = T∞ − B·e^(−t/τ) a las lecturas en contacto (ver temperatura.h)
bool EvaluadorTemperatura::predecir(float* equilibrio) const {
  const int n = n_;
  if (n < 12) return false;
  const Muestra& primera = hist_[(pos_ - n + kMuestras) % kMuestras];
  const Muestra& ultima = hist_[(pos_ - 1 + kMuestras) % kMuestras];
  float mejorError = 1e9f, mejorT = 0, mejorTau = 0;
  for (float tau = 10.0f; tau <= 300.0f; tau *= 1.15f) {
    // Regresión lineal y = a + b·x con x = e^(−t/τ), t en s desde la primera lectura: T∞ = a, B = −b
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) {
      const Muestra& m = hist_[(pos_ - n + i + kMuestras) % kMuestras];
      const float x = expf(-(float)(m.ms - primera.ms) / 1000.0f / tau);
      sx += x;
      sy += m.c;
      sxx += x * x;
      sxy += x * m.c;
    }
    const float det = n * sxx - sx * sx;
    if (fabsf(det) < 1e-6f) continue;
    const float b = (n * sxy - sx * sy) / det, a = (sy - b * sx) / n;
    float error = 0;
    for (int i = 0; i < n; i++) {
      const Muestra& m = hist_[(pos_ - n + i + kMuestras) % kMuestras];
      const float e = m.c - (a + b * expf(-(float)(m.ms - primera.ms) / 1000.0f / tau));
      error += e * e;
    }
    if (error < mejorError) {
      mejorError = error;
      mejorT = a;
      mejorTau = tau;
    }
  }
  if (sqrtf(mejorError / n) > p_.errorAjusteMax) return false;  // las lecturas no siguen la curva
  // Con τ en el extremo de la grilla la curva casi no se frena: extrapolar sería adivinar
  if (mejorTau > 260.0f && fabsf(mejorT - ultima.c) > 0.2f) return false;
  // Lejos del equilibrio un error pequeño en τ cambia mucho T∞: se predice solo el último tramo
  if (mejorT < ultima.c - 0.5f || mejorT > ultima.c + p_.extrapolacionMax) return false;
  *equilibrio = mejorT;
  return true;
}

void EvaluadorTemperatura::agregar(float celsius, uint32_t ms) {
  if (celsius < p_.minimoContacto) {  // al aire: se olvida la historia
    estado_ = EstadoTemperatura::SinContacto;
    valor_ = redondear(celsius + p_.correccion);
    n_ = pos_ = nPred_ = posPred_ = 0;
    predicha_ = false;
    return;
  }
  if (estado_ == EstadoTemperatura::SinSonda || estado_ == EstadoTemperatura::SinContacto) inicioContacto_ = ms;

  hist_[pos_] = {ms, celsius};
  pos_ = (pos_ + 1) % kMuestras;
  if (n_ < kMuestras) n_++;
  const uint32_t enContacto = ms - inicioContacto_;

  // Confirmación: la medición directa ya no cambia
  float minimo = celsius, maximo = celsius;
  for (int i = 0; i < n_; i++) {
    const Muestra& m = hist_[(pos_ - 1 - i + kMuestras) % kMuestras];
    if (ms - m.ms > p_.ventanaMs) break;
    if (m.c < minimo) minimo = m.c;
    if (m.c > maximo) maximo = m.c;
  }
  if (enContacto >= p_.ventanaMs && maximo - minimo <= p_.variacionMax) {
    estado_ = EstadoTemperatura::Estable;
    valor_ = redondear(celsius + p_.correccion);
    predicha_ = false;
    ultimaConfirmacion_ = ms;
    return;
  }

  // Predicción: las últimas estimaciones del equilibrio deben coincidir
  float equilibrio;
  if (enContacto >= p_.minimoPrediccionMs && predecir(&equilibrio)) {
    predicciones_[posPred_] = equilibrio;
    msPredicciones_[posPred_] = ms;
    posPred_ = (posPred_ + 1) % kPredicciones;
    if (nPred_ < kPredicciones) nPred_++;
  } else {
    nPred_ = posPred_ = 0;  // una curva que no se ajusta reinicia el acuerdo
  }
  if (nPred_ == kPredicciones) {
    float pmin = predicciones_[0], pmax = predicciones_[0], suma = 0;
    uint32_t primera = msPredicciones_[0];
    for (int i = 0; i < kPredicciones; i++) {
      if (predicciones_[i] < pmin) pmin = predicciones_[i];
      if (predicciones_[i] > pmax) pmax = predicciones_[i];
      if ((int32_t)(msPredicciones_[i] - primera) < 0) primera = msPredicciones_[i];
      suma += predicciones_[i];
    }
    if (pmax - pmin <= p_.acuerdoPrediccion && ms - primera >= p_.acuerdoMs) {
      estado_ = EstadoTemperatura::Estable;
      valor_ = redondear(suma / kPredicciones + p_.correccion);
      predicha_ = true;
      ultimaConfirmacion_ = ms;
      return;
    }
  }

  // Sin resultado nuevo, un valor estable se conserva (una predicción suelta que falla no debe hacer
  // parpadear "midiendo"): 20 s, mientras la medición no se aleje, o, si fue predicho, mientras la
  // sonda siga por debajo (todavía está llegando al valor predicho).
  const float medido = celsius + p_.correccion;
  if (estado_ == EstadoTemperatura::Estable &&
      (ms - ultimaConfirmacion_ < 20000 || fabsf(medido - valor_) <= 0.3f || (predicha_ && medido <= valor_ + 0.3f)))
    return;
  estado_ = EstadoTemperatura::Estabilizando;
  valor_ = redondear(celsius + p_.correccion);
  predicha_ = false;
}

RangoTemperatura EvaluadorTemperatura::clasificar(float c) {
  const float t = redondear(c);  // el backend también trabaja con un decimal
  if (t <= 35.0f) return RangoTemperatura::Hipotermia;
  if (t <= 36.0f) return RangoTemperatura::Baja;
  if (t <= 38.0f) return RangoTemperatura::Normal;
  if (t <= 39.0f) return RangoTemperatura::Fiebre;
  return RangoTemperatura::FiebreAlta;
}

const char* EvaluadorTemperatura::nombre(RangoTemperatura r) {
  switch (r) {
    case RangoTemperatura::Hipotermia:
      return "Hipotermia";
    case RangoTemperatura::Baja:
      return "Baja";
    case RangoTemperatura::Normal:
      return "Normal";
    case RangoTemperatura::Fiebre:
      return "Fiebre";
    default:
      return "Fiebre alta";
  }
}
