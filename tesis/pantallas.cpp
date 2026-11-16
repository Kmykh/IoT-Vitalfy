// pantallas.cpp — ver pantallas.h
//
// Sistema de diseño de las dos OLED (firmware 2.2). Todas las pantallas se arman con las mismas piezas,
// así las dos OLED se ven como un solo equipo:
//   Fuentes  la clásica 5x7 para los textos, FreeSansBold9pt para los títulos (y la temperatura y la FC
//            del ECG) y FreeSansBold18pt para la FC y la SpO2.
//   Filas    OLED principal: 0–7 barra de estado · 8–15 contexto · 17–49 cuerpo · 51–63 pie. Las 16 de
//            arriba son amarillas en las OLED bicolor: ningún texto queda partido entre los dos colores.
//   Piezas   barraEstado (Wi-Fi, servidor, pendientes, batería y hora), iconoBateria, mensaje (ícono y
//            título centrados juntos, y un subtítulo), pie (píldora con borde = qué hacer; llena y
//            parpadeando = alarma), pildora ("1/3", "5/6", "ECG"), barraProgreso (redondeada) y
//            filaChequeo (✓ listo, ○ pendiente, ✗ no instalado).
#include "pantallas.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vinculacion.h"

namespace {

const uint16_t kOn = 1;   // píxel encendido
const uint16_t kOff = 0;  // píxel apagado

// Filas de la OLED principal
const int kYContexto = 8;    // línea de contexto: código del sensor, o cama e iniciales
const int kYTitulo = 32;     // línea base del título del cuerpo
const int kYSubtitulo = 38;
const int kYPie = 51;        // píldora del pie: filas 51–63
const int kAltoPie = 13;

// Pantalla de signos
const int kBaseNumeros = 44;
const int kBaseGuiones = 40;       // "--" centrado a la altura de los dígitos
const int kYLineaInferior = 47;

// Caracteres de la fuente clásica (CP437) para escribir en español
const char kGrado = (char)0xF8;
const char kFlechaArriba = (char)0x18;
const char kAbreExclamacion = (char)0xAD;
#define I_ACENTO "\xA1"  // í
#define ENIE "\xA4"      // ñ

// Íconos en arte ASCII ('#' = píxel encendido): se editan a mano sin herramientas.
const char* const kCorazonLleno[] = {
    ".##.##.",
    "#######",
    "#######",
    ".#####.",
    "..###..",
    "...#...",
};
const char* const kCorazonVacio[] = {
    ".##.##.",
    "#..#..#",
    "#.....#",
    ".#...#.",
    "..#.#..",
    "...#...",
};
const char* const kListo[] = {  // ✓
    "......#",
    ".....##",
    "#...##.",
    "##.##..",
    ".###...",
    "..#....",
};
const char* const kPendiente[] = {  // ○
    ".###.",
    "#...#",
    "#...#",
    "#...#",
    ".###.",
};
const char* const kNoInstalado[] = {  // ✗
    "#...#",
    ".#.#.",
    "..#..",
    ".#.#.",
    "#...#",
};
const char* const kNubeLlena[] = {
    "....###.....",
    "..#######...",
    ".#########..",
    "############",
    "############",
    ".##########.",
};
const char* const kNubeVacia[] = {
    "....###.....",
    "..##...##...",
    ".#.......##.",
    "#..........#",
    "#..........#",
    ".##########.",
};
const char* const kTermometro[] = {
    ".#.",
    "#.#",
    "#.#",
    "#.#",
    "###",
    "###",
    "###",
};
// Íconos de los mensajes (alineados con la base del título)
const char* const kTelefono[] = {  // emparejando: la enfermera completa el registro en la app
    ".#######.",
    "#.......#",
    "#.#####.#",
    "#.#...#.#",
    "#.#...#.#",
    "#.#...#.#",
    "#.#...#.#",
    "#.#...#.#",
    "#.#####.#",
    "#.......#",
    "#...#...#",
    "#.......#",
    ".#######.",
};
const char* const kRelojArena[] = {  // QR vencido
    "###########",
    ".#.......#.",
    ".#.#####.#.",
    "..#.###.#..",
    "...#.#.#...",
    "....#.#....",
    ".....#.....",
    "....#.#....",
    "...#...#...",
    "..#..#..#..",
    ".#..###..#.",
    ".#.#####.#.",
    "###########",
};
const char* const kQrChico[] = {  // pidiendo el QR al servidor
    "####.#.#.####",
    "#..#..#..#..#",
    "#..#.#.#.#..#",
    "####.#...####",
    ".....##.#....",
    "#.##.#..#.##.",
    ".#..#.##.#..#",
    "..#.#...#.#..",
    "####..##.#.#.",
    "#..#.#..#..##",
    "#..#..#.##...",
    "####.#.#..#.#",
};
const char* const kEnchufe[] = {  // liberado: el sensor ya no está en un paciente
    "...#.....#...",
    "...#.....#...",
    "...#.....#...",
    ".###########.",
    ".###########.",
    ".###########.",
    "..#########..",
    "...#######...",
    "....#####....",
    ".....###.....",
    "......#......",
    "......#......",
    "......#......",
};
const char* const kBluetooth[] = {  // barra de estado del modo de configuración del Wi-Fi
    "..#..",
    "..##.",
    "#.#.#",
    ".###.",
    ".###.",
    "#.#.#",
    "..##.",
    "..#..",
};
const char* const kElectrodo[] = {  // ECG sin electrodos
    "..#####..",
    ".#.....#.",
    "#..###..#",
    "#.#####.#",
    "#.#####.#",
    "#..###..#",
    ".#.....#.",
    "..#####..",
    "....#....",
    "....#....",
};

// Logo de Vitalify (la persona con los brazos abiertos que forman una cruz y un corazón, y los dos trazos de
// alerta), pixelado desde el logo original y simétrico. Grande: 55x54 para la OLED principal; la cabeza ocupa
// justo las 16 filas amarillas de las OLED bicolor y el cuerpo, las azules, como los dos tonos del original.
const char* const kLogoGrande[] = {
    "........................#######........................",
    "......................###########......................",
    ".....................#############.....................",
    "....................###############....................",
    "....................#####.....#####....................",
    "...................#####.......#####...................",
    "...................####.........####...................",
    "...................####.........####...................",
    "...................####.........####...................",
    "...................####.........####...................",
    "...................#####.......#####...................",
    "....................#####.....#####....................",
    "....................###############....................",
    ".....................#############.....................",
    "......................###########......................",
    "........................#######........................",
    ".......#############...............#############.......",
    ".....##################.........##################.....",
    "....####################.......####################....",
    "...######################.....######################...",
    "..#######..........########.########..........#######..",
    ".######..............#############..............######.",
    ".#####................###########................#####.",
    "#####...................#######...................#####",
    "#####....................#####....................#####",
    "####......................###......................####",
    "####...............................................####",
    "####...............................................####",
    "#####.............................................#####",
    "#####.............................................#####",
    ".#####...........................................#####.",
    ".######.........................................######.",
    "..#######.....................................#######..",
    "...################.................################...",
    "....################...............################....",
    ".....################.............################.....",
    "........#############.............#############........",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................####.............####.................",
    ".................#####...........#####.................",
    "..................####...........####..................",
    "..................#####.........#####..................",
    "...................######.....######...................",
    "....................###############....................",
    ".....................#############.....................",
    "......................###########......................",
    "........................#######........................",
};
const char* const kLogoGrandeAcento[] = {  // desde la columna 44 del logo
    ".###.........",
    ".###.........",
    ".###.........",
    "####.........",
    "####......##.",
    "####.....####",
    "###.....#####",
    "###....#####.",
    "###...#####..",
    "##...#####...",
    ".....###.....",
    "....###......",
};
// Mini: 15x14, de trazo fino, para el encabezado del chequeo de arranque
const char* const kLogoMini[] = {
    "......###....#.",
    ".....#...#...#.",
    ".....#...#..#..",
    "......###......",
    ".#####...#####.",
    "#.....#.#.....#",
    "#......#......#",
    "#.............#",
    ".####.....####.",
    "....#.....#....",
    "....#.....#....",
    "....#.....#....",
    ".....#...#.....",
    "......###......",
};

struct Icono {
  const char* const* filas = nullptr;
  int alto = 0;
  int ancho() const { return filas ? (int)strlen(filas[0]) : 0; }
};
template <int N>
Icono de(const char* const (&filas)[N]) {
  return {filas, N};
}

void icono(Adafruit_GFX& g, int x, int y, Icono ic, uint16_t color = kOn) {
  for (int j = 0; j < ic.alto; j++)
    for (int i = 0; ic.filas[j][i]; i++)
      if (ic.filas[j][i] == '#') g.drawPixel(x + i, y + j, color);
}
template <int N>
void icono(Adafruit_GFX& g, int x, int y, const char* const (&filas)[N], uint16_t color = kOn) {
  icono(g, x, y, de(filas), color);
}

// ---------- Texto ----------

// Texto con la fuente clásica 5x7 (6 px por carácter)
void texto(Adafruit_GFX& g, int x, int y, const char* s, uint16_t color = kOn) {
  g.setFont(nullptr);
  g.setTextSize(1);
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(s);
}
int anchoTexto(const char* s) { return 6 * (int)strlen(s) - 1; }
void textoDerecha(Adafruit_GFX& g, int xDer, int y, const char* s, uint16_t color = kOn) {
  texto(g, xDer - anchoTexto(s) + 1, y, s, color);
}
void textoCentrado(Adafruit_GFX& g, int cx, int y, const char* s, uint16_t color = kOn) {
  texto(g, cx - anchoTexto(s) / 2, y, s, color);
}

// Títulos (FreeSansBold9pt): ancho real del trazo y dibujo desde su borde izquierdo
int anchoTitulo(Adafruit_GFX& g, const char* s) {
  g.setFont(&FreeSansBold9pt7b);
  g.setTextSize(1);
  int16_t x1, y1;
  uint16_t ancho, alto;
  g.getTextBounds(s, 0, 20, &x1, &y1, &ancho, &alto);
  g.setFont(nullptr);
  return ancho;
}
void titulo(Adafruit_GFX& g, int x, int base, const char* s, uint16_t color = kOn) {
  g.setFont(&FreeSansBold9pt7b);
  g.setTextSize(1);
  int16_t x1, y1;
  uint16_t ancho, alto;
  g.getTextBounds(s, 0, base, &x1, &y1, &ancho, &alto);
  g.setTextColor(color);
  g.setCursor(x - x1, base);
  g.print(s);
  g.setFont(nullptr);
}

// Números grandes con espaciado compacto: en FreeSans el "1" avanza 19 px aunque su trazo mide
// 10, y "100" o "128" no cabían en media pantalla. Se avanza lo que mide el trazo + 2 px.
int avanceCompacto(const GFXglyph& gl) {
  const int trazo = gl.xOffset + gl.width + 2;
  return trazo < gl.xAdvance ? trazo : gl.xAdvance;
}
void numeroGrande(Adafruit_GFX& g, const GFXfont* fuente, int cx, int base, const char* s) {
  int ancho = 0;
  for (const char* c = s; *c; c++) {
    const GFXglyph& gl = fuente->glyph[(uint8_t)*c - fuente->first];
    ancho += c[1] ? avanceCompacto(gl) : gl.xOffset + gl.width;
  }
  g.setFont(fuente);
  int x = cx - ancho / 2;
  for (const char* c = s; *c; c++) {
    const GFXglyph& gl = fuente->glyph[(uint8_t)*c - fuente->first];
    g.drawChar(x, base, (unsigned char)*c, kOn, kOn, 1);
    x += avanceCompacto(gl);
  }
  g.setFont(nullptr);
}

const char* puntos(uint32_t ms) {
  static const char* const kPuntos[] = {"", ".", "..", "..."};
  return kPuntos[(ms / 400) % 4];
}

// Texto que termina en "..." animado, sin que el resto se corra al cambiar la cantidad de puntos
void textoConPuntosCentrado(Adafruit_GFX& g, int cx, int y, const char* s, uint32_t ms) {
  char linea[28];
  snprintf(linea, sizeof linea, "%s%s", s, puntos(ms));
  texto(g, cx - (anchoTexto(s) + 18) / 2, y, linea);
}

void lineaPunteada(Adafruit_GFX& g, int x, int y0, int y1) {
  for (int y = y0; y <= y1; y += 2) g.drawPixel(x, y, kOn);
}

// ---------- Piezas ----------

// Píldora llena con el texto calado, alineada a la derecha en xDer ("1/3", "5/6"). Devuelve su x.
int pildora(Adafruit_GFX& g, int xDer, int y, const char* s) {
  const int ancho = anchoTexto(s) + 7;
  const int x = xDer - ancho + 1;
  g.fillRoundRect(x, y, ancho, 9, 3, kOn);
  texto(g, x + 4, y + 1, s, kOff);
  return x;
}

// Barra de progreso redondeada: el borde y, adentro, la parte cumplida
void barraProgreso(Adafruit_GFX& g, int x, int y, int ancho, int alto, int valor, int total) {
  g.drawRoundRect(x, y, ancho, alto, alto / 2, kOn);
  if (total <= 0 || valor <= 0) return;
  if (valor > total) valor = total;
  const int interior = ancho - 4;
  const int lleno = (interior * valor + total - 1) / total;
  g.fillRoundRect(x + 2, y + 2, lleno, alto - 4, (alto - 4) / 2, kOn);
}

// Pie: una píldora con borde y qué hacer; llena y parpadeando si es una alarma (caída, batería)
void pie(Adafruit_GFX& g, const char* s, bool alarma = false, uint32_t ms = 0) {
  const bool lleno = alarma && (ms / 500) % 2 == 0;
  if (lleno)
    g.fillRoundRect(0, kYPie, 128, kAltoPie, 4, kOn);
  else
    g.drawRoundRect(0, kYPie, 128, kAltoPie, 4, kOn);
  textoCentrado(g, 64, kYPie + 3, s, lleno ? kOff : kOn);
}

// Wi-Fi (4 barras, filas 0–6; buscando red: se animan) y broker MQTT (nube llena = conectado; vacía
// parpadeando = intentando; "!" = credenciales rechazadas), desde la columna x0
void iconosRed(Adafruit_GFX& g, int x0, int8_t wifiBarras, uint8_t broker, uint32_t ms) {
  const bool parpadeo = (ms / 400) % 2;
  const int alturas[4] = {2, 3, 5, 7};
  const int nivel = wifiBarras < 0 ? (int)((ms / 250) % 5) : wifiBarras;
  for (int i = 0; i < 4; i++) {
    const int x = x0 + i * 3;
    if (i < nivel)
      g.fillRect(x, 7 - alturas[i], 2, alturas[i], kOn);
    else
      g.drawFastHLine(x, 6, 2, kOn);
  }
  switch (broker) {
    case 2:
      icono(g, x0 + 14, 1, kNubeLlena);
      break;
    case 1:
      if (parpadeo) icono(g, x0 + 14, 1, kNubeVacia);
      break;
    case 3:
      icono(g, x0 + 14, 1, kNubeVacia);
      if (parpadeo) texto(g, x0 + 27, 0, "!");
      break;
    default:
      icono(g, x0 + 14, 1, kNubeVacia);
      break;
  }
}

// Batería: el porcentaje y una pila de 13x7 que se llena con la carga, alineados a la derecha en xDer.
// Sin divisor, "--" y la pila vacía. Crítica (10 % o menos), la pila parpadea. compacto = sin el "%".
void iconoBateria(Adafruit_GFX& g, int xDer, int y, int8_t pct, EstadoBateria estado, uint32_t ms, bool compacto) {
  const int x = xDer - 12;  // cuerpo de 11x7 y la punta de 2x3
  if (!(estado == EstadoBateria::Critica && (ms / 500) % 2)) {
    g.drawRect(x, y, 11, 7, kOn);
    g.fillRect(x + 11, y + 2, 2, 3, kOn);
    if (pct > 0) g.fillRect(x + 1, y + 1, (9 * pct + 99) / 100, 5, kOn);  // 9 columnas por dentro
  }
  char s[6];
  if (pct < 0)
    snprintf(s, sizeof s, "--");
  else
    snprintf(s, sizeof s, compacto ? "%d" : "%d%%", pct);
  textoDerecha(g, x - 2, y, s);
}

// La misma barra de estado en todas las pantallas de la OLED principal
struct Barra {
  int8_t wifiBarras = -1;
  uint8_t broker = 0;
  uint16_t pendientes = 0;
  int8_t bateria = -1;
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  bool horaValida = false;
  uint8_t hora = 0, minuto = 0;
  uint32_t ms = 0;
};

void barraEstado(Adafruit_GFX& g, const Barra& b) {
  iconosRed(g, 0, b.wifiBarras, b.broker, b.ms);

  // Lecturas guardadas sin enviar
  if (b.pendientes > 0) {
    char p[8];
    snprintf(p, sizeof p, "%c%u", kFlechaArriba, (unsigned)(b.pendientes > 999 ? 999 : b.pendientes));
    texto(g, 30, 0, p);
  }

  // Hora local (NTP) y, a su izquierda, la batería
  char hora[12];
  if (b.horaValida)
    snprintf(hora, sizeof hora, "%02u:%02u", (unsigned)(b.hora % 24), (unsigned)(b.minuto % 60));
  else
    strcpy(hora, "--:--");
  textoDerecha(g, 127, 0, hora);
  iconoBateria(g, 127 - anchoTexto(hora) - 5, 0, b.bateria, b.estadoBateria, b.ms, false);
}

Barra barraDe(const VistaEcg& v) {
  Barra b;
  b.wifiBarras = v.wifiBarras;
  b.broker = v.broker;
  b.pendientes = v.pendientes;
  b.bateria = v.bateria;
  b.estadoBateria = v.estadoBateria;
  b.horaValida = v.horaValida;
  b.hora = v.hora;
  b.minuto = v.minuto;
  b.ms = v.ms;
  return b;
}

// Una fila de chequeo: ✓ listo, ○ pendiente (parpadea si se está intentando) o ✗ no instalado; el nombre
// y, a la derecha (hasta xDer), el detalle
enum class Chequeo : uint8_t { Listo, Pendiente, Intentando, NoInstalado };
void filaChequeo(Adafruit_GFX& g, int x, int y, const char* nombre, Chequeo c, const char* detalle, int xDer,
                 uint32_t ms) {
  switch (c) {
    case Chequeo::Listo:
      icono(g, x, y, kListo);
      break;
    case Chequeo::NoInstalado:
      icono(g, x + 1, y + 1, kNoInstalado);
      break;
    case Chequeo::Intentando:
      if ((ms / 400) % 2) icono(g, x + 1, y + 1, kPendiente);
      break;
    default:
      icono(g, x + 1, y + 1, kPendiente);
  }
  texto(g, x + 10, y, nombre);
  if (detalle && detalle[0]) textoDerecha(g, xDer, y, detalle);
}

// Íconos del cuerpo: dibujados (✔ en un círculo, Wi-Fi) o en arte ASCII
enum class IconoMensaje : uint8_t { Ninguno, Vinculado, Liberado, Telefono, Vencido, Qr, Wifi, Error };

int anchoIcono(IconoMensaje ic) {
  switch (ic) {
    case IconoMensaje::Vinculado:
      return 15;
    case IconoMensaje::Liberado:
      return de(kEnchufe).ancho();
    case IconoMensaje::Telefono:
      return de(kTelefono).ancho();
    case IconoMensaje::Vencido:
      return de(kRelojArena).ancho();
    case IconoMensaje::Qr:
      return de(kQrChico).ancho();
    case IconoMensaje::Wifi:
      return 19;
    case IconoMensaje::Error:
      return 15;
    default:
      return 0;
  }
}

// El ícono con su borde inferior en la base del título
void dibujarIcono(Adafruit_GFX& g, IconoMensaje ic, int x, int base, uint32_t ms) {
  switch (ic) {
    case IconoMensaje::Vinculado: {  // ✔ calado en un círculo lleno, como el paso "Listo"
      const int cx = x + 7, cy = base - 7;
      g.fillCircle(cx, cy, 7, kOn);
      for (int k = 0; k < 2; k++) {
        g.drawLine(cx - 4, cy + k, cx - 1, cy + 3 + k, kOff);
        g.drawLine(cx - 1, cy + 3 + k, cx + 4, cy - 2 + k, kOff);
      }
      return;
    }
    case IconoMensaje::Wifi: {  // arcos que aparecen de a uno mientras conecta
      const int cx = x + 9, cy = base;
      g.fillCircle(cx, cy - 1, 1, kOn);
      const int arcos = 1 + (int)((ms / 300) % 3);
      for (int i = 0; i < arcos && i < 2; i++) {
        const int r = 5 + 4 * i;
        g.drawCircleHelper(cx, cy - 1, r, 1 | 2, kOn);
        g.drawCircleHelper(cx, cy - 1, r - 1, 1 | 2, kOn);
      }
      return;
    }
    case IconoMensaje::Error: {  // ✗ en un círculo, como el ✔ de "Vinculado" pero vacío
      const int cx = x + 7, cy = base - 7;
      g.drawCircle(cx, cy, 7, kOn);
      g.drawCircle(cx, cy, 6, kOn);
      for (int k = 0; k < 2; k++) {
        g.drawLine(cx - 3 + k, cy - 3, cx + 3 + k - 1, cy + 3, kOn);
        g.drawLine(cx + 3 - k, cy - 3, cx - 3 - k + 1, cy + 3, kOn);
      }
      return;
    }
    case IconoMensaje::Telefono: {  // la pantalla del teléfono "carga" mientras la app registra
      const Icono t = de(kTelefono);
      icono(g, x, base - t.alto + 1, t);
      const int fila = (int)((ms / 250) % 5);
      g.drawFastHLine(x + 3, base - t.alto + 4 + fila, 3, kOn);
      return;
    }
    default:
      break;
  }
  const Icono t = ic == IconoMensaje::Liberado ? de(kEnchufe)
                  : ic == IconoMensaje::Vencido ? de(kRelojArena)
                                                : de(kQrChico);
  icono(g, x, base - t.alto + 1, t);
}

// Cuerpo de un mensaje: ícono y título centrados juntos y, debajo, el subtítulo (con "..." animado si
// termina en "...")
void mensaje(Adafruit_GFX& g, IconoMensaje ic, const char* t, const char* sub, uint32_t ms, int base = kYTitulo) {
  const int wi = anchoIcono(ic), separacion = wi ? 4 : 0, wt = anchoTitulo(g, t);
  int x = 64 - (wi + separacion + wt) / 2;
  if (x < 0) x = 0;
  if (wi) dibujarIcono(g, ic, x, base, ms);
  titulo(g, x + wi + separacion, base, t);
  if (!sub || !sub[0]) return;
  const size_t n = strlen(sub);
  if (n > 3 && strcmp(sub + n - 3, "...") == 0) {
    char sin[24];
    snprintf(sin, sizeof sin, "%.*s", (int)(n - 3), sub);
    textoConPuntosCentrado(g, 64, kYSubtitulo, sin, ms);
  } else {
    textoCentrado(g, 64, kYSubtitulo, sub);
  }
}

// ---------- Pantallas con el paciente ----------

// Celda de la temperatura (izquierda de la fila inferior)
void celdaTemperatura(Adafruit_GFX& g, const VistaPrincipal& v) {
  switch (v.estadoTemp) {
    case EstadoTemperatura::SinSonda:
      icono(g, 2, 50, kTermometro);
      texto(g, 9, 49, "Sin");
      texto(g, 9, 57, "sonda");
      return;
    case EstadoTemperatura::SinContacto:  // al aire: el número sería la temperatura ambiente
      icono(g, 2, 50, kTermometro);
      texto(g, 9, 49, "Coloque");
      texto(g, 9, 57, "la sonda");
      return;
    default:
      break;
  }
  // Fuera de lo normal (NEWS2: hipotermia, baja, fiebre), la celda va en negativo: salta a la vista
  const bool resaltar = v.estadoTemp == EstadoTemperatura::Estable && v.rangoTemp != RangoTemperatura::Normal;
  const uint16_t tinta = resaltar ? kOff : kOn;
  if (resaltar) g.fillRoundRect(0, 48, 62, 16, 3, kOn);
  icono(g, 3, 52, kTermometro, tinta);
  char t[8];
  snprintf(t, sizeof t, "%.1f", v.temp);
  titulo(g, 9, 62, t, tinta);
  const int x = 9 + anchoTitulo(g, t) + 2;
  const char unidad[3] = {kGrado, 'C', 0};
  texto(g, x, 49, unidad, tinta);
  if (v.estadoTemp == EstadoTemperatura::Estabilizando && (v.ms / 500) % 2) texto(g, x, 56, "..", tinta);
}

void celdaFr(Adafruit_GFX& g, const VistaPrincipal& v) {
  texto(g, 66, 49, "FR");
  const char* fuente = v.fuenteFr == FuenteFr::Mpu         ? "MPU"
                       : v.fuenteFr == FuenteFr::Ecg       ? "ECG"
                       : v.fuenteFr == FuenteFr::Ppg       ? "PPG"
                       : v.fuenteFr == FuenteFr::MpuYPulso ? "M+P"
                                                            : "";
  if (v.fr > 0) {
    char s[8];
    snprintf(s, sizeof s, "%d", v.fr);
    texto(g, 66, 57, fuente);
    titulo(g, 109 - anchoTitulo(g, s), 62, s);
    texto(g, 110, 56, "rpm");
    return;
  }
  // Sin FR: por qué (el MPU pide quietud; el pulso necesita ~30 s de latidos)
  if (v.acelerometro && v.estadoFr == EstadoRespiracion::Movimiento)
    texto(g, 66, 57, "mov.");
  else if ((v.ms / 500) % 2)
    texto(g, 66, 57, "...");
  titulo(g, 125 - anchoTitulo(g, "--"), 58, "--");
}

// Fila inferior: dos celdas iguales, temperatura y FR (o, en la píldora del pie, la alerta de caída, un
// aviso del backend o la batería crítica)
void filaInferior(Adafruit_GFX& g, const VistaPrincipal& v) {
  // La batería crítica alterna con las celdas cada 2 s: avisa sin tapar los signos
  const bool bateriaCritica = v.estadoBateria == EstadoBateria::Critica && (v.ms / 2000) % 2 == 0;
  if (v.alertaCaida || v.aviso[0] || bateriaCritica) {
    char msg[24];
    if (v.alertaCaida)
      snprintf(msg, sizeof msg, "%cCAIDA DETECTADA!", kAbreExclamacion);
    else if (v.aviso[0])
      snprintf(msg, sizeof msg, "%s", v.aviso);
    else
      snprintf(msg, sizeof msg, "Bater" I_ACENTO "a baja: cargue");
    pie(g, msg, true, v.ms);
    return;
  }
  g.drawFastHLine(0, kYLineaInferior - 1, 128, kOn);
  lineaPunteada(g, 63, kYLineaInferior + 1, 63);
  celdaTemperatura(g, v);
  celdaFr(g, v);
}

// Corazón grande con una línea de pulso calada (pantalla de espera)
void corazonGrande(Adafruit_GFX& g, int cx, int cy) {
  g.fillCircle(cx - 6, cy, 6, kOn);
  g.fillCircle(cx + 6, cy, 6, kOn);
  g.fillTriangle(cx - 12, cy + 2, cx + 12, cy + 2, cx, cy + 16, kOn);
  const int8_t pulso[][2] = {{-14, 4}, {-5, 4}, {-3, 0}, {-1, 9}, {2, -3}, {4, 6}, {6, 4}, {14, 4}};
  for (unsigned i = 0; i + 1 < sizeof pulso / sizeof pulso[0]; i++) {
    for (int grosor = 0; grosor < 2; grosor++)
      g.drawLine(cx + pulso[i][0], cy + pulso[i][1] + grosor, cx + pulso[i + 1][0], cy + pulso[i + 1][1] + grosor,
                 kOff);
  }
}

// Fila de arriba con el paciente: la cama y las iniciales (vinculado) o el código del sensor
void contextoArriba(Adafruit_GFX& g, const VistaPrincipal& v) {
  char info[22];  // 21 caracteres de 6 px
  if (v.cama[0])
    snprintf(info, sizeof info, "%s  %s", v.cama, v.iniciales);
  else
    snprintf(info, sizeof info, "%s", v.codigo);
  textoCentrado(g, 64, 0, info);
}

void pantallaEspera(Adafruit_GFX& g, const VistaPrincipal& v) {
  if (v.avisoRed) textoCentrado(g, 64, kYContexto, v.red);  // qué pasa con la red, si algo pasa
  corazonGrande(g, 16, 25);
  if (v.sensorPpg) {
    texto(g, 34, 22, "Coloque su dedo");
    texto(g, 34, 32, "en el sensor");
  } else {
    texto(g, 34, 22, "Sensor SpO2");
    texto(g, 34, 32, "no detectado");
  }
}

// Sin sesión: qué sensor ya está en el paciente (✓), cuál falta colocar (○) y cuál no está instalado (✗)
void pantallaConexion(Adafruit_GFX& g, const VistaPrincipal& v) {
  char contexto[22];
  if (v.avisoRed)  // sin servidor no hay vinculación: se dice qué pasa con la red
    snprintf(contexto, sizeof contexto, "%s", v.red);
  else if (v.broker != 2)
    snprintf(contexto, sizeof contexto, "%s", v.wifiBarras < 0 ? "Sin Wi-Fi: buscando" : "Sin servidor");
  else
    snprintf(contexto, sizeof contexto, "Conecte al paciente");
  textoCentrado(g, 64, kYContexto, contexto);

  const bool dedo = v.ppg != EstadoPpg::SinDedo;
  filaChequeo(g, 0, 19, "SpO2", !v.sensorPpg ? Chequeo::NoInstalado : (dedo ? Chequeo::Listo : Chequeo::Pendiente),
              !v.sensorPpg ? "no instalado" : (dedo ? "OK" : "coloque el dedo"), 127, v.ms);
  filaChequeo(g, 0, 29, "ECG",
              !v.ecgInstalado ? Chequeo::NoInstalado : (v.electrodos ? Chequeo::Listo : Chequeo::Pendiente),
              !v.ecgInstalado ? "no instalado" : (v.electrodos ? "OK" : "conecte cables"), 127, v.ms);
  const Chequeo temp = v.estadoTemp == EstadoTemperatura::SinSonda      ? Chequeo::NoInstalado
                       : v.estadoTemp == EstadoTemperatura::SinContacto ? Chequeo::Pendiente
                                                                         : Chequeo::Listo;
  filaChequeo(g, 0, 39, "Temp", temp,
              temp == Chequeo::NoInstalado ? "no instalado" : (temp == Chequeo::Pendiente ? "coloque sonda" : "OK"), 127,
              v.ms);
  if (dedo)
    pie(g, "Iniciando monitoreo");
  else
    pie(g, v.sensorPpg ? "Inicia con el dedo" : "Inicia: ECG o temp.");
}

void pantallaSignos(Adafruit_GFX& g, const VistaPrincipal& v) {
  // Etiquetas (línea de contexto)
  texto(g, 2, kYContexto, "FC");
  icono(g, 16, kYContexto + 1, v.pulso ? kCorazonLleno : kCorazonVacio);
  if (v.fc > 0) textoDerecha(g, 61, kYContexto, v.fcDelEcg ? "ECG" : "PPG");
  texto(g, 67, kYContexto, "SpO2");
  textoDerecha(g, 127, kYContexto, "%");
  lineaPunteada(g, 63, kYContexto, kBaseNumeros);

  // FC
  char s[12];
  if (v.fc > 0) {
    snprintf(s, sizeof s, "%d", v.fc);
    numeroGrande(g, &FreeSansBold18pt7b, 31, kBaseNumeros, s);
  } else {
    numeroGrande(g, &FreeSansBold18pt7b, 31, kBaseGuiones, "--");
  }

  // SpO2, o por qué todavía no hay SpO2
  if (v.spo2 > 0) {
    snprintf(s, sizeof s, "%d", v.spo2);
    numeroGrande(g, &FreeSansBold18pt7b, 96, kBaseNumeros, s);
    return;
  }
  if (!v.sensorPpg) {
    textoCentrado(g, 96, 28, "sin sensor");
  } else if (v.ppg == EstadoPpg::SinDedo) {
    textoCentrado(g, 96, 28, "sin dedo");
  } else if (v.ppg == EstadoPpg::SenalDebil) {
    textoCentrado(g, 96, 22, "reacomode");
    textoCentrado(g, 96, 32, "el dedo");
  } else {  // estabilizando o midiendo: barra de progreso bajo los guiones
    numeroGrande(g, &FreeSansBold18pt7b, 96, kBaseGuiones, "--");
    barraProgreso(g, 68, 40, 56, 6, v.progreso, 100);
  }
}

}  // namespace

void dibujarPrincipal(Adafruit_GFX& g, const VistaPrincipal& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  // Con el paciente, la principal es solo del paciente: el estado del equipo (Wi-Fi, servidor, batería y
  // hora) va en la OLED de 128x32. Arriba, a quién está puesto el wearable.
  contextoArriba(g, v);
  if (!v.sesionActiva) {  // sin paciente: lista de qué falta conectar
    pantallaConexion(g, v);
    return;
  }
  if (v.ppg != EstadoPpg::SinDedo || v.fc > 0)
    pantallaSignos(g, v);
  else
    pantallaEspera(g, v);
  filaInferior(g, v);
}

namespace {

// El QR se genera solo cuando cambia el texto (la pantalla se redibuja 5 veces por segundo)
struct CacheQr {
  char texto[72] = "";
  uint8_t qr[kQrTamBuffer];
  int lado = 0;
} cacheQr;

int qrDe(const char* texto) {
  if (strcmp(cacheQr.texto, texto) != 0) {
    snprintf(cacheQr.texto, sizeof cacheQr.texto, "%s", texto);
    cacheQr.lado = generarQr(texto, cacheQr.qr);
  }
  return cacheQr.lado;
}

// QR oscuro sobre claro a 2 px por módulo, con los tres patrones de posición redondeados (anillo de 7x7
// módulos y centro de 3x3). Los lectores los encuentran igual: buscan la proporción 1:1:3:1:1 a través del
// centro, que no cambia (comprobado con un lector de QR, también con la franja de las OLED bicolor).
void dibujarQr(Adafruit_GFX& g, int ox, int oy, int lado) {
  const auto esPatron = [lado](int x, int y) {
    return (x < 7 && y < 7) || (x >= lado - 7 && y < 7) || (x < 7 && y >= lado - 7);
  };
  for (int y = 0; y < lado; y++)
    for (int x = 0; x < lado; x++)
      if (!esPatron(x, y) && moduloQr(cacheQr.qr, x, y)) g.fillRect(ox + 2 * x, oy + 2 * y, 2, 2, kOff);
  const int esquinas[3][2] = {{0, 0}, {lado - 7, 0}, {0, lado - 7}};
  for (const auto& e : esquinas) {
    const int x = ox + 2 * e[0], y = oy + 2 * e[1];
    g.fillRoundRect(x, y, 14, 14, 3, kOff);
    g.fillRoundRect(x + 2, y + 2, 10, 10, 2, kOn);
    g.fillRoundRect(x + 4, y + 4, 6, 6, 1, kOff);
  }
}

}  // namespace

namespace {

// ---------- Logo animado y estado del equipo (arranque y vinculación) ----------

// Curva de salida (ease-out cúbica): rápido al principio y suave al llegar. t fuera de [0, 1] se recorta.
float suavizar(float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  const float u = 1 - t;
  return 1 - u * u * u;
}

// El logo grande "se arma" como la persona del logo abriendo los brazos: la cabeza crece desde su centro
// (0–300 ms) y el cuerpo se abre desde el eje hacia los lados (200–800 ms). t en ms desde que empieza.
void logoRevelado(Adafruit_GFX& g, int x0, int y0, uint32_t t) {
  const Icono l = de(kLogoGrande);
  const float radio = suavizar((float)t / 300.0f) * 9.5f;
  const float abierto = suavizar(((float)t - 200.0f) / 600.0f) * 28.0f;
  for (int j = 0; j < l.alto; j++)
    for (int i = 0; l.filas[j][i]; i++) {
      if (l.filas[j][i] != '#') continue;
      const float dx = (float)i - 27.0f, dy = (float)j - 7.5f;
      const bool ver = j < 16 ? dx * dx + dy * dy <= radio * radio : fabsf(dx) <= abierto;
      if (ver) g.drawPixel(x0 + i, y0 + j, kOn);
    }
}

// Los dos trazos de alerta del logo. Conectando, laten como una señal (ninguno, uno, los dos); conectado,
// quedan fijos.
enum class Acentos : uint8_t { Ocultos, Laten, Fijos };
void acentosLogo(Adafruit_GFX& g, int xLogo, int y0, Acentos modo, uint32_t ms) {
  if (modo == Acentos::Ocultos) return;
  const int fase = modo == Acentos::Fijos ? 3 : (int)((ms / 260) % 4);
  const Icono a = de(kLogoGrandeAcento);
  for (int j = 0; j < a.alto; j++)
    for (int i = 0; a.filas[j][i]; i++) {
      if (a.filas[j][i] != '#') continue;
      const bool primero = i <= 3;  // el trazo casi vertical; el resto, el diagonal
      if ((primero && fase >= 1) || (!primero && fase >= 2)) g.drawPixel(xLogo + 44 + i, y0 + j, kOn);
    }
}

// Panel de la marca a la derecha del logo (x 62–127): "Vitalify" (las primeras letras si se está
// escribiendo), qué hace en dos líneas cortas y una línea fina: progreso (0–100) o, con -1, un tramo que
// recorre la línea (espera sin fin conocido).
void panelMarca(Adafruit_GFX& g, int letras, const char* linea1, const char* linea2, int progreso, uint32_t ms) {
  const int cx = 95;
  const char* nombre = "Vitalify";
  const int x = cx - anchoTitulo(g, nombre) / 2;
  char parcial[9];
  snprintf(parcial, sizeof parcial, "%.*s", letras < 0 ? 0 : (letras > 8 ? 8 : letras), nombre);
  if (parcial[0]) titulo(g, x, 28, parcial);
  if (letras < 8) return;
  if (linea1 && linea1[0]) textoCentrado(g, cx, 37, linea1);
  if (linea2 && linea2[0]) {  // si termina en "...", los puntos se animan
    const size_t n = strlen(linea2);
    if (n > 3 && strcmp(linea2 + n - 3, "...") == 0) {
      char sin[16];
      snprintf(sin, sizeof sin, "%.*s", (int)(n - 3), linea2);
      textoConPuntosCentrado(g, cx, 46, sin, ms);
    } else {
      textoCentrado(g, cx, 46, linea2);
    }
  }
  for (int i = 66; i <= 124; i += 2) g.drawPixel(i, 58, kOn);  // la pista, punteada
  if (progreso >= 0) {
    g.fillRect(66, 57, 59 * (progreso > 100 ? 100 : progreso) / 100, 3, kOn);
  } else {
    const int pos = (int)((ms / 30) % 74) - 15;  // un tramo de 15 px que va y vuelve a empezar
    for (int i = pos; i < pos + 15; i++)
      if (i >= 0 && i < 59) g.drawFastVLine(66 + i, 57, 3, kOn);
  }
}

// Estado del equipo en la OLED de 128x32, para cualquier persona: a la izquierda el logo chico y un latido
// (el equipo está vivo); a la derecha tres filas con su ícono: batería, Wi-Fi y sistema, y al final ✓ si
// esa parte está bien.
struct EstadoEquipo {
  int8_t wifiBarras = -1;
  uint8_t broker = 0;
  const char* red = "";
  bool avisoRed = false;
  int8_t bateria = -1;
  EstadoBateria estadoBateria = EstadoBateria::SinMedicion;
  uint32_t ms = 0;
};

void pilaChica(Adafruit_GFX& g, int x, int y, int8_t pct, bool parpadea, uint32_t ms) {
  if (parpadea && (ms / 500) % 2) return;
  g.drawRect(x, y, 11, 7, kOn);
  g.fillRect(x + 11, y + 2, 2, 3, kOn);
  if (pct > 0) g.fillRect(x + 1, y + 1, (9 * pct + 99) / 100, 5, kOn);
}

void barrasChicas(Adafruit_GFX& g, int x, int y, int8_t nivel, uint32_t ms) {
  const int alturas[4] = {2, 3, 5, 7};
  const int n = nivel < 0 ? (int)((ms / 250) % 5) : nivel;  // buscando: se animan
  for (int i = 0; i < 4; i++) {
    if (i < n)
      g.fillRect(x + i * 3, y + 7 - alturas[i], 2, alturas[i], kOn);
    else
      g.drawFastHLine(x + i * 3, y + 6, 2, kOn);
  }
}

void estadoEquipo(Adafruit_GFX& g, const EstadoEquipo& e) {
  // Vivo: el logo chico y un latido que pasa por debajo
  icono(g, 0, 1, kLogoMini);
  const int c = (int)((e.ms / 70) % 26) - 5;
  for (int x = 0; x < 16; x += 3) g.drawPixel(x, 26, kOn);
  const int8_t latido[][2] = {{0, 0}, {1, -4}, {2, 3}, {3, 0}};
  for (int k = 0; k + 1 < 4; k++) {
    const int xa = c + latido[k][0], xb = c + latido[k + 1][0];
    if (xa >= 0 && xb < 16) g.drawLine(xa, 26 + latido[k][1], xb, 26 + latido[k + 1][1], kOn);
  }

  char t[16];
  // Batería
  const bool critica = e.estadoBateria == EstadoBateria::Critica;
  pilaChica(g, 20, 1, e.bateria, critica, e.ms);
  if (e.bateria < 0)
    snprintf(t, sizeof t, "sin medir");
  else
    snprintf(t, sizeof t, "%d%%%s", e.bateria, critica ? " cargue!" : (e.estadoBateria == EstadoBateria::Baja ? " baja" : ""));
  texto(g, 38, 1, t);
  if (e.estadoBateria == EstadoBateria::Normal) icono(g, 121, 1, kListo);

  // Wi-Fi
  const bool conWifi = e.wifiBarras >= 0;
  barrasChicas(g, 20, 12, e.wifiBarras, e.ms);
  if (conWifi && !e.avisoRed) {
    snprintf(t, sizeof t, "%.13s", e.red);
    texto(g, 38, 12, t);
    icono(g, 121, 12, kListo);
  } else if (e.avisoRed) {
    snprintf(t, sizeof t, "%.13s", e.red);
    texto(g, 38, 12, t);
  } else {
    snprintf(t, sizeof t, "buscando%s", puntos(e.ms));
    texto(g, 38, 12, t);
  }

  // Sistema (el servidor de Vitalify)
  icono(g, 20, 24, e.broker == 2 ? kNubeLlena : kNubeVacia);
  if (e.broker == 2) {
    texto(g, 38, 23, "en l" I_ACENTO "nea");
    icono(g, 121, 23, kListo);
  } else if (e.broker == 3) {
    texto(g, 38, 23, "sin acceso");
  } else if (!conWifi) {
    texto(g, 38, 23, "espera Wi-Fi");
  } else {
    snprintf(t, sizeof t, "conectando%s", puntos(e.ms));
    texto(g, 38, 23, t);
  }
}

// Mensaje con entrada animada: el ícono y el título llegan deslizándose desde la derecha (250 ms)
void mensajeEntrante(Adafruit_GFX& g, IconoMensaje ic, const char* t, const char* sub, uint32_t ms, uint32_t msPantalla) {
  const int corrimiento = (int)lroundf((1.0f - suavizar((float)msPantalla / 250.0f)) * 30.0f);
  const int wi = anchoIcono(ic), separacion = wi ? 4 : 0, wt = anchoTitulo(g, t);
  int x = 64 - (wi + separacion + wt) / 2 + corrimiento;
  if (x < 0) x = 0;
  if (wi) dibujarIcono(g, ic, x, kYTitulo, ms);
  titulo(g, x + wi + separacion, kYTitulo, t);
  if (sub && sub[0] && msPantalla >= 150) {
    const size_t n = strlen(sub);
    if (n > 3 && strcmp(sub + n - 3, "...") == 0) {
      char sin[24];
      snprintf(sin, sizeof sin, "%.*s", (int)(n - 3), sub);
      textoConPuntosCentrado(g, 64, kYSubtitulo, sin, ms);
    } else {
      textoCentrado(g, 64, kYSubtitulo, sub);
    }
  }
}

// Pie sin recuadro: qué hacer, en una línea, bajo una línea fina punteada
void pieSimple(Adafruit_GFX& g, const char* s) {
  for (int x = 28; x <= 100; x += 2) g.drawPixel(x, 50, kOn);
  textoCentrado(g, 64, 55, s);
}

}  // namespace

void dibujarVinculacion(Adafruit_GFX& g, const VistaVinculacion& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  // El estado del equipo (batería, Wi-Fi, sistema) va en la OLED de 128x32: aquí solo lo que hay que hacer.

  const int lado = v.pantalla == PantallaVinculo::Qr ? qrDe(v.qr) : 0;
  if (lado > 0 && 2 * lado <= 64) {
    // El QR se arma: el cuadro claro crece (0–300 ms) y los módulos aparecen en diagonal (300–700 ms). Oscuro
    // sobre claro, con esquinas y patrones redondeados; el borde claro es su margen de silencio. En las OLED
    // bicolor hay una franja sin píxeles entre la fila 15 y la 16: a 4 px del borde queda justo encima de la
    // última fila oscura de los patrones de arriba y solo la engrosa (comprobado con un lector de QR).
    const uint32_t t = v.msPantalla;
    const int s = (int)lroundf(64.0f * suavizar((float)t / 300.0f));
    if (s > 0) g.fillRoundRect(32 - s / 2, 32 - s / 2, s, s, s >= 12 ? 5 : s / 3, kOn);
    if (t >= 300) {
      const int ox = (64 - 2 * lado) / 2, oy = 16 - 2 * 6;
      if (t >= 700) {
        dibujarQr(g, ox, oy, lado);
      } else {  // aparece en diagonal: primero la esquina de arriba a la izquierda
        const int hasta = (int)((float)(t - 300) / 400.0f * 2 * lado);
        for (int y = 0; y < lado; y++)
          for (int x = 0; x < lado; x++)
            if (x + y <= hasta && moduloQr(cacheQr.qr, x, y)) g.fillRect(ox + 2 * x, oy + 2 * y, 2, 2, kOff);
      }
    }

    // A la derecha (centro x = 97), entrando desde el borde: el código del sensor, qué hacer y cuánto le
    // queda al QR en grande, con una línea fina que se acorta a lo largo de los 10 minutos.
    const int dx = (int)lroundf((1.0f - suavizar(((float)t - 150.0f) / 350.0f)) * 40.0f);
    const int cx = 97 + dx;
    char codigo[11];
    snprintf(codigo, sizeof codigo, "%s", v.codigo);
    textoCentrado(g, cx, 4, codigo);
    textoCentrado(g, cx, 18, "Escanee");
    textoCentrado(g, cx, 27, "con la app");
    char vence[8];
    if (v.segundosRestantes >= 0)
      snprintf(vence, sizeof vence, "%d:%02d", v.segundosRestantes / 60 % 100, v.segundosRestantes % 60);
    else
      snprintf(vence, sizeof vence, "--:--");
    const bool ultimoMinuto = v.segundosRestantes >= 0 && v.segundosRestantes < 60;
    if (!ultimoMinuto || (v.ms / 500) % 2) titulo(g, cx - anchoTitulo(g, vence) / 2, 51, vence);
    for (int x = 68 + dx; x <= 126; x += 2) g.drawPixel(x, 58, kOn);
    if (v.segundosRestantes > 0) {
      const int r = v.segundosRestantes > kVigenciaQrS ? kVigenciaQrS : v.segundosRestantes;
      g.fillRect(68 + dx, 57, (58 * r + kVigenciaQrS - 1) / kVigenciaQrS, 3, kOn);
    }
    return;
  }

  // Todavía sin QR: el logo y la marca, como al encender, y qué está esperando
  if (v.pantalla == PantallaVinculo::Solicitando) {
    logoRevelado(g, 2, 0, 10000);
    acentosLogo(g, 2, 0, v.broker == 2 ? Acentos::Fijos : Acentos::Laten, v.ms);
    if (v.broker == 2)
      panelMarca(g, 8, "Pidiendo", "el QR...", -1, v.ms);
    else if (v.broker == 3)
      panelMarca(g, 8, "Sin acceso", "al sistema", -1, v.ms);
    else if (v.wifiBarras < 0)
      panelMarca(g, 8, "Buscando", "Wi-Fi...", -1, v.ms);
    else
      panelMarca(g, 8, "Conectando", "al sistema...", -1, v.ms);
    return;
  }

  // Mensajes: arriba el código del sensor; al medio, ícono y título que entran deslizándose; abajo, qué hacer
  textoCentrado(g, 64, 4, v.codigo);
  char linea[24];
  switch (v.pantalla) {
    case PantallaVinculo::Vinculado:
      snprintf(linea, sizeof linea, "%s  %s", v.cama, v.iniciales);
      mensajeEntrante(g, IconoMensaje::Vinculado, "Vinculado", linea, v.ms, v.msPantalla);
      pieSimple(g, "Coloque los sensores");
      return;
    case PantallaVinculo::Desvinculado:
      mensajeEntrante(g, IconoMensaje::Liberado, "Liberado", "Sensor desvinculado", v.ms, v.msPantalla);
      pieSimple(g, "Retire los sensores");
      return;
    case PantallaVinculo::Emparejando:
      // Abajo, cuánto le queda a la app para confirmar (alterna con qué hacer si no alcanza). Al vencer, el
      // wearable pide un QR nuevo solo.
      mensajeEntrante(g, IconoMensaje::Telefono, "Emparejando", "Registrando...", v.ms, v.msPantalla);
      if (v.segundosRestantes == 0)
        snprintf(linea, sizeof linea, "Pidiendo QR%s", puntos(v.ms));
      else if (v.segundosRestantes > 0 && (v.ms / 2500) % 2 == 0)
        snprintf(linea, sizeof linea, "Quedan %d:%02d", v.segundosRestantes / 60 % 100, v.segundosRestantes % 60);
      else
        snprintf(linea, sizeof linea, "BOOT: QR nuevo");
      pieSimple(g, linea);
      return;
    case PantallaVinculo::Vencido:
      mensajeEntrante(g, IconoMensaje::Vencido, "QR vencido", "pidiendo otro...", v.ms, v.msPantalla);
      pieSimple(g, "BOOT: pedir otro");
      return;
    default:
      return;
  }
}

namespace {
// Tres pasos en la OLED de 128x32: arriba qué hacer ahora y la píldora "n/3"; debajo, círculos unidos por
// una línea (llena si ya se pasó, punteada si no). Hecho = círculo lleno con ✓; el actual parpadea; los
// que faltan, solo el contorno con su número.
void pasos(Adafruit_GFX& g, const char* tituloPaso, int paso, const char* const etiquetas[3], uint32_t ms) {
  char titulo17[17];
  snprintf(titulo17, sizeof titulo17, "%s", tituloPaso);  // deja lugar para la píldora "n/3"
  texto(g, 0, 1, titulo17);
  char cuenta[6];
  snprintf(cuenta, sizeof cuenta, "%d/3", paso < 3 ? paso + 1 : 3);
  pildora(g, 127, 0, cuenta);

  const int cx[3] = {10, 64, 117}, cy = 18, r = 6;
  for (int i = 0; i < 2; i++) {
    if (paso > i)
      g.drawFastHLine(cx[i] + r + 2, cy, cx[i + 1] - cx[i] - 2 * r - 4, kOn);
    else
      for (int x = cx[i] + r + 2; x < cx[i + 1] - r - 1; x += 3) g.drawPixel(x, cy, kOn);
  }
  const bool parpadeo = (ms / 400) % 2;
  for (int i = 0; i < 3; i++) {
    const char numero[2] = {(char)('1' + i), 0};
    if (i < paso) {
      g.fillCircle(cx[i], cy, r, kOn);
      icono(g, cx[i] - 3, cy - 3, kListo, kOff);
    } else if (i == paso && parpadeo) {
      g.fillCircle(cx[i], cy, r, kOn);
      texto(g, cx[i] - 2, cy - 3, numero, kOff);
    } else {
      g.drawCircle(cx[i], cy, r, kOn);
      texto(g, cx[i] - 2, cy - 3, numero);
    }
  }
  texto(g, cx[0] - anchoTexto(etiquetas[0]) / 2 < 0 ? 0 : cx[0] - anchoTexto(etiquetas[0]) / 2, 25, etiquetas[0]);
  textoCentrado(g, cx[1], 25, etiquetas[1]);
  textoDerecha(g, 127, 25, etiquetas[2]);
}
}  // namespace

void dibujarPasosVinculo(Adafruit_GFX& g, const VistaVinculacion& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  EstadoEquipo e;
  e.wifiBarras = v.wifiBarras;
  e.broker = v.broker;
  e.red = v.red;
  e.avisoRed = v.avisoRed;
  e.bateria = v.bateria;
  e.estadoBateria = v.estadoBateria;
  e.ms = v.ms;
  estadoEquipo(g, e);
}

namespace {

// Al encender, la presentación con el logo. Pasa al chequeo con ✓ ✗ ○ solo si, pasados 3 s, falta un sensor:
// ahí hace falta saber cuál.
const uint32_t kMsPresentacion = 3000;

bool faltaSensor(const VistaArranque& v) { return !v.ppg || !v.acelerometro || !v.temperatura || !v.ecgModulo; }

bool conDetalle(const VistaArranque& v) { return faltaSensor(v) && v.ms >= kMsPresentacion; }

int chequeosListos(const VistaArranque& v) {
  const bool listos[] = {v.ppg, v.acelerometro, v.temperatura, v.ecgModulo, v.wifi, v.broker == 2};
  int n = 0;
  for (const bool listo : listos) n += listo;
  return n;
}

// La barra sube en el primer segundo hasta lo que ya está listo, y sigue con cada chequeo
int progresoArranque(const VistaArranque& v) {
  const int listo = chequeosListos(v) * 100 / 6, rampa = (int)(v.ms / 10);
  return listo < rampa ? listo : rampa;
}

// Al encender: el logo se arma al centro (0–800 ms), se corre a la izquierda (900–1300 ms) y "Vitalify" se
// escribe letra por letra; debajo, qué está haciendo en palabras simples y una línea fina que avanza con cada
// chequeo. Los trazos del logo laten mientras se conecta y quedan fijos cuando todo está listo.
void presentacion(Adafruit_GFX& g, const VistaArranque& v) {
  const uint32_t t = v.ms;
  const int xLogo = 36 - (int)lroundf(34.0f * suavizar(((float)t - 900.0f) / 400.0f));
  logoRevelado(g, xLogo, 0, t);
  const bool listo = !faltaSensor(v) && v.broker == 2;
  acentosLogo(g, xLogo, 0, t < 800 ? Acentos::Ocultos : (listo ? Acentos::Fijos : Acentos::Laten), t);
  if (t < 1300) return;
  const int letras = (int)((t - 1300) / 70);
  if (faltaSensor(v))
    panelMarca(g, letras, "Iniciando", "sensores...", progresoArranque(v), t);
  else if (!v.wifi)
    panelMarca(g, letras, "Buscando", "Wi-Fi...", progresoArranque(v), t);
  else if (v.broker == 3)
    panelMarca(g, letras, "Sin acceso", "al sistema", progresoArranque(v), t);
  else if (v.broker != 2)
    panelMarca(g, letras, "Conectando", "al sistema...", progresoArranque(v), t);
  else
    panelMarca(g, letras, "Listo", "", 100, t);
}

}  // namespace

void dibujarArranque(Adafruit_GFX& g, const VistaArranque& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  if (!conDetalle(v)) {
    presentacion(g, v);
    return;
  }

  // Falta un sensor: el chequeo detallado. Encabezado: el logo mini y la marca; la versión y la batería
  icono(g, 0, 1, kLogoMini);
  titulo(g, 19, 12, "Vitalify");
  char version[12];
  snprintf(version, sizeof version, "v%s", v.version);
  textoDerecha(g, 127, 0, version);
  iconoBateria(g, 127, 8, v.bateria, v.estadoBateria, v.ms, false);

  // Los seis chequeos en dos columnas: ✓ listo, ✗ falta, ○ conectando (parpadea)
  const Chequeo wifi = v.wifi ? Chequeo::Listo : Chequeo::Intentando;
  const Chequeo broker = v.broker == 2 ? Chequeo::Listo : (v.broker == 3 ? Chequeo::NoInstalado : Chequeo::Intentando);
  const auto sensor = [](bool ok) { return ok ? Chequeo::Listo : Chequeo::NoInstalado; };
  filaChequeo(g, 2, 19, "SpO2", sensor(v.ppg), nullptr, 0, v.ms);
  filaChequeo(g, 66, 19, "Acel.", sensor(v.acelerometro), nullptr, 0, v.ms);
  filaChequeo(g, 2, 29, "Temp.", sensor(v.temperatura), nullptr, 0, v.ms);
  filaChequeo(g, 66, 29, "ECG", sensor(v.ecgModulo), nullptr, 0, v.ms);
  filaChequeo(g, 2, 39, "Wi-Fi", wifi, nullptr, 0, v.ms);
  filaChequeo(g, 66, 39, "Servidor", broker, nullptr, 0, v.ms);

  char linea[22];
  const bool faltaSensor = !v.ppg || !v.acelerometro || !v.temperatura || !v.ecgModulo;
  if (faltaSensor)  // no sigue hasta que respondan los cuatro sensores (o se pulse BOOT)
    snprintf(linea, sizeof linea, "%s", (v.ms / 1500) % 2 ? "BOOT = seguir igual" : "Revise los marcados");
  else if (v.broker == 3)
    snprintf(linea, sizeof linea, "Servidor: clave?");
  else if (v.broker == 2)
    snprintf(linea, sizeof linea, "Listo: %s", v.codigo);
  else if (v.red[0])
    snprintf(linea, sizeof linea, "%s", v.red);
  else
    snprintf(linea, sizeof linea, "Buscando Wi-Fi%s", puntos(v.ms));
  pie(g, linea);
}

// ---------- Configuración del Wi-Fi desde la app (Bluetooth) ----------
namespace {

// Barra de estado del modo: el ícono de Bluetooth y qué se está haciendo; la batería a la derecha
void barraConfigWifi(Adafruit_GFX& g, const VistaConfigWifi& v) {
  icono(g, 0, 0, kBluetooth);
  texto(g, 8, 0, "Config. Wi-Fi");
  iconoBateria(g, 127, 0, v.bateria, v.estadoBateria, v.ms, true);
}

const char* textoFallo(FalloConfigWifi f) {
  switch (f) {
    case FalloConfigWifi::Clave:
      return "Clave incorrecta";
    case FalloConfigWifi::RedNoEncontrada:
      return "Red no encontrada";
    case FalloConfigWifi::CodigoIncorrecto:
      return "Codigo incorrecto";
    default:
      return "";
  }
}

}  // namespace

void dibujarConfigWifi(Adafruit_GFX& g, const VistaConfigWifi& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);

  const bool esperando = v.etapa == EtapaConfigWifi::Preparando || v.etapa == EtapaConfigWifi::Esperando;
  const int lado = esperando ? qrDe(v.qr) : 0;
  if (lado > 0 && 2 * lado <= 64) {
    // El QR de configuración con el mismo estilo que el de vinculación; a la derecha (centro x = 97), el
    // código de un solo uso en un recuadro para escribirlo a mano si la cámara no lo lee
    g.fillRoundRect(0, 0, 64, 64, 5, kOn);
    dibujarQr(g, (64 - 2 * lado) / 2, 16 - 2 * 6, lado);
    icono(g, 68, 0, kBluetooth);
    texto(g, 76, 0, "WiFi");
    iconoBateria(g, 127, 0, v.bateria, v.estadoBateria, v.ms, true);
    char codigo[11];
    snprintf(codigo, sizeof codigo, "%s", v.codigo);
    textoCentrado(g, 97, 8, codigo);
    if (v.fallo == FalloConfigWifi::CodigoIncorrecto) {  // la app escribió otro código: que vuelva a escanear
      if ((v.ms / 500) % 2) {
        textoCentrado(g, 97, 19, "Escanee de");
        textoCentrado(g, 97, 28, "nuevo");
      }
    } else {
      textoCentrado(g, 97, 19, "Escanee en");
      textoCentrado(g, 97, 28, "app admin");
    }
    g.drawRoundRect(68, 38, 59, 11, 3, kOn);
    textoCentrado(g, 97, 40, v.pop);
    textoCentrado(g, 97, 54, "BOOT=salir");
    return;
  }

  barraConfigWifi(g, v);
  textoCentrado(g, 64, kYContexto, v.codigo);
  char linea[22];
  snprintf(linea, sizeof linea, "%s", v.ssid);
  switch (v.etapa) {
    case EtapaConfigWifi::Probando:
      mensaje(g, IconoMensaje::Wifi, "Conectando", linea, v.ms);
      pie(g, "Probando la clave");
      return;
    case EtapaConfigWifi::Fallo:
      mensaje(g, IconoMensaje::Error, "Error", textoFallo(v.fallo), v.ms);
      pie(g, "Corrija en la app");
      return;
    case EtapaConfigWifi::Lista: {
      mensaje(g, IconoMensaje::Vinculado, "Wi-Fi listo", linea, v.ms);
      char reinicio[22];
      snprintf(reinicio, sizeof reinicio, "Reiniciando%s", puntos(v.ms));
      pie(g, reinicio);
      return;
    }
    default:  // la app se conectó por Bluetooth: elige la red allá
      mensaje(g, IconoMensaje::Telefono, "Elija la red", "en la app del admin", v.ms);
      pie(g, "BOOT: cancelar");
      return;
  }
}

void dibujarPasosConfigWifi(Adafruit_GFX& g, const VistaConfigWifi& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  int paso = 0;
  char titulo[22];
  switch (v.etapa) {
    case EtapaConfigWifi::AppConectada:
      paso = 1;
      snprintf(titulo, sizeof titulo, "Elija la red");
      break;
    case EtapaConfigWifi::Probando:
      paso = 2;
      snprintf(titulo, sizeof titulo, "Probando%s", puntos(v.ms));
      break;
    case EtapaConfigWifi::Fallo:
      paso = 1;
      snprintf(titulo, sizeof titulo, "%s", v.fallo == FalloConfigWifi::Clave ? "Revise la clave" : "Red no hallada");
      break;
    case EtapaConfigWifi::Lista:
      paso = 3;
      snprintf(titulo, sizeof titulo, "Listo: %s", v.ssid);
      break;
    default:
      snprintf(titulo, sizeof titulo, "%s",
               v.fallo == FalloConfigWifi::CodigoIncorrecto ? "Escanee de nuevo" : "Escanee el QR");
  }
  const char* const etiquetas[3] = {"QR", "Red", "Listo"};
  pasos(g, titulo, paso, etiquetas, v.ms);
}

// ---------- Estado del equipo (OLED 128x32 durante el arranque) ----------
void dibujarVerificacion(Adafruit_GFX& g, const VistaArranque& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  EstadoEquipo e;
  e.wifiBarras = v.wifi ? v.wifiBarras : -1;
  e.broker = v.broker;
  e.red = v.red;
  e.avisoRed = v.avisoRed;
  e.bateria = v.bateria;
  e.estadoBateria = v.estadoBateria;
  e.ms = v.ms;
  estadoEquipo(g, e);
}

// ---------- ECG ----------
BarridoEcg::BarridoEcg(int muestrasPorColumna, int ancho)
    : porColumna_(muestrasPorColumna < 1 ? 1 : muestrasPorColumna), ancho_(kAnchoMax) {
  fijarAncho(ancho);
}

void BarridoEcg::fijarAncho(int ancho) {
  ancho_ = ancho < kHueco + 1 ? kHueco + 1 : (ancho > kAnchoMax ? kAnchoMax : ancho);
  limpiar();
}

void BarridoEcg::limpiar() {
  for (int x = 0; x < kAnchoMax; x++) {
    valida_[x] = false;
    min_[x] = max_[x] = primero_[x] = ultimo_[x] = media_[x] = 0;
  }
  cursor_ = 0;
  enColumna_ = 0;
}

void BarridoEcg::agregar(float v) {
  if (enColumna_ == 0) {
    accMin_ = accMax_ = accPrimero_ = v;
    accSuma_ = 0;
  } else {
    if (v < accMin_) accMin_ = v;
    if (v > accMax_) accMax_ = v;
  }
  accUltimo_ = v;
  accSuma_ += v;
  if (++enColumna_ < porColumna_) return;
  enColumna_ = 0;
  min_[cursor_] = accMin_;
  max_[cursor_] = accMax_;
  primero_[cursor_] = accPrimero_;
  ultimo_[cursor_] = accUltimo_;
  media_[cursor_] = accSuma_ / (float)porColumna_;
  valida_[cursor_] = true;
  cursor_ = (cursor_ + 1) % ancho_;
  for (int k = 0; k < kHueco; k++) valida_[(cursor_ + k) % ancho_] = false;
}

void BarridoEcg::actualizarEscala() {
  float vmin = 0, vmax = 0;
  bool hay = false;
  for (int x = 0; x < ancho_; x++) {
    if (!valida_[x]) continue;
    if (!hay || min_[x] < vmin) vmin = min_[x];
    if (!hay || max_[x] > vmax) vmax = max_[x];
    hay = true;
  }
  if (!hay) return;
  float objetivo = (vmax - vmin) * 1.15f;
  if (objetivo < 80) objetivo = 80;  // con señal plana no amplifica el ruido
  if (objetivo > rango_)
    rango_ = objetivo;
  else
    rango_ += (objetivo - rango_) * 0.05f;
  centro_ += ((vmax + vmin) * 0.5f - centro_) * 0.2f;
}

namespace {
// Trazo del barrido en la franja [y0, y0 + alto): la escala sigue a la amplitud real de la onda
void dibujarTrazo(Adafruit_GFX& g, BarridoEcg& b, int y0, int alto) {
  b.actualizarEscala();
  const float centro = b.centro(), rango = b.rango();
  const float medio = y0 + (alto - 1) / 2.0f, escala = (alto - 2) / rango;
  const auto aY = [&](float valor) {
    const int y = (int)lroundf(medio - (valor - centro) * escala);
    return y < y0 ? y0 : (y > y0 + alto - 1 ? y0 + alto - 1 : y);
  };
  // Columna "fina" (variación de hasta 3 px): un punto en su promedio, unido al anterior. Columna con un
  // salto mayor (QRS): de mínimo a máximo, entrando por su primer valor y saliendo por el último.
  int salidaAnterior = -1;
  for (int x = 0; x < b.ancho(); x++) {
    if (!b.valida(x)) {
      salidaAnterior = -1;
      continue;
    }
    const int arriba = aY(b.maximo(x)), abajo = aY(b.minimo(x));
    const bool fina = abajo - arriba <= 3;
    const int entrada = fina ? aY(b.media(x)) : aY(b.primero(x));
    if (fina)
      g.drawPixel(x, entrada, kOn);
    else
      g.drawFastVLine(x, arriba, abajo - arriba + 1, kOn);
    if (salidaAnterior >= 0) g.drawLine(x - 1, salidaAnterior, x, entrada, kOn);
    salidaAnterior = fina ? entrada : aY(b.ultimo(x));
  }
}
}  // namespace


void dibujarEcg(Adafruit_GFX& g, BarridoEcg& b, const VistaEcg& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  // Arriba, el estado del equipo; debajo (filas 10–31), el ECG. La FC va en la principal.
  barraEstado(g, barraDe(v));
  for (int x = 0; x < 128; x += 2) g.drawPixel(x, 9, kOn);

  if (!v.electrodos) {  // sin trazo: ícono y qué hacer, sobre la línea de base quieta
    if ((v.ms / 700) % 2) icono(g, 4, 14, kElectrodo);
    texto(g, 18, 12, "Conecte los");
    texto(g, 18, 21, "electrodos");
    for (int x = 84; x < 128; x += 3) g.drawPixel(x, 24, kOn);
    return;
  }
  if (v.ruido) {  // dibujar el ruido no sirve: se dice qué hacer
    textoCentrado(g, 64, 13, "Se" ENIE "al con ruido");
    textoCentrado(g, 64, 23, (v.ms / 2000) % 2 ? "Revise el RL" : "Fije electrodos");
    return;
  }
  // Cuadrícula de puntos cada 8 px, como el papel de un electrocardiógrafo: da la escala sin tapar el trazo
  for (int y = 13; y < 32; y += 8)
    for (int x = 4; x < b.ancho(); x += 8) g.drawPixel(x, y, kOn);
  // El corazón late con cada QRS, en la esquina del trazo
  icono(g, 120, 11, v.pulso ? kCorazonLleno : kCorazonVacio);
  dibujarTrazo(g, b, 11, 21);
}
