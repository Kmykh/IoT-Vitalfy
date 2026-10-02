// temperatura.h — Temperatura corporal a partir del DS18B20.
//
// El DS18B20 mide la superficie donde está apoyado: al aire marca el ambiente (~24 °C), en la mano
// ~32 °C y en la axila, tras unos minutos, ~36–37 °C. Para no enviar al backend un número que no es
// del paciente:
//   1. Contacto: por debajo de minimoContacto la sonda no está sobre la piel y no se informa.
//   2. Predicción (como los termómetros clínicos electrónicos): la sonda se calienta siguiendo una
//      exponencial T(t) = T∞ − B·e^(−t/τ). Con todas las lecturas desde que tocó la piel se ajusta
//      esa curva por mínimos cuadrados (para cada τ de una grilla de 10 a 300 s, T∞ y B salen de una
//      regresión lineal; se queda el τ con menor error). Si el ajuste es bueno y las últimas
//      predicciones coinciden (±0,15 °C), se informa T∞ sin esperar a que la sonda llegue.
//   3. Confirmación: si la medición directa varía menos de 0,2 °C en 60 s, se informa esa medición.
//   4. Corrección (opcional): diferencia medida contra un termómetro clínico en el mismo sitio.
//   5. Clasificación con las bandas de temperatura de NEWS2 (las mismas que usa el backend).
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stdint.h>

enum class EstadoTemperatura : uint8_t { SinSonda, SinContacto, Estabilizando, Estable };
enum class RangoTemperatura : uint8_t { Hipotermia, Baja, Normal, Fiebre, FiebreAlta };

struct ParametrosTemperatura {
  float minimoContacto = 32.0f;  // °C: por debajo, la sonda está al aire
  float correccion = 0.0f;       // °C que se suman a la lectura (calibración del sitio)
  uint32_t ventanaMs = 60000;    // confirmación: la medición debe mantenerse estable este tiempo
  float variacionMax = 0.2f;     // °C de variación máxima dentro de esa ventana
  uint32_t minimoPrediccionMs = 30000;  // predicción: tiempo mínimo de contacto antes de predecir
  float acuerdoPrediccion = 0.15f;     // °C entre las últimas predicciones para darlas por buenas
  float errorAjusteMax = 0.06f;        // °C de error cuadrático medio aceptable en el ajuste
  float extrapolacionMax = 1.5f;       // °C: no se predice más allá de esto sobre lo ya medido
  uint32_t acuerdoMs = 9000;           // las predicciones deben coincidir durante este tiempo
};

class EvaluadorTemperatura {
 public:
  explicit EvaluadorTemperatura(const ParametrosTemperatura& p = ParametrosTemperatura());

  void agregar(float celsius, uint32_t ms);  // una lectura nueva de la sonda
  void sinSonda();                           // la sonda no responde

  EstadoTemperatura estado() const { return estado_; }
  // Estable: la temperatura del paciente (predicha o confirmada). Si no, la lectura actual.
  // En °C ya corregidos y redondeados a 0,1.
  float valor() const { return valor_; }
  bool predicha() const { return predicha_; }  // el valor estable sale de la predicción
  RangoTemperatura rango() const { return clasificar(valor_); }

  // Bandas de NEWS2: ≤35,0 · 35,1–36,0 · 36,1–38,0 · 38,1–39,0 · ≥39,1
  static RangoTemperatura clasificar(float celsius);
  static const char* nombre(RangoTemperatura r);

 private:
  struct Muestra {
    uint32_t ms;
    float c;
  };
  static const int kMuestras = 48;      // una lectura cada ~1,8 s: algo más de 60 s
  static const int kPredicciones = 6;

  bool predecir(float* equilibrio) const;
  static float redondear(float c) { return (float)(int)(c * 10.0f + (c >= 0 ? 0.5f : -0.5f)) / 10.0f; }

  ParametrosTemperatura p_;
  EstadoTemperatura estado_ = EstadoTemperatura::SinSonda;
  float valor_ = 0;
  bool predicha_ = false;
  Muestra hist_[kMuestras] = {};
  int n_ = 0, pos_ = 0;
  uint32_t inicioContacto_ = 0, ultimaConfirmacion_ = 0;
  float predicciones_[kPredicciones] = {};
  uint32_t msPredicciones_[kPredicciones] = {};
  int nPred_ = 0, posPred_ = 0;
};
