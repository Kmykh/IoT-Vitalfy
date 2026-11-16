// fr_cardiaca.cpp — ver fr_cardiaca.h
#include "fr_cardiaca.h"

#include <math.h>
#include <stdlib.h>

namespace {

const int kMaxMuestras = 160;  // 32 s a 4 Hz = 128 (con margen)

// Interpola linealmente la serie (t, v) a kFs, desde t[0]
int interpolar(const float* t, const float* v, int n, float* salida) {
  const float paso = 1.0f / FrCardiaca::kFs;
  int m = 0, j = 0;
  for (float x = t[0]; x <= t[n - 1] && m < kMaxMuestras; x += paso) {
    while (j + 1 < n - 1 && t[j + 1] < x) j++;
    const float dt = t[j + 1] - t[j];
    const float u = dt > 0 ? (x - t[j]) / dt : 0;
    salida[m++] = v[j] + u * (v[j + 1] - v[j]);
  }
  return m;
}

}  // namespace

bool FrCardiaca::frDeSerie(const float* t, const float* v, int n, float* rpm, float* correlacion) {
  *correlacion = 0;
  if (n < 8) return false;
  // Estáticos: el loop del ESP32 tiene 8 KB de pila (solo se llama desde el loop)
  static float x[kMaxMuestras], y[kMaxMuestras], r[kMaxMuestras / 2 + 2];
  const int m = interpolar(t, v, n, x);
  if (m < 64) return false;  // al menos 16 s

  // Tendencia lineal fuera y energía
  const float centro = 0.5f * (float)(m - 1);
  float media = 0, pendiente = 0, sumaT2 = 0;
  for (int i = 0; i < m; i++) {
    media += x[i];
    pendiente += ((float)i - centro) * x[i];
    sumaT2 += ((float)i - centro) * ((float)i - centro);
  }
  media /= (float)m;
  pendiente /= sumaT2;
  float energia = 0;
  for (int i = 0; i < m; i++) {
    x[i] -= media + pendiente * ((float)i - centro);
    energia += x[i] * x[i];
  }
  if (energia <= 1e-12f) return false;

  // Suavizado de 3 muestras (0,75 s): lo que oscila más rápido que la respiración no es respiración
  for (int i = 0; i < m; i++) {
    const float a = x[i > 0 ? i - 1 : i], b = x[i], c = x[i + 1 < m ? i + 1 : i];
    y[i] = (a + 2 * b + c) * 0.25f;
  }

  // Autocorrelación normalizada para períodos de 1,5 a 10 s (40 a 6 rpm)
  const int kMin = (int)(60.0f * kFs / kMaxRpm);
  int kMax = (int)ceilf(60.0f * kFs / kMinRpm);
  if (kMax + 1 > m / 2) kMax = m / 2 - 1;  // al menos dos ciclos
  if (kMax <= kMin) return false;
  for (int k = 1; k <= kMax + 1; k++) {
    float suma = 0, e1 = 0, e2 = 0;
    for (int i = 0; i + k < m; i++) {
      suma += y[i] * y[i + k];
      e1 += y[i] * y[i];
      e2 += y[i + k] * y[i + k];
    }
    const float den = sqrtf(e1 * e2);
    r[k] = den > 0 ? suma / den : 0;
  }

  // El primer pico después de un valle negativo que llegue al 80 % del más alto (no un armónico)
  float minimoAntes = 1.0f, mejor = 0;
  int candidatos[48], nCandidatos = 0;
  for (int k = 2; k <= kMax; k++) {
    if (k >= kMin && r[k] >= r[k - 1] && r[k] > r[k + 1] && r[k] >= kCorrelacionMinima && minimoAntes < 0 &&
        nCandidatos < 48) {
      candidatos[nCandidatos++] = k;
      if (r[k] > mejor) mejor = r[k];
    }
    if (r[k] < minimoAntes) minimoAntes = r[k];
  }
  int k = -1;
  for (int i = 0; i < nCandidatos && k < 0; i++)
    if (r[candidatos[i]] >= 0.8f * mejor) k = candidatos[i];
  if (k < 0) return false;
  const float a = r[k - 1], b = r[k], d = r[k + 1];
  const float curvatura = a - 2 * b + d;
  float retardo = (float)k;
  if (curvatura < 0) retardo += 0.5f * (a - d) / curvatura;
  *correlacion = b;
  *rpm = 60.0f * kFs / retardo;
  return *rpm >= kMinRpm && *rpm <= kMaxRpm;
}

void FrCardiaca::agregar(float t, float amplitud, float base) {
  if (cantidad_ > 0) {
    const Latido& ultimo = latidos_[(inicio_ + cantidad_ - 1) % kMaxLatidos];
    if (t <= ultimo.t || t - ultimo.t > 3.0f) reiniciar();  // reloj que vuelve o 3 s sin latidos: de nuevo
  }
  if (cantidad_ == kMaxLatidos) {
    inicio_ = (inicio_ + 1) % kMaxLatidos;
    cantidad_--;
  }
  latidos_[(inicio_ + cantidad_) % kMaxLatidos] = {t, amplitud, base};
  cantidad_++;
}

void FrCardiaca::reiniciar() {
  inicio_ = cantidad_ = 0;
  estimaciones_.limpiar();
  tValida_ = -1;
  fr_ = -1;
  modulaciones_ = 0;
  ultimas_[0] = ultimas_[1] = ultimas_[2] = 0;
}

bool FrCardiaca::estimar() {
  if (cantidad_ < 2) return false;
  const float tFin = latidos_[(inicio_ + cantidad_ - 1) % kMaxLatidos].t;
  if (fr_ > 0 && tFin - tValida_ > kVigencia) fr_ = -1;

  // Los latidos de la ventana
  static float t[kMaxLatidos], rr[kMaxLatidos], am[kMaxLatidos], base[kMaxLatidos];  // fuera de la pila
  int n = 0;
  for (int i = 0; i < cantidad_; i++) {
    const Latido& l = latidos_[(inicio_ + i) % kMaxLatidos];
    if (l.t < tFin - kVentana) continue;
    t[n] = l.t;
    am[n] = l.amplitud;
    base[n] = l.base;
    n++;
  }
  if (n < 16 || t[n - 1] - t[0] < 0.75f * kVentana) return false;

  // RR y su mediana; los intervalos raros (latido perdido o extra) no entran en la FM
  static float rrOrdenados[kMaxLatidos];
  for (int i = 1; i < n; i++) rr[i] = rrOrdenados[i - 1] = t[i] - t[i - 1];
  qsort(rrOrdenados, n - 1, sizeof(float), [](const void* a, const void* b) {
    const float x = *(const float*)a, y = *(const float*)b;
    return (x > y) - (x < y);
  });
  const float rrMediana = rrOrdenados[(n - 1) / 2];
  const float fc = 60.0f / rrMediana;
  static float tFm[kMaxLatidos], vFm[kMaxLatidos];
  int nFm = 0, raros = 0;
  for (int i = 1; i < n; i++) {
    if (rr[i] < 0.75f * rrMediana || rr[i] > 1.3f * rrMediana) {
      raros++;
      continue;
    }
    tFm[nFm] = 0.5f * (t[i] + t[i - 1]);
    vFm[nFm++] = rr[i];
  }

  float estimado[3];
  bool valido[3] = {false, false, false};
  float corr;
  if (raros * 5 <= n) valido[0] = frDeSerie(tFm, vFm, nFm, &estimado[0], &corr);
  valido[1] = frDeSerie(t, am, n, &estimado[1], &corr);
  if (conBase_) valido[2] = frDeSerie(t, base, n, &estimado[2], &corr);
  for (int i = 0; i < 3; i++) {
    if (valido[i] && estimado[i] > fc / 2.5f) valido[i] = false;  // muy pocos latidos por respiración
    ultimas_[i] = valido[i] ? estimado[i] : 0;
  }

  // Fusión inteligente: el grupo más grande (al menos 2) que coincide dentro de 4 rpm
  float mejorSuma = 0;
  int mejorCantidad = 0;
  for (int i = 0; i < 3; i++) {
    if (!valido[i]) continue;
    float suma = 0;
    int cantidad = 0;
    for (int j = 0; j < 3; j++)
      if (valido[j] && fabsf(estimado[j] - estimado[i]) <= kAcuerdo) {
        suma += estimado[j];
        cantidad++;
      }
    if (cantidad > mejorCantidad) {
      mejorCantidad = cantidad;
      mejorSuma = suma;
    }
  }
  modulaciones_ = mejorCantidad;
  if (mejorCantidad < 2) return false;
  estimaciones_.agregar(mejorSuma / (float)mejorCantidad);
  fr_ = (int)lroundf(estimaciones_.medianaUltimos(3));
  tValida_ = tFin;
  return true;
}

int fusionarFr(int frMpu, int frEcg, int frPpg, FuenteFr* fuente) {
  // El pulso: el ECG marca el instante del latido con más precisión que el PPG
  int pulso = -1;
  FuenteFr fuentePulso = FuenteFr::Ninguna;
  if (frEcg > 0 && frPpg > 0) {
    pulso = abs(frEcg - frPpg) <= (int)FrCardiaca::kAcuerdo ? (frEcg + frPpg + 1) / 2 : frEcg;
    fuentePulso = FuenteFr::Ecg;
  } else if (frEcg > 0) {
    pulso = frEcg;
    fuentePulso = FuenteFr::Ecg;
  } else if (frPpg > 0) {
    pulso = frPpg;
    fuentePulso = FuenteFr::Ppg;
  }

  if (frMpu > 0 && pulso > 0 && abs(frMpu - pulso) <= (int)FrCardiaca::kAcuerdo) {
    *fuente = FuenteFr::MpuYPulso;
    return (int)lroundf((2.0f * (float)frMpu + (float)pulso) / 3.0f);
  }
  if (frMpu > 0) {
    *fuente = FuenteFr::Mpu;
    return frMpu;
  }
  *fuente = fuentePulso;
  return pulso;
}
