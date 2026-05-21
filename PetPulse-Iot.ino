/**
 * =========================================
 *  PetPulse - Coleira Inteligente IoT
 *  Disciplina: Disruptive Architectures
 *  Challenge FIAP 2026 - CLYVO VET
 * =========================================
 *
 *
 * Sensores:
 *   - MPU6050 (I2C)             → aceleração / nível de atividade
 *   - Potenciômetro 1 (GPIO 34) → frequência cardíaca (bpm)
 *   - Potenciômetro 2 (GPIO 35) → pressão sistólica (mmHg)
 *
 * Rotas:
 *   GET /          → dashboard HTML completo
 *   GET /api/dados → JSON com todas as leituras
 *
 * LEDs:
 *   GPIO 18 → verde   (OK)
 *   GPIO 19 → amarelo (ALERTA)
 *   GPIO 21 → vermelho (CRITICO)
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <ArduinoJson.h>

#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

WebServer server(80);
MPU6050 mpu(Wire);

// =========================
// PINOS
// =========================
const int PIN_BPM     = 34;
const int PIN_PRESSAO = 35;
const int LED_OK      = 18;
const int LED_ALERTA  = 19;
const int LED_CRITICO = 5;

// =========================
// IDENTIFICAÇÃO
// =========================
const char* ID_DISPOSITIVO = "COLLAR-001";
const char* NOME_PET       = "Rex";

// =========================
// PARÂMETROS NORMAIS (cão adulto)
// =========================
const int   BPM_MIN      = 60,  BPM_MAX      = 140, BPM_CRIT      = 180;
const int   PRES_MIN     = 100, PRES_MAX      = 160, PRES_CRIT     = 200;
const float ACCEL_REP    = 1.5, ACCEL_CAM     = 4.0;

// =========================
// ESTADO
// =========================
enum Estado { ESTADO_OK, ESTADO_ALERTA, ESTADO_CRITICO };
Estado estadoAtual = ESTADO_OK;

// =========================
// LEITURAS GLOBAIS
// =========================
int    g_bpm = 0, g_pSis = 0, g_pDias = 0, g_ativ = 0;
float  g_accel = 0.0;
String g_descAtiv = "repouso";
String g_alerta   = "";
unsigned long g_uptime = 0;

unsigned long lastRead = 0;
const unsigned long INTERVALO = 2000;

// =========================
// LEITURA DOS SENSORES
// =========================
int lerBPM() {
  return map(analogRead(PIN_BPM), 0, 4095, 40, 200);
}

int lerPressao() {
  return map(analogRead(PIN_PRESSAO), 0, 4095, 80, 220);
}

float lerAceleracao() {
  mpu.update();
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();
  return sqrt(ax*ax + ay*ay + (az - 1.0)*(az - 1.0));
}

void classificarAtividade(float a) {
  if (a < ACCEL_REP) {
    g_ativ    = map((int)(a*100), 0, (int)(ACCEL_REP*100), 0, 30);
    g_descAtiv = "repouso";
  } else if (a < ACCEL_CAM) {
    g_ativ    = map((int)(a*100), (int)(ACCEL_REP*100), (int)(ACCEL_CAM*100), 31, 60);
    g_descAtiv = "caminhada";
  } else {
    g_ativ    = min(100, (int)map((int)(a*100), (int)(ACCEL_CAM*100), 1000, 61, 100));
    g_descAtiv = "corrida";
  }
}

Estado avaliarEstado(int bpm, int pres) {
  if (bpm > BPM_CRIT  || pres > PRES_CRIT)  return ESTADO_CRITICO;
  if (bpm < BPM_MIN   || bpm > BPM_MAX ||
      pres < PRES_MIN || pres > PRES_MAX)    return ESTADO_ALERTA;
  return ESTADO_OK;
}

String motivoAlerta(int bpm, int pres) {
  if (bpm > BPM_CRIT)   return "Frequencia cardiaca critica";
  if (bpm > BPM_MAX)    return "Frequencia cardiaca elevada";
  if (bpm < BPM_MIN)    return "Frequencia cardiaca baixa";
  if (pres > PRES_CRIT) return "Pressao arterial critica";
  if (pres > PRES_MAX)  return "Pressao arterial elevada";
  if (pres < PRES_MIN)  return "Pressao arterial baixa";
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
  g_bpm    = lerBPM();
  g_pSis   = lerPressao();
  g_pDias  = (int)(g_pSis * 0.62);
  g_accel  = lerAceleracao();
  g_uptime = millis() / 1000;

  classificarAtividade(g_accel);

  estadoAtual = avaliarEstado(g_bpm, g_pSis);
  g_alerta    = motivoAlerta(g_bpm, g_pSis);

  atualizarLEDs();

  Serial.printf("[LEITURA] BPM:%d | Pressao:%d/%d | Accel:%.2f | Ativ:%s (%d%%) | Estado:%s\n",
    g_bpm, g_pSis, g_pDias, g_accel,
    g_descAtiv.c_str(), g_ativ, estadoStr().c_str());
}

// =========================
// ROTA: GET /api/dados
// Retorna JSON com leituras atuais
// =========================
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
  json += "\"estado\":\"" + estadoStr() + "\",";
  json += "\"alerta\":\"" + g_alerta + "\",";
  json += "\"uptime\":" + String(g_uptime);
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// =========================
// ROTA: GET /
// Serve o dashboard HTML
// O JS do dashboard faz GET /api/dados a cada 3s
// =========================
void handleRoot() {
  String page = R"(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PetPulse - Coleira IoT</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: sans-serif; background: #0d1117; color: #e6edf3; padding: 24px; }

    h1 { text-align: center; color: #58a6ff; font-size: 1.8em; margin-bottom: 4px; }
    .sub { text-align: center; color: #8b949e; font-size: 0.9em; margin-bottom: 24px; }

    .badge {
      display: block; width: fit-content; margin: 0 auto 24px;
      padding: 6px 28px; border-radius: 20px;
      font-weight: bold; font-size: 1em; color: #000;
      transition: background 0.4s;
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 16px;
      margin-bottom: 24px;
    }

    .card {
      background: #161b22;
      border: 1px solid #30363d;
      border-radius: 14px;
      padding: 24px 16px;
      text-align: center;
    }
    .card .label {
      font-size: 0.75em; color: #8b949e;
      text-transform: uppercase; letter-spacing: 1px;
      margin-bottom: 10px;
    }
    .card .value {
      font-size: 2.6em; font-weight: bold; color: #58a6ff;
      transition: color 0.4s;
    }
    .card .unit { font-size: 0.8em; color: #8b949e; margin-top: 4px; }

    .card.alerta  .value { color: #ffd600; }
    .card.critico .value { color: #ff5555; }

    .alerta-box {
      background: #1a1a2e;
      border: 1px solid #ff555544;
      border-radius: 10px;
      padding: 14px 20px;
      color: #ff9999;
      font-size: 0.9em;
      min-height: 44px;
      margin-bottom: 16px;
    }
    .alerta-box.vazio { color: #484f58; border-color: #30363d; }

    .footer {
      text-align: center; color: #484f58;
      font-size: 0.75em; margin-top: 16px;
    }
    .dot {
      display: inline-block; width: 8px; height: 8px;
      border-radius: 50%; background: #00c853;
      margin-right: 6px; animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0%, 100% { opacity: 1; } 50% { opacity: 0.3; }
    }
  </style>
</head>
<body>

  <h1>&#128062; PetPulse</h1>
  <p class="sub">Coleira Inteligente &mdash; Rex &mdash; FIAP Challenge 2026</p>

  <div class="badge" id="badge" style="background:#00c853">NORMAL</div>

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
  </div>

  <div class="alerta-box vazio" id="alerta-box">
    Nenhum alerta no momento.
  </div>

  <p class="footer">
    <span class="dot"></span>
    Atualiza a cada 3s &nbsp;&middot;&nbsp; Uptime: <span id="uptime">--</span>s
  </p>

  <script>
    const COR   = { OK: '#00c853', ALERTA: '#ffd600', CRITICO: '#ff5555' };
    const LABEL = { OK: 'NORMAL',  ALERTA: 'ALERTA',  CRITICO: 'CR\u00cdTICO' };

    async function atualizar() {
      try {
        const resp = await fetch('/api/dados');
        const d    = await resp.json();

        // Atualiza valores
        document.getElementById('bpm').textContent     = d.frequenciaCardiaca;
        document.getElementById('pressao').textContent = d.pressaoSistolica + '/' + d.pressaoDiastolica;
        document.getElementById('ativ').textContent    = d.nivelAtividade;
        document.getElementById('desc-ativ').textContent = d.descAtividade;
        document.getElementById('accel').textContent   = d.aceleracaoTotal;
        document.getElementById('uptime').textContent  = d.uptime;

        // Atualiza badge de estado
        const badge = document.getElementById('badge');
        badge.style.background = COR[d.estado]   || COR.OK;
        badge.textContent      = LABEL[d.estado] || LABEL.OK;

        // Atualiza cor dos cards
        ['bpm', 'pressao', 'ativ', 'accel'].forEach(id => {
          const card = document.getElementById('card-' + id);
          card.classList.remove('alerta', 'critico');
          if (d.estado === 'ALERTA')  card.classList.add('alerta');
          if (d.estado === 'CRITICO') card.classList.add('critico');
        });

        // Atualiza caixa de alerta
        const box = document.getElementById('alerta-box');
        if (d.alerta && d.alerta !== '') {
          box.textContent = '\u26A0\uFE0F ' + d.alerta +
            ' | BPM: ' + d.frequenciaCardiaca +
            ' | Press\u00e3o: ' + d.pressaoSistolica + 'mmHg';
          box.classList.remove('vazio');
        } else {
          box.textContent = 'Nenhum alerta no momento.';
          box.classList.add('vazio');
        }

      } catch (e) {
        console.error('Erro ao buscar dados:', e);
      }
    }

    // Busca imediatamente e depois a cada 3s
    atualizar();
    setInterval(atualizar, 3000);
  </script>

</body>
</html>
  )";

  server.send(200, "text/html", page);
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n===== PetPulse - Coleira IoT =====");

  pinMode(LED_OK,      OUTPUT);
  pinMode(LED_ALERTA,  OUTPUT);
  pinMode(LED_CRITICO, OUTPUT);

  // Boot: acende todos brevemente
  digitalWrite(LED_OK, HIGH); digitalWrite(LED_ALERTA, HIGH); digitalWrite(LED_CRITICO, HIGH);
  delay(400);
  digitalWrite(LED_OK, LOW);  digitalWrite(LED_ALERTA, LOW);  digitalWrite(LED_CRITICO, LOW);

  // MPU6050
  Wire.begin();
  byte st = mpu.begin();
  if (st == 0) {
    Serial.println("[MPU6050] OK — Calibrando...");
    delay(1000);
    mpu.calcOffsets(true, true);
    Serial.println("[MPU6050] Calibracao concluida!");
  } else {
    Serial.printf("[MPU6050] Erro (codigo %d)\n", st);
  }

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("[WiFi] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" Conectado!");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("[WiFi] Abra http://localhost:8280 no navegador!");

  // Rotas
  server.on("/",          handleRoot);
  server.on("/api/dados", handleApiDados);
  server.begin();

  Serial.println("[HTTP] Servidor iniciado na porta 80");
  Serial.println("[HTTP] Rotas: GET /  |  GET /api/dados\n");
}

// =========================
// LOOP
// =========================
void loop() {
  server.handleClient();
  delay(2);

  if (millis() - lastRead >= INTERVALO) {
    lastRead = millis();
    atualizarSensores();
  }
}