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
  v.sondaTemp = true;
  v.tempValida = true;
  v.temp = 36.8f;
  v.acelerometro = true;
  v.codigo = "ESP32-001";
  v.red = "MiRedCasa";
  v.ms = 1000;
  return v;
}

int main() {
  GFXcanvas1 principal(128, 64), ecg(128, 32);

  VistaArranque a;
  a.version = "2.0.0";
  a.codigo = "ESP32-001";
  a.ppg = a.acelerometro = a.oledEcg = true;
  a.temperatura = false;
  dibujarArranque(principal, a);
  guardar(principal, "01_arranque");

  VistaPrincipal v = base();
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
  dibujarPrincipal(principal, v);
  guardar(principal, "05_tres_digitos");

  v = base();
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
  v.sondaTemp = false;
  v.acelerometro = false;
  v.sensorPpg = false;
  v.red = "MQTT: usuario/clave?";
  v.avisoRed = true;
  dibujarPrincipal(principal, v);
  guardar(principal, "08_sin_sensores");

  v = base();
  v.fc = 64;
  v.fcDelEcg = true;
  dibujarPrincipal(principal, v);
  guardar(principal, "09_solo_ecg");

  // ECG: 6 s de señal sintética pasada por el mismo filtro del firmware
  ProcesadorEcg p;
  GeneradorEcg gen;
  gen.fc = 72;
  BarridoEcg barrido;
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
  return 0;
}
