// vinculacion.cpp — ver vinculacion.h
#include "vinculacion.h"

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"

const char kSolicitudQr[] = "{\"accion\":\"solicitar-qr\"}";

namespace {

// Lector mínimo de JSON: solo lo que necesitan los comandos (un objeto plano; los valores anidados se
// saltan). No reserva memoria.
struct Lector {
  const char* p;
  const char* fin;

  void espacios() {
    while (p < fin && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  }
  bool es(char c) {
    espacios();
    if (p < fin && *p == c) {
      p++;
      return true;
    }
    return false;
  }

  static void agregarUtf8(uint32_t cp, char* salida, size_t tam, size_t& n, bool& cabe) {
    char b[4];
    size_t k;
    if (cp < 0x80) {
      b[0] = (char)cp;
      k = 1;
    } else if (cp < 0x800) {
      b[0] = (char)(0xC0 | (cp >> 6));
      b[1] = (char)(0x80 | (cp & 0x3F));
      k = 2;
    } else {
      b[0] = (char)(0xE0 | (cp >> 12));
      b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
      b[2] = (char)(0x80 | (cp & 0x3F));
      k = 3;
    }
    for (size_t i = 0; i < k; i++) {
      if (salida && n + 1 < tam)
        salida[n++] = b[i];
      else
        cabe = false;
    }
  }

  // Lee un texto JSON (p apunta a la comilla). Si salida no es nulo, lo decodifica ahí.
  bool texto(char* salida, size_t tam, bool* cabe) {
    espacios();
    if (p >= fin || *p != '"') return false;
    p++;
    size_t n = 0;
    bool ok = true;
    while (p < fin && *p != '"') {
      char c = *p++;
      if ((unsigned char)c < 0x20) return false;
      if (c == '\\') {
        if (p >= fin) return false;
        const char e = *p++;
        switch (e) {
          case '"': case '\\': case '/': c = e; break;
          case 'b': c = '\b'; break;
          case 'f': c = '\f'; break;
          case 'n': c = '\n'; break;
          case 'r': c = '\r'; break;
          case 't': c = '\t'; break;
          case 'u': {
            if (fin - p < 4) return false;
            uint32_t cp = 0;
            for (int i = 0; i < 4; i++) {
              const char h = *p++;
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (uint32_t)(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (uint32_t)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (uint32_t)(h - 'A' + 10);
              else return false;
            }
            agregarUtf8(cp, salida, tam, n, ok);
            continue;
          }
          default:
            return false;
        }
      }
      if (salida && n + 1 < tam)
        salida[n++] = c;
      else if (salida)
        ok = false;
    }
    if (p >= fin) return false;
    p++;  // comilla de cierre
    if (salida && tam) salida[n < tam ? n : tam - 1] = 0;
    if (cabe) *cabe = ok;
    return true;
  }

  // Salta un valor cualquiera (texto, número, literal, objeto o arreglo).
  bool saltarValor() {
    espacios();
    if (p >= fin) return false;
    if (*p == '"') return texto(nullptr, 0, nullptr);
    if (*p == '{' || *p == '[') {
      int nivel = 0;
      while (p < fin) {
        if (*p == '"') {
          if (!texto(nullptr, 0, nullptr)) return false;
          continue;
        }
        if (*p == '{' || *p == '[') nivel++;
        if (*p == '}' || *p == ']') nivel--;
        p++;
        if (nivel == 0) return true;
      }
      return false;
    }
    const char* inicio = p;
    while (p < fin && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p > inicio;
  }
};

}  // namespace

bool valorTextoJson(const char* json, size_t largo, const char* clave, char* salida, size_t tam) {
  if (!json || !salida || tam == 0) return false;
  salida[0] = 0;
  Lector l{json, json + largo};
  if (!l.es('{')) return false;
  if (l.es('}')) return false;
  for (;;) {
    char nombre[24];
    bool cabe = true;
    if (!l.texto(nombre, sizeof nombre, &cabe) || !l.es(':')) return false;
    l.espacios();
    if (cabe && strcmp(nombre, clave) == 0) {
      if (l.p >= l.fin || *l.p != '"') return false;  // la clave está pero no es un texto
      bool cupo = true;
      return l.texto(salida, tam, &cupo) && cupo;
    }
    if (!l.saltarValor()) return false;
    if (l.es(',')) continue;
    return false;  // fin del objeto (o JSON inválido) sin encontrar la clave
  }
}

int64_t interpretarIso8601(const char* t) {
  if (!t) return 0;
  int anio, mes, dia, hora, minuto, segundo, consumidos = 0;
  if (sscanf(t, "%4d-%2d-%2dT%2d:%2d:%2d%n", &anio, &mes, &dia, &hora, &minuto, &segundo, &consumidos) != 6) return 0;
  const char* resto = t + consumidos;
  int ms = 0;
  if (*resto == '.') {
    resto++;
    int digitos = 0;
    while (*resto >= '0' && *resto <= '9') {
      if (digitos < 3) ms = ms * 10 + (*resto - '0');
      digitos++;
      resto++;
    }
    if (digitos == 0) return 0;
    for (; digitos < 3; digitos++) ms *= 10;
  }
  if (*resto != 'Z' || resto[1] != 0) return 0;
  if (mes < 1 || mes > 12 || dia < 1 || dia > 31 || hora > 23 || minuto > 59 || segundo > 60) return 0;
  // Días desde 1970-01-01 (algoritmo days_from_civil de H. Hinnant): sin depender de timegm()
  const int y = anio - (mes <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (mes + (mes > 2 ? -3 : 9)) + 2) / 5 + dia - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t dias = (int64_t)era * 146097 + doe - 719468;
  return ((dias * 24 + hora) * 60 + minuto) * 60000LL + segundo * 1000LL + ms;
}

void aAscii(const char* s, char* salida, size_t tam) {
  if (!salida || tam == 0) return;
  size_t n = 0;
  while (s && *s && n + 1 < tam) {
    const unsigned char c = (unsigned char)*s;
    if (c < 0x80) {
      salida[n++] = (char)c;
      s++;
      continue;
    }
    if (c == 0xC3 && s[1]) {
      static const char kLatin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
      const unsigned char d = (unsigned char)s[1];
      salida[n++] = (d >= 0x80 && d <= 0xBF) ? kLatin[d - 0x80] : '?';
      s += 2;
      continue;
    }
    // Otro carácter de varios bytes: uno solo '?'
    s++;
    while ((unsigned char)*s >= 0x80 && (unsigned char)*s < 0xC0) s++;
    salida[n++] = '?';
  }
  salida[n] = 0;
}

bool interpretarComando(const char* json, size_t largo, Comando* c) {
  *c = Comando();
  char accion[24];
  if (!valorTextoJson(json, largo, "accion", accion, sizeof accion)) return false;
  if (strcmp(accion, "mostrar-qr") == 0) {
    char expira[32] = "";
    if (!valorTextoJson(json, largo, "qr", c->qr, sizeof c->qr) || !c->qr[0]) return false;
    if (valorTextoJson(json, largo, "expira", expira, sizeof expira)) c->expiraMs = interpretarIso8601(expira);
    c->accion = AccionComando::MostrarQr;
  } else if (strcmp(accion, "emparejando") == 0) {
    c->accion = AccionComando::Emparejando;
  } else if (strcmp(accion, "vinculado") == 0) {
    char texto[48];
    if (valorTextoJson(json, largo, "cama", texto, sizeof texto)) aAscii(texto, c->cama, sizeof c->cama);
    if (valorTextoJson(json, largo, "iniciales", texto, sizeof texto)) aAscii(texto, c->iniciales, sizeof c->iniciales);
    c->accion = AccionComando::Vinculado;
  } else if (strcmp(accion, "desvinculado") == 0) {
    c->accion = AccionComando::Desvinculado;
  } else if (strcmp(accion, "repetir-medicion") == 0) {
    char texto[96];
    if (valorTextoJson(json, largo, "variable", texto, sizeof texto)) aAscii(texto, c->variable, sizeof c->variable);
    if (valorTextoJson(json, largo, "motivo", texto, sizeof texto)) aAscii(texto, c->motivo, sizeof c->motivo);
    c->accion = AccionComando::RepetirMedicion;
  }
  return true;  // otra acción: Desconocida, se ignora
}

void Vinculacion::alConectar(uint32_t ms) {
  conectado_ = true;
  comandoEnEstaConexion_ = false;
  conectadoMs_ = ms;
}

void Vinculacion::alDesconectar() {
  conectado_ = false;
  comandoEnEstaConexion_ = false;
}

void Vinculacion::cambiar(EstadoVinculo nuevo, uint32_t ms, EfectoComando* efecto) {
  if (nuevo != EstadoVinculo::Vinculado) efecto->descartarBufer = true;
  if (nuevo == estado_) return;
  estado_ = nuevo;
  desdeMs_ = ms;
  efecto->cambioEstado = true;
}

EfectoComando Vinculacion::aplicar(const Comando& c, uint32_t ms) {
  EfectoComando efecto;
  if (c.accion == AccionComando::Desconocida) return efecto;
  comandoEnEstaConexion_ = true;
  switch (c.accion) {
    case AccionComando::MostrarQr:
      strncpy(qr_, c.qr, sizeof qr_ - 1);
      qr_[sizeof qr_ - 1] = 0;
      expiraMs_ = c.expiraMs;
      cambiar(EstadoVinculo::EsperandoQr, ms, &efecto);
      efecto.cambioEstado = true;  // otro QR: hay que redibujar aunque el estado sea el mismo
      break;
    case AccionComando::Desvinculado:
      qr_[0] = 0;  // el QR nuevo llega enseguida (o se pide)
      expiraMs_ = 0;
      cama_[0] = iniciales_[0] = 0;
      cambiar(EstadoVinculo::EsperandoQr, ms, &efecto);
      break;
    case AccionComando::Emparejando:
      cambiar(EstadoVinculo::Emparejando, ms, &efecto);
      break;
    case AccionComando::Vinculado:
      strncpy(cama_, c.cama, sizeof cama_ - 1);
      cama_[sizeof cama_ - 1] = 0;
      strncpy(iniciales_, c.iniciales, sizeof iniciales_ - 1);
      iniciales_[sizeof iniciales_ - 1] = 0;
      qr_[0] = 0;
      expiraMs_ = 0;
      cambiar(EstadoVinculo::Vinculado, ms, &efecto);
      break;
    case AccionComando::RepetirMedicion:
      snprintf(aviso_, sizeof aviso_, "Repita: %s", c.variable[0] ? c.variable : "medicion");
      avisoMs_ = ms ? ms : 1;
      break;
    default:
      break;
  }
  return efecto;
}

bool Vinculacion::debeSolicitarQr(uint32_t ms, int64_t ahoraEpochMs, bool botonPulsado) {
  if (!conectado_) return false;
  if (solicitoAlguna_ && ms - ultimaSolicitudMs_ < kEntreSolicitudesMs) return false;
  bool pedir = false;
  if (!comandoEnEstaConexion_) {
    pedir = ms - conectadoMs_ >= kEsperaPrimerComandoMs;  // el broker no tenía nada retenido
  } else if (estado_ == EstadoVinculo::EsperandoQr) {
    pedir = botonPulsado || qrVencido(ahoraEpochMs) || (!qr_[0] && ms - desdeMs_ >= kEsperaPrimerComandoMs);
  }
  if (pedir) {
    solicitoAlguna_ = true;
    ultimaSolicitudMs_ = ms;
  }
  return pedir;
}

int generarQr(const char* texto, uint8_t qr[kQrTamBuffer]) {
  uint8_t temporal[kQrTamBuffer];
  if (!texto || !qrcodegen_encodeText(texto, temporal, qr, qrcodegen_Ecc_MEDIUM, 1, kQrVersionMaxima, qrcodegen_Mask_AUTO,
                                      true))
    return 0;
  return qrcodegen_getSize(qr);
}

bool moduloQr(const uint8_t qr[kQrTamBuffer], int x, int y) { return qrcodegen_getModule(qr, x, y); }
