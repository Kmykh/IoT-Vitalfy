// vista_previa.cpp — Dibuja cada pantalla de las OLED en memoria y la guarda como imagen
// (salida/*.pbm; vista_previa.py arma un PNG con todas). Sirve para revisar el diseño sin
// hardware y para las figuras del documento de tesis.
#include <Adafruit_GFX.h>
#include <stdio.h>

#include "ecg.h"
#include "pantallas.h"
#include "senales.h"

static void guardar(GFXcanvas1& c, const char* nombre) {
  char ruta[128];
  snprintf(ruta, sizeof ruta, "salida/%s.pbm", nombre);
  FILE* f = fopen(ruta, "wb");
  if (!f) return;
  fprintf(f, "P4\n%d %d\n", c.width(), c.height());
  fwrite(c.getBuffer(), 1, (size_t)((c.width() + 7) / 8) * c.height(), f);
  fclose(f);
}

static VistaPrincipal base() {
  VistaPrincipal v;
  v.wifiBarras = 3;
  v.broker = 2;
  v.horaValida = true;
  v.hora = 14;
  v.minuto = 5;
  v.estadoTemp = EstadoTemperatura::Estable;
  v.temp = 36.8f;
  v.rangoTemp = RangoTemperatura::Normal;
  v.acelerometro = true;
  v.codigo = "ESP32-001";
  v.red = "MiRedCasa";
  v.sesionActiva = true;
  v.ms = 1000;
  return v;
}

int main() {
  GFXcanvas1 principal(128, 64), ecg(128, 32);

  VistaArranque a;
  a.version = "2.0.0";
  a.codigo = "ESP32-001";
  a.ppg = a.acelerometro = a.temperatura = a.ecgModulo = true;  // faltan los electrodos: no sigue
  a.ms = 400;
  dibujarArranque(principal, a);
  guardar(principal, "01a_arranque_conectando");
  a.electrodos = a.wifi = true;
  a.broker = 2;
  a.red = "MiRedCasa";
  dibujarArranque(principal, a);
  guardar(principal, "01b_arranque_todo_conectado");

  // Sin paciente: qué falta conectar
  VistaPrincipal v = base();
  v.sesionActiva = false;
  v.estadoTemp = EstadoTemperatura::SinContacto;
  v.ms = 600;
  dibujarPrincipal(principal, v);
  guardar(principal, "01c_conecte_paciente");
  v.ppg = EstadoPpg::Estabilizando;
  v.electrodos = true;
  v.estadoTemp = EstadoTemperatura::Estabilizando;
  dibujarPrincipal(principal, v);
  guardar(principal, "01d_conecte_paciente_iniciando");

  v = base();
  v.temp = 36.4f;
  v.red = "Hotspot del celular";  // SSID largo: se recorta detrás del código
  dibujarPrincipal(principal, v);
  guardar(principal, "02_espera");

  v = base();
  v.wifiBarras = -1;
  v.broker = 0;
  v.horaValida = false;
  v.ppg = EstadoPpg::Midiendo;
  v.progreso = 50;
  v.pulso = true;
  dibujarPrincipal(principal, v);
  guardar(principal, "03_midiendo");

  v = base();
  v.ppg = EstadoPpg::Valido;
  v.fc = 72;
  v.fcDelEcg = true;
  v.spo2 = 98;
  v.pulso = true;
  v.pendientes = 12;
  dibujarPrincipal(principal, v);
  guardar(principal, "04_signos");

  v = base();
  v.wifiBarras = 1;
  v.broker = 1;
  v.ms = 1500;
  v.ppg = EstadoPpg::Valido;
  v.fc = 128;
  v.spo2 = 100;
  v.movimiento = true;
  v.temp = 38.6f;
  v.rangoTemp = RangoTemperatura::Fiebre;
  dibujarPrincipal(principal, v);
  guardar(principal, "05_tres_digitos");

  v = base();
  v.estadoTemp = EstadoTemperatura::Estabilizando;
  v.temp = 35.4f;
  v.ppg = EstadoPpg::SenalDebil;
  v.ms = 700;
  dibujarPrincipal(principal, v);
  guardar(principal, "06_senal_debil");

  v = base();
  v.ppg = EstadoPpg::Valido;
  v.fc = 95;
  v.spo2 = 96;
  v.alertaCaida = true;
  v.ms = 0;
  dibujarPrincipal(principal, v);
  guardar(principal, "07_caida");

  v = base();
  v.broker = 3;
  v.ms = 500;
  v.estadoTemp = EstadoTemperatura::SinSonda;
  v.acelerometro = false;
  v.sensorPpg = false;
  v.red = "MQTT: usuario/clave?";
  v.avisoRed = true;
  dibujarPrincipal(principal, v);
  guardar(principal, "08_sin_sensores");

  v = base();
  v.fc = 64;
  v.fcDelEcg = true;
  v.estadoTemp = EstadoTemperatura::SinContacto;
  dibujarPrincipal(principal, v);
  guardar(principal, "09_solo_ecg");

  // ECG: 6 s de señal sintética pasada por el mismo filtro del firmware
  ProcesadorEcg p;
  GeneradorEcg gen;
  gen.fc = 72;
  BarridoEcg barrido(8, 97);  // OLED 128x32: 97 columnas de trazo y el panel de la FC
  VistaEcg ve;
  for (int i = 0; i < 250 * 6; i++) {
    bool latido = false;
    barrido.agregar(p.procesar(gen.muestra(), true, &latido));
  }
  for (int i = 0; i < 30; i++) barrido.actualizarEscala();  // como tras varios cuadros
  ve.electrodos = true;
  ve.fc = p.resultado().fc;
  ve.pulso = true;
  dibujarEcg(ecg, barrido, ve);
  guardar(ecg, "10_ecg");

  ve.electrodos = false;
  ve.ms = 700;
  barrido.limpiar();
  dibujarEcg(ecg, barrido, ve);
  guardar(ecg, "11_ecg_sin_electrodos");

  // OLED del ECG durante el arranque: verificación animada
  VistaArranque va;
  va.ppg = va.acelerometro = va.temperatura = va.ecgModulo = va.wifi = true;
  va.broker = 1;
  va.ms = 1150;
  dibujarVerificacion(ecg, va);
  guardar(ecg, "12_ecg_verificando");

  va.electrodos = true;
  va.broker = 2;
  va.ms = 1700;
  dibujarVerificacion(ecg, va);
  guardar(ecg, "13_ecg_verificacion_lista");
  return 0;
}
