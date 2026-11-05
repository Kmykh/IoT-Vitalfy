// vinculacion.h — Comandos del backend al wearable y estado de la vinculación por QR
// (Vitalify/Backend/docs/contrato-telemetria.md, "Comandos al wearable", y docs/vinculacion-qr.md).
//
// El backend publica en device/<codigo>/comandos (QoS 1; los marcados con R van retenidos):
//   {"accion":"mostrar-qr","qr":"VTF1:ESP32-001:K7P3QX9M","expira":"2026-11-02T15:10:00Z"}   R
//   {"accion":"emparejando"}
//   {"accion":"vinculado","cama":"MED-B-01","iniciales":"R.M."}                              R
//   {"accion":"desvinculado"}                                                                R
//   {"accion":"repetir-medicion","variable":"presion","motivo":"..."}
//
//   Desconocido ──(primer comando)──> EsperandoQr ──emparejando──> Emparejando ──vinculado──> Vinculado
//   (al arrancar)                          ^                                                       │
//                                          └───────── mostrar-qr / desvinculado ───────────────────┘
//
// · Solo en Vinculado se publica telemetría. Al salir de Vinculado (o al saber que no lo estaba) se
//   descarta el búfer offline: esas lecturas eran del paciente anterior.
// · Tras reconectarse, no se publica hasta recibir el comando retenido de esa conexión (el estado pudo
//   cambiar mientras el wearable estaba fuera de línea).
// · Desconocido (recién encendido, sin broker): se guardan las lecturas por si sigue vinculado.
// · Se pide un QR ({"accion":"solicitar-qr"} en device/<codigo>/solicitudes) si al conectarse no llega
//   ningún comando en 5 s, si el QR venció o si se pulsa el botón; como máximo uno cada 10 s.
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class AccionComando : uint8_t { Desconocida, MostrarQr, Emparejando, Vinculado, Desvinculado, RepetirMedicion };

struct Comando {
  AccionComando accion = AccionComando::Desconocida;
  char qr[72] = "";        // "VTF1:<codigo de hasta 32>:<8 caracteres>"
  int64_t expiraMs = 0;    // UTC en ms; 0 = sin dato
  char cama[24] = "";
  char iniciales[12] = "";
  char variable[16] = "";
  char motivo[48] = "";    // en ASCII (sin tildes): la fuente de la OLED no las tiene
};

// Interpreta el JSON de un comando. false si no es un objeto JSON válido o no trae "accion"; una acción
// que el firmware no conoce da true con AccionComando::Desconocida (se ignora).
bool interpretarComando(const char* json, size_t largo, Comando* c);

// Valor de texto de una clave del primer nivel de un objeto JSON, ya sin escapes (UTF-8).
// Devuelve false si la clave no está, no es un texto o no cabe.
bool valorTextoJson(const char* json, size_t largo, const char* clave, char* salida, size_t tam);

// "2026-11-02T15:10:00Z" o "2026-11-02T15:10:00.250Z" -> ms desde 1970 (UTC); 0 si no es válida.
int64_t interpretarIso8601(const char* texto);

// Copia el texto UTF-8 en ASCII: quita las tildes (á -> a, ñ -> n) y cambia lo demás por '?'.
void aAscii(const char* utf8, char* salida, size_t tam);

extern const char kSolicitudQr[];  // {"accion":"solicitar-qr"}

// QR de la vinculación (generador de Nayuki, qrcodegen.c). La versión más chica en la que entra el texto,
// hasta la 3 (29x29 módulos: a 2 px por módulo son 58 px, lo que cabe en los 64 px de alto de la OLED),
// con corrección de errores M o mayor si cabe en la misma versión. "VTF1:ESP32-001:K7P3QX9M" usa la
// versión 2 (25x25): 50 px y 7 px de margen claro arriba y abajo.
const int kQrVersionMaxima = 3;
const int kQrTamBuffer = ((kQrVersionMaxima * 4 + 17) * (kQrVersionMaxima * 4 + 17) + 7) / 8 + 1;
// Devuelve el lado en módulos (21, 25 o 29), o 0 si el texto no entra.
int generarQr(const char* texto, uint8_t qr[kQrTamBuffer]);
bool moduloQr(const uint8_t qr[kQrTamBuffer], int x, int y);  // true = módulo oscuro

enum class EstadoVinculo : uint8_t { Desconocido, EsperandoQr, Emparejando, Vinculado };

struct EfectoComando {
  bool cambioEstado = false;
  bool descartarBufer = false;  // las lecturas guardadas eran de antes: no se envían
};

class Vinculacion {
 public:
  static const uint32_t kEsperaPrimerComandoMs = 5000;
  static const uint32_t kEntreSolicitudesMs = 10000;

  void alConectar(uint32_t ms);
  void alDesconectar();
  EfectoComando aplicar(const Comando& c, uint32_t ms);

  // Publicar telemetría: vinculado y con el comando retenido de esta conexión ya recibido.
  bool puedePublicar() const { return estado_ == EstadoVinculo::Vinculado && comandoEnEstaConexion_; }
  // Guardar lecturas nuevas: vinculado, o todavía sin saberlo (recién encendido).
  bool aceptaLecturas() const { return estado_ == EstadoVinculo::Vinculado || estado_ == EstadoVinculo::Desconocido; }
  // ¿Hay que publicar solicitar-qr ahora? ahoraEpochMs = 0 si todavía no hay hora NTP.
  bool debeSolicitarQr(uint32_t ms, int64_t ahoraEpochMs, bool botonPulsado);
  bool qrVencido(int64_t ahoraEpochMs) const { return ahoraEpochMs > 0 && expiraMs_ > 0 && ahoraEpochMs >= expiraMs_; }

  EstadoVinculo estado() const { return estado_; }
  const char* qr() const { return qr_; }
  int64_t expiraMs() const { return expiraMs_; }
  const char* cama() const { return cama_; }
  const char* iniciales() const { return iniciales_; }
  const char* aviso() const { return aviso_; }  // del último repetir-medicion
  uint32_t avisoMs() const { return avisoMs_; }
  uint32_t estadoDesdeMs() const { return desdeMs_; }

 private:
  void cambiar(EstadoVinculo nuevo, uint32_t ms, EfectoComando* efecto);

  EstadoVinculo estado_ = EstadoVinculo::Desconocido;
  bool conectado_ = false, comandoEnEstaConexion_ = false, solicitoAlguna_ = false;
  uint32_t conectadoMs_ = 0, ultimaSolicitudMs_ = 0, desdeMs_ = 0, avisoMs_ = 0;
  char qr_[72] = "";
  int64_t expiraMs_ = 0;
  char cama_[24] = "", iniciales_[12] = "", aviso_[64] = "";
};
