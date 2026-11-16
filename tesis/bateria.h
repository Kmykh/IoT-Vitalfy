// bateria.h — Carga de la batería del wearable: una LiPo de 1 celda (3,7 V nominal; 503562, 1250 mAh),
// llena a 4,2 V y vacía a ~3,3 V. El ESP32 no puede leer su propia batería: la lee por un divisor de dos
// resistencias iguales (100 kΩ + 100 kΩ) desde el + de la batería, después del interruptor, al pin VP
// (GPIO 36), que es del ADC1 y funciona con el Wi-Fi encendido:
//
//   +B (después del SWITCH) ──[100 kΩ]──┬── VP (GPIO 36)
//                                       ├──[100 kΩ]── GND
//                                       └──[100 nF]── GND   (opcional: estabiliza la lectura)
//
// Sin el divisor el pin queda al aire y lee ruido cerca de 0 V: se informa "sin medición" en vez de un
// porcentaje inventado. El voltaje se suaviza (la radio del Wi-Fi lo hace caer unos milivoltios en cada
// transmisión) y el porcentaje mostrado tiene histéresis, para que no salte entre dos valores.
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stdint.h>

enum class EstadoBateria : uint8_t {
  SinMedicion,  // sin divisor (o con uno distinto): no se sabe la carga
  Normal,
  Baja,         // 20 % o menos: el ícono queda con una sola barra
  Critica,      // 10 % o menos: el ícono parpadea y la OLED pide cargarla
};

struct ParametrosBateria {
  float divisor = 2.0f;      // Vbat = Vpin × divisor (100 kΩ / 100 kΩ)
  float correccion = 1.0f;   // ajuste contra un multímetro: Vreal / Vmedido
  int bajaPct = 20;
  int criticaPct = 10;
};

class MedidorBateria {
 public:
  explicit MedidorBateria(ParametrosBateria p = ParametrosBateria());

  // Una lectura del pin, en mV (promedio de varias muestras). Devuelve true si cambió el porcentaje
  // mostrado o el estado.
  bool agregarMvPin(int mvPin);
  void reiniciar();

  float voltios() const { return voltios_; }  // de la batería, suavizado (0 = sin medición)
  int porcentaje() const { return porcentaje_; }  // 0..100, o -1 sin medición
  EstadoBateria estado() const { return estado_; }

  // Curva de descarga de una LiPo de 1 celda con poca carga (≈ 0,1–0,2 C, como este wearable):
  // 4,20 V = 100 %, 3,84 V = 50 %, 3,73 V = 20 %, 3,27 V = 0 %.
  static int porcentajeDe(float voltios);

  static constexpr float kMinimoValido = 2.5f;  // menos: pin al aire o sin batería
  static constexpr float kMaximoValido = 4.6f;  // más: el divisor no es el configurado
  static constexpr float kSuavizado = 0.1f;     // filtro exponencial: con una lectura por segundo, ~10 s
  static constexpr int kHisteresisPct = 3;      // el porcentaje mostrado cambia de a 3 puntos o más

 private:
  ParametrosBateria p_;
  float voltios_ = 0;
  int porcentaje_ = -1;
  EstadoBateria estado_ = EstadoBateria::SinMedicion;
  uint8_t invalidas_ = 0;
};
