// filtros.h — Filtros IIR (biquad) y estadística robusta para los algoritmos.
// C++ puro: se compila igual en el ESP32 y en el computador (carpeta pruebas/).
#pragma once

#include <math.h>
#include <stdint.h>

// Sección de segundo orden en forma directa II transpuesta (estable en float).
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;

  float procesar(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  // Deja el filtro en régimen permanente para una entrada constante x0:
  // así no aparece un transitorio al arrancar.
  void asentar(float x0) {
    const float y0 = gananciaDc() * x0;
    z2 = b2 * x0 - a2 * y0;
    z1 = (b1 + b2) * x0 - (a1 + a2) * y0;
  }

  float gananciaDc() const { return (b0 + b1 + b2) / (1.0f + a1 + a2); }

  // Diseños del "Audio EQ Cookbook" (R. Bristow-Johnson) por transformación bilineal.
  static Biquad pasaBajos(float fs, float fc, float q = 0.70710678f) {
    const float w = 2.0f * (float)M_PI * fc / fs, c = cosf(w), alfa = sinf(w) / (2.0f * q);
    return normalizar((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alfa, -2 * c, 1 - alfa);
  }
  static Biquad pasaAltos(float fs, float fc, float q = 0.70710678f) {
    const float w = 2.0f * (float)M_PI * fc / fs, c = cosf(w), alfa = sinf(w) / (2.0f * q);
    return normalizar((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alfa, -2 * c, 1 - alfa);
  }
  static Biquad rechazaBanda(float fs, float f0, float q) {
    const float w = 2.0f * (float)M_PI * f0 / fs, c = cosf(w), alfa = sinf(w) / (2.0f * q);
    return normalizar(1, -2 * c, 1, 1 + alfa, -2 * c, 1 - alfa);
  }

 private:
  static Biquad normalizar(float b0, float b1, float b2, float a0, float a1, float a2) {
    Biquad f;
    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = a1 / a0;
    f.a2 = a2 / a0;
    return f;
  }
};

// Mediana de n valores (n pequeño: copia y ordena por inserción).
inline float mediana(const float* v, int n) {
  float t[16];
  if (n > 16) n = 16;
  if (n <= 0) return 0;
  for (int i = 0; i < n; i++) {
    const float x = v[i];
    int j = i - 1;
    while (j >= 0 && t[j] > x) {
      t[j + 1] = t[j];
      j--;
    }
    t[j + 1] = x;
  }
  return (n % 2) ? t[n / 2] : 0.5f * (t[n / 2 - 1] + t[n / 2]);
}

// Últimos N valores de una serie (por ejemplo, intervalos RR) con mediana y dispersión.
template <int N>
struct Historial {
  float v[N] = {};
  int cantidad = 0;
  int pos = 0;

  void agregar(float x) {
    v[pos] = x;
    pos = (pos + 1) % N;
    if (cantidad < N) cantidad++;
  }
  void limpiar() { cantidad = pos = 0; }

  // Copia los últimos k valores (del más nuevo al más viejo) y devuelve cuántos copió.
  int ultimos(int k, float* salida) const {
    if (k > cantidad) k = cantidad;
    for (int i = 0; i < k; i++) salida[i] = v[(pos - 1 - i + N) % N];
    return k;
  }
  float medianaUltimos(int k) const {
    float t[N];
    return mediana(t, ultimos(k, t));
  }
  // Promedio de los últimos k valores que están a ±tolerancia (fracción) de su mediana:
  // la mediana descarta un latido perdido o extra y el promedio da precisión bajo la muestra.
  float mediaRobusta(int k, float tolerancia) const {
    float t[N];
    const int n = ultimos(k, t);
    const float m = mediana(t, n);
    float suma = 0;
    int usados = 0;
    for (int i = 0; i < n; i++) {
      if (fabsf(t[i] - m) <= tolerancia * m) {
        suma += t[i];
        usados++;
      }
    }
    return usados ? suma / (float)usados : m;
  }
  // Desviación absoluta mediana relativa (MAD / mediana): 0 = perfectamente regular.
  float dispersionUltimos(int k) const {
    float t[N];
    const int n = ultimos(k, t);
    if (n < 2) return 0;
    const float m = mediana(t, n);
    if (m <= 0) return 0;
    for (int i = 0; i < n; i++) t[i] = fabsf(t[i] - m);
    return mediana(t, n) / m;
  }
};
