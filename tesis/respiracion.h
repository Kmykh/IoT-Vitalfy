// respiracion.h — Frecuencia respiratoria (FR) con el acelerómetro del MPU6050.
//
// El wearable va sujeto sobre el tórax o el abdomen. Al respirar, la pared del tórax sube y baja y la
// placa se inclina unos pocos grados: la gravedad cambia de eje entre 5 y 50 mg al ritmo de la
// respiración. Eso se mide así:
//   1. Promedio de bloques de 10 muestras (100 Hz -> 10 Hz) y pasa-bajos de 1 Hz por eje: quita el
//      ruido y el latido cardíaco (balistocardiograma), que es más rápido que la respiración.
//   2. Ventana de 32 s. Se quita la tendencia lineal de cada eje (la gravedad y la deriva lenta).
//   3. Componente principal (iteración de potencia sobre la covarianza 3x3): la dirección en la que
//      más oscila la gravedad. Así no importa cómo esté puesto el wearable.
//   4. Autocorrelación normalizada de esa señal para períodos de 1,2 a 12 s (50 a 5 rpm). El período
//      es el primer pico alto que viene después de un valle (una oscilación de verdad) y que llega
//      al 80 % del pico más alto (si no, un ciclo con dos jorobas daría el doble). Se interpola el
//      pico con una parábola.
//   5. Solo vale con el paciente quieto: un salto en la inclinación o mucha variación de |a| en la
//      ventana la invalida. También exige una amplitud mínima (que respire y que el wearable esté
//      sobre el tórax) y una correlación de al menos 0,5. La FR informada es la mediana de las 3
//      últimas estimaciones válidas.
// Se recalcula cada 2 s. Tras un movimiento, la FR vuelve tras 32 s de quietud.
// C++ puro: se prueba en el computador con señales sintéticas (pruebas/).
#pragma once

#include <stdint.h>

#include "filtros.h"

enum class EstadoRespiracion : uint8_t {
  Midiendo,    // juntando una ventana completa y quieta
  Valida,      // hay FR
  Movimiento,  // el paciente se mueve: la FR no es confiable
  SinSenal,    // ventana quieta sin oscilación respiratoria (wearable fuera del tórax o señal muy débil)
};

struct ParametrosRespiracion {
  float fs = 100.0f;                 // muestras por segundo de entrada
  int decimacion = 10;               // -> 10 Hz
  float corte = 1.0f;                // Hz del pasa-bajos tras decimar
  float ventana = 32.0f;             // s analizados
  float cadaCuanto = 2.0f;           // s entre estimaciones
  float minRpm = 5.0f, maxRpm = 50.0f;
  float correlacionMinima = 0.5f;
  float valleMaximo = 0.2f;          // la autocorrelación debe bajar de esto antes del pico
  float fraccionDelMejor = 0.8f;     // el pico elegido debe llegar a esta fracción del más alto
  float amplitudMinima = 0.0008f;    // g RMS de la señal respiratoria
  float amplitudMaxima = 0.12f;      // g RMS: más que esto es movimiento, no respiración
  float desvioMovimiento = 0.025f;   // g de desviación estándar de |a| dentro de un bloque de 0,1 s
  float saltoMovimiento = 0.06f;     // g de cambio de un eje entre bloques seguidos (0,1 s)
  float vigencia = 10.0f;            // s que se mantiene la última FR sin estimaciones válidas nuevas
};

class MonitorRespiracion {
 public:
  static const int kMaxVentana = 400;  // muestras decimadas (40 s a 10 Hz)

  explicit MonitorRespiracion(const ParametrosRespiracion& p = ParametrosRespiracion());

  // Una muestra del acelerómetro en g.
  void procesar(float ax, float ay, float az);
  void reiniciar();

  int fr() const { return fr_; }                  // rpm; -1 si no hay una válida
  EstadoRespiracion estado() const { return estado_; }
  float correlacion() const { return correlacion_; }  // del último análisis (diagnóstico)
  float amplitud() const { return amplitud_; }        // g RMS del último análisis (diagnóstico)
  float ultimaEstimacion() const { return ultima_; }  // rpm sin redondear del último análisis válido

 private:
  void agregarBloque(float x, float y, float z, bool movimiento);
  void analizar();
  bool estimar(float* rpm);

  ParametrosRespiracion p_;
  int largo_;           // muestras decimadas de la ventana
  int cadaMuestras_;    // muestras decimadas entre análisis
  int vigenciaMuestras_;
  float fsDec_;

  // Bloque en curso (a la frecuencia de entrada)
  int enBloque_ = 0;
  float sx_ = 0, sy_ = 0, sz_ = 0, sMag_ = 0, sMag2_ = 0;

  // Señal decimada y filtrada, por eje, en un anillo
  Biquad fx_, fy_, fz_;
  bool filtrosAsentados_ = false;
  float x_[kMaxVentana], y_[kMaxVentana], z_[kMaxVentana];
  int pos_ = 0, quietas_ = 0;     // muestras seguidas sin movimiento en el anillo
  float antX_ = 0, antY_ = 0, antZ_ = 0;
  bool hayAnterior_ = false;
  int desdeAnalisis_ = 0, desdeValida_ = 0;

  Historial<3> estimaciones_;
  int fr_ = -1;
  float ultima_ = 0, correlacion_ = 0, amplitud_ = 0;
  EstadoRespiracion estado_ = EstadoRespiracion::Midiendo;
};
