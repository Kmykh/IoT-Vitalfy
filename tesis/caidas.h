// caidas.h — Detección de caídas y nivel de actividad con el ADXL345 (100 Hz, ±16 g).
//
// Máquina de estados basada en la nota AN-1023 de Analog Devices:
//   1. Ingravidez: |a| < 0,75 g durante al menos 50 ms (el cuerpo cae).
//   2. Impacto:    |a| > 2 g dentro de 1 s desde la ingravidez
//                  (o un impacto fuerte > 3 g aunque no haya habido ingravidez).
//   3. Quietud:    tras 1 s de rebotes, |a| se queda en 1 g ± 0,19 g durante 2 s.
//   4. Postura:    la gravedad cambió de eje respecto de antes del evento (> 0,7 g, ~41°).
// Solo si se cumplen todos se confirma la caída. Saltar, sentarse de golpe o dejar caer el
// brazo no pasan el paso 3 o el 4.
// C++ puro: se prueba en el computador con trazas sintéticas (pruebas/).
#pragma once

#include <stdint.h>

enum class Actividad : uint8_t { Reposo, Movimiento };
enum class FaseCaida : uint8_t { Normal, Ingravidez, Impacto, Evaluando };

struct ParametrosCaidas {
  float fs = 100.0f;               // muestras por segundo
  float umbralIngravidez = 0.75f;  // g
  float tiempoIngravidez = 0.05f;  // s
  float umbralImpacto = 2.0f;      // g, tras la ingravidez
  float umbralImpactoFuerte = 3.0f;  // g, sin ingravidez previa
  float ventanaImpacto = 1.0f;     // s desde que empezó la ingravidez
  float esperaTrasImpacto = 1.0f;  // s de rebotes que se ignoran
  float ventanaQuietud = 2.0f;     // s que debe quedarse quieto
  float quietudMaxDesvio = 0.19f;  // g de desvío de |a| respecto de 1 g
  float cambioPostura = 0.7f;      // g de diferencia entre la gravedad antes y después
  float umbralMovimiento = 0.06f;  // g de desviación estándar de |a| para "Movimiento"
};

class DetectorCaidas {
 public:
  explicit DetectorCaidas(const ParametrosCaidas& p = ParametrosCaidas());

  // Una muestra en g. Devuelve true en la muestra en que se confirma una caída.
  bool procesar(float ax, float ay, float az);

  Actividad actividad() const;
  FaseCaida fase() const { return fase_; }
  float magnitud() const { return magnitud_; }
  uint32_t caidas() const { return caidas_; }

 private:
  uint32_t muestras(float segundos) const { return (uint32_t)(segundos * p_.fs + 0.5f); }

  ParametrosCaidas p_;
  FaseCaida fase_ = FaseCaida::Normal;
  uint32_t n_ = 0;
  float magnitud_ = 1.0f;

  // Gravedad (media lenta) y variabilidad de |a| para la actividad
  float gx_ = 0, gy_ = 0, gz_ = 1;
  bool iniciado_ = false;
  float mediaMag_ = 1.0f, varMag_ = 0;

  uint32_t cuentaIngravidez_ = 0;
  uint32_t inicioEvento_ = 0, muestraImpacto_ = 0;
  float antesX_ = 0, antesY_ = 0, antesZ_ = 1;  // postura antes del evento
  float sumaX_ = 0, sumaY_ = 0, sumaZ_ = 0;     // postura durante la quietud
  uint32_t nQuietud_ = 0;
  uint32_t caidas_ = 0;
};
