// respiracion.cpp — ver respiracion.h
#include "respiracion.h"

#include <math.h>

namespace {
// Señal proyectada y autocorrelación del último análisis: fuera de la pila (la del loop es de 8 KB).
float proyeccion[MonitorRespiracion::kMaxVentana];
float autocorr[MonitorRespiracion::kMaxVentana];
}  // namespace

MonitorRespiracion::MonitorRespiracion(const ParametrosRespiracion& p) : p_(p) {
  if (p_.decimacion < 1) p_.decimacion = 1;
  fsDec_ = p_.fs / (float)p_.decimacion;
  largo_ = (int)(p_.ventana * fsDec_ + 0.5f);
  if (largo_ > kMaxVentana) largo_ = kMaxVentana;
  cadaMuestras_ = (int)(p_.cadaCuanto * fsDec_ + 0.5f);
  if (cadaMuestras_ < 1) cadaMuestras_ = 1;
  vigenciaMuestras_ = (int)(p_.vigencia * fsDec_ + 0.5f);
  fx_ = fy_ = fz_ = Biquad::pasaBajos(fsDec_, p_.corte);
  reiniciar();
}

void MonitorRespiracion::reiniciar() {
  enBloque_ = 0;
  sx_ = sy_ = sz_ = sMag_ = sMag2_ = 0;
  filtrosAsentados_ = false;
  pos_ = quietas_ = 0;
  hayAnterior_ = false;
  desdeAnalisis_ = 0;
  desdeValida_ = vigenciaMuestras_ + 1;
  estimaciones_.limpiar();
  fr_ = -1;
  ultima_ = correlacion_ = amplitud_ = 0;
  estado_ = EstadoRespiracion::Midiendo;
}

void MonitorRespiracion::procesar(float ax, float ay, float az) {
  const float mag = sqrtf(ax * ax + ay * ay + az * az);
  sx_ += ax;
  sy_ += ay;
  sz_ += az;
  sMag_ += mag;
  sMag2_ += mag * mag;
  if (++enBloque_ < p_.decimacion) return;

  const float n = (float)enBloque_;
  const float x = sx_ / n, y = sy_ / n, z = sz_ / n, media = sMag_ / n;
  const float varianza = sMag2_ / n - media * media;
  bool movimiento = sqrtf(varianza > 0 ? varianza : 0) > p_.desvioMovimiento;
  if (hayAnterior_)
    movimiento |= fabsf(x - antX_) > p_.saltoMovimiento || fabsf(y - antY_) > p_.saltoMovimiento ||
                  fabsf(z - antZ_) > p_.saltoMovimiento;
  antX_ = x;
  antY_ = y;
  antZ_ = z;
  hayAnterior_ = true;
  enBloque_ = 0;
  sx_ = sy_ = sz_ = sMag_ = sMag2_ = 0;
  agregarBloque(x, y, z, movimiento);
}

void MonitorRespiracion::agregarBloque(float x, float y, float z, bool movimiento) {
  if (!filtrosAsentados_) {
    fx_.asentar(x);
    fy_.asentar(y);
    fz_.asentar(z);
    filtrosAsentados_ = true;
  }
  x_[pos_] = fx_.procesar(x);
  y_[pos_] = fy_.procesar(y);
  z_[pos_] = fz_.procesar(z);
  pos_ = (pos_ + 1) % largo_;

  if (movimiento) {
    quietas_ = 0;
    estimaciones_.limpiar();  // lo de antes del movimiento no se mezcla con lo de después
    estado_ = EstadoRespiracion::Movimiento;
  } else if (quietas_ < largo_) {
    quietas_++;
  }

  if (desdeValida_ <= vigenciaMuestras_) desdeValida_++;
  if (desdeValida_ > vigenciaMuestras_) fr_ = -1;  // la última FR ya no representa al paciente

  if (++desdeAnalisis_ >= cadaMuestras_) {
    desdeAnalisis_ = 0;
    analizar();
  }
}

void MonitorRespiracion::analizar() {
  if (quietas_ < largo_) {  // la ventana todavía tiene movimiento o no está completa
    if (estado_ != EstadoRespiracion::Movimiento || quietas_ >= cadaMuestras_) estado_ = EstadoRespiracion::Midiendo;
    return;
  }
  float rpm = 0;
  if (!estimar(&rpm)) {
    estado_ = amplitud_ > p_.amplitudMaxima ? EstadoRespiracion::Movimiento : EstadoRespiracion::SinSenal;
    return;
  }
  ultima_ = rpm;
  estimaciones_.agregar(rpm);
  fr_ = (int)lroundf(estimaciones_.medianaUltimos(3));
  desdeValida_ = 0;
  estado_ = EstadoRespiracion::Valida;
}

bool MonitorRespiracion::estimar(float* rpm) {
  const int n = largo_;
  const int inicio = pos_;  // el anillo está lleno: pos_ es la muestra más antigua
  const auto muestra = [&](const float* v, int i) { return v[(inicio + i) % n]; };

  // 1. Tendencia lineal de cada eje (gravedad y deriva lenta): y = media + pendiente·t, t centrado
  const float centro = 0.5f * (float)(n - 1);
  float sumaT2 = 0;
  float media[3] = {0, 0, 0}, pendiente[3] = {0, 0, 0};
  const float* ejes[3] = {x_, y_, z_};
  for (int i = 0; i < n; i++) {
    const float t = (float)i - centro;
    sumaT2 += t * t;
    for (int e = 0; e < 3; e++) {
      const float v = muestra(ejes[e], i);
      media[e] += v;
      pendiente[e] += t * v;
    }
  }
  for (int e = 0; e < 3; e++) {
    media[e] /= (float)n;
    pendiente[e] /= sumaT2;
  }
  const auto sinTendencia = [&](int e, int i) {
    return muestra(ejes[e], i) - media[e] - pendiente[e] * ((float)i - centro);
  };

  // 2. Covarianza 3x3 y su vector propio principal (iteración de potencia)
  float c[3][3] = {};
  for (int i = 0; i < n; i++) {
    const float d[3] = {sinTendencia(0, i), sinTendencia(1, i), sinTendencia(2, i)};
    for (int a = 0; a < 3; a++)
      for (int b = a; b < 3; b++) c[a][b] += d[a] * d[b];
  }
  c[1][0] = c[0][1];
  c[2][0] = c[0][2];
  c[2][1] = c[1][2];
  float v[3] = {0.577f, 0.577f, 0.577f};
  for (int it = 0; it < 30; it++) {
    float w[3];
    for (int a = 0; a < 3; a++) w[a] = c[a][0] * v[0] + c[a][1] * v[1] + c[a][2] * v[2];
    const float norma = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (norma < 1e-12f) {
      amplitud_ = 0;
      correlacion_ = 0;
      return false;
    }
    for (int a = 0; a < 3; a++) v[a] = w[a] / norma;
  }

  // 3. Señal respiratoria: proyección sobre esa dirección, y su energía acumulada
  float energia = 0;
  for (int i = 0; i < n; i++) {
    proyeccion[i] = v[0] * sinTendencia(0, i) + v[1] * sinTendencia(1, i) + v[2] * sinTendencia(2, i);
    energia += proyeccion[i] * proyeccion[i];
  }
  amplitud_ = sqrtf(energia / (float)n);
  correlacion_ = 0;
  if (amplitud_ < p_.amplitudMinima || amplitud_ > p_.amplitudMaxima) return false;

  // 4. Autocorrelación normalizada para los retardos posibles
  const int kMin = (int)(60.0f * fsDec_ / p_.maxRpm);
  int kMax = (int)ceilf(60.0f * fsDec_ / p_.minRpm);
  if (kMax + 1 > n / 2) kMax = n / 2 - 1;  // al menos dos ciclos en la ventana
  if (kMin < 2 || kMax <= kMin) return false;
  for (int k = 1; k <= kMax + 1; k++) {
    float suma = 0, e1 = 0, e2 = 0;
    for (int i = 0; i + k < n; i++) {
      suma += proyeccion[i] * proyeccion[i + k];
      e1 += proyeccion[i] * proyeccion[i];
      e2 += proyeccion[i + k] * proyeccion[i + k];
    }
    const float den = sqrtf(e1 * e2);
    autocorr[k] = den > 0 ? suma / den : 0;
  }

  // 5. Picos que siguen a un valle (oscilación real) y son lo bastante altos; el primero que llega al
  //    80 % del más alto (no un armónico de un ciclo con dos jorobas)
  float minimoAntes = 1.0f, mejor = 0;
  int candidatos[32], nCandidatos = 0;
  for (int k = 1; k <= kMax; k++) {
    if (k >= kMin && k >= 2 && autocorr[k] >= autocorr[k - 1] && autocorr[k] > autocorr[k + 1] &&
        autocorr[k] >= p_.correlacionMinima && minimoAntes <= p_.valleMaximo && nCandidatos < 32) {
      candidatos[nCandidatos++] = k;
      if (autocorr[k] > mejor) mejor = autocorr[k];
    }
    if (autocorr[k] < minimoAntes) minimoAntes = autocorr[k];
  }
  int k = -1;
  for (int i = 0; i < nCandidatos && k < 0; i++)
    if (autocorr[candidatos[i]] >= p_.fraccionDelMejor * mejor) k = candidatos[i];
  if (k < 0) return false;

  // Interpolación parabólica del pico (precisión por debajo de una muestra)
  const float a = autocorr[k - 1], b = autocorr[k], d = autocorr[k + 1];
  const float curvatura = a - 2 * b + d;
  float retardo = (float)k;
  if (curvatura < 0) retardo += 0.5f * (a - d) / curvatura;
  correlacion_ = b;
  *rpm = 60.0f * fsDec_ / retardo;
  return *rpm >= p_.minRpm && *rpm <= p_.maxRpm;
}
