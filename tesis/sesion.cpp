// sesion.cpp — ver sesion.h
#include "sesion.h"

SesionPaciente::SesionPaciente(const ParametrosSesion& p) : p_(p) {}

bool SesionPaciente::actualizar(const SenalesPaciente& s, uint32_t ms) {
  if (estado_ == EstadoSesion::EsperandoPaciente) {
    // Sin oxímetro instalado, la señal de inicio son los electrodos o la sonda en la piel
    const bool inicio = s.sensorPpg ? s.dedo : (s.electrodos || s.sondaEnPiel);
    if (!inicio) {
      contando_ = false;
      return false;
    }
    if (!contando_) {
      contando_ = true;
      desdeInicio_ = ms;
    }
    if (ms - desdeInicio_ < p_.inicioMs) return false;
    estado_ = EstadoSesion::Monitoreando;
    ultimaSenal_ = ms;
    return true;
  }

  if (s.dedo || s.electrodos || s.sondaEnPiel) {
    ultimaSenal_ = ms;
    return false;
  }
  if (ms - ultimaSenal_ < p_.finMs) return false;
  estado_ = EstadoSesion::EsperandoPaciente;
  contando_ = false;
  return true;
}
