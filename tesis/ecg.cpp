// ecg.cpp — ver ecg.h
#include "ecg.h"

namespace {
const float kRefractario = 0.20f;     // s: no hay dos QRS a menos de 200 ms
const float kVentanaOndaT = 0.36f;    // s: un pico antes de esto con poca pendiente es una onda T
const float kRrMin = 0.27f;           // s (222 lpm)
const float kRrMax = 2.4f;            // s (25 lpm)
const float kVigencia = 3.0f;         // s sin QRS para dar la FC por perdida
const float kAsentamiento = 0.5f;     // s tras conectar los electrodos
const float kAprendizaje = 2.0f;      // s para fijar los umbrales iniciales
const float kIrregularidadMax = 0.25f;
}  // namespace

ProcesadorEcg::ProcesadorEcg(const ParametrosEcg& p) : p_(p) {
  paVis_ = Biquad::pasaAltos(p_.fs, 0.5f);
  usarNotch_ = p_.frecuenciaRed > 0 && p_.frecuenciaRed < p_.fs / 2;
  if (usarNotch_) notch_ = Biquad::rechazaBanda(p_.fs, p_.frecuenciaRed, 10.0f);
  pbVis_ = Biquad::pasaBajos(p_.fs, 40.0f);
  paQrs_ = Biquad::pasaAltos(p_.fs, 5.0f);
  pbQrs_ = Biquad::pasaBajos(p_.fs, 15.0f);
  ventana_ = (int)muestras(0.15f);
  if (ventana_ > kMaxVentana) ventana_ = kMaxVentana;
  if (ventana_ < 1) ventana_ = 1;
}

float ProcesadorEcg::procesar(int crudo, bool conectados, bool* latido) {
  if (latido) *latido = false;
  n_++;
  if (!conectados) {
    r_ = ResultadoEcg();  // sin electrodos no hay señal: se olvida todo
    return 0.0f;
  }
  const float x = (float)crudo - 2048.0f;  // el AD8232 reposa a media escala
  if (!r_.electrodos) reiniciar(x);

  float v = paVis_.procesar(x);
  if (usarNotch_) v = notch_.procesar(v);
  v = pbVis_.procesar(v);

  // Pan-Tompkins: pasa-banda, derivada de 5 puntos, cuadrado e integración en 150 ms
  for (int i = 4; i > 0; i--) q_[i] = q_[i - 1];
  q_[0] = pbQrs_.procesar(paQrs_.procesar(x));
  historial_[n_ % kHistorial] = q_[0];
  const float d = (2.0f * q_[0] + q_[1] - q_[3] - 2.0f * q_[4]) * 0.125f;
  const float c = d * d;
  suma_ += c - cuadrados_[iVentana_];
  cuadrados_[iVentana_] = c;
  pendientes_[iVentana_] = d < 0 ? -d : d;
  if (++iVentana_ == ventana_) {
    iVentana_ = 0;
    suma_ = 0;  // suma exacta cada vuelta: evita la deriva por redondeo
    for (int i = 0; i < ventana_; i++) suma_ += cuadrados_[i];
  }
  const float mwi = suma_ / (float)ventana_;

  const uint32_t desdeConexion = n_ - muestraConexion_;
  const bool esMaximo = mwi1_ > mwi2_ && mwi1_ >= mwi;
  const float pico = mwi1_;
  mwi2_ = mwi1_;
  mwi1_ = mwi;

  if (desdeConexion < muestras(kAsentamiento)) return v;
  if (aprendiendo_) {
    if (mwi > maxAprendizaje_) maxAprendizaje_ = mwi;
    sumaAprendizaje_ += mwi;
    nAprendizaje_++;
    if (desdeConexion < muestras(kAsentamiento + kAprendizaje)) return v;
    aprendiendo_ = false;
    spki_ = maxAprendizaje_ / 3.0f;
    npki_ = 0.5f * sumaAprendizaje_ / (float)nAprendizaje_;
    umbral_ = npki_ + 0.25f * (spki_ - npki_);
  }

  if (esMaximo) {
    const uint32_t m = n_ - 1;
    const uint32_t desdeQrs = m - ultimoQrs_;
    if (r_.latidos == 0 || desdeQrs > muestras(kRefractario)) {
      float pendiente = 0;
      for (int i = 0; i < ventana_; i++)
        if (pendientes_[i] > pendiente) pendiente = pendientes_[i];
      if (pico > umbral_) {
        const bool ondaT = r_.latidos > 0 && desdeQrs < muestras(kVentanaOndaT) &&
                           pendiente < 0.5f * pendienteUltimoQrs_;
        if (ondaT) {
          npki_ = 0.125f * pico + 0.875f * npki_;
        } else {
          spki_ = 0.125f * pico + 0.875f * spki_;
          aceptarQrs(m, pendiente, ubicarPicoR(m));
          if (latido) *latido = true;
        }
      } else {
        npki_ = 0.125f * pico + 0.875f * npki_;
        if (pico > mejorCandidato_) {
          mejorCandidato_ = pico;
          pendienteMejor_ = pendiente;
          muestraMejor_ = m;
          picoRMejor_ = ubicarPicoR(m);
        }
      }
      umbral_ = npki_ + 0.25f * (spki_ - npki_);
    }
  }

  // Búsqueda hacia atrás: si pasó 1,66 RR sin QRS, acepta el mejor pico que superó la mitad del umbral
  if (rr_.cantidad > 0 && mejorCandidato_ > 0) {
    const float rrMedio = rr_.medianaUltimos(5);
    if ((float)(n_ - ultimoQrs_) > 1.66f * rrMedio && mejorCandidato_ > 0.5f * umbral_) {
      spki_ = 0.25f * mejorCandidato_ + 0.75f * spki_;
      umbral_ = npki_ + 0.25f * (spki_ - npki_);
      aceptarQrs(muestraMejor_, pendienteMejor_, picoRMejor_);
      if (latido) *latido = true;
    }
  }

  if (r_.latidos > 0 && n_ - ultimoQrs_ > muestras(kVigencia)) r_.fc = -1;
  return v;
}

void ProcesadorEcg::reiniciar(float x0) {
  r_ = ResultadoEcg();
  r_.electrodos = true;
  muestraConexion_ = n_;
  paVis_.asentar(x0);
  notch_.asentar(0);
  pbVis_.asentar(0);
  paQrs_.asentar(x0);
  pbQrs_.asentar(0);
  for (float& q : q_) q = 0;
  for (float& h : historial_) h = 0;
  for (int i = 0; i < kMaxVentana; i++) cuadrados_[i] = pendientes_[i] = 0;
  iVentana_ = 0;
  suma_ = mwi1_ = mwi2_ = 0;
  aprendiendo_ = true;
  maxAprendizaje_ = sumaAprendizaje_ = 0;
  nAprendizaje_ = 0;
  spki_ = npki_ = umbral_ = 0;
  ultimoQrs_ = ultimoPicoR_ = n_;
  pendienteUltimoQrs_ = 0;
  mejorCandidato_ = pendienteMejor_ = 0;
  muestraMejor_ = picoRMejor_ = 0;
  rr_.limpiar();
}

// El máximo de la integración llega con un retardo que varía entre latidos; el pico R en la
// señal pasa-banda (máximo de |q| en la ventana que terminó en m) es un instante mucho más estable.
uint32_t ProcesadorEcg::ubicarPicoR(uint32_t m) const {
  int atras = ventana_ + 8;
  if (atras > kHistorial - 1) atras = kHistorial - 1;
  uint32_t mejor = m;
  float max = -1;
  for (int k = 0; k <= atras; k++) {
    const uint32_t i = m - (uint32_t)k;
    const float a = fabsf(historial_[i % kHistorial]);
    if (a > max) {
      max = a;
      mejor = i;
    }
  }
  return mejor;
}

void ProcesadorEcg::aceptarQrs(uint32_t muestra, float pendiente, uint32_t picoR) {
  if (r_.latidos > 0) {
    const uint32_t rr = picoR - ultimoPicoR_;
    if (rr >= muestras(kRrMin) && rr <= muestras(kRrMax)) rr_.agregar((float)rr);
  }
  ultimoQrs_ = muestra;
  ultimoPicoR_ = picoR;
  pendienteUltimoQrs_ = pendiente;
  mejorCandidato_ = 0;
  r_.latidos++;
  if (rr_.cantidad >= 3) {
    r_.irregularidad = rr_.dispersionUltimos(5);
    // RR muy irregulares suelen ser ruido o movimiento: no se informa FC del ECG
    r_.fc = r_.irregularidad < kIrregularidadMax ? (int)(60.0f * p_.fs / rr_.mediaRobusta(6, 0.15f) + 0.5f) : -1;
  }
}
