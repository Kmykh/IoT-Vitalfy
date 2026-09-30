// ppg.h — Oximetría de pulso con el MAX30102: dedo, latidos, FC y SpO2.
//
// Cadena por muestra (IR y rojo, 100 Hz):
//   1. Dedo: el IR debe saltar sobre el nivel ambiente aprendido (con histéresis y antirrebote).
//   2. Filtro pasa-banda 0,5–5 Hz en cada canal (quita la continua y el ruido).
//   3. Latidos: función suma de pendientes (SSF, Zong et al. 2003) con umbral adaptativo.
//   4. FC = 60 / mediana de los últimos 5 intervalos RR (inmune a un latido perdido o extra).
//   5. SpO2 por razón de razones R = (AC_rojo/DC_rojo) / (AC_ir/DC_ir) en cada latido,
//      mediana de los últimos 8 y curva de calibración SpO2 = A·R² + B·R + C.
// C++ puro: se prueba en el computador con señales sintéticas (pruebas/).
#pragma once

#include <stdint.h>

#include "filtros.h"

enum class EstadoPpg : uint8_t {
  SinDedo,        // no hay dedo en el sensor
  Estabilizando,  // dedo recién puesto: se esperan los filtros
  Midiendo,       // buscando los primeros latidos
  Valido,         // FC confiable (y SpO2 cuando hay suficientes latidos)
  SenalDebil,     // hay dedo pero no se detecta pulso
};

struct ParametrosPpg {
  float fs = 100.0f;           // muestras por segundo que entrega el sensor
  uint32_t dedoSalto = 3000;   // el IR debe subir esto sobre el ambiente para detectar el dedo
  uint32_t dedoMinimo = 8000;  // IR mínimo absoluto con dedo
  uint32_t dedoSalida = 5000;  // bajo este IR el dedo se da por retirado
  float spo2A = -45.060f;      // curva de referencia de Maxim (MAXREFDES117); calibrar con un oxímetro
  float spo2B = 30.354f;
  float spo2C = 94.845f;
};

struct ResultadoPpg {
  EstadoPpg estado = EstadoPpg::SinDedo;
  int fc = -1;           // lpm (-1 = sin dato válido)
  int spo2 = -1;         // % (-1 = sin dato válido)
  float perfusion = 0;   // índice de perfusión del último latido, en %
  uint8_t progreso = 0;  // 0..100 mientras busca los primeros latidos
  uint32_t latidos = 0;  // latidos desde que se puso el dedo
};

class ProcesadorPpg {
 public:
  explicit ProcesadorPpg(const ParametrosPpg& p = ParametrosPpg());

  // Procesa una muestra cruda (18 bits). Devuelve true si en ella se detectó un latido.
  bool procesar(uint32_t ir, uint32_t rojo);
  // Avisa que el FIFO del sensor perdió muestras: mantiene el reloj y descarta el intervalo en curso.
  void saltar(uint32_t muestras);

  const ResultadoPpg& resultado() const { return r_; }
  bool hayDedo() const { return dedo_; }
  uint32_t ultimoIr() const { return ultimoIr_; }

 private:
  uint32_t muestras(float segundos) const { return (uint32_t)(segundos * p_.fs + 0.5f); }
  void entrarDedo(uint32_t ir, uint32_t rojo);
  void soltarDedo(uint32_t irActual);
  void registrarLatido();
  void actualizarEstado();

  ParametrosPpg p_;
  ResultadoPpg r_;
  uint32_t n_ = 0;  // muestras procesadas: es el reloj del algoritmo
  uint32_t ultimoIr_ = 0;

  // Detección de dedo
  float ambiente_ = 1000.0f;  // IR sin dedo (arranca bajo para aceptar un dedo ya puesto al encender)
  float ambienteAlEntrar_ = 0;
  bool dedo_ = false;
  uint32_t cuentaEntrada_ = 0, cuentaSalida_ = 0;
  uint32_t muestraDedo_ = 0;

  // Acondicionamiento
  float refIr_ = 0, refRojo_ = 0;  // nivel al poner el dedo (se resta antes de filtrar)
  float dcIr_ = 0, dcRojo_ = 0;    // componente continua (media exponencial de ~1 s)
  Biquad paIr_, pbIr_, paRojo_, pbRojo_;
  float fIr_ = 0, fRojo_ = 0;      // última salida filtrada de cada canal

  // Latidos: función suma de pendientes (SSF)
  static const int kMaxVentana = 32;
  float pendientes_[kMaxVentana] = {};
  int ventana_ = 12, iVentana_ = 0;
  float suma_ = 0, previo_ = 0;
  float picoSsf_ = 0, maxPulso_ = 0;
  bool enPulso_ = false;
  uint32_t muestraUltimoLatido_ = 0;
  bool intervaloValido_ = false;  // el intervalo en curso sirve para RR y SpO2

  // Amplitud del intervalo en curso (para SpO2)
  float maxIr_ = 0, minIr_ = 0, maxRojo_ = 0, minRojo_ = 0;
  bool saturado_ = false;

  Historial<8> rr_;      // intervalos RR, en muestras
  Historial<8> ratios_;  // R de cada latido
};
