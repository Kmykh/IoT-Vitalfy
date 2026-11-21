// pantallas.h — Diseño de las dos OLED (se dibuja sobre Adafruit_GFX, sin tocar el hardware).
//
// Un mismo sistema de diseño en las dos (pantallas.cpp): tres fuentes, la misma barra de estado con la
// batería, ícono + título centrados, el pie en una píldora y las mismas barras, círculos y corazón.
// La principal muestra el arranque, el QR y después los signos; la de 128x32, la verificación, los tres
// pasos de la vinculación y después el ECG.
//
//   OLED principal 128x64 (filas 0–15 amarillas en las bicolor)    OLED 128x32
//   ┌──────────────────────────────┐   barra de estado              ┌───────────────────────┬──────┐
//   │ ▂▄▆█ ☁ ↑3      87%▕▓▓▓▏ 14:05 │   Wi-Fi, servidor,             │    /\        /\       │(ECG♥)│
//   │ FC ♥     ECG  │ SpO2       % │   pendientes, batería, hora    │___/  \/\____/  \/\___ │  72  │
//   │      72       │      98      │                                │                    ▏  │  lpm │
//   ├───────────────┴──────────────┤                                └───────────────────────┴──────┘
//   │ 🌡 36.8°C            Normal │
//   │                   FR 16/min │
//   └──────────────────────────────┘
//
//   Sin vincular: el QR (esquinas redondeadas) y, a la derecha, qué hacer y cuánto le queda
//   ┌──────────┬───────────────────┐          ┌──────────────────────────────┐
//   │╭▓▓ QR ▓▓╮│ ▂▄▆█ ☁     87▕▓▓▏ │          │ Escanee el QR          (1/3) │
//   │ ▓▓▓▓▓▓▓▓ │     VTLY-001      │          │  (1)·········(2)·········(3) │
//   │ ▓▓▓▓▓▓▓▓ │      Escanee      │          │  QR          App       Listo │
//   │ ▓▓▓▓▓▓▓▓ │     con la app    │          └──────────────────────────────┘
//   │ ▓▓▓▓▓▓▓▓ │      ◷ 9:32       │
//   │╰▓▓▓▓▓▓▓▓╯│  (████████░░░░)   │
//   └──────────┴───────────────────┘
//
//   Mensajes (conectando, pidiendo QR, QR vencido, emparejando, vinculado, liberado)
//   ┌──────────────────────────────┐
//   │ ▂▄▆█ ☁         87%▕▓▓▓▏ 14:05 │   barra de estado
//   │          VTLY-001            │   contexto: código del sensor, o cama e iniciales
//   │      ✔  Vinculado            │   ícono + título, centrados juntos
//   │       MED-B-01  R.M.         │   subtítulo
//   │ ( Coloque los sensores     ) │   pie: píldora con borde = qué hacer;
//   └──────────────────────────────┘        píldora llena que parpadea = alarma
//
// Las mismas funciones dibujan en la SSD1306 (firmware) y en un lienzo en memoria
// (pruebas/vista_previa.cpp genera imágenes PNG de cada pantalla).
#pragma once

#include <stdint.h>

#include "bateria.h"
#include "config_wifi.h"
#include "fr_cardiaca.h"
#include "ppg.h"
#include "respiracion.h"
#include "temperatura.h"

class Adafruit_GFX;

struct VistaPrincipal {
  // Barra de estado (red)
  int8_t wifiBarras = -1;    // -1 = buscando red; 0..4 = conectado con esa señal
  uint8_t broker = 0;        // 0 sin conexión, 1 conectando, 2 conectado, 3 usuario/clave rechazados
  uint16_t pendientes = 0;   // lecturas guardadas esperando envío
  bool horaValida = false;
  uint8_t hora = 0, minuto = 0;
  int8_t bateria = -1;       // % (-1 = sin medición: falta el divisor)
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;

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
  bool acelerometro = false; // MPU6050 (o ADXL345) presente
  bool movimiento = false;
  bool alertaCaida = false;
  int fr = -1;               // respiraciones por minuto (-1 = sin dato)
  EstadoRespiracion estadoFr = EstadoRespiracion::Midiendo;  // del MPU, para decir por qué no hay FR
  FuenteFr fuenteFr = FuenteFr::Ninguna;  // fr_cardiaca.h: MPU, pulso (ECG o PPG) o los dos

  // Paciente vinculado (comando "vinculado" del backend): solo cama e iniciales
  const char* cama = "";
  const char* iniciales = "";
  const char* aviso = "";    // aviso del backend (repetir-medicion), "" = ninguno

  // Sesión: sin paciente se muestra qué falta conectar (ver sesion.h)
  bool sesionActiva = false;

  // Pantalla de espera
  const char* codigo = "";   // código del dispositivo
  const char* red = "";      // red Wi-Fi, o un aviso de conectividad (máx. 21 caracteres)
  bool avisoRed = false;     // red es un aviso: se muestra solo, sin el código
  uint32_t ms = 0;           // millis() para los parpadeos
};

void dibujarPrincipal(Adafruit_GFX& g, const VistaPrincipal& v);

// Sin vincular a un paciente: el QR que escanea la enfermera, o "Emparejando..." mientras registra al
// paciente (docs/vinculacion-qr.md del backend). El QR se dibuja oscuro sobre claro, a 2 px por módulo,
// en un cuadro claro de 64x64 con esquinas redondeadas (su margen de silencio), con los tres patrones
// de posición redondeados, y a 4 px del borde de arriba: así la franja sin píxeles de las OLED bicolor
// (entre las filas 15 y 16) no corta esos patrones.
enum class PantallaVinculo : uint8_t { Qr, Solicitando, Vencido, Emparejando, Vinculado, Desvinculado };
struct VistaVinculacion {
  PantallaVinculo pantalla = PantallaVinculo::Solicitando;
  const char* qr = "";
  int segundosRestantes = -1;  // hasta que vence el QR (-1 = sin hora NTP)
  const char* codigo = "";     // código del dispositivo
  int8_t wifiBarras = -1;
  uint8_t broker = 0;          // como en VistaPrincipal
  const char* red = "";        // red Wi-Fi a la que está unido, o un aviso de conectividad
  bool avisoRed = false;       // red es un aviso
  int8_t bateria = -1;         // como en VistaPrincipal
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  bool horaValida = false;     // la misma barra de estado que con el paciente
  uint8_t hora = 0, minuto = 0;
  const char* cama = "";       // al vincular (pantalla Vinculado)
  const char* iniciales = "";
  uint32_t ms = 0;
  uint32_t msPantalla = 0;     // desde que apareció esta pantalla: animación de entrada (el QR se arma)
};
const int kVigenciaQrS = 600;  // el backend da 10 min a cada QR (docs/vinculacion-qr.md): escala de la barra
void dibujarVinculacion(Adafruit_GFX& g, const VistaVinculacion& v);
// La OLED 128x32 mientras se vincula: el mismo estado del equipo que en el arranque.
void dibujarPasosVinculo(Adafruit_GFX& g, const VistaVinculacion& v);

// Configuración del Wi-Fi desde la app por Bluetooth (config_wifi.h). Esperando a la app: el QR "VTFW:..."
// a la izquierda y, a la derecha, el código de un solo uso para escribirlo si la cámara no lee. Después,
// mensajes con el mismo estilo: "Elija la red", "Conectando" (la red que se prueba), "Error" (clave o red) y
// "Wi-Fi listo". La OLED de 128x32 muestra los tres pasos: QR → Red → Listo.
struct VistaConfigWifi {
  EtapaConfigWifi etapa = EtapaConfigWifi::Esperando;
  FalloConfigWifi fallo = FalloConfigWifi::Ninguno;
  const char* qr = "";
  const char* pop = "";
  const char* codigo = "";
  const char* ssid = "";       // la red que se prueba (nunca la clave)
  int8_t bateria = -1;
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  uint32_t ms = 0;
};
void dibujarConfigWifi(Adafruit_GFX& g, const VistaConfigWifi& v);
void dibujarPasosConfigWifi(Adafruit_GFX& g, const VistaConfigWifi& v);

// Pantalla de arranque: chequeo del hardware y de la conexión. No sigue mientras falte un sensor (el
// botón BOOT permite seguir igual) y espera al broker hasta 25 s. Los electrodos no se esperan: van en el
// paciente después de vincular (la pantalla "Conecte al paciente" los pide).
struct VistaArranque {
  const char* version = "";
  const char* codigo = "";
  bool ppg = false, acelerometro = false, temperatura = false;
  bool ecgModulo = false;    // el módulo AD8232 responde
  bool electrodos = false;   // y los electrodos están conectados (sobre la piel)
  bool wifi = false;         // conectado a una red
  int8_t wifiBarras = -1;    // señal (como en VistaPrincipal)
  uint8_t broker = 0;        // 0 sin conexión, 1 conectando, 2 conectado, 3 usuario/clave rechazados
  const char* red = "";      // red Wi-Fi, o un aviso de conectividad
  bool avisoRed = false;     // red es un aviso
  int8_t bateria = -1;       // como en VistaPrincipal
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  uint32_t ms = 0;           // millis() para la animación de "conectando"
};
// Al encender: el logo se arma (la cabeza crece y los brazos se abren), se corre a la izquierda y aparece
// "Vitalify" letra por letra; los dos trazos del logo laten mientras se conecta y quedan fijos al terminar.
// Si pasados 3 s falta un sensor, el chequeo detallado con ✓ ✗ ○.
void dibujarArranque(Adafruit_GFX& g, const VistaArranque& v);
// La OLED de 128x32 mientras tanto (arranque y vinculación): el estado del equipo en palabras simples, para
// cualquier persona: la batería, el Wi-Fi (y su nombre) y si está en línea con el sistema, con el logo chico
// y un latido que dice que el equipo está vivo.
void dibujarVerificacion(Adafruit_GFX& g, const VistaArranque& v);

// Trazo del ECG en modo barrido: guarda mínimo, máximo, primer y último valor y el promedio por
// columna. Una columna con mucha variación (el QRS, más angosto que una columna) se dibuja de mínimo a
// máximo para no perderlo; las demás, con su promedio, así el ruido leve no engrosa la línea.
class BarridoEcg {
 public:
  static const int kAnchoMax = 128;
  static const int kHueco = 4;   // columnas borradas delante del cursor

  // 250 Hz / 8 = 31 col/s: 128 columnas son ~4 s en pantalla
  explicit BarridoEcg(int muestrasPorColumna = 8, int ancho = kAnchoMax);
  void fijarAncho(int ancho);    // 128: a todo lo ancho de la OLED de 128x32
  int ancho() const { return ancho_; }
  void agregar(float v);
  void limpiar();

  int cursor() const { return cursor_; }
  bool valida(int x) const { return valida_[x]; }
  float minimo(int x) const { return min_[x]; }
  float maximo(int x) const { return max_[x]; }
  float primero(int x) const { return primero_[x]; }
  float ultimo(int x) const { return ultimo_[x]; }
  float media(int x) const { return media_[x]; }
  // Escala automática al rango visible: crece de golpe y se achica despacio (el trazo no "respira").
  void actualizarEscala();
  float centro() const { return centro_; }
  float rango() const { return rango_; }

 private:
  float min_[kAnchoMax], max_[kAnchoMax], primero_[kAnchoMax], ultimo_[kAnchoMax], media_[kAnchoMax];
  bool valida_[kAnchoMax];
  int cursor_ = 0, enColumna_ = 0, porColumna_, ancho_;
  float accMin_ = 0, accMax_ = 0, accPrimero_ = 0, accUltimo_ = 0, accSuma_ = 0;
  float centro_ = 0, rango_ = 400;
};

// La OLED de 128x32 con el paciente: arriba, el estado del equipo (Wi-Fi, servidor, pendientes, batería y
// hora: lo mismo que antes ocupaba la principal); debajo, el ECG en barrido a todo lo ancho. Así la
// principal queda solo para el paciente.
struct VistaEcg {
  bool electrodos = false;
  bool ruido = false;        // señal inservible (ResultadoEcg::ruidosa): se avisa en vez de dibujarla
  int fc = -1;
  bool pulso = false;
  // Estado del equipo (como en VistaPrincipal)
  int8_t wifiBarras = -1;
  uint8_t broker = 0;
  uint16_t pendientes = 0;
  int8_t bateria = -1;
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  bool horaValida = false;
  uint8_t hora = 0, minuto = 0;
  uint32_t ms = 0;
};
void dibujarEcg(Adafruit_GFX& g, BarridoEcg& b, const VistaEcg& v);  // OLED 128x32 dedicada
