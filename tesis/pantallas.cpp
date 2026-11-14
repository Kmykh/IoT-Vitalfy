// pantallas.cpp — ver pantallas.h
//
// Distribución de la OLED principal (128x64). El encabezado ocupa las 16 filas superiores, que en
// las OLED bicolor son amarillas: así ningún texto queda partido entre los dos colores.
//   filas  0–7   barra de estado (Wi-Fi, broker, pendientes, hora)
//   filas  8–14  etiquetas (FC / SpO2) o identidad del dispositivo en espera
//   filas 16–46  valores grandes
//   filas 47–63  temperatura (izquierda), su rango y la FR (derecha), o la alerta de caída
//
// Sin vincular, la pantalla es otra (dibujarVinculacion): el QR a la izquierda y las indicaciones a la
// derecha.
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

const int kYEtiquetas = 8;
const int kBaseNumeros = 44;
const int kBaseGuiones = 40;       // "--" centrado a la altura de los dígitos
const int kYLineaInferior = 47;

// Caracteres de la fuente clásica (CP437) para escribir en español
const char kGrado = (char)0xF8;
const char kFlechaArriba = (char)0x18;
const char kAbreExclamacion = (char)0xAD;

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

template <int N>
void icono(Adafruit_GFX& g, int x, int y, const char* const (&filas)[N], uint16_t color = kOn) {
  for (int j = 0; j < N; j++)
    for (int i = 0; filas[j][i]; i++)
      if (filas[j][i] == '#') g.drawPixel(x + i, y + j, color);
}

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

void lineaPunteada(Adafruit_GFX& g, int x, int y0, int y1) {
  for (int y = y0; y <= y1; y += 2) g.drawPixel(x, y, kOn);
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

// ---------- Barra de estado: Wi-Fi, broker MQTT, pendientes y hora ----------
void barraEstado(Adafruit_GFX& g, const VistaPrincipal& v) {
  iconosRed(g, 0, v.wifiBarras, v.broker, v.ms);

  // Lecturas guardadas sin enviar
  if (v.pendientes > 0) {
    char p[8];
    snprintf(p, sizeof p, "%c%u", kFlechaArriba, (unsigned)(v.pendientes > 999 ? 999 : v.pendientes));
    texto(g, 33, 0, p);
  }

  if (v.acelerometro && v.movimiento) texto(g, 66, 0, "MOV");

  // Hora local (NTP)
  char hora[12];
  if (v.horaValida)
    snprintf(hora, sizeof hora, "%02u:%02u", (unsigned)(v.hora % 24), (unsigned)(v.minuto % 60));
  else
    strcpy(hora, "--:--");
  textoDerecha(g, 127, 0, hora);
}

// ---------- Fila inferior: temperatura a la izquierda; su rango y la FR a la derecha ----------
// (o, a pantalla completa, la alerta de caída o un aviso del backend)
void textoFr(const VistaPrincipal& v, char* s, size_t tam) {
  s[0] = 0;
  if (!v.acelerometro) return;
  if (v.fr > 0) {
    snprintf(s, tam, "FR %d/min", v.fr);
    return;
  }
  switch (v.estadoFr) {
    case EstadoRespiracion::Movimiento:
      snprintf(s, tam, "FR: quieto");  // pide quietud: moviéndose no se puede contar
      break;
    case EstadoRespiracion::SinSenal:
      snprintf(s, tam, "FR --");
      break;
    default:
      snprintf(s, tam, "FR midiendo");
  }
}

void filaInferior(Adafruit_GFX& g, const VistaPrincipal& v) {
  if (v.alertaCaida || v.aviso[0]) {
    const bool invertido = (v.ms / 500) % 2 == 0;
    if (invertido) g.fillRect(0, kYLineaInferior + 1, 128, 64 - kYLineaInferior - 1, kOn);
    char msg[24];
    if (v.alertaCaida)
      snprintf(msg, sizeof msg, "%cCAIDA DETECTADA!", kAbreExclamacion);
    else
      snprintf(msg, sizeof msg, "%s", v.aviso);
    textoCentrado(g, 64, 52, msg, invertido ? kOff : kOn);
    return;
  }

  g.drawFastHLine(0, kYLineaInferior, 128, kOn);
  char fr[24];
  textoFr(v, fr, sizeof fr);
  textoDerecha(g, 127, 57, fr);
  icono(g, 1, 53, kTermometro);
  switch (v.estadoTemp) {
    case EstadoTemperatura::SinSonda:
      texto(g, 8, 49, "Sin sonda temp.");
      return;
    case EstadoTemperatura::SinContacto:  // al aire: el número sería la temperatura ambiente
      texto(g, 8, 49, "Coloque la sonda");
      return;
    default:
      break;
  }
  char t[8];
  snprintf(t, sizeof t, "%.1f", v.temp);
  g.setFont(&FreeSansBold9pt7b);
  g.setTextColor(kOn);
  g.setCursor(8, 62);
  g.print(t);
  const int x = g.getCursorX() + 1;
  const char unidad[3] = {kGrado, 'C', 0};
  texto(g, x, 50, unidad);
  if (v.estadoTemp == EstadoTemperatura::Estabilizando)
    textoDerecha(g, 127, 49, (v.ms / 500) % 2 ? "midiendo" : "midiendo.");
  else
    textoDerecha(g, 127, 49, EvaluadorTemperatura::nombre(v.rangoTemp));
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

void pantallaEspera(Adafruit_GFX& g, const VistaPrincipal& v) {
  // Encabezado: con qué código registrar el sensor y a qué red está unido, o qué falla en la red
  char info[22];  // 21 caracteres de 6 px
  if (v.avisoRed)
    snprintf(info, sizeof info, "%s", v.red);
  else if (v.cama[0])  // vinculado: la cama y las iniciales del paciente
    snprintf(info, sizeof info, "%s  %s", v.cama, v.iniciales);
  else
    snprintf(info, sizeof info, "%s %s", v.codigo, v.red);
  textoCentrado(g, 64, kYEtiquetas, info);

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
enum class Conexion : uint8_t { Lista, Pendiente, NoInstalado };
void filaConexion(Adafruit_GFX& g, int y, const char* nombre, Conexion c, const char* falta) {
  if (c == Conexion::Lista)
    icono(g, 0, y, kListo);
  else if (c == Conexion::Pendiente)
    icono(g, 1, y + 1, kPendiente);
  else
    icono(g, 1, y + 1, kNoInstalado);
  texto(g, 10, y, nombre);
  textoDerecha(g, 127, y, c == Conexion::Lista ? "OK" : (c == Conexion::NoInstalado ? "no instalado" : falta));
}

void pantallaConexion(Adafruit_GFX& g, const VistaPrincipal& v) {
  char titulo[22];
  if (v.cama[0])  // vinculado: a qué paciente hay que conectarlo
    snprintf(titulo, sizeof titulo, "%s  %s", v.cama, v.iniciales);
  else if (v.avisoRed)  // sin servidor no hay vinculación: se dice qué pasa con la red
    snprintf(titulo, sizeof titulo, "%s", v.red);
  else if (v.broker != 2)
    snprintf(titulo, sizeof titulo, "%s", v.wifiBarras < 0 ? "Sin Wi-Fi: buscando" : "Sin servidor");
  else
    snprintf(titulo, sizeof titulo, "Conecte al paciente");
  textoCentrado(g, 64, kYEtiquetas, titulo);
  const bool dedo = v.ppg != EstadoPpg::SinDedo;
  filaConexion(g, 19, "SpO2", !v.sensorPpg ? Conexion::NoInstalado : (dedo ? Conexion::Lista : Conexion::Pendiente),
               "coloque el dedo");
  filaConexion(g, 29, "ECG",
               !v.ecgInstalado ? Conexion::NoInstalado : (v.electrodos ? Conexion::Lista : Conexion::Pendiente),
               "conecte cables");
  const Conexion temp = v.estadoTemp == EstadoTemperatura::SinSonda      ? Conexion::NoInstalado
                        : v.estadoTemp == EstadoTemperatura::SinContacto ? Conexion::Pendiente
                                                                          : Conexion::Lista;
  filaConexion(g, 39, "Temp", temp, "coloque sonda");
  g.drawFastHLine(0, 49, 128, kOn);
  if (dedo)
    textoCentrado(g, 64, 53, "Iniciando monitoreo");
  else if ((v.ms / 600) % 2)
    textoCentrado(g, 64, 53, v.sensorPpg ? "Ponga el dedo: inicia" : "Inicia con ECG o temp");
}

void pantallaSignos(Adafruit_GFX& g, const VistaPrincipal& v) {
  // Etiquetas (encabezado)
  texto(g, 2, kYEtiquetas, "FC");
  icono(g, 16, kYEtiquetas + 1, v.pulso ? kCorazonLleno : kCorazonVacio);
  if (v.fc > 0) textoDerecha(g, 61, kYEtiquetas, v.fcDelEcg ? "ECG" : "PPG");
  texto(g, 67, kYEtiquetas, "SpO2");
  textoDerecha(g, 127, kYEtiquetas, "%");
  lineaPunteada(g, 63, kYEtiquetas, kBaseNumeros);

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
    g.drawRect(68, 41, 56, 4, kOn);
    g.fillRect(69, 42, 54 * v.progreso / 100, 2, kOn);
  }
}

}  // namespace

void dibujarPrincipal(Adafruit_GFX& g, const VistaPrincipal& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  barraEstado(g, v);
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

void textoGrandeCentrado(Adafruit_GFX& g, int cx, int base, const char* s) {
  g.setFont(&FreeSansBold9pt7b);
  g.setTextSize(1);
  g.setTextColor(kOn);
  int16_t x1, y1;
  uint16_t ancho, alto;
  g.getTextBounds(s, 0, base, &x1, &y1, &ancho, &alto);
  g.setCursor(cx - (int)ancho / 2 - x1, base);
  g.print(s);
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

// ✓ grande (pantalla Vinculado): dos trazos de 3 px en una caja de 21x17 desde (x, y)
void checkGrande(Adafruit_GFX& g, int x, int y) {
  for (int k = 0; k < 3; k++) {
    g.drawLine(x, y + 8 + k, x + 6, y + 14 + k, kOn);
    g.drawLine(x + 6, y + 14 + k, x + 20, y + k, kOn);
  }
}

// Una fila de la conexión: ✓ listo u ○ pendiente (parpadea), el nombre y el detalle a la derecha
void filaRed(Adafruit_GFX& g, int y, const char* nombre, bool listo, const char* detalle, uint32_t ms) {
  if (listo)
    icono(g, 0, y, kListo);
  else if ((ms / 400) % 2)
    icono(g, 1, y + 1, kPendiente);
  texto(g, 10, y, nombre);
  textoDerecha(g, 127, y, detalle);
}

// Pantalla sin QR: barra de estado con la red a la izquierda y el código del sensor a la derecha
void encabezadoVinculo(Adafruit_GFX& g, const VistaVinculacion& v) {
  iconosRed(g, 0, v.wifiBarras, v.broker, v.ms);
  textoDerecha(g, 127, 0, v.codigo);
}

}  // namespace

void dibujarVinculacion(Adafruit_GFX& g, const VistaVinculacion& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);

  const int lado = v.pantalla == PantallaVinculo::Qr ? qrDe(v.qr) : 0;
  if (lado > 0 && 2 * lado <= 64) {
    // QR oscuro sobre un cuadro claro de 64x64: el borde claro es su margen de silencio.
    // En las OLED bicolor hay una franja sin píxeles entre la fila 15 (amarilla) y la 16 (azul). Si cae
    // dentro de los patrones de posición de arriba, el QR no se lee. A 4 px del borde, la franja queda justo
    // encima de la última fila oscura de esos patrones (la 6) y solo la engrosa: se lee igual (comprobado
    // con un lector de QR sobre la imagen con la franja simulada).
    g.fillRect(0, 0, 64, 64, kOn);
    const int ox = (64 - 2 * lado) / 2, oy = 16 - 2 * 6;
    for (int y = 0; y < lado; y++)
      for (int x = 0; x < lado; x++)
        if (moduloQr(cacheQr.qr, x, y)) g.fillRect(ox + 2 * x, oy + 2 * y, 2, 2, kOff);

    // A la derecha: red y código del sensor en el encabezado; debajo, qué hacer y cuánto le queda al QR
    // (en texto y en una barra que se vacía a lo largo de los 10 minutos)
    iconosRed(g, 68, v.wifiBarras, v.broker, v.ms);
    char codigo[11];
    snprintf(codigo, sizeof codigo, "%s", v.codigo);
    texto(g, 68, 8, codigo);
    texto(g, 68, 21, "Escanee");
    texto(g, 68, 31, "con la app");
    char vence[12];
    if (v.segundosRestantes >= 0)
      snprintf(vence, sizeof vence, "vence %d:%02d", v.segundosRestantes / 60 % 100, v.segundosRestantes % 60);
    else
      snprintf(vence, sizeof vence, "vence --:--");
    const bool ultimoMinuto = v.segundosRestantes >= 0 && v.segundosRestantes < 60;
    if (!ultimoMinuto || (v.ms / 500) % 2) texto(g, 68, 45, vence);  // el último minuto parpadea
    g.drawRect(68, 55, 60, 6, kOn);
    if (v.segundosRestantes > 0) {
      const int s = v.segundosRestantes > kVigenciaQrS ? kVigenciaQrS : v.segundosRestantes;
      g.fillRect(70, 57, (56 * s + kVigenciaQrS - 1) / kVigenciaQrS, 2, kOn);
    }
    return;
  }

  // Sin QR que mostrar: a lo ancho. Arriba la red y el código del sensor; al medio, en grande, qué pasa;
  // abajo, el detalle. El título queda entero en la zona azul de las OLED bicolor (desde la fila 16).
  encabezadoVinculo(g, v);
  char linea[24];
  switch (v.pantalla) {
    case PantallaVinculo::Vinculado:
      checkGrande(g, 2, 17);
      g.setFont(&FreeSansBold9pt7b);
      g.setTextColor(kOn);
      g.setCursor(30, 31);
      g.print("Vinculado");
      g.setFont(nullptr);
      snprintf(linea, sizeof linea, "%s  %s", v.cama, v.iniciales);
      textoCentrado(g, 64, 41, linea);
      g.drawFastHLine(0, 51, 128, kOn);
      textoCentrado(g, 64, 55, "Coloque los sensores");
      return;
    case PantallaVinculo::Desvinculado:
      textoGrandeCentrado(g, 64, 31, "Desvinculado");
      textoCentrado(g, 64, 41, "Sensor liberado");
      g.drawFastHLine(0, 51, 128, kOn);
      textoCentrado(g, 64, 55, "Retire los sensores");
      return;
    case PantallaVinculo::Emparejando:
      // Abajo, cuánto le queda a la app para confirmar (alterna con qué hacer si no alcanza). Al vencer, el
      // wearable pide un QR nuevo solo.
      textoGrandeCentrado(g, 64, 31, "Emparejando");
      textoConPuntosCentrado(g, 64, 41, "Registrando", v.ms);
      g.drawFastHLine(0, 51, 128, kOn);
      if (v.segundosRestantes == 0) {
        textoConPuntosCentrado(g, 64, 55, "Pidiendo QR", v.ms);
      } else if (v.segundosRestantes > 0 && (v.ms / 2500) % 2 == 0) {
        snprintf(linea, sizeof linea, "Quedan %d:%02d", v.segundosRestantes / 60 % 100, v.segundosRestantes % 60);
        textoCentrado(g, 64, 55, linea);
      } else {
        textoCentrado(g, 64, 55, "BOOT: QR nuevo");
      }
      return;
    case PantallaVinculo::Vencido:
      textoGrandeCentrado(g, 64, 31, "QR vencido");
      textoConPuntosCentrado(g, 64, 41, "pidiendo otro", v.ms);
      g.drawFastHLine(0, 51, 128, kOn);
      textoCentrado(g, 64, 55, "BOOT: pedir otro");
      return;
    default:
      break;
  }
  if (v.broker == 2) {  // conectado: el backend manda el QR enseguida
    textoGrandeCentrado(g, 64, 31, "Pidiendo QR");
    textoConPuntosCentrado(g, 64, 41, "al servidor", v.ms);
    g.drawFastHLine(0, 51, 128, kOn);
    textoCentrado(g, 64, 55, "BOOT: pedir otro");
    return;
  }
  // Todavía sin servidor: cómo va la conexión, paso a paso (sin ella no hay QR). Abajo, la red a la que se
  // unió, el aviso de conectividad o, buscando, que el ESP32 solo ve redes de 2,4 GHz.
  textoGrandeCentrado(g, 64, 31, "Conectando");
  const bool wifi = v.wifiBarras >= 0;
  filaRed(g, 37, "Wi-Fi", wifi, wifi ? "OK" : "buscando", v.ms);
  filaRed(g, 46, "Servidor", false, v.broker == 3 ? "clave?" : (wifi ? "conectando" : "--"), v.ms);
  char pie[22];
  snprintf(pie, sizeof pie, "%s", v.avisoRed || wifi ? v.red : "Use una red 2,4 GHz");
  textoCentrado(g, 64, 56, pie);
}

void dibujarPasosVinculo(Adafruit_GFX& g, const VistaVinculacion& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);

  // Paso actual (0 = QR, 1 = App, 2 = Listo; 3 = los tres completos) y qué hacer ahora
  int paso = 0;
  char titulo[22];
  switch (v.pantalla) {
    case PantallaVinculo::Qr:
      snprintf(titulo, sizeof titulo, "Escanee el QR");
      break;
    case PantallaVinculo::Vencido:
      snprintf(titulo, sizeof titulo, "QR vencido");
      break;
    case PantallaVinculo::Emparejando:
      paso = 1;
      if (v.segundosRestantes > 0)
        snprintf(titulo, sizeof titulo, "Registrando %d:%02d", v.segundosRestantes / 60 % 100, v.segundosRestantes % 60);
      else
        snprintf(titulo, sizeof titulo, "Registrando%s", puntos(v.ms));
      break;
    case PantallaVinculo::Vinculado:
      paso = 3;
      snprintf(titulo, sizeof titulo, "Listo: %s", v.cama);
      break;
    case PantallaVinculo::Desvinculado:
      snprintf(titulo, sizeof titulo, "Desvinculado");
      break;
    default:
      snprintf(titulo, sizeof titulo, "%s%s",
               v.broker == 2 ? "Pidiendo QR" : (v.wifiBarras < 0 ? "Buscando Wi-Fi" : "Conectando"), puntos(v.ms));
  }
  titulo[17] = '\0';  // deja lugar para "n/3"
  texto(g, 0, 0, titulo);
  char cuenta[6];
  snprintf(cuenta, sizeof cuenta, "%d/3", paso < 3 ? paso + 1 : 3);
  textoDerecha(g, 127, 0, cuenta);

  // Los tres pasos: círculos unidos por una línea (llena si ya se pasó, punteada si no). Hecho = círculo
  // lleno con ✓; el actual parpadea; los que faltan, solo el contorno con su número.
  const int cx[3] = {10, 64, 117}, cy = 18, r = 6;
  for (int i = 0; i < 2; i++) {
    if (paso > i)
      g.drawFastHLine(cx[i] + r + 2, cy, cx[i + 1] - cx[i] - 2 * r - 4, kOn);
    else
      for (int x = cx[i] + r + 2; x < cx[i + 1] - r - 1; x += 3) g.drawPixel(x, cy, kOn);
  }
  const bool parpadeo = (v.ms / 400) % 2;
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
  texto(g, cx[0] - 5, 25, "QR");
  textoCentrado(g, cx[1], 25, "App");
  textoDerecha(g, 127, 25, "Listo");
}

void dibujarArranque(Adafruit_GFX& g, const VistaArranque& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  g.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds("Vitalify", 0, 13, &x1, &y1, &w, &h);
  g.setTextColor(kOn);
  g.setCursor(64 - (int)w / 2 - x1, 13);
  g.print("Vitalify");
  g.setFont(nullptr);

  char linea[24];
  snprintf(linea, sizeof linea, "%s  v%s", v.codigo, v.version);
  textoCentrado(g, 64, 17, linea);
  g.drawFastHLine(0, 26, 128, kOn);

  // "..." animado mientras se conecta
  static const char* const kPuntos[] = {".", "..", "..."};
  const char* puntos = kPuntos[(v.ms / 350) % 3];
  const char* broker = v.broker == 2 ? "OK" : (v.broker == 3 ? "CLAVE" : (v.wifi ? puntos : "--"));
  const struct {
    const char* nombre;
    const char* estado;
    int x, y;
  } items[] = {{"SpO2", v.ppg ? "OK" : "NO", 2, 29},         {"Acel", v.acelerometro ? "OK" : "NO", 68, 29},
               {"Temp", v.temperatura ? "OK" : "NO", 2, 38}, {"ECG", v.ecgModulo ? "OK" : "NO", 68, 38},
               {"WiFi", v.wifi ? "OK" : puntos, 2, 47},      {"Serv", broker, 68, 47}};
  for (const auto& it : items) {
    texto(g, it.x, it.y, it.nombre);
    textoDerecha(g, it.x + 57, it.y, it.estado);
  }
  g.drawFastHLine(0, 55, 128, kOn);
  char pie[22];
  const bool faltaSensor = !v.ppg || !v.acelerometro || !v.temperatura || !v.ecgModulo;
  if (faltaSensor)  // no sigue hasta que respondan los cuatro sensores (o se pulse BOOT)
    snprintf(pie, sizeof pie, "%s", (v.ms / 1500) % 2 ? "BOOT = seguir igual" : "Revise los NO");
  else if (v.broker == 2)
    snprintf(pie, sizeof pie, "Todo conectado");
  else if (v.red[0])
    snprintf(pie, sizeof pie, "%s", v.red);
  else
    snprintf(pie, sizeof pie, "Buscando Wi-Fi%s", puntos);
  textoCentrado(g, 64, 57, pie);
}

// ---------- Verificación (OLED 128x32 durante el arranque) ----------
namespace {
// Un latido estilizado cada 64 px (P, QRS y T): columna y desplazamiento vertical de cada vértice
const int8_t kLatido[][2] = {{0, 0},  {6, 0},  {9, -2},  {12, 0}, {16, 0}, {17, 2}, {19, -10},
                             {21, 6}, {23, 0}, {30, 0}, {34, -3}, {38, 0}, {64, 0}};

int alturaLatido(int x) {
  const int u = x % 64;
  for (size_t i = 1; i < sizeof kLatido / sizeof kLatido[0]; i++) {
    if (u > kLatido[i][0]) continue;
    const int x0 = kLatido[i - 1][0], y0 = kLatido[i - 1][1], x1 = kLatido[i][0], y1 = kLatido[i][1];
    return y0 + (y1 - y0) * (u - x0) / (x1 - x0);
  }
  return 0;
}
}  // namespace

void dibujarVerificacion(Adafruit_GFX& g, const VistaArranque& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);

  // Los seis chequeos de la OLED principal; arriba, el primero que falta
  const bool listos[] = {v.ppg, v.acelerometro, v.temperatura, v.ecgModulo, v.wifi, v.broker == 2};
  int n = 0;
  for (const bool listo : listos) n += listo;
  const char* paso = !v.ppg            ? "Falta sensor SpO2"
                     : !v.acelerometro ? "Falta acelerometro"
                     : !v.temperatura  ? "Falta sonda temp."
                     : !v.ecgModulo    ? "Falta modulo ECG"
                     : !v.wifi         ? "Buscando Wi-Fi"
                     : v.broker == 3   ? "Servidor: clave?"
                     : v.broker != 2   ? "Esperando servidor"
                                       : "Todo listo";
  texto(g, 0, 0, paso);
  char cuenta[6];
  snprintf(cuenta, sizeof cuenta, "%d/6", n);
  textoDerecha(g, 127, 0, cuenta);

  // Latido que recorre la pantalla como en un monitor, sobre una línea de base punteada. La cabeza
  // sigue de largo hasta que la estela sale por la derecha.
  const int base = 18, estela = 80;
  const int cabeza = (int)((v.ms / 12) % (128 + estela));
  for (int x = 0; x < 128; x += 4) g.drawPixel(x, base, kOn);
  for (int x = cabeza - estela + 1; x <= cabeza && x < 128; x++)
    if (x >= 1) g.drawLine(x - 1, base + alturaLatido(x - 1), x, base + alturaLatido(x), kOn);
  if (cabeza < 127) g.fillRect(cabeza, base + alturaLatido(cabeza) - 1, 2, 2, kOn);

  // Progreso de los chequeos
  g.drawRect(0, 28, 128, 4, kOn);
  g.fillRect(1, 29, 126 * n / 6, 2, kOn);
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
  const int panel = b.ancho() + 1;  // x del separador
  lineaPunteada(g, panel, 0, 31);

  // Panel derecho: FC del ECG
  texto(g, panel + 3, 0, "ECG");
  icono(g, 121, 1, v.pulso && v.electrodos && !v.ruido ? kCorazonLleno : kCorazonVacio);
  char s[12];
  if (v.electrodos && !v.ruido && v.fc > 0)
    snprintf(s, sizeof s, "%d", v.fc);
  else
    strcpy(s, "--");
  numeroGrande(g, &FreeSansBold9pt7b, panel + 15, 23, s);
  textoCentrado(g, panel + 15, 25, "lpm");

  const int cx = b.ancho() / 2;
  if (!v.electrodos) {
    if ((v.ms / 700) % 2) {
      textoCentrado(g, cx, 7, "Electrodos");
      textoCentrado(g, cx, 17, "desconectados");
    }
    g.drawFastHLine(0, 30, b.ancho(), kOn);
    return;
  }
  if (v.ruido) {  // dibujar el ruido no sirve: se dice qué hacer, con la línea de base quieta
    textoCentrado(g, cx, 3, "Se\xA4" "al con ruido");  // 0xA4 = ñ en CP437
    textoCentrado(g, cx, 13, (v.ms / 2000) % 2 ? "Revise el RL" : "Fije electrodos");
    for (int x = 0; x < b.ancho(); x += 3) g.drawPixel(x, 27, kOn);
    return;
  }
  dibujarTrazo(g, b, 0, 32);
}
