// caidas.cpp — ver caidas.h
#include "caidas.h"

#include <math.h>

DetectorCaidas::DetectorCaidas(const ParametrosCaidas& p) : p_(p) {}

Actividad DetectorCaidas::actividad() const {
  return sqrtf(varMag_) > p_.umbralMovimiento ? Actividad::Movimiento : Actividad::Reposo;
}

bool DetectorCaidas::procesar(float ax, float ay, float az) {
  n_++;
  const float mag = sqrtf(ax * ax + ay * ay + az * az);
  magnitud_ = mag;

  // Gravedad y variabilidad con medias exponenciales de ~1 s
  const float alfa = 1.0f / p_.fs;
  if (!iniciado_) {
    iniciado_ = true;
    gx_ = ax;
    gy_ = ay;
    gz_ = az;
    mediaMag_ = mag;
  }
  gx_ += (ax - gx_) * alfa;
  gy_ += (ay - gy_) * alfa;
  gz_ += (az - gz_) * alfa;
  mediaMag_ += (mag - mediaMag_) * alfa;
  varMag_ += ((mag - mediaMag_) * (mag - mediaMag_) - varMag_) * alfa;

  const auto recordarPostura = [&]() {
    antesX_ = gx_;
    antesY_ = gy_;
    antesZ_ = gz_;
  };
  const auto impacto = [&]() {
    fase_ = FaseCaida::Impacto;
    muestraImpacto_ = n_;
  };

  // Un impacto fuerte (re)inicia la evaluación desde cualquier fase
  if (mag > p_.umbralImpactoFuerte && fase_ != FaseCaida::Ingravidez) {
    if (fase_ == FaseCaida::Normal) recordarPostura();
    impacto();
    return false;
  }

  switch (fase_) {
    case FaseCaida::Normal:
      if (mag < p_.umbralIngravidez) {
        if (cuentaIngravidez_ == 0) recordarPostura();  // postura justo antes de caer
        if (++cuentaIngravidez_ >= muestras(p_.tiempoIngravidez)) {
          fase_ = FaseCaida::Ingravidez;
          inicioEvento_ = n_ - cuentaIngravidez_;
        }
      } else {
        cuentaIngravidez_ = 0;
      }
      break;

    case FaseCaida::Ingravidez:
      if (mag > p_.umbralImpacto) {
        impacto();
      } else if (n_ - inicioEvento_ > muestras(p_.ventanaImpacto)) {
        fase_ = FaseCaida::Normal;  // no hubo impacto: fue un salto o un movimiento brusco
        cuentaIngravidez_ = 0;
      }
      break;

    case FaseCaida::Impacto:
      if (n_ - muestraImpacto_ >= muestras(p_.esperaTrasImpacto)) {
        fase_ = FaseCaida::Evaluando;
        sumaX_ = sumaY_ = sumaZ_ = 0;
        nQuietud_ = 0;
      }
      break;

    case FaseCaida::Evaluando:
      if (fabsf(mag - 1.0f) > p_.quietudMaxDesvio) {
        fase_ = FaseCaida::Normal;  // se movió: no quedó tendido
        cuentaIngravidez_ = 0;
        break;
      }
      sumaX_ += ax;
      sumaY_ += ay;
      sumaZ_ += az;
      if (++nQuietud_ >= muestras(p_.ventanaQuietud)) {
        fase_ = FaseCaida::Normal;
        cuentaIngravidez_ = 0;
        const float k = 1.0f / (float)nQuietud_;
        const float dx = sumaX_ * k - antesX_, dy = sumaY_ * k - antesY_, dz = sumaZ_ * k - antesZ_;
        if (sqrtf(dx * dx + dy * dy + dz * dz) > p_.cambioPostura) {
          caidas_++;
          return true;
        }
      }
      break;
  }
  return false;
}
