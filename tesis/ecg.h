// ecg.h — Procesamiento del ECG del AD8232 (250 Hz).
//
// Dos cadenas en paralelo sobre la señal cruda del ADC:
//   · Visualización: pasa-altos 0,5 Hz (deriva de la línea base) + notch de la red eléctrica
//     (60 Hz en Perú) + pasa-bajos 40 Hz. Es lo que se dibuja en la OLED.
//   · Detección de QRS (Pan & Tompkins, 1985, simplificado): pasa-banda 5–15 Hz, derivada,
//     cuadrado, integración en ventana de 150 ms y umbral adaptativo señal/ruido, con periodo
//     refractario, rechazo de ondas T por pendiente y búsqueda hacia atrás de latidos perdidos.
// FC = 60 / mediana de los últimos 5 RR; se descarta si los RR son demasiado irregulares (ruido).
// Calidad: la señal es ruidosa si la interferencia de la red (lo que quita el notch) es grande o si
// el ADC se satura seguido; suele ser un electrodo mal pegado o sin el de referencia (RL). Con señal
// ruidosa no se informa FC y la OLED lo avisa en lugar de dibujar el ruido. Las muestras saturadas
// no entran al trazo: se repite la última buena.
// C++ puro: se prueba en el computador con señales sintéticas (pruebas/).
#pragma once

#include <stdint.h>

#include "filtros.h"

struct ParametrosEcg {
  float fs = 250.0f;             // muestras por segundo
  float frecuenciaRed = 60.0f;   // Hz de la red eléctrica (0 = sin notch)
};

struct ResultadoEcg {
  bool electrodos = false;   // true = los tres electrodos están puestos
  int fc = -1;               // lpm (-1 = sin dato confiable)
  uint32_t latidos = 0;      // QRS detectados desde que se conectaron los electrodos
  float irregularidad = 0;   // dispersión relativa de los RR (0 = regular)
  bool ruidosa = false;      // señal inservible: mucha interferencia de la red o saturación
  float ruidoRed = 0;        // conteos RMS de la interferencia de la red (lo que quita el notch)
  float saturacion = 0;      // fracción de muestras en los rieles del ADC (últimos ~2 s)
};

class ProcesadorEcg {
 public:
  explicit ProcesadorEcg(const ParametrosEcg& p = ParametrosEcg());

  // crudo: lectura del ADC (0..4095). conectados: electrodos puestos (sin LEAD OFF).
  // Devuelve la muestra filtrada para graficar (conteos del ADC centrados en 0).
  // Si se pasa latido, queda en true cuando esta muestra confirma un QRS.
  float procesar(int crudo, bool conectados, bool* latido = nullptr);

  const ResultadoEcg& resultado() const { return r_; }

 private:
  uint32_t muestras(float segundos) const { return (uint32_t)(segundos * p_.fs + 0.5f); }
  void reiniciar(float x0);       // electrodos recién conectados: filtros y detección
  void reiniciarDeteccion();      // tras un artefacto: solo la detección de QRS
  uint32_t ubicarPicoR(uint32_t m) const;
  void aceptarQrs(uint32_t muestra, float pendiente, uint32_t picoR);

  ParametrosEcg p_;
  ResultadoEcg r_;
  uint32_t n_ = 0;
  uint32_t muestraConexion_ = 0;   // último reinicio de la detección (conexión o saturación)
  uint32_t muestraElectrodos_ = 0; // conexión de los electrodos

  Biquad paVis_, notch_, pbVis_;  // visualización
  Biquad paQrs_, pbQrs_;          // detección
  bool usarNotch_ = true;
  float xVis_ = 0;                // última muestra no saturada (la que sigue al trazo)
  float potenciaRed_ = 0;         // promedio de la interferencia al cuadrado

  // Pan-Tompkins
  float q_[5] = {};                 // salida del pasa-banda (para la derivada de 5 puntos)
  static const int kHistorial = 64; // ~256 ms del pasa-banda: ahí se ubica el pico R de cada QRS
  float historial_[kHistorial] = {};
  static const int kMaxVentana = 64;
  float cuadrados_[kMaxVentana] = {};
  float pendientes_[kMaxVentana] = {};
  int ventana_ = 38, iVentana_ = 0;
  float suma_ = 0;
  float mwi1_ = 0, mwi2_ = 0;       // integración en n-1 y n-2 (para hallar máximos locales)
  bool aprendiendo_ = true;
  float maxAprendizaje_ = 0, sumaAprendizaje_ = 0;
  uint32_t nAprendizaje_ = 0;
  float spki_ = 0, npki_ = 0, umbral_ = 0;
  uint32_t ultimoQrs_ = 0;          // máximo de la integración del último QRS (refractario, onda T)
  uint32_t ultimoPicoR_ = 0;        // pico R del último QRS (para medir el RR con precisión)
  float pendienteUltimoQrs_ = 0;
  float mejorCandidato_ = 0, pendienteMejor_ = 0;  // para la búsqueda hacia atrás
  uint32_t muestraMejor_ = 0, picoRMejor_ = 0;

  Historial<8> rr_;  // intervalos RR, en muestras
};
