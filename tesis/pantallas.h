// pantallas.h — Diseño de las OLED (se dibuja sobre Adafruit_GFX, sin tocar el hardware).
//
// La OLED principal muestra el arranque y después los signos. La OLED 128x32 muestra una
// verificación animada durante el arranque y después el ECG; la principal nunca muestra el ECG.
//
//   OLED principal 128x64                     OLED ECG 128x32
//   ┌──────────────────────────────┐          ┌───────────────────────┬──────┐
//   │ ▂▄▆█ ☁ ↑3              14:05 │          │    /\        /\       │ ECG ♥│
//   ├───────────────┬──────────────┤          │___/  \/\____/  \/\___ │  72  │
//   │ FC ♥     ECG  │ SpO2       % │          │                    ▏  │  lpm │
//   │      72       │      98      │          └───────────────────────┴──────┘
//   ├───────────────┴──────────────┤          Barrido tipo monitor: el trazo se
//   │ 🌡 36.8°C              Reposo │          escribe de izquierda a derecha y
//   └──────────────────────────────┘          borra delante del cursor.
//
//   OLED ECG 128x32 durante el arranque (verificación)
//   ┌──────────────────────────────┐
//   │ Conectando MQTT          5/6 │          Arriba, lo primero que falta; el latido recorre
//   │ · · · ·_/\_·|\/·_^_· ●       │          la pantalla mientras tanto y la barra cuenta
//   │ ███████████████████████░░░░░ │          los chequeos listos.
//   └──────────────────────────────┘
//
// Las mismas funciones dibujan en la SSD1306 (firmware) y en un lienzo en memoria
// (pruebas/vista_previa.cpp genera imágenes PNG de cada pantalla).
#pragma once

#include <stdint.h>

#include "ppg.h"
#include "temperatura.h"

class Adafruit_GFX;

struct VistaPrincipal {
  // Barra de estado (red)
  int8_t wifiBarras = -1;    // -1 = buscando red; 0..4 = conectado con esa señal
  uint8_t broker = 0;        // 0 sin conexión, 1 conectando, 2 conectado, 3 usuario/clave rechazados
  uint16_t pendientes = 0;   // lecturas guardadas esperando envío
  bool horaValida = false;
  uint8_t hora = 0, minuto = 0;

  // Signos vitales
  bool sensorPpg = true;     // MAX30102 presente
  EstadoPpg ppg = EstadoPpg::SinDedo;
  uint8_t progreso = 0;      // 0..100 mientras mide
  int fc = -1;               // lpm (-1 = sin dato)
  bool fcDelEcg = false;     // de dónde sale la FC
  int spo2 = -1;             // % (-1 = sin dato)
  bool pulso = false;        // hubo un latido hace menos de ~150 ms
  EstadoTemperatura estadoTemp = EstadoTemperatura::SinSonda;
  float temp = 0;            // °C (corregidos)
  RangoTemperatura rangoTemp = RangoTemperatura::Normal;
  bool ecgInstalado = true;  // el módulo AD8232 responde
  bool electrodos = false;   // electrodos del ECG puestos
  bool acelerometro = false; // ADXL345 presente
  bool movimiento = false;
  bool alertaCaida = false;

  // Sesión: sin paciente se muestra qué falta conectar (ver sesion.h)
  bool sesionActiva = false;

  // Pantalla de espera
  const char* codigo = "";   // código del dispositivo
  const char* red = "";      // red Wi-Fi, o un aviso de conectividad (máx. 21 caracteres)
  bool avisoRed = false;     // red es un aviso: se muestra solo, sin el código
  uint32_t ms = 0;           // millis() para los parpadeos
};

void dibujarPrincipal(Adafruit_GFX& g, const VistaPrincipal& v);

// Pantalla de arranque: chequeo del hardware y de la conexión. No sigue mientras falte un sensor o
// los electrodos del ECG (el botón BOOT permite seguir igual) y espera al broker hasta 25 s.
struct VistaArranque {
  const char* version = "";
  const char* codigo = "";
  bool ppg = false, acelerometro = false, temperatura = false;
  bool ecgModulo = false;    // el módulo AD8232 responde
  bool electrodos = false;   // y los electrodos están conectados (sobre la piel)
  bool wifi = false;         // conectado a una red
  uint8_t broker = 0;        // 0 sin conexión, 1 conectando, 2 conectado, 3 usuario/clave rechazados
  const char* red = "";      // red Wi-Fi, o un aviso de conectividad
  uint32_t ms = 0;           // millis() para la animación de "conectando"
};
void dibujarArranque(Adafruit_GFX& g, const VistaArranque& v);
// La misma verificación en la OLED 128x32: qué falta ahora, cuántos de los seis chequeos están
// listos y un latido que recorre la pantalla mientras tanto.
void dibujarVerificacion(Adafruit_GFX& g, const VistaArranque& v);

// Trazo del ECG en modo barrido: guarda mínimo, máximo, primer y último valor por columna,
// así el QRS (más angosto que una columna) no se pierde al comprimir el tiempo.
class BarridoEcg {
 public:
  static const int kAnchoMax = 128;
  static const int kHueco = 4;   // columnas borradas delante del cursor

  // 250 Hz / 8 = 31 col/s: 128 columnas son ~4 s en pantalla
  explicit BarridoEcg(int muestrasPorColumna = 8, int ancho = kAnchoMax);
  void fijarAncho(int ancho);    // 97 en la OLED 128x32 (deja el panel de la FC)
  int ancho() const { return ancho_; }
  void agregar(float v);
  void limpiar();

  int cursor() const { return cursor_; }
  bool valida(int x) const { return valida_[x]; }
  float minimo(int x) const { return min_[x]; }
  float maximo(int x) const { return max_[x]; }
  float primero(int x) const { return primero_[x]; }
  float ultimo(int x) const { return ultimo_[x]; }
  // Escala automática al rango visible: crece de golpe y se achica despacio (el trazo no "respira").
  void actualizarEscala();
  float centro() const { return centro_; }
  float rango() const { return rango_; }

 private:
  float min_[kAnchoMax], max_[kAnchoMax], primero_[kAnchoMax], ultimo_[kAnchoMax];
  bool valida_[kAnchoMax];
  int cursor_ = 0, enColumna_ = 0, porColumna_, ancho_;
  float accMin_ = 0, accMax_ = 0, accPrimero_ = 0, accUltimo_ = 0;
  float centro_ = 0, rango_ = 400;
};

struct VistaEcg {
  bool electrodos = false;
  int fc = -1;
  bool pulso = false;
  uint32_t ms = 0;
};
void dibujarEcg(Adafruit_GFX& g, BarridoEcg& b, const VistaEcg& v);  // OLED 128x32 dedicada
