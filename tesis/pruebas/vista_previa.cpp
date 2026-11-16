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
  v.fr = 16;
  v.estadoFr = EstadoRespiracion::Valida;
  v.fuenteFr = FuenteFr::MpuYPulso;  // el MPU y el pulso coinciden
  v.cama = "MED-B-01";  // vinculado: solo cama e iniciales
  v.iniciales = "R.M.";
  v.codigo = "ESP32-001";
  v.red = "MiRedCasa";
  v.sesionActiva = true;
  v.bateria = 87;  // LiPo con el divisor en VP (bateria.h)
  v.estadoBateria = EstadoBateria::Normal;
  v.ms = 1000;
  return v;
}

int main() {
  GFXcanvas1 principal(128, 64), ecg(128, 32);

  // Sin vincular: el QR que escanea la enfermera (contenido real del backend) y los demás casos
  VistaVinculacion vq;
  vq.pantalla = PantallaVinculo::Qr;
  vq.qr = "VTF1:ESP32-001:K7P3QX9M";
  vq.segundosRestantes = 572;
  vq.codigo = "ESP32-001";
  vq.wifiBarras = 3;
  vq.broker = 2;
  vq.bateria = 87;
  vq.estadoBateria = EstadoBateria::Normal;
  vq.horaValida = true;
  vq.hora = 14;
  vq.minuto = 5;
  vq.red = "Hospital-Medicina";
  vq.ms = 400;
  vq.msPantalla = 5000;  // ya terminó su animación de entrada
  dibujarVinculacion(principal, vq);
  guardar(principal, "00a_vinculacion_qr");
  dibujarPasosVinculo(ecg, vq);
  guardar(ecg, "00a_vinculacion_qr_oled_ecg");

  // Antes del QR: sin Wi-Fi, con Wi-Fi y sin servidor, y con servidor (pidiendo el QR)
  VistaVinculacion vc = vq;
  vc.pantalla = PantallaVinculo::Solicitando;
  vc.wifiBarras = -1;
  vc.broker = 0;
  vc.ms = 1200;
  dibujarVinculacion(principal, vc);
  guardar(principal, "00b1_conectando_sin_wifi");
  dibujarPasosVinculo(ecg, vc);
  guardar(ecg, "00b1_conectando_sin_wifi_oled_ecg");
  vc.wifiBarras = 3;
  vc.broker = 1;
  vc.red = "Iphone de Mayck";
  dibujarVinculacion(principal, vc);
  guardar(principal, "00b2_conectando_servidor");
  vq.pantalla = PantallaVinculo::Solicitando;
  vq.ms = 800;
  dibujarVinculacion(principal, vq);
  guardar(principal, "00b3_pidiendo_qr");

  vq.pantalla = PantallaVinculo::Vencido;
  dibujarVinculacion(principal, vq);
  guardar(principal, "00c_vinculacion_qr_vencido");
  vq.pantalla = PantallaVinculo::Emparejando;
  vq.segundosRestantes = 272;  // lo que le queda a la app para confirmar (emparejando trae "expira")
  vq.ms = 1200;
  dibujarVinculacion(principal, vq);
  guardar(principal, "00d_emparejando");
  dibujarPasosVinculo(ecg, vq);
  guardar(ecg, "00d_emparejando_oled_ecg");
  vq.segundosRestantes = 0;  // venció sin confirmar: pide un QR nuevo
  dibujarVinculacion(principal, vq);
  guardar(principal, "00d2_emparejando_vencido");

  // Los 5 s al cambiar la vinculación
  vq.pantalla = PantallaVinculo::Vinculado;
  vq.cama = "MED-B-01";
  vq.iniciales = "R.M.";
  dibujarVinculacion(principal, vq);
  guardar(principal, "00e_vinculado");
  dibujarPasosVinculo(ecg, vq);
  guardar(ecg, "00e_vinculado_oled_ecg");
  vq.pantalla = PantallaVinculo::Desvinculado;
  dibujarVinculacion(principal, vq);
  guardar(principal, "00f_desvinculado");

  // Configuración del Wi-Fi desde la app por Bluetooth (BOOT 3 s, o solo sin red conocida)
  VistaConfigWifi cw;
  cw.qr = "VTFW:ESP32-001:K7P3QX9M";
  cw.pop = "K7P3QX9M";
  cw.codigo = "ESP32-001";
  cw.bateria = 87;
  cw.estadoBateria = EstadoBateria::Normal;
  cw.ms = 400;
  dibujarConfigWifi(principal, cw);
  guardar(principal, "14a_wifi_qr");
  dibujarPasosConfigWifi(ecg, cw);
  guardar(ecg, "14a_wifi_qr_oled_ecg");
  cw.etapa = EtapaConfigWifi::AppConectada;
  dibujarConfigWifi(principal, cw);
  guardar(principal, "14b_wifi_app_conectada");
  dibujarPasosConfigWifi(ecg, cw);
  guardar(ecg, "14b_wifi_app_conectada_oled_ecg");
  cw.etapa = EtapaConfigWifi::Probando;
  cw.ssid = "Hospital-Medicina";
  cw.ms = 1300;
  dibujarConfigWifi(principal, cw);
  guardar(principal, "14c_wifi_probando");
  cw.etapa = EtapaConfigWifi::Fallo;
  cw.fallo = FalloConfigWifi::Clave;
  dibujarConfigWifi(principal, cw);
  guardar(principal, "14d_wifi_clave_incorrecta");
  dibujarPasosConfigWifi(ecg, cw);
  guardar(ecg, "14d_wifi_clave_incorrecta_oled_ecg");
  cw.etapa = EtapaConfigWifi::Lista;
  cw.fallo = FalloConfigWifi::Ninguno;
  dibujarConfigWifi(principal, cw);
  guardar(principal, "14e_wifi_listo");
  dibujarPasosConfigWifi(ecg, cw);
  guardar(ecg, "14e_wifi_listo_oled_ecg");

  // Animaciones: cuadros sueltos del arranque (el logo se arma, se corre y se escribe el nombre) y de la
  // entrada del QR (el cuadro crece y los módulos aparecen en diagonal)
  VistaArranque anim;
  anim.version = "2.5.0";
  anim.codigo = "ESP32-001";
  anim.ppg = anim.acelerometro = anim.temperatura = anim.ecgModulo = true;
  const uint32_t tiempos[] = {150, 450, 800, 1100, 1650, 2200};
  for (const uint32_t t : tiempos) {
    anim.ms = t;
    char nombre[40];
    snprintf(nombre, sizeof nombre, "15_anim_arranque_%04u", (unsigned)t);
    dibujarArranque(principal, anim);
    guardar(principal, nombre);
  }
  VistaVinculacion animQr = vq;
  animQr.pantalla = PantallaVinculo::Qr;
  animQr.segundosRestantes = 572;
  const uint32_t tiemposQr[] = {120, 380, 550, 900};
  for (const uint32_t t : tiemposQr) {
    animQr.msPantalla = t;
    char nombre[40];
    snprintf(nombre, sizeof nombre, "16_anim_qr_%04u", (unsigned)t);
    dibujarVinculacion(principal, animQr);
    guardar(principal, nombre);
  }

  // Al encender: la presentación con el logo en las dos OLED (la barra sube con cada chequeo)
  VistaArranque p0;
  p0.version = "2.5.0";
  p0.codigo = "ESP32-001";
  p0.ppg = p0.acelerometro = p0.temperatura = p0.ecgModulo = true;
  p0.bateria = 87;
  p0.estadoBateria = EstadoBateria::Normal;
  p0.ms = 2300;
  dibujarArranque(principal, p0);
  guardar(principal, "00_presentacion_wifi");
  dibujarVerificacion(ecg, p0);
  guardar(ecg, "00_presentacion_wifi_oled_ecg");
  p0.wifi = true;
  p0.wifiBarras = 3;
  p0.red = "Hospital-Medicina";
  p0.broker = 2;
  p0.ms = 2400;
  dibujarArranque(principal, p0);
  guardar(principal, "00_presentacion_lista");
  dibujarVerificacion(ecg, p0);
  guardar(ecg, "00_presentacion_lista_oled_ecg");

  VistaArranque a;
  a.version = "2.5.0";
  a.bateria = 87;
  a.estadoBateria = EstadoBateria::Normal;
  a.codigo = "ESP32-001";
  a.ppg = a.acelerometro = a.ecgModulo = true;  // falta la sonda: pasados 3 s, el chequeo detallado
  a.ms = 3400;
  dibujarArranque(principal, a);
  guardar(principal, "01a_arranque_falta_sensor");
  a.temperatura = a.wifi = true;
  a.acelerometro = false;
  a.broker = 2;
  a.red = "MiRedCasa";
  a.ms = 4600;
  dibujarArranque(principal, a);
  guardar(principal, "01b_arranque_falta_acelerometro");

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
  v.fr = -1;
  v.estadoFr = EstadoRespiracion::Movimiento;
  dibujarPrincipal(principal, v);
  guardar(principal, "09_solo_ecg");

  v = base();
  v.fc = 78;
  v.spo2 = 97;
  v.fr = -1;
  v.estadoFr = EstadoRespiracion::Midiendo;
  v.estadoTemp = EstadoTemperatura::Estabilizando;
  v.temp = 35.9f;
  dibujarPrincipal(principal, v);
  guardar(principal, "09b_fr_midiendo");

  v = base();
  v.fc = 78;
  v.spo2 = 97;
  v.aviso = "Repita: presion";
  v.ms = 0;
  dibujarPrincipal(principal, v);
  guardar(principal, "09c_aviso_del_backend");

  // Batería: crítica (la pila parpadea y el pie alterna con la temperatura) y sin el divisor ("--")
  v = base();
  v.fc = 74;
  v.spo2 = 97;
  v.bateria = 8;
  v.estadoBateria = EstadoBateria::Critica;
  v.ms = 0;
  dibujarPrincipal(principal, v);
  guardar(principal, "09d_bateria_critica");
  v.bateria = 18;
  v.estadoBateria = EstadoBateria::Baja;
  v.ms = 1000;
  dibujarPrincipal(principal, v);
  guardar(principal, "09e_bateria_baja");
  v.bateria = -1;
  v.estadoBateria = EstadoBateria::SinMedicion;
  dibujarPrincipal(principal, v);
  guardar(principal, "09f_bateria_sin_divisor");

  // ECG: 6 s de señal sintética pasada por el mismo filtro del firmware
  ProcesadorEcg p;
  GeneradorEcg gen;
  gen.fc = 72;
  BarridoEcg barrido(8, 128);  // OLED 128x32: el trazo a todo lo ancho
  VistaEcg ve;
  for (int i = 0; i < 250 * 6; i++) {
    bool latido = false;
    barrido.agregar(p.procesar(gen.muestra(), true, &latido));
  }
  for (int i = 0; i < 30; i++) barrido.actualizarEscala();  // como tras varios cuadros
  ve.electrodos = true;
  ve.fc = p.resultado().fc;
  ve.pulso = true;
  ve.wifiBarras = 3;  // el estado del equipo va en esta OLED
  ve.broker = 2;
  ve.pendientes = 3;
  ve.bateria = 87;
  ve.estadoBateria = EstadoBateria::Normal;
  ve.horaValida = true;
  ve.hora = 14;
  ve.minuto = 5;
  dibujarEcg(ecg, barrido, ve);
  guardar(ecg, "10_ecg");

  ve.electrodos = false;
  ve.ms = 700;
  barrido.limpiar();
  dibujarEcg(ecg, barrido, ve);
  guardar(ecg, "11_ecg_sin_electrodos");

  ve.electrodos = true;
  ve.ruido = true;  // electrodo flojo o sin RL: mucha red de 60 Hz o el ADC saturado
  ve.ms = 1000;
  dibujarEcg(ecg, barrido, ve);
  guardar(ecg, "11b_ecg_con_ruido");

  // OLED del ECG durante el arranque: verificación animada
  VistaArranque va;
  va.ppg = va.temperatura = va.ecgModulo = va.wifi = true;  // falta el acelerómetro pasados 3 s
  va.broker = 1;
  va.ms = 4150;
  dibujarVerificacion(ecg, va);
  guardar(ecg, "12_ecg_verificando_falta_sensor");
  return 0;
}
