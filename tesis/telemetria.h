// telemetria.h — Lectura de signos vitales y su JSON según el contrato del backend
// (Vitalify/Backend/docs/contrato-telemetria.md):
//
//   {"dispositivo":"ESP32-001","ts":"2026-09-30T14:05:10.250Z","seq":18234,
//    "fc":82,"spo2":97,"temp":36.8,"caida":false}
//
// Una variable que el sensor no midió se omite (el backend la trata como ausente).
// Este wearable no mide fr, pas, pad ni bateria, así que nunca los envía.
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stddef.h>
#include <stdint.h>

const int16_t kSinTemperatura = INT16_MIN;

struct Lectura {
  int64_t monoUs = 0;    // reloj monotónico del ESP32 (esp_timer) en el momento de medir
  int64_t epochMs = 0;   // hora UTC de la medición en ms; 0 = aún sin hora NTP
  uint32_t seq = 0;      // contador creciente de lecturas desde el arranque
  int16_t fc = -1;       // lpm; -1 = no medido
  int16_t spo2 = -1;     // %; -1 = no medido
  int16_t tempDecimas = kSinTemperatura;  // °C × 10
  bool caida = false;

  bool tieneDatos() const { return fc >= 0 || spo2 >= 0 || tempDecimas != kSinTemperatura || caida; }
};

// Instante UTC de la medición = ahora − edad de la lectura (medida con el reloj monotónico).
// Así una lectura tomada antes de sincronizar el NTP recibe su hora real, no la del envío.
inline void asignarHora(Lectura& l, int64_t ahoraEpochMs, int64_t ahoraMonoUs) {
  if (l.epochMs == 0) l.epochMs = ahoraEpochMs - (ahoraMonoUs - l.monoUs) / 1000;
}

// Búfer circular de lecturas a la espera de que el broker confirme su recepción.
// Sale primero la más antigua; si se llena, se descarta la más antigua.
template <int N>
class BuferLecturas {
 public:
  // Devuelve false si tuvo que descartar la lectura más antigua para hacer lugar.
  bool agregar(const Lectura& l) {
    bool cupo = true;
    if (cantidad_ == N) {
      quitarFrente();
      perdidas_++;
      cupo = false;
    }
    v_[(inicio_ + cantidad_) % N] = l;
    cantidad_++;
    return cupo;
  }
  void quitarFrente() {
    if (cantidad_ == 0) return;
    inicio_ = (inicio_ + 1) % N;
    cantidad_--;
  }
  Lectura& frente() { return v_[inicio_]; }
  int cantidad() const { return cantidad_; }
  uint32_t perdidas() const { return perdidas_; }
  void asignarHoras(int64_t ahoraEpochMs, int64_t ahoraMonoUs) {
    for (int i = 0; i < cantidad_; i++) asignarHora(v_[(inicio_ + i) % N], ahoraEpochMs, ahoraMonoUs);
  }

 private:
  Lectura v_[N];
  int inicio_ = 0, cantidad_ = 0;
  uint32_t perdidas_ = 0;
};

// "2026-09-30T14:05:10.250Z" (ISO 8601, UTC, milisegundos). Devuelve la longitud o 0 si no cabe.
size_t formatearIso8601(int64_t epochMs, char* buf, size_t tam);

// JSON del contrato. Devuelve la longitud o 0 si no cabe o si la lectura aún no tiene hora.
size_t construirJsonTelemetria(const char* dispositivo, const Lectura& l, char* buf, size_t tam);
