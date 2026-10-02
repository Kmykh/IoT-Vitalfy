// sesion.h — Sesión de monitoreo: el wearable solo mide para el backend cuando está en el paciente.
//
//   Esperando paciente ──(dedo en el oxímetro ≥ 1 s)──> Monitoreando
//          ^                                                │
//          └──(60 s sin dedo, sin electrodos y sin sonda en la piel)┘
//
// · Esperando: la pantalla muestra qué falta conectar y no se envía nada (tampoco caídas: mover la
//   placa sin paciente no es una caída).
// · El dedo en el oxímetro es la señal de que el equipo ya está en el paciente. Si el oxímetro no
//   está instalado, inicia con los electrodos o con la sonda en la piel.
// · Monitoreando: cada sensor se usa y se envía desde que se conecta (electrodos o sonda pueden
//   ponerse después). Retirar el dedo un momento no corta la sesión.
// C++ puro: se prueba en el computador (pruebas/).
#pragma once

#include <stdint.h>

enum class EstadoSesion : uint8_t { EsperandoPaciente, Monitoreando };

struct SenalesPaciente {
  bool sensorPpg = true;      // el MAX30102 está instalado
  bool dedo = false;          // hay dedo en el oxímetro
  bool electrodos = false;    // electrodos del ECG puestos
  bool sondaEnPiel = false;   // sonda de temperatura sobre la piel
};

struct ParametrosSesion {
  uint32_t inicioMs = 1000;   // el dedo debe sostenerse este tiempo para iniciar
  uint32_t finMs = 60000;     // sin ninguna señal del paciente este tiempo, la sesión termina
};

class SesionPaciente {
 public:
  explicit SesionPaciente(const ParametrosSesion& p = ParametrosSesion());

  // Devuelve true si en esta llamada la sesión empezó o terminó.
  bool actualizar(const SenalesPaciente& s, uint32_t ms);

  EstadoSesion estado() const { return estado_; }
  bool monitoreando() const { return estado_ == EstadoSesion::Monitoreando; }

 private:
  ParametrosSesion p_;
  EstadoSesion estado_ = EstadoSesion::EsperandoPaciente;
  uint32_t desdeInicio_ = 0, ultimaSenal_ = 0;
  bool contando_ = false;
};
