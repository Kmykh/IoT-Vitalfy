// pruebas.cpp — Pruebas de los algoritmos del wearable con señales sintéticas.
// Se ejecutan en el computador con ./ejecutar.sh (no necesitan el ESP32).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "bateria.h"
#include "caidas.h"
#include "redes.h"
#include "fr_cardiaca.h"
#include "ecg.h"
#include "ppg.h"
#include "respiracion.h"
#include "senales.h"
#include "config.h"
#include "telemetria.h"
#include "sesion.h"
#include "temperatura.h"
#include "vinculacion.h"

static int fallas = 0, total = 0;

#define VERIFICAR(cond, ...)             \
  do {                                   \
    total++;                             \
    if (!(cond)) {                       \
      fallas++;                          \
      printf("  FALLA: " __VA_ARGS__);   \
      printf("   [%s:%d]\n", __FILE__, __LINE__); \
    }                                    \
  } while (0)

static int spo2Esperado(double R) {
  const double vertice = 30.354 / (2 * 45.060);
  if (R < vertice) R = vertice;
  double s = -45.060 * R * R + 30.354 * R + 94.845;
  if (s > 100) s = 100;
  return (int)(s + 0.5);
}

// Corre el generador PPG durante `segundos` y devuelve el resultado final.
static ResultadoPpg correrPpg(ProcesadorPpg& p, GeneradorPpg& g, double segundos) {
  const int n = (int)(segundos * g.fs);
  for (int i = 0; i < n; i++) {
    uint32_t ir, rojo;
    g.muestra(ir, rojo);
    p.procesar(ir, rojo);
  }
  return p.resultado();
}

static void pruebasPpg() {
  printf("PPG (MAX30102)\n");

  // FC y SpO2 en todo el rango de frecuencias
  for (double fc : {40.0, 50.0, 60.0, 75.0, 90.0, 110.0, 130.0, 150.0, 180.0}) {
    ProcesadorPpg p;
    GeneradorPpg g;
    g.fc = fc;
    g.ruido = 20;
    ResultadoPpg r = correrPpg(p, g, 20);
    printf("  fc real %5.1f -> fc %3d  spo2 %3d (esperado %d)  PI %.2f%%\n", fc, r.fc, r.spo2,
           spo2Esperado(g.R), r.perfusion);
    VERIFICAR(r.estado == EstadoPpg::Valido, "estado no válido a %.0f lpm", fc);
    VERIFICAR(fabs(r.fc - fc) <= 2, "fc %d, esperado %.0f", r.fc, fc);
    VERIFICAR(abs(r.spo2 - spo2Esperado(g.R)) <= 1, "spo2 %d, esperado %d", r.spo2, spo2Esperado(g.R));
  }

  // SpO2 en la zona clínica (R define la saturación)
  for (double R : {0.45, 0.6, 0.7, 0.8, 0.9}) {
    ProcesadorPpg p;
    GeneradorPpg g;
    g.R = R;
    g.ruido = 20;
    ResultadoPpg r = correrPpg(p, g, 20);
    printf("  R %.2f -> spo2 %3d (esperado %d)\n", R, r.spo2, spo2Esperado(R));
    VERIFICAR(abs(r.spo2 - spo2Esperado(R)) <= 1, "spo2 %d con R=%.2f, esperado %d", r.spo2, R, spo2Esperado(R));
  }

  // Variabilidad de ritmo, ruido fuerte y perfusión baja
  {
    ProcesadorPpg p;
    GeneradorPpg g;
    g.variabilidadRr = 0.08;
    g.ruido = 100;  // 10 % del AC
    ResultadoPpg r = correrPpg(p, g, 30);
    printf("  RR ±8%% y ruido 10%% -> fc %d (esperado ~%.0f)\n", r.fc, 60.0 * g.latidos / g.t);
    VERIFICAR(fabs(r.fc - 75) <= 4, "fc %d con variabilidad", r.fc);
  }
  {
    ProcesadorPpg p;
    GeneradorPpg g;
    g.perfusion = 0.002;  // dedo frío
    g.ruido = 10;
    ResultadoPpg r = correrPpg(p, g, 20);
    printf("  perfusión 0,2%% -> fc %d spo2 %d\n", r.fc, r.spo2);
    VERIFICAR(fabs(r.fc - 75) <= 2, "fc %d con perfusión baja", r.fc);
    VERIFICAR(abs(r.spo2 - spo2Esperado(g.R)) <= 2, "spo2 %d con perfusión baja", r.spo2);
  }
  {
    ProcesadorPpg p;
    GeneradorPpg g;
    g.fc = 60;
    g.dicrota = 0.6;  // onda dícrota marcada: no debe contarse como otro latido
    ResultadoPpg r = correrPpg(p, g, 20);
    printf("  onda dícrota 60%% -> fc %d\n", r.fc);
    VERIFICAR(fabs(r.fc - 60) <= 2, "fc %d con dícrota fuerte (doble conteo)", r.fc);
  }

  // Dedo: ambiente -> dedo -> retiro
  {
    ProcesadorPpg p;
    for (int i = 0; i < 300; i++) p.procesar(1500, 1200);  // 3 s sin dedo (cuarto oscuro)
    VERIFICAR(p.resultado().estado == EstadoPpg::SinDedo, "detectó dedo sin dedo");
    GeneradorPpg g;
    ResultadoPpg r = correrPpg(p, g, 12);
    VERIFICAR(r.estado == EstadoPpg::Valido && fabs(r.fc - 75) <= 2, "no midió tras poner el dedo (fc %d)", r.fc);
    int muestrasHastaSalir = 0;
    while (p.hayDedo() && muestrasHastaSalir < 100) {
      p.procesar(1500, 1200);
      muestrasHastaSalir++;
    }
    printf("  dedo retirado detectado en %d ms\n", muestrasHastaSalir * 10);
    VERIFICAR(!p.hayDedo() && muestrasHastaSalir <= 10, "tardó %d muestras en soltar", muestrasHastaSalir);
    VERIFICAR(p.resultado().fc == -1 && p.resultado().spo2 == -1, "quedaron valores viejos sin dedo");
  }

  // Luz del teclado: salto de IR sin pulso -> se libera y no vuelve a entrar en bucle
  {
    ProcesadorPpg p;
    for (int i = 0; i < 300; i++) p.procesar(1000, 900);
    bool entro = false, senalDebil = false;
    for (int i = 0; i < 1500; i++) {  // 15 s de luz constante
      p.procesar(17000, 15000);
      entro |= p.hayDedo();
      senalDebil |= p.resultado().estado == EstadoPpg::SenalDebil;
    }
    printf("  luz ambiente sin pulso: entró=%d, avisó señal débil=%d, al final dedo=%d\n", entro, senalDebil,
           p.hayDedo());
    VERIFICAR(entro && senalDebil && !p.hayDedo(), "no liberó la luz ambiente sin pulso");
    GeneradorPpg g;
    g.dcIr = 60000;
    g.dcRojo = 50000;
    ResultadoPpg r = correrPpg(p, g, 12);
    VERIFICAR(r.estado == EstadoPpg::Valido && fabs(r.fc - 75) <= 2, "no midió un dedo real después (fc %d)", r.fc);
  }

  // Desborde del FIFO: se pierden muestras y la medición sigue
  {
    ProcesadorPpg p;
    GeneradorPpg g;
    correrPpg(p, g, 10);
    for (int i = 0; i < 7; i++) {
      uint32_t ir, rojo;
      g.muestra(ir, rojo);  // estas muestras "no llegan"
    }
    p.saltar(7);
    ResultadoPpg r = correrPpg(p, g, 5);
    VERIFICAR(fabs(r.fc - 75) <= 2, "fc %d tras perder muestras", r.fc);
  }
}

static ResultadoEcg correrEcg(ProcesadorEcg& p, GeneradorEcg& g, double segundos, int* detectados = nullptr) {
  const int n = (int)(segundos * g.fs);
  for (int i = 0; i < n; i++) {
    bool latido = false;
    p.procesar(g.muestra(), true, &latido);
    if (latido && detectados) (*detectados)++;
  }
  return p.resultado();
}

static void pruebasEcg() {
  printf("ECG (AD8232)\n");
  for (double fc : {40.0, 60.0, 80.0, 100.0, 130.0, 160.0, 200.0}) {
    ProcesadorEcg p;
    GeneradorEcg g;
    g.fc = fc;
    int detectados = 0;
    correrEcg(p, g, 3, &detectados);  // incluye conexión y aprendizaje
    const int antes = g.latidos;
    detectados = 0;
    ResultadoEcg r = correrEcg(p, g, 20, &detectados);
    const int reales = g.latidos - antes;
    printf("  fc real %5.1f -> fc %3d  QRS %d/%d\n", fc, r.fc, detectados, reales);
    VERIFICAR(fabs(r.fc - fc) <= 2, "fc %d, esperado %.0f", r.fc, fc);
    VERIFICAR(abs(detectados - reales) <= 1, "detectó %d QRS de %d", detectados, reales);
  }
  {
    ProcesadorEcg p;
    GeneradorEcg g;
    g.fc = 60;
    g.ondaT = 0.8;  // onda T alta: no debe contarse como QRS
    ResultadoEcg r = correrEcg(p, g, 20);
    printf("  onda T alta -> fc %d\n", r.fc);
    VERIFICAR(fabs(r.fc - 60) <= 2, "fc %d con onda T alta", r.fc);
  }
  {
    ProcesadorEcg p;
    GeneradorEcg g;
    g.variabilidadRr = 0.1;
    g.ruido = 40;
    g.red = 150;
    ResultadoEcg r = correrEcg(p, g, 30);
    printf("  RR ±10%%, red 150 y ruido 40 -> fc %d (irregularidad %.2f)\n", r.fc, r.irregularidad);
    VERIFICAR(fabs(r.fc - 75) <= 4, "fc %d con ruido", r.fc);
  }
  {
    // Calidad: con buen contacto no hay aviso; con un electrodo flojo (mucha red de 60 Hz) sí, sin FC,
    // y al volver el buen contacto el aviso se va y la FC vuelve
    ProcesadorEcg p;
    GeneradorEcg g;
    ResultadoEcg r = correrEcg(p, g, 10);
    printf("  buen contacto -> ruido de red %.0f RMS, ruidosa %d, fc %d\n", r.ruidoRed, r.ruidosa, r.fc);
    VERIFICAR(!r.ruidosa, "señal limpia marcada ruidosa (red %.0f RMS)", r.ruidoRed);
    g.red = 400;
    r = correrEcg(p, g, 5);
    printf("  electrodo flojo (red 400) -> ruido de red %.0f RMS, ruidosa %d, fc %d\n", r.ruidoRed, r.ruidosa, r.fc);
    VERIFICAR(r.ruidosa, "no avisó con %.0f conteos RMS de red", r.ruidoRed);
    VERIFICAR(r.fc == -1, "dio FC %d con la señal ruidosa", r.fc);
    g.red = 60;
    r = correrEcg(p, g, 8);
    printf("  vuelve el buen contacto -> ruidosa %d, fc %d\n", r.ruidosa, r.fc);
    VERIFICAR(!r.ruidosa, "el aviso de ruido no se fue (red %.0f RMS)", r.ruidoRed);
    VERIFICAR(fabs(r.fc - 75) <= 2, "no recuperó la FC tras el ruido (fc %d)", r.fc);
  }
  {
    // Al conectar, el AD8232 toca el riel mientras se asienta: eso no es una señal ruidosa
    ProcesadorEcg p;
    GeneradorEcg g;
    for (int i = 0; i < 100; i++) p.procesar(4095, true);  // 0,4 s en el riel
    ResultadoEcg r = correrEcg(p, g, 1);
    printf("  asentamiento al conectar -> saturación %.1f %%, ruidosa %d\n", 100 * r.saturacion, r.ruidosa);
    VERIFICAR(!r.ruidosa, "marcó ruidosa el asentamiento (saturación %.1f %%)", 100 * r.saturacion);
  }
  {
    // Electrodos: se desconectan y se vuelven a conectar
    ProcesadorEcg p;
    GeneradorEcg g;
    correrEcg(p, g, 10);
    for (int i = 0; i < 500; i++) p.procesar(4095, false);
    VERIFICAR(!p.resultado().electrodos && p.resultado().fc == -1, "siguió dando FC sin electrodos");
    ResultadoEcg r = correrEcg(p, g, 8);
    printf("  reconexión -> fc %d\n", r.fc);
    VERIFICAR(fabs(r.fc - 75) <= 2, "no recuperó la FC tras reconectar (fc %d)", r.fc);
  }
  {
    // Electrodos que se mueven: la señal toca el riel del ADC. Esos saltos no deben contarse como QRS
    ProcesadorEcg p;
    GeneradorEcg g;
    correrEcg(p, g, 10);
    int falsos = 0;
    for (int i = 0; i < 250 * 4; i++) {  // 4 s de artefactos: cada 0,3 s salta al riel y vuelve
      const int crudo = (i % 75) < 8 ? ((i / 75) % 2 ? 4095 : 0) : g.muestra();
      bool latido = false;
      p.procesar(crudo, true, &latido);
      if (latido) falsos++;
      if (i % 75 == 74) VERIFICAR(p.resultado().fc == -1, "dio FC %d durante los artefactos", p.resultado().fc);
    }
    VERIFICAR(p.resultado().ruidosa, "no avisó con la señal saturada (%.0f %%)", 100 * p.resultado().saturacion);
    ResultadoEcg r = correrEcg(p, g, 8);
    printf("  artefactos de riel -> QRS falsos %d, FC al pasar %d\n", falsos, r.fc);
    VERIFICAR(falsos == 0, "contó %d artefactos como QRS", falsos);
    VERIFICAR(fabs(r.fc - 75) <= 2, "no recuperó la FC tras los artefactos (fc %d)", r.fc);
  }
  {
    // El notch quita 60 Hz y deja pasar el QRS (10 Hz)
    ProcesadorEcg p;
    double max60 = 0, max10 = 0;
    for (int i = 0; i < 2500; i++) {
      const double t = i / 250.0;
      const float v = p.procesar((int)(2048 + 300 * sin(2 * M_PI * 60 * t)), true);
      if (i > 1250 && fabs(v) > max60) max60 = fabs(v);
    }
    ProcesadorEcg q;
    for (int i = 0; i < 2500; i++) {
      const double t = i / 250.0;
      const float v = q.procesar((int)(2048 + 300 * sin(2 * M_PI * 10 * t)), true);
      if (i > 1250 && fabs(v) > max10) max10 = fabs(v);
    }
    printf("  60 Hz: %.1f de 300 conteos | 10 Hz: %.1f de 300\n", max60, max10);
    VERIFICAR(max60 < 15, "el notch deja pasar %.1f conteos de 60 Hz", max60);
    VERIFICAR(max10 > 250, "el filtro atenúa el QRS (%.1f de 300)", max10);
  }
}

// Escenarios del acelerómetro (g, 100 Hz)
struct Traza {
  DetectorCaidas d;
  Aleatorio rnd{99};
  double t = 0;
  int caidas = 0;
  void muestra(double x, double y, double z) {
    const double r = 0.01;
    if (d.procesar((float)(x + r * rnd.normal()), (float)(y + r * rnd.normal()), (float)(z + r * rnd.normal())))
      caidas++;
    t += 0.01;
  }
  void quieto(double seg, double x, double y, double z) {
    for (int i = 0; i < (int)(seg * 100); i++) muestra(x, y, z);
  }
  void caminar(double seg) {
    for (int i = 0; i < (int)(seg * 100); i++) {
      const double ph = 2 * M_PI * 2.0 * t;
      muestra(0.25 * sin(ph / 2), 0.1 * cos(ph), 1.0 + 0.35 * sin(ph));
    }
  }
  void ingravidez(double seg) {
    for (int i = 0; i < (int)(seg * 100); i++) muestra(0.05, 0.05, 0.2);
  }
  void impacto(double g, double x, double y, double z) {  // 60 ms de golpe en la dirección dada
    for (int i = 0; i < 6; i++) muestra(g * x, g * y, g * z);
  }
  void rebotes(double seg, double x, double y, double z) {
    for (int i = 0; i < (int)(seg * 100); i++) {
      const double a = 1.0 + 0.6 * sin(2 * M_PI * 6 * t) * exp(-3.0 * i / 100.0);
      muestra(a * x, a * y, a * z);
    }
  }
};

static void pruebasCaidas() {
  printf("Caídas (ADXL345)\n");
  {
    Traza s;
    s.quieto(30, 0, 0, 1);
    VERIFICAR(s.caidas == 0 && s.d.actividad() == Actividad::Reposo, "quieto de pie: %d caídas", s.caidas);
  }
  {
    Traza s;
    s.caminar(60);
    VERIFICAR(s.caidas == 0, "caminar: %d caídas", s.caidas);
    VERIFICAR(s.d.actividad() == Actividad::Movimiento, "caminar no se ve como movimiento");
  }
  {
    Traza s;  // caída real: ingravidez, golpe, rebotes y queda tendido de lado
    s.quieto(3, 0, 0, 1);
    s.ingravidez(0.4);
    s.impacto(3.5, 0.7, 0, 0.7);
    s.rebotes(0.6, 1, 0, 0);
    s.quieto(5, 1, 0, 0);
    printf("  caída con ingravidez -> %d\n", s.caidas);
    VERIFICAR(s.caidas == 1, "caída típica: %d detecciones", s.caidas);
  }
  {
    Traza s;  // desmayo sin ingravidez clara pero con golpe fuerte
    s.quieto(3, 0, 0, 1);
    s.impacto(3.4, 0.7, 0.7, 0);
    s.rebotes(0.5, 0, 1, 0);
    s.quieto(5, 0, 1, 0);
    printf("  golpe fuerte sin ingravidez -> %d\n", s.caidas);
    VERIFICAR(s.caidas == 1, "desmayo con golpe: %d detecciones", s.caidas);
  }
  {
    Traza s;  // salto: ingravidez y aterrizaje, pero sigue de pie y caminando
    s.quieto(3, 0, 0, 1);
    s.ingravidez(0.3);
    s.impacto(3.0, 0, 0, 1);
    s.caminar(10);
    VERIFICAR(s.caidas == 0, "salto: %d caídas", s.caidas);
  }
  {
    Traza s;  // sentarse de golpe: impacto sin cambio de postura
    s.quieto(3, 0, 0, 1);
    s.impacto(3.2, 0, 0, 1);
    s.quieto(6, 0, 0, 1);
    VERIFICAR(s.caidas == 0, "sentarse de golpe: %d caídas", s.caidas);
  }
  {
    Traza s;  // acostarse despacio: cambia la postura pero sin impacto
    s.quieto(3, 0, 0, 1);
    for (int i = 0; i <= 200; i++) {
      const double a = M_PI / 2 * i / 200.0;
      s.muestra(sin(a), 0, cos(a));
    }
    s.quieto(6, 1, 0, 0);
    VERIFICAR(s.caidas == 0, "acostarse: %d caídas", s.caidas);
  }
  {
    Traza s;  // cae y se levanta de inmediato (se mueve): no se confirma
    s.quieto(3, 0, 0, 1);
    s.ingravidez(0.4);
    s.impacto(3.5, 1, 0, 0);
    s.rebotes(0.6, 1, 0, 0);
    s.caminar(6);
    VERIFICAR(s.caidas == 0, "caída con recuperación inmediata: %d", s.caidas);
  }
}

static void pruebasMuestreoSinRed() {
  printf("Lectura por segundo y búfer sin red\n");
  VERIFICAR(INTERVALO_TELEMETRIA_MS == 1000, "intervalo: %d ms", INTERVALO_TELEMETRIA_MS);
  MuestreoSinRed m((int64_t)INTERVALO_BUFER_OFFLINE_MS * 1000);
  Lectura l;
  int aceptadas = 0;
  for (int s = 0; s < 60; s++) {  // un minuto con el broker conectado: pasan las 60
    l.monoUs = (int64_t)s * 1000000;
    aceptadas += m.aceptar(l, true);
  }
  VERIFICAR(aceptadas == 60, "conectado: %d de 60", aceptadas);
  aceptadas = 0;
  for (int s = 60; s < 120; s++) {  // un minuto sin red: una cada 10 s
    l.monoUs = (int64_t)s * 1000000;
    aceptadas += m.aceptar(l, false);
  }
  VERIFICAR(aceptadas == 6, "sin red: %d en 60 s", aceptadas);
  l.monoUs = 121 * 1000000LL;
  l.caida = true;
  VERIFICAR(m.aceptar(l, false), "una caída sin red se guarda siempre");
  l.caida = false;
  l.monoUs = 122 * 1000000LL;
  VERIFICAR(m.aceptar(l, true), "al volver la red pasa todo");
  printf("  conectado 60/60, sin red 6/60 (360 lecturas = %d min)\n", CAPACIDAD_BUFER_OFFLINE * INTERVALO_BUFER_OFFLINE_MS / 60000);
}

static void pruebasTelemetria() {
  printf("Telemetría (contrato del backend)\n");
  char buf[256];
  VERIFICAR(formatearIso8601(1790777110250LL, buf, sizeof buf) && strcmp(buf, "2026-09-30T14:05:10.250Z") == 0,
            "ISO 8601: %s", buf);
  VERIFICAR(formatearIso8601(1709251199001LL, buf, sizeof buf) && strcmp(buf, "2024-02-29T23:59:59.001Z") == 0,
            "ISO 8601 (bisiesto): %s", buf);

  Lectura l;
  l.epochMs = 1790777110250LL;
  l.seq = 18234;
  l.fc = 82;
  l.fr = 16;
  l.spo2 = 97;
  l.tempDecimas = 368;
  size_t n = construirJsonTelemetria("ESP32-001", l, buf, sizeof buf);
  printf("  %s\n", buf);
  VERIFICAR(n > 0 && strcmp(buf,
                            "{\"dispositivo\":\"ESP32-001\",\"ts\":\"2026-09-30T14:05:10.250Z\",\"seq\":18234,"
                            "\"fc\":82,\"fr\":16,\"spo2\":97,\"temp\":36.8,\"caida\":false}") == 0,
            "JSON completo: %s", buf);
  l.bateria = 87;  // con el divisor de la batería conectado (bateria.h)
  construirJsonTelemetria("ESP32-001", l, buf, sizeof buf);
  VERIFICAR(strcmp(buf, "{\"dispositivo\":\"ESP32-001\",\"ts\":\"2026-09-30T14:05:10.250Z\",\"seq\":18234,"
                        "\"fc\":82,\"fr\":16,\"spo2\":97,\"temp\":36.8,\"caida\":false,\"bateria\":87}") == 0,
            "JSON con batería: %s", buf);
  l.bateria = -1;
  Lectura soloBateria;  // la batería sola no es una lectura: sin paciente no se envía nada
  soloBateria.bateria = 50;
  VERIFICAR(!soloBateria.tieneDatos(), "la batería sola cuenta como lectura");
  Lectura soloFr;  // paciente quieto sin dedo: la FR sola ya es una lectura
  soloFr.fr = 18;
  VERIFICAR(soloFr.tieneDatos(), "una lectura con solo FR no tiene datos");

  Lectura s;  // solo temperatura y una caída
  s.epochMs = 1790777110250LL;
  s.seq = 5;
  s.tempDecimas = -5;
  s.caida = true;
  construirJsonTelemetria("ESP32-001", s, buf, sizeof buf);
  VERIFICAR(strcmp(buf, "{\"dispositivo\":\"ESP32-001\",\"ts\":\"2026-09-30T14:05:10.250Z\",\"seq\":5,"
                        "\"temp\":-0.5,\"caida\":true}") == 0,
            "JSON parcial: %s", buf);

  Lectura sinHora;
  sinHora.fc = 70;
  VERIFICAR(construirJsonTelemetria("ESP32-001", sinHora, buf, sizeof buf) == 0, "armó un JSON sin hora");
  VERIFICAR(construirJsonTelemetria("ESP32-001", l, buf, 40) == 0, "no detectó búfer chico");
  VERIFICAR(!Lectura().tieneDatos() && l.tieneDatos(), "tieneDatos()");

  // Búfer offline: orden, desborde y hora de lecturas medidas antes del NTP
  BuferLecturas<4> b;
  for (uint32_t i = 0; i < 4; i++) {
    Lectura x;
    x.seq = i;
    VERIFICAR(b.agregar(x), "descartó con lugar libre");
  }
  Lectura quinta;
  quinta.seq = 4;
  VERIFICAR(!b.agregar(quinta) && b.cantidad() == 4 && b.perdidas() == 1 && b.frente().seq == 1,
            "desborde: cantidad %d, perdidas %u, frente %u", b.cantidad(), (unsigned)b.perdidas(), (unsigned)b.frente().seq);
  bool enOrden = true;
  for (uint32_t esperado = 1; b.cantidad() > 0; esperado++) {
    enOrden &= b.frente().seq == esperado;
    b.quitarFrente();
    Lectura x;
    x.seq = esperado + 4;
    if (esperado < 3) b.agregar(x);  // se sigue agregando mientras se vacía (da la vuelta al arreglo)
  }
  VERIFICAR(enOrden, "el búfer no respetó el orden al dar la vuelta");

  BuferLecturas<8> sinNtp;
  Lectura vieja, nueva;
  vieja.monoUs = 5000000;   // medida a los 5 s de encendido
  nueva.monoUs = 95000000;  // medida a los 95 s
  sinNtp.agregar(vieja);
  sinNtp.agregar(nueva);
  sinNtp.asignarHoras(1790777110250LL, 100000000);  // el NTP sincroniza a los 100 s
  VERIFICAR(sinNtp.frente().epochMs == 1790777110250LL - 95000, "hora asignada: %lld",
            (long long)sinNtp.frente().epochMs);
  sinNtp.quitarFrente();
  VERIFICAR(sinNtp.frente().epochMs == 1790777110250LL - 5000, "hora asignada: %lld",
            (long long)sinNtp.frente().epochMs);
  sinNtp.asignarHoras(1790777999000LL, 200000000);  // una segunda sincronización no la cambia
  VERIFICAR(sinNtp.frente().epochMs == 1790777110250LL - 5000, "la hora de una lectura cambió al reenviarla");
}

// Sonda DS18B20 simulada: exponencial hacia el equilibrio, ruido, 12 bits (0,0625 °C) y mediana
// de 3 como en sensores.cpp. Devuelve los segundos hasta "Estable" (0 si nunca) y el valor informado.
struct ResultadoSonda {
  float segundos = 0, valor = 0;
  bool predicha = false;
  int parpadeos = 0;  // vuelve a "midiendo" después de haber dado un resultado
};
static ResultadoSonda simularSonda(float equilibrio, float tau, float duracion, uint64_t semilla = 5) {
  EvaluadorTemperatura t;
  Aleatorio rnd(semilla);
  ResultadoSonda r;
  float ultimas[3] = {24.5f, 24.5f, 24.5f};
  bool estable = false;
  for (float seg = 0; seg < duracion; seg += 1.8f) {
    const float real = equilibrio - (equilibrio - 24.5f) * expf(-seg / tau);
    const float leida = roundf((real + 0.02f * (float)rnd.normal()) / 0.0625f) * 0.0625f;
    ultimas[0] = ultimas[1];
    ultimas[1] = ultimas[2];
    ultimas[2] = leida;
    float a = ultimas[0], b = ultimas[1], c = ultimas[2];  // mediana de 3
    const float mediana = a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));
    t.agregar(mediana, (uint32_t)(seg * 1000));
    if (t.estado() == EstadoTemperatura::Estable && !estable) {
      estable = true;
      r.segundos = seg;
      r.valor = t.valor();
      r.predicha = t.predicha();
    } else if (estable && t.estado() != EstadoTemperatura::Estable) {
      r.parpadeos++;
      estable = false;
    }
  }
  return r;
}

static void pruebasTemperatura() {
  printf("Temperatura (DS18B20)\n");
  EvaluadorTemperatura t;
  uint32_t ms = 0;
  for (int i = 0; i < 20; i++, ms += 1800) t.agregar(24.5f, ms);  // al aire
  VERIFICAR(t.estado() == EstadoTemperatura::SinContacto, "al aire no debe informarse");

  // Sonda en la axila: con la predicción el resultado sale mucho antes de que la sonda llegue
  for (float tau : {30.0f, 60.0f, 90.0f}) {
    float peorError = 0, peorTiempo = 0, sinPrediccion = 0;
    int parpadeos = 0;
    for (uint64_t semilla = 1; semilla <= 6; semilla++) {  // varias secuencias de ruido
      const ResultadoSonda r = simularSonda(36.7f, tau, 500, semilla);
      VERIFICAR(r.segundos > 0, "nunca dio resultado (tau %.0f)", tau);
      if (fabsf(r.valor - 36.7f) > peorError) peorError = fabsf(r.valor - 36.7f);
      if (r.segundos > peorTiempo) peorTiempo = r.segundos;
      if (!r.predicha) sinPrediccion++;
      parpadeos += r.parpadeos;
    }
    const float antes = tau * logf(12.2f / 0.1f);  // lo que tardaba esperando a la sonda (a 0,1 °C)
    printf("  axila, tau %3.0f s: peor error %.1f C, peor tiempo %3.0f s (sin predicción ~%3.0f s), "
           "medidas sin predecir %.0f/6\n", tau, peorError, peorTiempo, antes, sinPrediccion);
    VERIFICAR(peorError <= 0.25f, "error %.2f C con tau %.0f", peorError, tau);
    VERIFICAR(peorTiempo < antes, "la predicción no adelantó el resultado (tau %.0f)", tau);
    VERIFICAR(parpadeos == 0, "%d parpadeos a 'midiendo'", parpadeos);
  }
  {
    const ResultadoSonda r = simularSonda(38.6f, 60, 400, 9);  // paciente con fiebre
    printf("  fiebre: %.1f C a los %.0f s -> %s\n", r.valor, r.segundos,
           EvaluadorTemperatura::nombre(EvaluadorTemperatura::clasificar(r.valor)));
    VERIFICAR(EvaluadorTemperatura::clasificar(r.valor) == RangoTemperatura::Fiebre, "fiebre: %.1f", r.valor);
  }
  {
    EvaluadorTemperatura e;  // sonda ya caliente (estaba en la axila): plana con ruido
    Aleatorio rnd(3);
    uint32_t m = 0;
    for (; m < 120000 && e.estado() != EstadoTemperatura::Estable; m += 1800)
      e.agregar(36.5f + 0.03f * (float)rnd.normal(), m);
    printf("  sonda ya en equilibrio: %.1f C a los %u s\n", e.valor(), (unsigned)(m / 1000));
    VERIFICAR(e.estado() == EstadoTemperatura::Estable && fabsf(e.valor() - 36.5f) <= 0.1f, "plana: %.1f", e.valor());
    e.agregar(25.0f, m);  // se retira
    VERIFICAR(e.estado() == EstadoTemperatura::SinContacto, "no detectó que se retiró la sonda");
    e.sinSonda();
    VERIFICAR(e.estado() == EstadoTemperatura::SinSonda, "sinSonda()");
  }

  ParametrosTemperatura conCorreccion;  // calibrada contra un termómetro clínico
  conCorreccion.correccion = 0.4f;
  EvaluadorTemperatura c(conCorreccion);
  c.agregar(36.2f, 0);
  VERIFICAR(fabsf(c.valor() - 36.6f) < 0.01f, "corrección: %.2f", c.valor());

  // Bandas de NEWS2
  VERIFICAR(EvaluadorTemperatura::clasificar(35.0f) == RangoTemperatura::Hipotermia, "35,0");
  VERIFICAR(EvaluadorTemperatura::clasificar(35.1f) == RangoTemperatura::Baja, "35,1");
  VERIFICAR(EvaluadorTemperatura::clasificar(36.04f) == RangoTemperatura::Baja, "36,04 redondea a 36,0");
  VERIFICAR(EvaluadorTemperatura::clasificar(36.1f) == RangoTemperatura::Normal, "36,1");
  VERIFICAR(EvaluadorTemperatura::clasificar(38.0f) == RangoTemperatura::Normal, "38,0");
  VERIFICAR(EvaluadorTemperatura::clasificar(38.1f) == RangoTemperatura::Fiebre, "38,1");
  VERIFICAR(EvaluadorTemperatura::clasificar(39.1f) == RangoTemperatura::FiebreAlta, "39,1");
}

static void pruebasSesion() {
  printf("Sesión de monitoreo\n");
  SesionPaciente s;
  SenalesPaciente nada, dedo, soloEcg;
  dedo.dedo = true;
  soloEcg.electrodos = true;
  uint32_t ms = 0;
  for (; ms < 10000; ms += 100) s.actualizar(soloEcg, ms);
  VERIFICAR(!s.monitoreando(), "inició sin el dedo (solo electrodos)");
  bool cambio = false;
  for (int i = 0; i < 5; i++, ms += 100) cambio |= s.actualizar(dedo, ms);
  VERIFICAR(!s.monitoreando() && !cambio, "inició con un toque de 0,5 s");
  for (int i = 0; i < 10; i++, ms += 100) cambio |= s.actualizar(dedo, ms);
  VERIFICAR(s.monitoreando() && cambio, "no inició con el dedo sostenido");
  for (int i = 0; i < 300; i++, ms += 100) s.actualizar(nada, ms);  // retira todo 30 s
  VERIFICAR(s.monitoreando(), "cortó la sesión antes de 60 s");
  for (int i = 0; i < 300; i++, ms += 100) s.actualizar(soloEcg, ms);  // sigue con electrodos
  VERIFICAR(s.monitoreando(), "cortó la sesión con electrodos puestos");
  for (int i = 0; i < 610; i++, ms += 100) s.actualizar(nada, ms);
  VERIFICAR(!s.monitoreando(), "no terminó tras 60 s sin paciente");

  SesionPaciente sinOximetro;  // MAX30102 no instalado: inicia con los electrodos
  SenalesPaciente ecgSinPpg;
  ecgSinPpg.sensorPpg = false;
  ecgSinPpg.electrodos = true;
  for (uint32_t t = 0; t < 1500; t += 100) sinOximetro.actualizar(ecgSinPpg, t);
  VERIFICAR(sinOximetro.monitoreando(), "sin oxímetro no inició con los electrodos");
}

// Corre el generador de respiración `segundos` y devuelve la FR al final (y el primer segundo con FR).
struct ResultadoFr {
  int fr = -1;
  EstadoRespiracion estado = EstadoRespiracion::Midiendo;
  double primeraFr = -1;  // s hasta la primera FR válida
};
static ResultadoFr correrRespiracion(MonitorRespiracion& m, GeneradorRespiracion& g, double segundos, double desde = 0) {
  ResultadoFr r;
  const int n = (int)(segundos * g.fs);
  for (int i = 0; i < n; i++) {
    float ax, ay, az;
    g.muestra(ax, ay, az);
    m.procesar(ax, ay, az);
    if (r.primeraFr < 0 && m.fr() > 0) r.primeraFr = desde + i / g.fs;
  }
  r.fr = m.fr();
  r.estado = m.estado();
  return r;
}

static void pruebasRespiracion() {
  printf("Frecuencia respiratoria (MPU6050 sobre el tórax)\n");

  // Todo el rango clínico: NEWS2 separa en ≤8, 9–11, 12–20, 21–24 y ≥25
  for (double rpm : {6.0, 8.0, 10.0, 12.0, 16.0, 20.0, 24.0, 28.0, 35.0, 45.0}) {
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = rpm;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    printf("  fr real %4.1f -> fr %2d (primera a los %.0f s, correlación %.2f, %.1f mg)\n", rpm, r.fr, r.primeraFr,
           m.correlacion(), 1000 * m.amplitud());
    VERIFICAR(r.estado == EstadoRespiracion::Valida && fabs(r.fr - rpm) <= 1, "FR a %.0f rpm: %d", rpm, r.fr);
    VERIFICAR(r.primeraFr > 0 && r.primeraFr <= 36, "la primera FR tardó %.0f s a %.0f rpm", r.primeraFr, rpm);
  }

  {  // Ritmo irregular (±15 % por ciclo)
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 18;
    g.variabilidad = 0.15;
    const ResultadoFr r = correrRespiracion(m, g, 90);
    printf("  irregular 18 -> %d\n", r.fr);
    VERIFICAR(r.fr > 0 && abs(r.fr - 18) <= 2, "FR irregular: %d", r.fr);
  }
  {  // Inspiración corta (1/3 del ciclo): la onda tiene armónicos, no debe dar el doble
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 14;
    g.inspiracion = 0.3;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    printf("  inspiración corta 14 -> %d\n", r.fr);
    VERIFICAR(r.fr > 0 && abs(r.fr - 14) <= 1, "FR con inspiración corta: %d", r.fr);
  }
  {  // Otra orientación: el wearable de pie (gravedad en y) y la respiración gira sobre x y z a la vez
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 22;
    g.gx = 0, g.gy = -1, g.gz = 0;
    g.ex = 0.6, g.ey = 0, g.ez = 0.8;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    printf("  de pie 22 -> %d\n", r.fr);
    VERIFICAR(r.fr > 0 && abs(r.fr - 22) <= 1, "FR de pie: %d", r.fr);
  }
  {  // Latido marcado (balistocardiograma de 4 mg a 75 lpm) con respiración lenta
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 12;
    g.latido = 0.004;
    g.fcLatido = 75;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    printf("  con latido 12 -> %d\n", r.fr);
    VERIFICAR(r.fr > 0 && abs(r.fr - 12) <= 1, "FR con latido: %d", r.fr);
  }
  {  // Respiración superficial: 3 mg de cambio de la gravedad con 3 mg de ruido
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 16;
    g.inclinacion = 0.003;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    printf("  superficial 16 -> %d (%.1f mg)\n", r.fr, 1000 * m.amplitud());
    VERIFICAR(r.fr > 0 && abs(r.fr - 16) <= 1, "FR superficial: %d", r.fr);
  }
  {  // Sobre la mesa (sin respiración): no inventa una FR
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.inclinacion = 0;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    VERIFICAR(r.fr < 0 && r.estado == EstadoRespiracion::SinSenal, "FR sin respiración: %d (estado %d)", r.fr, (int)r.estado);
  }
  {  // Caminando: movimiento, sin FR
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.movimiento = 0.3;
    const ResultadoFr r = correrRespiracion(m, g, 60);
    VERIFICAR(r.fr < 0 && r.estado == EstadoRespiracion::Movimiento, "FR caminando: %d (estado %d)", r.fr, (int)r.estado);
  }
  {  // Quieto -> se mueve -> quieto con otra FR: la FR anterior dura 10 s y la nueva llega tras la ventana
    MonitorRespiracion m;
    GeneradorRespiracion g;
    g.rpm = 16;
    ResultadoFr r = correrRespiracion(m, g, 50);
    VERIFICAR(r.fr == 16, "antes del movimiento: %d", r.fr);
    g.movimiento = 0.3;
    r = correrRespiracion(m, g, 5);
    VERIFICAR(r.estado == EstadoRespiracion::Movimiento && r.fr == 16, "durante el movimiento: %d (estado %d)", r.fr,
              (int)r.estado);
    r = correrRespiracion(m, g, 10);
    VERIFICAR(r.fr < 0, "la FR de antes siguió tras 15 s de movimiento: %d", r.fr);
    g.movimiento = 0;
    g.rpm = 26;
    r = correrRespiracion(m, g, 20);
    VERIFICAR(r.fr < 0 && r.estado == EstadoRespiracion::Midiendo, "dio FR sin una ventana quieta completa: %d", r.fr);
    r = correrRespiracion(m, g, 20);
    printf("  tras moverse: 26 -> %d\n", r.fr);
    VERIFICAR(r.fr > 0 && abs(r.fr - 26) <= 1, "FR después del movimiento: %d", r.fr);
  }
}

static bool comando(const char* json, Comando* c) { return interpretarComando(json, strlen(json), c); }

static void pruebasVinculacion() {
  printf("Comandos del backend y vinculación por QR\n");
  // Los mismos JSON que fija el backend (ContratoComandos)
  Comando c;
  VERIFICAR(comando("{\"accion\":\"mostrar-qr\",\"qr\":\"VTF1:ESP32-001:K7P3QX9M\",\"expira\":\"2026-11-02T15:10:00Z\"}", &c) &&
                c.accion == AccionComando::MostrarQr && strcmp(c.qr, "VTF1:ESP32-001:K7P3QX9M") == 0,
            "mostrar-qr: %s", c.qr);
  char iso[32];
  formatearIso8601(c.expiraMs, iso, sizeof iso);
  VERIFICAR(strcmp(iso, "2026-11-02T15:10:00.000Z") == 0, "expira: %s", iso);
  VERIFICAR(comando("{\"accion\":\"emparejando\"}", &c) && c.accion == AccionComando::Emparejando && c.expiraMs == 0,
            "emparejando");
  VERIFICAR(comando("{\"accion\":\"emparejando\",\"expira\":\"2026-11-02T15:05:00Z\"}", &c) &&
                c.accion == AccionComando::Emparejando && c.expiraMs == interpretarIso8601("2026-11-02T15:05:00Z"),
            "emparejando con expira");
  VERIFICAR(comando("{\"accion\":\"vinculado\",\"cama\":\"MED-B-01\",\"iniciales\":\"R.M.\"}", &c) &&
                c.accion == AccionComando::Vinculado && strcmp(c.cama, "MED-B-01") == 0 && strcmp(c.iniciales, "R.M.") == 0,
            "vinculado: %s %s", c.cama, c.iniciales);
  VERIFICAR(comando("{\"accion\":\"desvinculado\"}", &c) && c.accion == AccionComando::Desvinculado, "desvinculado");
  VERIFICAR(comando("{\"accion\":\"repetir-medicion\",\"variable\":\"presion\",\"motivo\":\"Falta la diastólica\"}", &c) &&
                c.accion == AccionComando::RepetirMedicion && strcmp(c.motivo, "Falta la diastolica") == 0,
            "repetir-medicion: %s", c.motivo);

  // Tolerante con el formato, estricto con el contenido
  VERIFICAR(comando(" { \"qr\" : \"VTF1:X:Y\" , \"extra\":{\"a\":[1,\"}\"]}, \"accion\" : \"mostrar-qr\" } ", &c) &&
                c.accion == AccionComando::MostrarQr && strcmp(c.qr, "VTF1:X:Y") == 0 && c.expiraMs == 0,
            "orden, espacios y valores anidados");
  VERIFICAR(comando("{\"accion\":\"reiniciar\"}", &c) && c.accion == AccionComando::Desconocida, "acción desconocida");
  VERIFICAR(!comando("{\"accion\":\"mostrar-qr\"}", &c), "mostrar-qr sin qr");
  VERIFICAR(!comando("{no es json", &c) && !comando("", &c) && !comando("{\"accion\":7}", &c), "JSON inválido");
  VERIFICAR(comando("{\"accion\":\"vinculado\",\"cama\":\"UCI\\u00d101\",\"iniciales\":\"\\\"J.\\\"\"}", &c) &&
                strcmp(c.cama, "UCIN01") == 0 && strcmp(c.iniciales, "\"J.\"") == 0,
            "escapes: %s %s", c.cama, c.iniciales);
  char ascii[32];
  aAscii("Ñandú í Ü €", ascii, sizeof ascii);
  VERIFICAR(strcmp(ascii, "Nandu i U ?") == 0, "aAscii: %s", ascii);
  VERIFICAR(interpretarIso8601("2026-11-02T15:10:00.250Z") == interpretarIso8601("2026-11-02T15:10:00Z") + 250 &&
                interpretarIso8601("2026-11-02 15:10:00") == 0 && interpretarIso8601("2026-13-02T15:10:00Z") == 0,
            "ISO 8601");

  // Máquina de estados
  Vinculacion v;
  Comando qr, empareja, vinc, desv;
  comando("{\"accion\":\"mostrar-qr\",\"qr\":\"VTF1:ESP32-001:K7P3QX9M\",\"expira\":\"2026-11-02T15:10:00Z\"}", &qr);
  comando("{\"accion\":\"emparejando\"}", &empareja);
  comando("{\"accion\":\"vinculado\",\"cama\":\"MED-B-01\",\"iniciales\":\"R.M.\"}", &vinc);
  comando("{\"accion\":\"desvinculado\"}", &desv);
  const int64_t antesDeVencer = qr.expiraMs - 60000;
  VERIFICAR(v.estado() == EstadoVinculo::Desconocido && v.aceptaLecturas() && !v.puedePublicar(),
            "al arrancar guarda lecturas pero no publica");
  v.alConectar(1000);
  VERIFICAR(!v.debeSolicitarQr(5999, antesDeVencer, false) && v.debeSolicitarQr(6000, antesDeVencer, false),
            "sin comando retenido pide un QR a los 5 s");
  VERIFICAR(!v.debeSolicitarQr(7000, antesDeVencer, true), "pidió dos QR en menos de 10 s");
  EfectoComando e = v.aplicar(qr, 7000);
  VERIFICAR(v.estado() == EstadoVinculo::EsperandoQr && e.descartarBufer && !v.aceptaLecturas(),
            "mostrar-qr: espera el escaneo y descarta lo guardado");
  VERIFICAR(!v.debeSolicitarQr(20000, antesDeVencer, false) && v.debeSolicitarQr(20000, qr.expiraMs, false),
            "pide otro QR solo cuando vence");
  VERIFICAR(v.debeSolicitarQr(31000, antesDeVencer, true), "el botón no pide otro QR");
  e = v.aplicar(empareja, 32000);
  VERIFICAR(v.estado() == EstadoVinculo::Emparejando && !v.aceptaLecturas() && !v.debeSolicitarQr(40000, qr.expiraMs, false),
            "emparejando: espera a la app");
  VERIFICAR(v.debeSolicitarQr(42000, qr.expiraMs, true), "emparejando: el botón pide un QR (el backend decide)");
  e = v.aplicar(vinc, 43000);
  VERIFICAR(v.estado() == EstadoVinculo::Vinculado && !e.descartarBufer && v.puedePublicar() &&
                strcmp(v.cama(), "MED-B-01") == 0 && !v.debeSolicitarQr(90000, qr.expiraMs, true),
            "vinculado: publica y no pide QR");
  v.alDesconectar();
  VERIFICAR(v.aceptaLecturas() && !v.puedePublicar(), "sin broker guarda pero no publica");
  v.alConectar(100000);
  VERIFICAR(!v.puedePublicar(), "tras reconectar publicó sin esperar el comando retenido");
  e = v.aplicar(vinc, 100200);  // el retenido: sigue vinculado
  VERIFICAR(v.puedePublicar() && !e.descartarBufer, "retenido vinculado: vacía el búfer");
  e = v.aplicar(desv, 200000);  // egreso
  VERIFICAR(v.estado() == EstadoVinculo::EsperandoQr && e.descartarBufer && !v.qr()[0] && !v.cama()[0],
            "desvinculado: vuelve a esperar el QR y descarta");
  VERIFICAR(!v.debeSolicitarQr(204000, 0, false) && v.debeSolicitarQr(205000, 0, false),
            "sin el QR nuevo a los 5 s del desvinculado, lo pide");
  Comando repetir;
  comando("{\"accion\":\"repetir-medicion\",\"variable\":\"presion\",\"motivo\":\"x\"}", &repetir);
  v.aplicar(repetir, 210000);
  VERIFICAR(strcmp(v.aviso(), "Repita: presion") == 0 && v.avisoMs() == 210000 && v.estado() == EstadoVinculo::EsperandoQr,
            "repetir-medicion: %s", v.aviso());

  // El escaneo vence sin "vinculado": la pantalla vuelve a un QR (antes quedaba en "Emparejando")
  Vinculacion w;
  Comando emp5;
  comando("{\"accion\":\"emparejando\",\"expira\":\"2026-11-02T15:05:00Z\"}", &emp5);
  const int64_t vence = emp5.expiraMs;
  w.alConectar(0);
  w.aplicar(qr, 1000);
  w.aplicar(emp5, 2000);
  VERIFICAR(w.emparejandoExpiraMs() == vence && !w.debeSolicitarQr(100000, vence - 1000, false) &&
                !w.debeSolicitarQr(101000, vence + 2000, false),
            "pidió un QR antes de que venza el escaneo");
  VERIFICAR(w.debeSolicitarQr(102000, vence + Vinculacion::kMargenVencimientoMs, false) && w.emparejamientoVencido(102000, vence + 5000),
            "con expira: a su hora (más el margen) pide un QR nuevo");
  VERIFICAR(!w.debeSolicitarQr(105000, vence + 8000, false) && w.debeSolicitarQr(112000, vence + 15000, false),
            "si no llega el QR, lo vuelve a pedir cada 10 s");
  w.aplicar(qr, 113000);
  VERIFICAR(w.estado() == EstadoVinculo::EsperandoQr && w.emparejandoExpiraMs() == 0, "el QR nuevo deja de emparejar");

  Vinculacion sinHora;  // sin NTP o un backend que no manda expira: 6 min desde que empezó a emparejar
  sinHora.alConectar(0);
  sinHora.aplicar(qr, 1000);
  sinHora.aplicar(empareja, 10000);
  VERIFICAR(!sinHora.debeSolicitarQr(10000 + Vinculacion::kEsperaEmparejandoMs - 1, 0, false) &&
                sinHora.debeSolicitarQr(10000 + Vinculacion::kEsperaEmparejandoMs, 0, false),
            "sin expira: a los 6 min pide un QR nuevo");

  Vinculacion otraVez;  // la enfermera vuelve a escanear el mismo QR: nuevo plazo
  Comando emp10;
  comando("{\"accion\":\"emparejando\",\"expira\":\"2026-11-02T15:10:00Z\"}", &emp10);
  otraVez.alConectar(0);
  otraVez.aplicar(qr, 1000);
  otraVez.aplicar(empareja, 2000);
  otraVez.aplicar(emp10, 300000);
  VERIFICAR(otraVez.emparejandoExpiraMs() == emp10.expiraMs && !otraVez.debeSolicitarQr(370000, 0, false) &&
                otraVez.debeSolicitarQr(300000 + Vinculacion::kEsperaEmparejandoMs, 0, false),
            "un nuevo escaneo vuelve a contar el plazo");

  // QR: versión más chica posible y que entre en la OLED
  uint8_t q[kQrTamBuffer];
  const int lado = generarQr("VTF1:ESP32-001:K7P3QX9M", q);
  VERIFICAR(lado == 25, "QR de un código de 9 caracteres: %d módulos", lado);
  VERIFICAR(moduloQr(q, 0, 0) && moduloQr(q, 6, 6) && !moduloQr(q, 7, 7), "patrón de posición del QR");
  const int largo = generarQr("VTF1:ABCDEFGHIJKLMNOPQRSTUVWXYZ012345:K7P3QX9M", q);  // código de 32 caracteres
  VERIFICAR(largo > 0 && largo * 2 <= 64, "QR de un código de 32 caracteres: %d módulos", largo);
  printf("  QR: %d módulos (%d px) con un código de 9; %d (%d px) con uno de 32\n", lado, 2 * lado, largo, 2 * largo);
}

static void pruebasBateria() {
  printf("Batería (LiPo de 1 celda por el divisor en VP)\n");
  // Curva de descarga: extremos, puntos de la tabla e interpolación
  VERIFICAR(MedidorBateria::porcentajeDe(4.25f) == 100 && MedidorBateria::porcentajeDe(4.20f) == 100, "llena");
  VERIFICAR(MedidorBateria::porcentajeDe(3.84f) == 50, "3,84 V = 50 %%: %d", MedidorBateria::porcentajeDe(3.84f));
  VERIFICAR(MedidorBateria::porcentajeDe(3.73f) == 20, "3,73 V = 20 %%");
  VERIFICAR(MedidorBateria::porcentajeDe(3.10f) == 0, "vacía");
  const int medio = MedidorBateria::porcentajeDe(4.05f);
  VERIFICAR(medio > 80 && medio < 85, "interpola entre 4,02 y 4,08 V: %d", medio);
  int anterior = 101;
  bool monotona = true;
  for (float v = 4.2f; v >= 3.2f; v -= 0.01f) {
    const int p = MedidorBateria::porcentajeDe(v);
    monotona = monotona && p <= anterior;
    anterior = p;
  }
  VERIFICAR(monotona, "la curva nunca sube al bajar el voltaje");

  // Divisor 100k/100k: 1960 mV en el pin = 3,92 V
  MedidorBateria m;
  VERIFICAR(m.porcentaje() == -1 && m.estado() == EstadoBateria::SinMedicion, "empieza sin medición");
  VERIFICAR(m.agregarMvPin(1960), "la primera lectura cambia el porcentaje");
  VERIFICAR(fabsf(m.voltios() - 3.92f) < 0.001f && m.porcentaje() == MedidorBateria::porcentajeDe(3.92f) &&
                m.estado() == EstadoBateria::Normal,
            "3,92 V: %.2f V, %d %%", m.voltios(), m.porcentaje());

  // El pico de la radio Wi-Fi (una lectura 60 mV más baja) no mueve el porcentaje mostrado
  const int antes = m.porcentaje();
  m.agregarMvPin(1930);
  VERIFICAR(m.porcentaje() == antes, "una lectura suelta movió el porcentaje: %d -> %d", antes, m.porcentaje());
  for (int i = 0; i < 3; i++) m.agregarMvPin(1960);
  VERIFICAR(m.porcentaje() == antes, "el porcentaje oscila cerca del mismo voltaje");

  // Descarga lenta hasta crítica
  for (int i = 0; i < 200; i++) m.agregarMvPin(1840);  // 3,68 V
  VERIFICAR(m.estado() == EstadoBateria::Critica && m.porcentaje() <= 10, "3,68 V es crítica: %d %%", m.porcentaje());
  for (int i = 0; i < 200; i++) m.agregarMvPin(1862);  // 3,724 V
  VERIFICAR(m.estado() == EstadoBateria::Baja, "3,72 V es baja: %d %%", m.porcentaje());

  // Sin divisor: el pin al aire lee cerca de 0. Una lectura mala se ignora; tres seguidas, "sin medición"
  m.agregarMvPin(40);
  VERIFICAR(m.estado() == EstadoBateria::Baja, "una lectura en 0 borró la medición");
  m.agregarMvPin(30);
  m.agregarMvPin(0);
  VERIFICAR(m.estado() == EstadoBateria::SinMedicion && m.porcentaje() == -1, "sin divisor no inventa un porcentaje");
  MedidorBateria sinDivisor;
  for (int i = 0; i < 10; i++) sinDivisor.agregarMvPin(i * 37 % 500);
  VERIFICAR(sinDivisor.porcentaje() == -1, "pin al aire: %d %%", sinDivisor.porcentaje());
  VERIFICAR(!sinDivisor.agregarMvPin(-1) && sinDivisor.estado() == EstadoBateria::SinMedicion, "sin lectura todavía");

  // Carga: al conectar el cargador el voltaje sube y el porcentaje lo sigue
  MedidorBateria carga;
  carga.agregarMvPin(1880);
  const int alConectar = carga.porcentaje();
  for (int i = 0; i < 100; i++) carga.agregarMvPin(2080);  // 4,16 V
  VERIFICAR(carga.porcentaje() > alConectar + 30, "cargando: %d -> %d %%", alConectar, carga.porcentaje());

  // Corrección contra un multímetro
  ParametrosBateria p;
  p.correccion = 1.02f;
  MedidorBateria corregido(p);
  corregido.agregarMvPin(1960);
  VERIFICAR(fabsf(corregido.voltios() - 3.92f * 1.02f) < 0.001f, "corrección: %.3f V", corregido.voltios());
}

static uint32_t semillaAzar = 12345;
static uint32_t azarDePrueba() {  // generador lineal: determinista en las pruebas
  semillaAzar = semillaAzar * 1103515245u + 12345u;
  return semillaAzar >> 8;
}

// Latidos sintéticos: FC base con arritmia sinusal respiratoria (RR ±5 %), amplitud ±10 % y línea de base
// ±2 % al ritmo de la FR, más ruido gaussiano. Estima cada 2 s, como el firmware.
static float normalDePrueba() {
  const float u1 = ((azarDePrueba() & 0xFFFF) + 1) / 65537.0f, u2 = (azarDePrueba() & 0xFFFF) / 65536.0f;
  return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}
static int simularLatidos(FrCardiaca& f, float fc, float fr, float segundos, float ruido, bool modular,
                          int perderLatidoCada = 0) {
  float t = 0, proxima = 2;
  int k = 0;
  while (t < segundos) {
    const float resp = modular ? sinf(6.2831853f * fr / 60.0f * t) : 0;
    t += 60.0f / fc * (1 - 0.05f * resp + ruido * normalDePrueba());
    k++;
    if (perderLatidoCada && k % perderLatidoCada == 0) continue;  // el detector se lo saltó
    const float amp = 1 + 0.1f * sinf(6.2831853f * fr / 60.0f * t + 0.6f) * (modular ? 1 : 0) + ruido * normalDePrueba();
    const float base = 1000 * (1 + 0.02f * resp) + 1000 * ruido * normalDePrueba();
    f.agregar(t, amp, base);
    if (t >= proxima) {
      f.estimar();
      proxima += 2;
    }
  }
  return f.fr();
}

static void pruebasFrCardiaca() {
  printf("FR desde el pulso (FM, AM y línea de base) y fusión con el MPU6050\n");
  const float casos[][2] = {{75, 12}, {75, 16}, {80, 20}, {60, 8}, {90, 24}};
  for (const auto& c : casos) {
    FrCardiaca ppg(true);
    const int fr = simularLatidos(ppg, c[0], c[1], 60, 0.01f, true);
    VERIFICAR(fr > 0 && fabsf((float)fr - c[1]) <= 1.5f && ppg.modulaciones() >= 2,
              "PPG: FC %.0f, FR %.0f -> %d rpm (%d huellas; FM %.1f AM %.1f base %.1f)", c[0], c[1], fr,
              ppg.modulaciones(), ppg.ultimaFm(), ppg.ultimaAm(), ppg.ultimaBase());
    FrCardiaca ecg(false);
    const int frEcg = simularLatidos(ecg, c[0], c[1], 60, 0.01f, true);
    VERIFICAR(frEcg > 0 && fabsf((float)frEcg - c[1]) <= 1.5f, "ECG (FM y AM): FC %.0f, FR %.0f -> %d rpm", c[0], c[1],
              frEcg);
  }

  FrCardiaca perdidos(true);
  const int frPerdidos = simularLatidos(perdidos, 75, 15, 60, 0.01f, true, 23);
  VERIFICAR(frPerdidos > 0 && abs(frPerdidos - 15) <= 2, "con un latido perdido cada 23: %d rpm", frPerdidos);

  int falsos = 0;
  for (int s = 0; s < 8; s++) {
    FrCardiaca ruido(true);
    if (simularLatidos(ruido, 72, 15, 60, 0.02f, false) > 0) falsos++;
  }
  VERIFICAR(falsos == 0, "sin respiración en el pulso no debe inventar una FR: %d de 8", falsos);

  FrCardiaca nyquist(true);
  VERIFICAR(simularLatidos(nyquist, 50, 26, 60, 0.01f, true) < 0, "FC 50 y FR 26: pocos latidos por respiración");

  FrCardiaca corto(true);
  VERIFICAR(simularLatidos(corto, 75, 15, 15, 0.01f, true) < 0, "con 15 s de latidos todavía no hay FR");

  FrCardiaca pausa(true);
  simularLatidos(pausa, 75, 15, 60, 0.01f, true);
  pausa.agregar(70, 1, 1000);  // 10 s sin latidos (se sacó el dedo): empieza de nuevo
  VERIFICAR(pausa.fr() < 0, "tras una pausa de latidos vuelve a medir desde cero");

  FuenteFr f;
  VERIFICAR(fusionarFr(16, 15, -1, &f) == 16 && f == FuenteFr::MpuYPulso, "MPU 16 y ECG 15 coinciden: 16");
  VERIFICAR(fusionarFr(12, 15, -1, &f) == 13 && f == FuenteFr::MpuYPulso, "MPU 12 y ECG 15: (2×12+15)/3 = 13");
  VERIFICAR(fusionarFr(14, 24, -1, &f) == 14 && f == FuenteFr::Mpu, "si no coinciden manda el MPU");
  VERIFICAR(fusionarFr(-1, 18, 17, &f) == 18 && f == FuenteFr::Ecg, "sin MPU: ECG y PPG coinciden -> promedio");
  VERIFICAR(fusionarFr(-1, -1, 17, &f) == 17 && f == FuenteFr::Ppg, "solo el PPG");
  VERIFICAR(fusionarFr(-1, 22, 14, &f) == 22 && f == FuenteFr::Ecg, "ECG y PPG no coinciden: el ECG");
  VERIFICAR(fusionarFr(-1, -1, -1, &f) == -1 && f == FuenteFr::Ninguna, "sin ninguna");
}

static void pruebasRedes() {
  printf("Redes configuradas desde la app y modo Wi-Fi por Bluetooth\n");
  ListaRedes l;
  VERIFICAR(l.agregar("Casa", "clave-casa-123") && l.cantidad() == 1, "agrega una red");
  VERIFICAR(l.agregar("Hospital", "") && l.cantidad() == 2 && strcmp(l.red(0).ssid, "Hospital") == 0,
            "la más reciente queda primero (y una red abierta va sin clave)");
  VERIFICAR(!l.agregar("", "clave1234") && !l.agregar("Corta", "1234567") &&
                !l.agregar("SSID-de-mas-de-treinta-y-dos-bytes!", "clave1234"),
            "rechaza nombres vacíos o largos y claves de 1 a 7 caracteres");
  VERIFICAR(l.agregar("casa", "clave-nueva-456") && l.cantidad() == 2 && strcmp(l.red(0).ssid, "casa") == 0 &&
                strcmp(l.red(0).clave, "clave-nueva-456") == 0 && strcmp(l.red(1).ssid, "Hospital") == 0,
            "la misma red (sin distinguir mayúsculas) se actualiza y sube, sin duplicarse");
  for (int i = 0; i < 6; i++) {
    char ssid[16];
    snprintf(ssid, sizeof ssid, "Red%d", i);
    l.agregar(ssid, "clave1234");
  }
  VERIFICAR(l.cantidad() == ListaRedes::kMax && strcmp(l.red(0).ssid, "Red5") == 0 &&
                strcmp(l.red(ListaRedes::kMax - 1).ssid, "Red1") == 0,
            "con 5 guardadas sale la más antigua: %s ... %s", l.red(0).ssid, l.red(ListaRedes::kMax - 1).ssid);
  VERIFICAR(l.quitar("red3") && l.cantidad() == 4 && !l.quitar("No-esta"), "quitar");

  // NVS: ida y vuelta, y datos corruptos
  uint8_t buf[ListaRedes::kTamSerializado];
  const size_t n = l.serializar(buf, sizeof buf);
  ListaRedes copia;
  VERIFICAR(n > 0 && copia.deserializar(buf, n) && copia.cantidad() == 4 && strcmp(copia.red(0).ssid, "Red5") == 0 &&
                strcmp(copia.red(0).clave, "clave1234") == 0,
            "serializa y vuelve a leer");
  buf[0] = 99;
  VERIFICAR(!copia.deserializar(buf, n) && copia.cantidad() == 0, "versión desconocida: queda vacía");
  VERIFICAR(!copia.deserializar(buf, 1) && !copia.deserializar(nullptr, 0), "largo inválido");
  VERIFICAR(l.serializar(buf, 10) == 0, "no detectó búfer chico");

  // Código de un solo uso, QR y nombre Bluetooth
  char pop[kLargoPop + 1], otro[kLargoPop + 1];
  generarPop(pop, azarDePrueba);
  generarPop(otro, azarDePrueba);
  bool alfabeto = strlen(pop) == (size_t)kLargoPop;
  for (const char* c = pop; *c; c++) alfabeto &= strchr(kAlfabetoPop, *c) != nullptr && !strchr("01OIL", *c);
  VERIFICAR(alfabeto && strcmp(pop, otro) != 0, "código de 8 sin 0/O/1/I/L y distinto cada vez: %s %s", pop, otro);
  char qr[48], codigo[33], popLeido[kLargoPop + 1];
  VERIFICAR(textoQrWifi("ESP32-001", pop, qr, sizeof qr) > 0 && strncmp(qr, "VTFW:ESP32-001:", 15) == 0, "QR: %s", qr);
  VERIFICAR(leerQrWifi(qr, codigo, popLeido) && strcmp(codigo, "ESP32-001") == 0 && strcmp(popLeido, pop) == 0,
            "lee el QR de configuración");
  VERIFICAR(!leerQrWifi("VTF1:ESP32-001:K7P3QX9M", codigo, popLeido) && !leerQrWifi("VTFW:ESP32-001:abc", codigo, popLeido) &&
                !leerQrWifi("VTFW::K7P3QX9M", codigo, popLeido),
            "no confunde el QR de vinculación ni acepta uno incompleto");
  uint8_t q[kQrTamBuffer];
  const int lado = generarQr(qr, q);
  VERIFICAR(lado > 0 && 2 * lado <= 64, "el QR de configuración entra en la OLED: %d módulos", lado);
  char nombre[kLargoNombreBle + 1];
  nombreBle("ESP32-001", nombre);
  VERIFICAR(strcmp(nombre, "VTF-ESP32-001") == 0, "nombre Bluetooth: %s", nombre);
  nombreBle("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", nombre);
  VERIFICAR(strlen(nombre) == (size_t)kLargoNombreBle && strncmp(nombre, "VTF-ABCDEFGHIJKLMNOP", 20) == 0,
            "nombre Bluetooth recortado a 20: %s", nombre);
}

int main() {
  pruebasSesion();
  pruebasTemperatura();
  pruebasPpg();
  pruebasEcg();
  pruebasCaidas();
  pruebasRespiracion();
  pruebasTelemetria();
  pruebasMuestreoSinRed();
  pruebasBateria();
  pruebasRedes();
  pruebasFrCardiaca();
  pruebasVinculacion();
  printf("\n%d de %d verificaciones pasaron\n", total - fallas, total);
  return fallas == 0 ? 0 : 1;
}
