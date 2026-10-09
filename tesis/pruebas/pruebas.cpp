// pruebas.cpp — Pruebas de los algoritmos del wearable con señales sintéticas.
// Se ejecutan en el computador con ./ejecutar.sh (no necesitan el ESP32).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "caidas.h"
#include "ecg.h"
#include "ppg.h"
#include "senales.h"
#include "telemetria.h"
#include "sesion.h"
#include "temperatura.h"

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
  l.spo2 = 97;
  l.tempDecimas = 368;
  size_t n = construirJsonTelemetria("ESP32-001", l, buf, sizeof buf);
  printf("  %s\n", buf);
  VERIFICAR(n > 0 && strcmp(buf,
                            "{\"dispositivo\":\"ESP32-001\",\"ts\":\"2026-09-30T14:05:10.250Z\",\"seq\":18234,"
                            "\"fc\":82,\"spo2\":97,\"temp\":36.8,\"caida\":false}") == 0,
            "JSON completo: %s", buf);

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

int main() {
  pruebasSesion();
  pruebasTemperatura();
  pruebasPpg();
  pruebasEcg();
  pruebasCaidas();
  pruebasTelemetria();
  printf("\n%d de %d verificaciones pasaron\n", total - fallas, total);
  return fallas == 0 ? 0 : 1;
}
