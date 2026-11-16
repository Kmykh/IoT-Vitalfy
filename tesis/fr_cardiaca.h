// fr_cardiaca.h — Frecuencia respiratoria (FR) a partir del pulso, y su fusión con la del MPU6050.
//
// La respiración deja tres huellas en cada latido (Charlton 2016; Karlen 2013):
//   · FM: arritmia sinusal respiratoria. Al inspirar el corazón se acelera y al espirar se frena: los
//     intervalos RR oscilan al ritmo de la respiración.
//   · AM: la amplitud del latido (la onda R del ECG, el AC del PPG) cambia con la presión del tórax.
//   · Base: la línea de base del PPG (DC) sube y baja con el retorno venoso.
// Cada serie latido a latido se interpola a 4 Hz, se le quita la tendencia lineal y se busca su período
// con la autocorrelación normalizada (como respiracion.h), entre 6 y 40 rpm.
//
// Fusión inteligente (Karlen 2013): una FR del pulso solo vale si al menos dos de esas huellas la dan y
// difieren entre sí 4 rpm o menos; es su promedio. Con pocos latidos por respiración la modulación no se
// puede ver (Nyquist): se exige FC ≥ 2,5 × FR. Los intervalos raros (latido perdido o extrasístole) no
// entran en la FM.
//
// Fusión con el MPU (fusionarFr): las dos miden lo mismo por caminos distintos. Si coinciden (±4 rpm) se
// promedian, con el doble de peso al MPU (mide el tórax directamente); si no coinciden, manda el MPU; si el
// MPU no tiene FR (el paciente se mueve o el wearable quedó flojo), la del pulso cubre el hueco.
//
// C++ puro: se prueba en el computador con señales sintéticas (pruebas/).
#pragma once

#include <stdint.h>

#include "filtros.h"

class FrCardiaca {
 public:
  static const int kMaxLatidos = 160;   // ~60 s a 160 lpm
  static constexpr float kVentana = 32.0f;      // s analizados
  static constexpr float kFs = 4.0f;            // Hz de la serie interpolada
  static constexpr float kMinRpm = 6.0f, kMaxRpm = 40.0f;
  static constexpr float kCorrelacionMinima = 0.45f;
  static constexpr float kAcuerdo = 4.0f;       // rpm entre modulaciones para aceptar
  static constexpr float kVigencia = 10.0f;     // s que se mantiene la última FR sin una nueva

  // conBase: el PPG también aporta la línea de base; el ECG filtrado no la tiene.
  explicit FrCardiaca(bool conBase) : conBase_(conBase) {}

  // Un latido: su instante (s, en el reloj de la fuente), su amplitud y la línea de base (solo PPG).
  void agregar(float t, float amplitud, float base = 0);
  void reiniciar();

  // Recalcula con los últimos 32 s (cada 2 s alcanza). Devuelve true si hay una FR nueva.
  bool estimar();

  int fr() const { return fr_; }        // rpm; -1 si no hay una vigente
  int modulaciones() const { return modulaciones_; }  // cuántas coincidieron en la última estimación
  float ultimaFm() const { return ultimas_[0]; }       // diagnóstico: rpm de cada huella (0 = ninguna)
  float ultimaAm() const { return ultimas_[1]; }
  float ultimaBase() const { return ultimas_[2]; }

  // La FR de una serie latido a latido (para las pruebas): t y v con n puntos, la FC en lpm.
  static bool frDeSerie(const float* t, const float* v, int n, float* rpm, float* correlacion);

 private:
  struct Latido {
    float t, amplitud, base;
  };
  bool conBase_;
  Latido latidos_[kMaxLatidos];
  int inicio_ = 0, cantidad_ = 0;
  Historial<3> estimaciones_;
  float tValida_ = -1;
  int fr_ = -1, modulaciones_ = 0;
  float ultimas_[3] = {0, 0, 0};
};

enum class FuenteFr : uint8_t { Ninguna, Mpu, Ecg, Ppg, MpuYPulso };

// La FR final: la del MPU, la del pulso (ECG o PPG; si las dos, y coinciden, su promedio) o las dos
// combinadas. -1 si no hay ninguna.
int fusionarFr(int frMpu, int frEcg, int frPpg, FuenteFr* fuente);
