/**
 * =========================================
 *  PetPulse - Coleira Inteligente IoT
 *  Disciplina: Disruptive Architectures
 *  Challenge FIAP 2026 - CLYVO VET
 * =========================================
 *
 * Sensores:
 *   - MPU6050 (I2C)             → aceleração / nível de atividade
 *   - DS18B20 (GPIO 4, 1-Wire)  → temperatura corporal (°C)
 *   - Potenciômetro 1 (GPIO 34) → frequência cardíaca (bpm)
 *   - Potenciômetro 2 (GPIO 35) → pressão sistólica (mmHg)
 *
 * Atuadores:
 *   - Buzzer passivo (GPIO 13)  → alertas sonoros estruturados
 *
 * Rotas:
 *   GET /          → dashboard HTML completo
 *   GET /api/dados → JSON com todas as leituras
 *
 * LEDs:
 *   GPIO 18 → verde   (OK)
 *   GPIO 19 → amarelo (ALERTA)
 *   GPIO 5  → vermelho (CRITICO)
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

LiquidCrystal_I2C ldc(0x27, 16, 2);
WebServer server(80);
MPU6050 mpu(Wire);

#define PIN_DS18B20 4
OneWire           oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

const int PIN_BPM     = 34;
const int PIN_PRESSAO = 35;
const int LED_OK      = 18;
const int LED_ALERTA  = 19;
const int LED_CRITICO = 5;
const int PIN_BUZZER  = 13;

#define BUZZER_CH   0
#define BUZZER_RES  8
#define FREQ_BOOT     1000
#define FREQ_ALERTA   1800
#define FREQ_SOS_DOT  2200
#define FREQ_SOS_DASH 1600

const char* ID_DISPOSITIVO = "COLLAR-001";
const char* NOME_PET       = "Rex";

const int   BPM_MIN   = 60,  BPM_MAX   = 140, BPM_CRIT  = 180;
const int   PRES_MIN  = 100, PRES_MAX  = 160, PRES_CRIT = 200;
const float ACCEL_REP = 1.5, ACCEL_CAM = 4.0;
const float TEMP_NORMAL_MIN = 37.5, TEMP_NORMAL_MAX = 39.2;
const float TEMP_FEBRE = 39.5, TEMP_HIPOTERMIA = 37.0;
const float TEMP_CRIT_ALTA = 40.5, TEMP_CRIT_BAIXA = 36.0;

enum Estado { ESTADO_OK, ESTADO_ALERTA, ESTADO_CRITICO };
Estado estadoAtual = ESTADO_OK, estadoAnterior = ESTADO_OK;

int    g_bpm = 0, g_pSis = 0, g_pDias = 0, g_ativ = 0;
float  g_accel = 0.0, g_temp = 38.5;
int    g_score = 100;  // score de saúde 0-100
String g_descAtiv = "repouso", g_alerta = "";
unsigned long g_uptime = 0;

unsigned long lastRead = 0, lastBipAlerta = 0, lastSosStep = 0;
const unsigned long INTERVALO = 2000, INTERVALO_ALERTA = 5000;

const int DIT = 120, DAH = 360, SYM_GAP = 120, CHAR_GAP = 360, SOS_GAP = 1200;
struct Tone { uint16_t freq; uint16_t dur; };
const Tone SOS_SEQ[] = {
  {FREQ_SOS_DOT,DIT},{0,SYM_GAP},{FREQ_SOS_DOT,DIT},{0,SYM_GAP},
  {FREQ_SOS_DOT,DIT},{0,CHAR_GAP},{FREQ_SOS_DASH,DAH},{0,SYM_GAP},
  {FREQ_SOS_DASH,DAH},{0,SYM_GAP},{FREQ_SOS_DASH,DAH},{0,CHAR_GAP},
  {FREQ_SOS_DOT,DIT},{0,SYM_GAP},{FREQ_SOS_DOT,DIT},{0,SYM_GAP},
  {FREQ_SOS_DOT,DIT},{0,SOS_GAP},
};
const int SOS_LEN = sizeof(SOS_SEQ)/sizeof(SOS_SEQ[0]);
int sosIndex = 0;

void buzzerTom(uint16_t f) { ledcWriteTone(PIN_BUZZER, f); }
void buzzerOff()           { ledcWriteTone(PIN_BUZZER, 0); }
void bipe(uint16_t f, int d) { buzzerTom(f); delay(d); buzzerOff(); }
void buzzerBoot() { bipe(800,80); delay(60); bipe(1200,80); delay(60); bipe(1600,120); }

int   lerBPM()     { return map(analogRead(PIN_BPM),     0, 4095, 40, 200); }
int   lerPressao() { return map(analogRead(PIN_PRESSAO), 0, 4095, 80, 220); }
float lerAceleracao() {
  mpu.update();
  float ax=mpu.getAccX(), ay=mpu.getAccY(), az=mpu.getAccZ();
  return sqrt(ax*ax + ay*ay + (az-1.0)*(az-1.0));
}
float lerTemperatura() {
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t < 30.0 || t > 45.0) return g_temp;
  return t;
}

void classificarAtividade(float a) {
  if (a < ACCEL_REP) {
    g_ativ = map((int)(a*100), 0, (int)(ACCEL_REP*100), 0, 30);
    g_descAtiv = "repouso";
  } else if (a < ACCEL_CAM) {
    g_ativ = map((int)(a*100), (int)(ACCEL_REP*100), (int)(ACCEL_CAM*100), 31, 60);
    g_descAtiv = "caminhada";
  } else {
    g_ativ = min(100, (int)map((int)(a*100), (int)(ACCEL_CAM*100), 1000, 61, 100));
    g_descAtiv = "corrida";
  }
}

// =========================
// SCORE DE SAÚDE (0-100)
// Penalidades por vital fora da faixa:
//   BPM:       peso 35 — desconta proporcional ao desvio
//   Pressão:   peso 30 — idem
//   Temp:      peso 25 — idem
//   Atividade: peso 10 — bônus se em movimento saudável
// =========================
int calcularScore(int bpm, int pres, float temp) {
  float score = 100.0;

  // ── BPM (peso 35) ──
  if (bpm > BPM_CRIT) {
    score -= 35.0;
  } else if (bpm > BPM_MAX) {
    score -= 35.0 * ((float)(bpm - BPM_MAX) / (float)(BPM_CRIT - BPM_MAX));
  } else if (bpm < BPM_MIN) {
    score -= 35.0 * ((float)(BPM_MIN - bpm) / (float)(BPM_MIN - 40));
  }

  // ── Pressão sistólica (peso 30) ──
  if (pres > PRES_CRIT) {
    score -= 30.0;
  } else if (pres > PRES_MAX) {
    score -= 30.0 * ((float)(pres - PRES_MAX) / (float)(PRES_CRIT - PRES_MAX));
  } else if (pres < PRES_MIN) {
    score -= 30.0 * ((float)(PRES_MIN - pres) / (float)(PRES_MIN - 80));
  }

  // ── Temperatura (peso 25) ──
  if (temp >= TEMP_CRIT_ALTA) {
    score -= 25.0;
  } else if (temp >= TEMP_FEBRE) {
    score -= 25.0 * ((temp - TEMP_FEBRE) / (TEMP_CRIT_ALTA - TEMP_FEBRE)) + 12.0;
  } else if (temp > TEMP_NORMAL_MAX) {
    score -= 12.0 * ((temp - TEMP_NORMAL_MAX) / (TEMP_FEBRE - TEMP_NORMAL_MAX));
  } else if (temp <= TEMP_CRIT_BAIXA) {
    score -= 25.0;
  } else if (temp < TEMP_NORMAL_MIN) {
    score -= 15.0 * ((TEMP_NORMAL_MIN - temp) / (TEMP_NORMAL_MIN - TEMP_CRIT_BAIXA));
  }

  // ── Atividade (peso 10) — bônus leve se caminhando ──
  if (g_ativ > 30 && g_ativ <= 60) score += 5.0;  // caminhada é positiva
  if (g_ativ > 60)                  score -= 5.0;  // corrida intensa desconta levemente

  // Febre em repouso penaliza extra
  if (temp >= TEMP_FEBRE && g_ativ <= 30) score -= 10.0;

  return (int)constrain(score, 0.0, 100.0);
}

Estado avaliarEstado(int bpm, int pres, float temp) {
  bool febreEmRepouso = (temp >= TEMP_FEBRE) && (g_ativ <= 30);
  if (bpm > BPM_CRIT || pres > PRES_CRIT ||
      temp >= TEMP_CRIT_ALTA || temp <= TEMP_CRIT_BAIXA ||
      febreEmRepouso) return ESTADO_CRITICO;
  if (bpm < BPM_MIN || bpm > BPM_MAX || pres < PRES_MIN || pres > PRES_MAX ||
      temp < TEMP_NORMAL_MIN || temp > TEMP_NORMAL_MAX) return ESTADO_ALERTA;
  return ESTADO_OK;
}

String motivoAlerta(int bpm, int pres, float temp) {
  if (bpm > BPM_CRIT)                                   return "Frequencia cardiaca critica";
  if (bpm > BPM_MAX)                                    return "Frequencia cardiaca elevada";
  if (bpm < BPM_MIN)                                    return "Frequencia cardiaca baixa";
  if (pres > PRES_CRIT)                                 return "Pressao arterial critica";
  if (pres > PRES_MAX)                                  return "Pressao arterial elevada";
  if (pres < PRES_MIN)                                  return "Pressao arterial baixa";
  if (temp >= TEMP_CRIT_ALTA)                           return "Hipertermia critica";
  if (temp <= TEMP_CRIT_BAIXA)                          return "Hipotermia critica";
  if (temp >= TEMP_FEBRE && g_ativ <= 30)               return "Febre em repouso - verificar imediatamente";
  if (temp > TEMP_NORMAL_MAX)                           return "Temperatura elevada";
  if (temp < TEMP_NORMAL_MIN && temp > TEMP_CRIT_BAIXA) return "Hipotermia leve";
  return "";
}

String estadoStr() {
  if (estadoAtual == ESTADO_ALERTA)  return "ALERTA";
  if (estadoAtual == ESTADO_CRITICO) return "CRITICO";
  return "OK";
}

void atualizarLEDs() {
  digitalWrite(LED_OK,      estadoAtual == ESTADO_OK      ? HIGH : LOW);
  digitalWrite(LED_ALERTA,  estadoAtual == ESTADO_ALERTA  ? HIGH : LOW);
  digitalWrite(LED_CRITICO, estadoAtual == ESTADO_CRITICO ? HIGH : LOW);
}

void atualizarSensores() {
  g_bpm   = lerBPM();
  g_pSis  = lerPressao();
  g_pDias = (int)(g_pSis * 0.62);
  g_accel = lerAceleracao();
  g_uptime = millis() / 1000;
  classificarAtividade(g_accel);
  estadoAnterior = estadoAtual;
  g_temp      = lerTemperatura();
  estadoAtual = avaliarEstado(g_bpm, g_pSis, g_temp);
  g_alerta    = motivoAlerta(g_bpm, g_pSis, g_temp);
  g_score     = calcularScore(g_bpm, g_pSis, g_temp);
  atualizarLEDs();
  if (estadoAtual == ESTADO_CRITICO && estadoAnterior != ESTADO_CRITICO) {
    sosIndex = 0; lastSosStep = millis();
    Serial.println("[BUZZER] Iniciando S.O.S sonoro!");
  }
  if (estadoAtual != ESTADO_CRITICO && estadoAnterior == ESTADO_CRITICO) {
    buzzerOff(); Serial.println("[BUZZER] S.O.S encerrado.");
  }
  Serial.printf("[LEITURA] BPM:%d | Pressao:%d/%d | Temp:%.1fC | Accel:%.2f | Ativ:%s (%d%%) | Score:%d | Estado:%s\n",
    g_bpm, g_pSis, g_pDias, g_temp, g_accel,
    g_descAtiv.c_str(), g_ativ, g_score, estadoStr().c_str());
}

void loopBuzzer() {
  unsigned long agora = millis();
  if (estadoAtual == ESTADO_CRITICO) {
    if (agora - lastSosStep >= SOS_SEQ[sosIndex].dur) {
      lastSosStep = agora;
      sosIndex = (sosIndex + 1) % SOS_LEN;
      buzzerTom(SOS_SEQ[sosIndex].freq);
    }
  } else if (estadoAtual == ESTADO_ALERTA) {
    if (agora - lastBipAlerta >= INTERVALO_ALERTA) {
      lastBipAlerta = agora;
      bipe(FREQ_ALERTA, 100); delay(100); bipe(FREQ_ALERTA, 100);
    }
  } else {
    buzzerOff();
  }
}

void handleApiDados() {
  String json = "{";
  json += "\"idDispositivo\":\"" + String(ID_DISPOSITIVO) + "\",";
  json += "\"pet\":\"" + String(NOME_PET) + "\",";
  json += "\"frequenciaCardiaca\":" + String(g_bpm) + ",";
  json += "\"nivelAtividade\":" + String(g_ativ) + ",";
  json += "\"descAtividade\":\"" + g_descAtiv + "\",";
  json += "\"aceleracaoTotal\":" + String(g_accel, 2) + ",";
  json += "\"pressaoSistolica\":" + String(g_pSis) + ",";
  json += "\"pressaoDiastolica\":" + String(g_pDias) + ",";
  json += "\"temperatura\":" + String(g_temp, 1) + ",";
  json += "\"score\":" + String(g_score) + ",";
  json += "\"estado\":\"" + estadoStr() + "\",";
  json += "\"alerta\":\"" + g_alerta + "\",";
  json += "\"uptime\":" + String(g_uptime);
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleRoot() {
  String page = R"(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PetPulse - Coleira IoT</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: sans-serif; background: #0d1117; color: #e6edf3; padding: 24px; }
    h1 { text-align: center; color: #58a6ff; font-size: 1.8em; margin-bottom: 4px; }
    .sub { text-align: center; color: #8b949e; font-size: 0.9em; margin-bottom: 24px; }
    .badge {
      display: block; width: fit-content; margin: 0 auto 24px;
      padding: 6px 28px; border-radius: 20px;
      font-weight: bold; font-size: 1em; color: #000; transition: background 0.4s;
    }

    /* Layout principal: cards + gauge lado a lado */
    .top-row { display: grid; grid-template-columns: 1fr auto; gap: 16px; margin-bottom: 24px; align-items: start; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(170px, 1fr));
      gap: 16px;
    }
    .card {
      background: #161b22; border: 1px solid #30363d;
      border-radius: 14px; padding: 20px 14px; text-align: center;
      transition: border-color 0.4s;
    }
    .card .label { font-size: 0.72em; color: #8b949e; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 10px; }
    .card .value { font-size: 2.4em; font-weight: bold; color: #58a6ff; transition: color 0.4s; }
    .card .unit  { font-size: 0.8em; color: #8b949e; margin-top: 4px; }
    .card.alerta  { border-color: #ffd600; } .card.alerta  .value { color: #ffd600; }
    .card.critico { border-color: #ff5555; } .card.critico .value { color: #ff5555; }
    .temp-bar-wrap { margin-top: 10px; background: #0d1117; border-radius: 6px; height: 6px; overflow: hidden; }
    .temp-bar { height: 6px; border-radius: 6px; width: 0%; transition: width 0.6s, background 0.4s; }

    /* Gauge de score */
    .score-panel {
      background: #161b22; border: 1px solid #30363d;
      border-radius: 14px; padding: 20px 16px; text-align: center;
      width: 200px; flex-shrink: 0;
    }
    .score-label { font-size: 0.72em; color: #8b949e; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 12px; }
    .gauge-wrap { position: relative; width: 160px; height: 90px; margin: 0 auto 8px; }
    .gauge-wrap canvas { width: 160px !important; height: 160px !important; margin-top: -70px; }
    .score-num {
      font-size: 2.2em; font-weight: bold; color: #58a6ff;
      transition: color 0.4s; line-height: 1;
    }
    .score-desc { font-size: 0.75em; color: #8b949e; margin-top: 4px; }

    .alerta-box {
      background: #1a1a2e; border: 1px solid #ff555544;
      border-radius: 10px; padding: 14px 20px;
      color: #ff9999; font-size: 0.9em; min-height: 44px; margin-bottom: 16px; transition: all 0.4s;
    }
    .alerta-box.vazio { color: #484f58; border-color: #30363d; }

    .chart-panel {
      background: #161b22; border: 1px solid #30363d;
      border-radius: 14px; padding: 20px; margin-bottom: 16px;
    }
    .chart-title { font-size: 0.75em; color: #8b949e; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 16px; }
    .chart-wrap  { position: relative; height: 220px; }

    .buzzer-panel {
      background: #161b22; border: 1px solid #30363d;
      border-radius: 10px; padding: 16px 20px; margin-bottom: 16px;
      display: flex; align-items: center; gap: 16px; flex-wrap: wrap;
    }
    .buzzer-icon { font-size: 1.6em; transition: all 0.3s; filter: grayscale(1) opacity(0.3); }
    .buzzer-icon.ativo { filter: none; animation: buzz 0.3s infinite alternate; }
    @keyframes buzz { from { transform: rotate(-4deg); } to { transform: rotate(4deg); } }
    .buzzer-info { flex: 1; font-size: 0.82em; color: #8b949e; line-height: 1.8; }
    .buzzer-info span { color: #e6edf3; font-weight: bold; }
    .buzzer-estado {
      font-size: 0.78em; font-weight: bold; padding: 4px 12px;
      border-radius: 12px; background: #0d1117; color: #484f58;
      border: 1px solid #30363d; transition: all 0.4s;
    }
    .buzzer-estado.alerta  { color: #ffd600; border-color: #ffd600; background: #1a1600; }
    .buzzer-estado.critico { color: #ff5555; border-color: #ff5555; background: #1a0000; animation: sosPulse 1s infinite; }
    @keyframes sosPulse { 0%,100%{opacity:1} 50%{opacity:0.4} }
    .footer { text-align: center; color: #484f58; font-size: 0.75em; margin-top: 16px; }
    .dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%;
      background: #00c853; margin-right: 6px; animation: pulse 1.5s infinite; }
    @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.3} }

    @media (max-width: 640px) {
      .top-row { grid-template-columns: 1fr; }
      .score-panel { width: 100%; }
    }
  </style>
</head>
<body>
  <h1>&#128062; PetPulse</h1>
  <p class="sub">Coleira Inteligente &mdash; Rex &mdash; FIAP Challenge 2026</p>
  <div class="badge" id="badge" style="background:#00c853">NORMAL</div>

  <div class="top-row">
    <!-- Cards de vitais -->
    <div class="grid">
      <div class="card" id="card-bpm">
        <div class="label">&#10084; Freq. Card&iacute;aca</div>
        <div class="value" id="bpm">--</div>
        <div class="unit">bpm</div>
      </div>
      <div class="card" id="card-pressao">
        <div class="label">&#128137; Press&atilde;o</div>
        <div class="value" id="pressao">--</div>
        <div class="unit">mmHg</div>
      </div>
      <div class="card" id="card-ativ">
        <div class="label">&#128062; Atividade</div>
        <div class="value" id="ativ">--</div>
        <div class="unit" id="desc-ativ">aguardando...</div>
      </div>
      <div class="card" id="card-accel">
        <div class="label">&#128225; Acelera&ccedil;&atilde;o</div>
        <div class="value" id="accel">--</div>
        <div class="unit">m/s&sup2;</div>
      </div>
      <div class="card" id="card-temp">
        <div class="label">&#127777; Temperatura</div>
        <div class="value" id="temp">--</div>
        <div class="unit">&deg;C</div>
        <div class="temp-bar-wrap"><div class="temp-bar" id="temp-bar"></div></div>
      </div>
    </div>

    <!-- Gauge de score -->
    <div class="score-panel">
      <div class="score-label">&#129657; Score de Sa&uacute;de</div>
      <div class="gauge-wrap">
        <canvas id="gaugeChart"></canvas>
      </div>
      <div class="score-num" id="score-num">--</div>
      <div class="score-desc" id="score-desc">calculando...</div>
    </div>
  </div>

  <div class="alerta-box vazio" id="alerta-box">Nenhum alerta no momento.</div>

  <div class="chart-panel">
    <div class="chart-title">&#128200; Hist&oacute;rico de Vitais &mdash; &uacute;ltimas leituras</div>
    <div class="chart-wrap">
      <canvas id="chartVitais"></canvas>
    </div>
  </div>

  <div class="buzzer-panel">
    <div class="buzzer-icon" id="buzzer-icon">&#128276;</div>
    <div class="buzzer-info">
      <span>Buzzer GPIO 13</span> &nbsp;&mdash;&nbsp;
      Boot: 3 bipes &middot; ALERTA: 2 bipes/5s &middot; CR&Iacute;TICO: S.O.S morse<br>
      &#127777; Febre em repouso (&ge;39.5&deg;C + MPU baixo) &rarr; alerta cr&iacute;tico imediato
    </div>
    <div class="buzzer-estado" id="buzzer-estado">SILENCIOSO</div>
  </div>

  <p class="footer">
    <span class="dot"></span>
    Atualiza a cada 3s &nbsp;&middot;&nbsp; Uptime: <span id="uptime">--</span>s
  </p>

  <script>
    const COR   = { OK: '#00c853', ALERTA: '#ffd600', CRITICO: '#ff5555' };
    const LABEL = { OK: 'NORMAL',  ALERTA: 'ALERTA',  CRITICO: 'CR\u00cdTICO' };
    const MAX_PONTOS = 20;
    const hist = { labels: [], bpm: [], pSis: [], temp: [] };

    function agora() {
      const d = new Date();
      return d.getHours().toString().padStart(2,'0') + ':' +
             d.getMinutes().toString().padStart(2,'0') + ':' +
             d.getSeconds().toString().padStart(2,'0');
    }
    function pushPonto(bpm, pSis, temp) {
      hist.labels.push(agora()); hist.bpm.push(bpm);
      hist.pSis.push(pSis);     hist.temp.push(temp);
      if (hist.labels.length > MAX_PONTOS) {
        hist.labels.shift(); hist.bpm.shift(); hist.pSis.shift(); hist.temp.shift();
      }
    }

    // ── Gauge (doughnut semicircular) ──
    const gCtx = document.getElementById('gaugeChart').getContext('2d');
    const gaugeChart = new Chart(gCtx, {
      type: 'doughnut',
      data: {
        datasets: [{
          data: [100, 0, 100],
          backgroundColor: ['#00c853', '#0d1117', '#21262d'],
          borderWidth: 0,
          circumference: 180,
          rotation: 270
        }]
      },
      options: {
        responsive: false,
        cutout: '72%',
        plugins: { legend: { display: false }, tooltip: { enabled: false } },
        animation: { duration: 600 }
      }
    });

    function scoreCor(s) {
      if (s >= 80) return '#00c853';
      if (s >= 60) return '#7ee787';
      if (s >= 40) return '#ffd600';
      if (s >= 20) return '#ff9900';
      return '#ff5555';
    }
    function scoreDesc(s) {
      if (s >= 80) return 'Excelente';
      if (s >= 60) return 'Bom';
      if (s >= 40) return 'Aten\u00e7\u00e3o';
      if (s >= 20) return 'Ruim';
      return 'Cr\u00edtico';
    }
    function atualizarGauge(score) {
      const cor = scoreCor(score);
      gaugeChart.data.datasets[0].data = [score, 0, 100 - score];
      gaugeChart.data.datasets[0].backgroundColor[0] = cor;
      gaugeChart.update();
      const el = document.getElementById('score-num');
      el.textContent = score;
      el.style.color = cor;
      document.getElementById('score-desc').textContent = scoreDesc(score);
    }

    // ── Gráfico de vitais ──
    const ctx = document.getElementById('chartVitais').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: hist.labels,
        datasets: [
          { label: 'BPM', data: hist.bpm, borderColor: '#58a6ff',
            backgroundColor: 'rgba(88,166,255,0.08)', borderWidth: 2, pointRadius: 3, tension: 0.4, yAxisID: 'yBpm' },
          { label: 'Press\u00e3o Sist\u00f3lica (mmHg)', data: hist.pSis, borderColor: '#bc8cff',
            backgroundColor: 'rgba(188,140,255,0.08)', borderWidth: 2, pointRadius: 3, tension: 0.4, yAxisID: 'yBpm' },
          { label: 'Temperatura (\u00b0C)', data: hist.temp, borderColor: '#ff9966',
            backgroundColor: 'rgba(255,153,102,0.08)', borderWidth: 2, pointRadius: 3, tension: 0.4, yAxisID: 'yTemp' }
        ]
      },
      options: {
        responsive: true, maintainAspectRatio: false,
        animation: { duration: 400 },
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { labels: { color: '#8b949e', font: { size: 11 }, boxWidth: 14 } },
          tooltip: { backgroundColor: '#1c2128', borderColor: '#30363d', borderWidth: 1,
            titleColor: '#e6edf3', bodyColor: '#8b949e' }
        },
        scales: {
          x: { ticks: { color: '#484f58', font: { size: 10 }, maxTicksLimit: 8 }, grid: { color: '#21262d' } },
          yBpm: { type: 'linear', position: 'left', min: 40, max: 220,
            ticks: { color: '#8b949e', font: { size: 10 } }, grid: { color: '#21262d' },
            title: { display: true, text: 'BPM / mmHg', color: '#484f58', font: { size: 10 } } },
          yTemp: { type: 'linear', position: 'right', min: 35, max: 42,
            ticks: { color: '#ff9966', font: { size: 10 } }, grid: { drawOnChartArea: false },
            title: { display: true, text: '\u00b0C', color: '#ff9966', font: { size: 10 } } }
        }
      }
    });

    function tempParaPct(t) { return Math.min(100, Math.max(0, ((t - 36) / 5) * 100)); }
    function tempCor(t) {
      if (t >= 40.5) return '#ff5555';
      if (t >= 39.5) return '#ff7700';
      if (t >= 39.2) return '#ffd600';
      if (t < 37.0)  return '#58a6ff';
      return '#00c853';
    }

    async function atualizar() {
      try {
        const d = await (await fetch('/api/dados')).json();

        document.getElementById('bpm').textContent       = d.frequenciaCardiaca;
        document.getElementById('pressao').textContent   = d.pressaoSistolica + '/' + d.pressaoDiastolica;
        document.getElementById('ativ').textContent      = d.nivelAtividade;
        document.getElementById('desc-ativ').textContent = d.descAtividade;
        document.getElementById('accel').textContent     = d.aceleracaoTotal;
        document.getElementById('temp').textContent      = d.temperatura;
        document.getElementById('uptime').textContent    = d.uptime;

        const bar = document.getElementById('temp-bar');
        bar.style.width = tempParaPct(d.temperatura) + '%';
        bar.style.background = tempCor(d.temperatura);

        const badge = document.getElementById('badge');
        badge.style.background = COR[d.estado] || COR.OK;
        badge.textContent = LABEL[d.estado] || LABEL.OK;

        ['bpm','pressao','ativ','accel','temp'].forEach(id => {
          const c = document.getElementById('card-' + id);
          c.classList.remove('alerta','critico');
          if (d.estado === 'ALERTA')  c.classList.add('alerta');
          if (d.estado === 'CRITICO') c.classList.add('critico');
        });

        const box = document.getElementById('alerta-box');
        if (d.alerta && d.alerta !== '') {
          box.textContent = '\u26A0\uFE0F ' + d.alerta +
            ' | BPM: ' + d.frequenciaCardiaca +
            ' | Press\u00e3o: ' + d.pressaoSistolica + 'mmHg' +
            ' | Temp: ' + d.temperatura + '\u00b0C';
          box.classList.remove('vazio');
        } else {
          box.textContent = 'Nenhum alerta no momento.';
          box.classList.add('vazio');
        }

        const icon   = document.getElementById('buzzer-icon');
        const estado = document.getElementById('buzzer-estado');
        estado.classList.remove('alerta','critico');
        if (d.estado === 'CRITICO') {
          icon.classList.add('ativo'); estado.textContent = 'S.O.S ATIVO'; estado.classList.add('critico');
        } else if (d.estado === 'ALERTA') {
          icon.classList.add('ativo'); estado.textContent = '2 BIPES / 5s'; estado.classList.add('alerta');
        } else {
          icon.classList.remove('ativo'); estado.textContent = 'SILENCIOSO';
        }

        // Gauge de score
        atualizarGauge(d.score);

        // Gráfico de vitais
        pushPonto(d.frequenciaCardiaca, d.pressaoSistolica, parseFloat(d.temperatura));
        chart.data.labels = hist.labels;
        chart.data.datasets[0].data = hist.bpm;
        chart.data.datasets[1].data = hist.pSis;
        chart.data.datasets[2].data = hist.temp;
        chart.update();

      } catch(e) { console.error('Erro ao buscar dados:', e); }
    }

    atualizar();
    setInterval(atualizar, 3000);
  </script>
</body>
</html>
  )";
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n===== PetPulse - Coleira IoT =====");

  ldc.init(); ldc.backlight();
  ldc.setCursor(0, 0); ldc.print("PetPulse Boot...");

  pinMode(LED_OK, OUTPUT); pinMode(LED_ALERTA, OUTPUT); pinMode(LED_CRITICO, OUTPUT);
  digitalWrite(LED_OK, HIGH); digitalWrite(LED_ALERTA, HIGH); digitalWrite(LED_CRITICO, HIGH);

  ledcAttach(PIN_BUZZER, 1000, BUZZER_RES);
  buzzerBoot();

  delay(400);
  digitalWrite(LED_OK, LOW); digitalWrite(LED_ALERTA, LOW); digitalWrite(LED_CRITICO, LOW);

  ds18b20.begin();
  Serial.println("[DS18B20] Iniciado!");

  Wire.begin();
  byte st = mpu.begin();
  if (st == 0) {
    Serial.println("[MPU6050] Calibrando...");
    ldc.setCursor(0, 1); ldc.print("MPU6050 OK      ");
    delay(1000); mpu.calcOffsets(true, true);
    Serial.println("[MPU6050] OK!");
  } else {
    Serial.printf("[MPU6050] Erro %d\n", st);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("[WiFi] Conectando");
  ldc.setCursor(0, 1); ldc.print("WiFi...         ");
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
  Serial.println(" OK!");
  Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());

  ldc.clear();
  ldc.setCursor(0, 0); ldc.print("IP:");
  ldc.setCursor(0, 1); ldc.print(WiFi.localIP());

  server.on("/",          handleRoot);
  server.on("/api/dados", handleApiDados);
  server.begin();
  Serial.println("[HTTP] Porta 80 pronta!\n");
}

void loop() {
  server.handleClient();
  loopBuzzer();

  if (millis() - lastRead >= INTERVALO) {
    lastRead = millis();
    atualizarSensores();
    ldc.clear();
    ldc.setCursor(0, 0);
    ldc.print("BPM:" + String(g_bpm) + " S:" + String(g_score));
    ldc.setCursor(0, 1);
    ldc.print(estadoStr() + " " + g_descAtiv);
  }
}