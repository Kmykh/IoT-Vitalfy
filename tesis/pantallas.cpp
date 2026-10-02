// pantallas.cpp — ver pantallas.h
//
// Distribución de la OLED principal (128x64). El encabezado ocupa las 16 filas superiores, que en
// las OLED bicolor son amarillas: así ningún texto queda partido entre los dos colores.
//   filas  0–7   barra de estado (Wi-Fi, broker, pendientes, hora)
//   filas  8–14  etiquetas (FC / SpO2) o identidad del dispositivo en espera
//   filas 16–46  valores grandes
//   filas 47–63  temperatura y actividad (o la alerta de caída)
#include "pantallas.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
const char* const kCorazonGrandeLleno[] = {
    ".###...###.",
    "#####.#####",
    "###########",
    "###########",
    "###########",
    ".#########.",
    "..#######..",
    "...#####...",
    "....###....",
    ".....#.....",
};
const char* const kCorazonGrandeVacio[] = {
    ".###...###.",
    "#...#.#...#",
    "#....#....#",
    "#.........#",
    "#.........#",
    ".#.......#.",
    "..#.....#..",
    "...#...#...",
    "....#.#....",
    ".....#.....",
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

// ---------- Barra de estado: Wi-Fi, broker MQTT, pendientes y hora ----------
void barraEstado(Adafruit_GFX& g, const VistaPrincipal& v) {
  const bool parpadeo = (v.ms / 400) % 2;

  // Wi-Fi: 4 barras (filas 0–6). Buscando red: las barras se animan.
  const int alturas[4] = {2, 3, 5, 7};
  const int nivel = v.wifiBarras < 0 ? (int)((v.ms / 250) % 5) : v.wifiBarras;
  for (int i = 0; i < 4; i++) {
    const int x = i * 3;
    if (i < nivel)
      g.fillRect(x, 7 - alturas[i], 2, alturas[i], kOn);
    else
      g.drawFastHLine(x, 6, 2, kOn);
  }

  // Broker MQTT: nube llena = conectado; vacía parpadeando = intentando; "!" = credenciales
  switch (v.broker) {
    case 2:
      icono(g, 14, 1, kNubeLlena);
      break;
    case 1:
      if (parpadeo) icono(g, 14, 1, kNubeVacia);
      break;
    case 3:
      icono(g, 14, 1, kNubeVacia);
      if (parpadeo) texto(g, 27, 0, "!");
      break;
    default:
      icono(g, 14, 1, kNubeVacia);
      break;
  }

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

// ---------- Fila inferior: temperatura y actividad (o alerta de caída) ----------
void filaInferior(Adafruit_GFX& g, const VistaPrincipal& v) {
  if (v.alertaCaida) {
    const bool invertido = (v.ms / 500) % 2 == 0;
    if (invertido) g.fillRect(0, kYLineaInferior + 1, 128, 64 - kYLineaInferior - 1, kOn);
    char msg[24];
    snprintf(msg, sizeof msg, "%cCAIDA DETECTADA!", kAbreExclamacion);
    textoCentrado(g, 64, 52, msg, invertido ? kOff : kOn);
    return;
  }

  g.drawFastHLine(0, kYLineaInferior, 128, kOn);
  icono(g, 1, 53, kTermometro);
  switch (v.estadoTemp) {
    case EstadoTemperatura::SinSonda:
      texto(g, 8, 53, "Sin sonda temp.");
      return;
    case EstadoTemperatura::SinContacto:  // al aire: el número sería la temperatura ambiente
      texto(g, 8, 53, "Coloque la sonda");
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
    textoDerecha(g, 127, 53, (v.ms / 500) % 2 ? "midiendo" : "midiendo.");
  else
    textoDerecha(g, 127, 53, EvaluadorTemperatura::nombre(v.rangoTemp));
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
  textoCentrado(g, 64, kYEtiquetas, "Conecte al paciente");
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
  } items[] = {{"SpO2", v.ppg ? "OK" : "NO", 2, 29},           {"Acel", v.acelerometro ? "OK" : "NO", 68, 29},
               {"Temp", v.temperatura ? "OK" : "NO", 2, 38},   {"ECG", !v.ecgModulo ? "NO" : (v.electrodos ? "OK" : "--"), 68, 38},
               {"WiFi", v.wifi ? "OK" : puntos, 2, 47},        {"MQTT", broker, 68, 47}};
  for (const auto& it : items) {
    texto(g, it.x, it.y, it.nombre);
    textoDerecha(g, it.x + 57, it.y, it.estado);
  }
  g.drawFastHLine(0, 55, 128, kOn);
  char pie[22];
  const bool faltaSensor = !v.ppg || !v.acelerometro || !v.temperatura || !v.ecgModulo;
  if (faltaSensor || !v.electrodos)  // no sigue hasta que esté todo (o se pulse BOOT)
    snprintf(pie, sizeof pie, "%s",
             (v.ms / 1500) % 2 ? "BOOT = seguir igual" : (faltaSensor ? "Revise los NO" : "Conecte electrodos"));
  else if (v.broker == 2)
    snprintf(pie, sizeof pie, "Todo conectado");
  else if (v.red[0])
    snprintf(pie, sizeof pie, "%s", v.red);
  else
    snprintf(pie, sizeof pie, "Buscando Wi-Fi%s", puntos);
  textoCentrado(g, 64, 57, pie);
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
    min_[x] = max_[x] = primero_[x] = ultimo_[x] = 0;
  }
  cursor_ = 0;
  enColumna_ = 0;
}

void BarridoEcg::agregar(float v) {
  if (enColumna_ == 0) {
    accMin_ = accMax_ = accPrimero_ = v;
  } else {
    if (v < accMin_) accMin_ = v;
    if (v > accMax_) accMax_ = v;
  }
  accUltimo_ = v;
  if (++enColumna_ < porColumna_) return;
  enColumna_ = 0;
  min_[cursor_] = accMin_;
  max_[cursor_] = accMax_;
  primero_[cursor_] = accPrimero_;
  ultimo_[cursor_] = accUltimo_;
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
  for (int x = 0; x < b.ancho(); x++) {
    if (!b.valida(x)) continue;
    const int arriba = aY(b.maximo(x)), abajo = aY(b.minimo(x));
    g.drawFastVLine(x, arriba, abajo - arriba + 1, kOn);
    if (x > 0 && b.valida(x - 1)) g.drawLine(x - 1, aY(b.ultimo(x - 1)), x, aY(b.primero(x)), kOn);
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
  icono(g, 121, 1, v.pulso ? kCorazonLleno : kCorazonVacio);
  char s[12];
  if (v.electrodos && v.fc > 0)
    snprintf(s, sizeof s, "%d", v.fc);
  else
    strcpy(s, "--");
  numeroGrande(g, &FreeSansBold9pt7b, panel + 15, 23, s);
  textoCentrado(g, panel + 15, 25, "lpm");

  if (!v.electrodos) {
    const int cx = b.ancho() / 2;
    if ((v.ms / 700) % 2) {
      textoCentrado(g, cx, 7, "Electrodos");
      textoCentrado(g, cx, 17, "desconectados");
    }
    g.drawFastHLine(0, 30, b.ancho(), kOn);
    return;
  }
  dibujarTrazo(g, b, 0, 32);
}

void dibujarEcgCompleto(Adafruit_GFX& g, BarridoEcg& b, const VistaPrincipal& p, const VistaEcg& v) {
  g.fillScreen(kOff);
  g.cp437(true);
  g.setTextWrap(false);
  barraEstado(g, p);

  // Encabezado: ECG, corazón que late con cada QRS y la FC calculada de los QRS
  texto(g, 2, kYEtiquetas, "ECG");
  char s[16];
  if (v.electrodos && v.fc > 0)
    snprintf(s, sizeof s, "%d lpm", v.fc);
  else
    strcpy(s, "-- lpm");
  textoDerecha(g, 127, kYEtiquetas, s);
  icono(g, 127 - anchoTexto(s) - 15, 7, v.pulso ? kCorazonGrandeLleno : kCorazonGrandeVacio);

  if (!v.electrodos) {
    if ((v.ms / 700) % 2) {
      textoCentrado(g, 64, 28, "Electrodos");
      textoCentrado(g, 64, 38, "desconectados");
    }
    g.drawFastHLine(0, 60, 128, kOn);
    return;
  }
  dibujarTrazo(g, b, 17, 47);
}
