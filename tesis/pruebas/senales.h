// senales.h — Generadores de señales sintéticas para probar los algoritmos sin hardware.
#pragma once

#include <math.h>
#include <stdint.h>

// Pseudoaleatorio determinista (xorshift64*): las pruebas siempre dan el mismo resultado.
struct Aleatorio {
  uint64_t s;
  explicit Aleatorio(uint64_t semilla) : s(semilla ? semilla : 1) {}
  double uniforme() {  // [0, 1)
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return (double)((s * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
  }
  double normal() {  // Box-Muller
    double u1 = uniforme(), u2 = uniforme();
    if (u1 < 1e-12) u1 = 1e-12;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
  }
};

// Fotopletismografía del MAX30102 (IR y rojo) con onda dícrota, respiración y ruido.
struct GeneradorPpg {
  double fs = 100;
  double fc = 75;               // lpm medio
  double variabilidadRr = 0;    // variación uniforme ± de cada RR (fracción)
  double dcIr = 100000, dcRojo = 80000;
  double perfusion = 0.01;      // AC/DC del IR
  double R = 0.5;               // razón de razones (define el AC del rojo)
  double ruido = 0;             // desvío del ruido blanco, en conteos
  double dicrota = 0.35;        // amplitud relativa de la onda dícrota
  double respiracion = 0.002;   // modulación respiratoria del DC (fracción)
  Aleatorio rnd{12345};

  double t = 0, inicio = 0, rr = 0;
  int latidos = 0;

  static double forma(double tb, double rr, double dicrota) {
    const double tp = fmin(0.15, 0.3 * rr);  // la subida sistólica dura ~150 ms
    const double s = tb < tp ? exp(-pow((tb - tp) / (tp / 2.2), 2)) : exp(-pow((tb - tp) / (0.35 * rr), 2));
    const double td = 0.45 * rr;
    return s + dicrota * exp(-pow((tb - td) / (0.08 * rr), 2));
  }

  void muestra(uint32_t& ir, uint32_t& rojo) {
    if (rr == 0) rr = 60.0 / fc;
    if (t - inicio >= rr) {
      inicio += rr;
      rr = 60.0 / fc * (1.0 + variabilidadRr * (2.0 * rnd.uniforme() - 1.0));
      latidos++;
    }
    const double p = forma(t - inicio, rr, dicrota);
    const double resp = respiracion * sin(2.0 * M_PI * 0.25 * t);
    const double vi = dcIr * (1.0 + resp - perfusion * p) + ruido * rnd.normal();
    const double vr = dcRojo * (1.0 + resp - perfusion * R * p) + ruido * rnd.normal();
    ir = vi < 0 ? 0 : (uint32_t)vi;
    rojo = vr < 0 ? 0 : (uint32_t)vr;
    t += 1.0 / fs;
  }
};

// ECG del AD8232 (conteos del ADC de 12 bits) como suma de gaussianas P-Q-R-S-T por latido,
// con deriva de línea base, interferencia de la red eléctrica y ruido blanco.
struct GeneradorEcg {
  double fs = 250;
  double fc = 75;
  double variabilidadRr = 0;
  double escala = 600;       // conteos por unidad (R = 1)
  double ondaT = 0.3;        // amplitud de la onda T relativa a R
  double deriva = 150;       // conteos, 0,3 Hz (respiración / movimiento)
  double red = 60;           // conteos de interferencia
  double frecuenciaRed = 60; // Hz
  double ruido = 15;         // desvío del ruido blanco, en conteos
  Aleatorio rnd{777};

  double t = 0, rPrevio = -10, rSiguiente = 0.3, rrActual = 0.8;
  int latidos = 0;

  double ondas(double dt, double rr) const {
    const double k = sqrt(rr);  // QT se acorta con la FC (Bazett)
    struct Onda { double a, c, w; } o[] = {
        {0.15, -0.16 * k, 0.025}, {-0.10, -0.03, 0.008}, {1.0, 0.0, 0.010},
        {-0.25, 0.03, 0.010},     {ondaT, 0.25 * k, 0.050}};
    double v = 0;
    for (const auto& w : o) v += w.a * exp(-0.5 * pow((dt - w.c) / w.w, 2));
    return v;
  }

  int muestra() {
    if (t >= rSiguiente) {
      rPrevio = rSiguiente;
      rrActual = 60.0 / fc * (1.0 + variabilidadRr * (2.0 * rnd.uniforme() - 1.0));
      rSiguiente += rrActual;
      latidos++;
    }
    double v = ondas(t - rPrevio, rrActual) + ondas(t - rSiguiente, rrActual);
    v = 2048 + escala * v + deriva * sin(2 * M_PI * 0.3 * t) + red * sin(2 * M_PI * frecuenciaRed * t) +
        ruido * rnd.normal();
    t += 1.0 / fs;
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    return (int)v;
  }
};
