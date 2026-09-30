// ppg.cpp — ver ppg.h
#include "ppg.h"

namespace {
const uint32_t kSaturacion = 260000;   // el ADC es de 18 bits (262143): por encima, la onda está recortada
const float kUmbral = 0.5f;            // fracción del pico típico de la SSF que marca un latido
const float kRrMin = 0.27f;            // s (222 lpm)
const float kRrMax = 2.4f;             // s (25 lpm)
const float kVigencia = 3.0f;          // s sin latidos para dar la FC por perdida
const float kSinPulsoLibera = 8.0f;    // s con "dedo" y sin pulso: se libera (luz ambiente, dedo sin contacto)
const float kEstabilizacion = 1.0f;    // s tras poner el dedo
const float kAprendizaje = 1.5f;       // s para medir la amplitud típica de la SSF
}  // namespace

ProcesadorPpg::ProcesadorPpg(const ParametrosPpg& p) : p_(p) {
  ventana_ = (int)muestras(0.12f);  // la subida sistólica dura ~100–150 ms
  if (ventana_ < 2) ventana_ = 2;
  if (ventana_ > kMaxVentana) ventana_ = kMaxVentana;
  paIr_ = paRojo_ = Biquad::pasaAltos(p_.fs, 0.5f);
  pbIr_ = pbRojo_ = Biquad::pasaBajos(p_.fs, 5.0f);
}

bool ProcesadorPpg::procesar(uint32_t ir, uint32_t rojo) {
  n_++;
  ultimoIr_ = ir;

  // 1. Dedo: entra con un salto sobre el ambiente, sostenido 100 ms
  if (!dedo_) {
    const bool candidato = ir >= p_.dedoMinimo && ir >= ambiente_ + p_.dedoSalto;
    if (candidato) {
      if (++cuentaEntrada_ >= muestras(0.10f)) entrarDedo(ir, rojo);
    } else {
      cuentaEntrada_ = 0;
      ambiente_ += ((float)ir - ambiente_) / p_.fs;  // aprende el ambiente solo sin dedo (~1 s)
    }
    return false;
  }

  // Sale rápido si el IR cae (60 ms seguidos) al nivel que había antes de poner el dedo
  const bool fuera = ir < p_.dedoSalida || (float)ir < ambienteAlEntrar_ + p_.dedoSalto / 2.0f;
  if (fuera) {
    if (++cuentaSalida_ >= muestras(0.06f)) soltarDedo(ir);
    return false;
  }
  cuentaSalida_ = 0;

  // Sale lento si hay "dedo" pero ningún latido en 8 s
  const uint32_t referencia = r_.latidos > 0 ? muestraUltimoLatido_ : muestraDedo_;
  if (n_ - referencia > muestras(kSinPulsoLibera)) {
    soltarDedo(ir);
    return false;
  }

  // 2. Acondicionamiento
  const float alfaDc = 1.0f / p_.fs;
  dcIr_ += ((float)ir - dcIr_) * alfaDc;
  dcRojo_ += ((float)rojo - dcRojo_) * alfaDc;
  fIr_ = pbIr_.procesar(paIr_.procesar((float)ir - refIr_));
  fRojo_ = pbRojo_.procesar(paRojo_.procesar((float)rojo - refRojo_));
  if (ir >= kSaturacion || rojo >= kSaturacion) saturado_ = true;
  if (fIr_ > maxIr_) maxIr_ = fIr_;
  if (fIr_ < minIr_) minIr_ = fIr_;
  if (fRojo_ > maxRojo_) maxRojo_ = fRojo_;
  if (fRojo_ < minRojo_) minRojo_ = fRojo_;

  // 3. SSF sobre el pulso invertido (la luz recibida baja cuando entra sangre en la sístole)
  const float x = -fIr_;
  const float d = x - previo_;
  previo_ = x;
  const float dp = d > 0 ? d : 0;
  suma_ += dp - pendientes_[iVentana_];
  pendientes_[iVentana_] = dp;
  if (++iVentana_ == ventana_) {
    iVentana_ = 0;
    suma_ = 0;  // recalcula la suma exacta para que no acumule error de redondeo
    for (int i = 0; i < ventana_; i++) suma_ += pendientes_[i];
  }
  const float ssf = suma_;

  const uint32_t desdeDedo = n_ - muestraDedo_;
  if (desdeDedo < muestras(kEstabilizacion)) {
    r_.estado = EstadoPpg::Estabilizando;
    return false;
  }
  if (desdeDedo < muestras(kEstabilizacion + kAprendizaje)) {
    if (ssf > picoSsf_) picoSsf_ = ssf;
    r_.estado = EstadoPpg::Midiendo;
    return false;
  }

  // Umbral adaptativo con periodo refractario proporcional al ritmo
  bool latido = false;
  const float umbral = kUmbral * picoSsf_;
  const uint32_t desdeLatido = n_ - muestraUltimoLatido_;
  const float rrTipico = rr_.cantidad ? rr_.medianaUltimos(5) : (float)muestras(1.0f);
  uint32_t refractario = muestras(kRrMin);
  if ((uint32_t)(0.4f * rrTipico) > refractario) refractario = (uint32_t)(0.4f * rrTipico);

  if (!enPulso_) {
    if (ssf > umbral && ssf > 0 && (r_.latidos == 0 || desdeLatido >= refractario)) {
      enPulso_ = true;
      maxPulso_ = ssf;
      registrarLatido();
      latido = true;
    } else if (desdeLatido > 1.5f * rrTipico) {
      picoSsf_ *= 0.995f;  // sin latidos: baja el umbral de a poco (la amplitud pudo caer)
    }
  } else {
    if (ssf > maxPulso_) maxPulso_ = ssf;
    if (ssf < 0.5f * umbral) {
      enPulso_ = false;
      picoSsf_ = 0.8f * picoSsf_ + 0.2f * maxPulso_;
    }
  }

  actualizarEstado();
  return latido;
}

void ProcesadorPpg::saltar(uint32_t m) {
  n_ += m;
  intervaloValido_ = false;
}

void ProcesadorPpg::entrarDedo(uint32_t ir, uint32_t rojo) {
  dedo_ = true;
  cuentaEntrada_ = cuentaSalida_ = 0;
  muestraDedo_ = n_;
  ambienteAlEntrar_ = ambiente_;
  refIr_ = dcIr_ = (float)ir;
  refRojo_ = dcRojo_ = (float)rojo;
  paIr_.asentar(0);
  pbIr_.asentar(0);
  paRojo_.asentar(0);
  pbRojo_.asentar(0);
  fIr_ = fRojo_ = 0;
  for (int i = 0; i < kMaxVentana; i++) pendientes_[i] = 0;
  iVentana_ = 0;
  suma_ = previo_ = 0;
  picoSsf_ = maxPulso_ = 0;
  enPulso_ = false;
  muestraUltimoLatido_ = n_;
  intervaloValido_ = false;
  maxIr_ = minIr_ = maxRojo_ = minRojo_ = 0;
  saturado_ = false;
  rr_.limpiar();
  ratios_.limpiar();
  r_ = ResultadoPpg();
  r_.estado = EstadoPpg::Estabilizando;
}

void ProcesadorPpg::soltarDedo(uint32_t irActual) {
  dedo_ = false;
  cuentaEntrada_ = cuentaSalida_ = 0;
  // El nivel actual pasa a ser el ambiente: para volver a entrar hace falta un salto real
  // (si no, la luz del teclado o un dedo sin pulso reentrarían en bucle).
  ambiente_ = (float)irActual;
  r_ = ResultadoPpg();
}

void ProcesadorPpg::registrarLatido() {
  if (r_.latidos > 0 && intervaloValido_) {
    const uint32_t rr = n_ - muestraUltimoLatido_;
    if (rr >= muestras(kRrMin) && rr <= muestras(kRrMax)) {
      rr_.agregar((float)rr);
      const float acIr = maxIr_ - minIr_, acRojo = maxRojo_ - minRojo_;
      if (!saturado_ && acIr > 0 && acRojo > 0 && dcIr_ > 0 && dcRojo_ > 0) {
        const float piIr = acIr / dcIr_;
        r_.perfusion = 100.0f * piIr;
        if (piIr >= 0.0005f && piIr <= 0.2f) {  // 0,05 % a 20 %: fuera de eso no es pulso
          const float R = (acRojo / dcRojo_) / piIr;
          if (R >= 0.2f && R <= 2.0f) ratios_.agregar(R);
        }
      }
    }
  }
  muestraUltimoLatido_ = n_;
  r_.latidos++;
  intervaloValido_ = true;
  maxIr_ = minIr_ = fIr_;
  maxRojo_ = minRojo_ = fRojo_;
  saturado_ = false;

  if (rr_.cantidad >= 3) r_.fc = (int)(60.0f * p_.fs / rr_.mediaRobusta(6, 0.15f) + 0.5f);
  if (ratios_.cantidad >= 3) {
    float R = ratios_.medianaUltimos(8);
    const float vertice = -p_.spo2B / (2.0f * p_.spo2A);  // la parábola es válida a la derecha del máximo
    if (p_.spo2A < 0 && R < vertice) R = vertice;
    float s = p_.spo2A * R * R + p_.spo2B * R + p_.spo2C;
    if (s > 100) s = 100;
    if (s < 0) s = 0;
    r_.spo2 = (int)(s + 0.5f);
  }
}

void ProcesadorPpg::actualizarEstado() {
  const bool vigente = r_.latidos > 0 && (n_ - muestraUltimoLatido_) <= muestras(kVigencia);
  if (r_.latidos > 0 && !vigente) {
    r_.estado = EstadoPpg::SenalDebil;
    r_.fc = r_.spo2 = -1;
    return;
  }
  if (r_.latidos == 0) {
    const bool esperaLarga = (n_ - muestraDedo_) > muestras(kEstabilizacion + kAprendizaje + kVigencia);
    r_.estado = esperaLarga ? EstadoPpg::SenalDebil : EstadoPpg::Midiendo;
  } else {
    r_.estado = r_.fc > 0 ? EstadoPpg::Valido : EstadoPpg::Midiendo;
  }
  const uint32_t p = r_.latidos * 25;  // 4 latidos para la primera FC
  r_.progreso = (uint8_t)(p > 100 ? 100 : p);
}
