/**
 * =========================================
 *  PetPulse - Coleira Inteligente IoT
 *  Disciplina: Disruptive Architectures
 *  Challenge FIAP 2026 - CLYVO VET
 * =========================================
 *
 * Sensores:
 *   - MPU6050 (acelerômetro + giroscópio) → nível de atividade
 *   - Potenciômetro 1 (GPIO 34)           → frequência cardíaca (bpm)
 *   - Potenciômetro 2 (GPIO 35)           → pressão sistólica (mmHg)
 *
 * Protocolo: HTTP (WebServer na porta 80)
 * Rotas:
 *   GET /        → página HTML com dashboard embutido (atualiza a cada 3s)
 *   GET /dados   → retorna JSON com todas as leituras
 *   GET /status  → retorna JSON com estado do dispositivo
 *
 * LEDs:
 *   GPIO 18 → verde   (ESTADO_OK)
 *   GPIO 19 → amarelo (ESTADO_ALERTA)
 *   GPIO 21 → vermelho (ESTADO_CRITICO)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <ArduinoJson.h>

// =========================
// CONFIGURACAO DO WIFI
// =========================
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

// =========================
// SERVIDOR HTTP
// =========================
WebServer server(80);

// =========================
// MPU6050
// =========================
MPU6050 mpu(Wire);

// =========================
// PINOS
// =========================
const int PIN_SENSOR_BPM     = 34;
const int PIN_SENSOR_PRESSAO = 35;

const int LED_OK      = 18;
const int LED_ALERTA  = 19;
const int LED_CRITICO = 21;

// =========================
// IDENTIFICAÇÃO DO DISPOSITIVO
// =========================
const char* ID_DISPOSITIVO = "COLLAR-001";
const char* NOME_PET       = "Rex";

// =========================
// PARÂMETROS NORMAIS (cão adulto)
// =========================
const int   BPM_MIN_NORMAL      = 60;
const int   BPM_MAX_NORMAL      = 140;
const int   BPM_MAX_CRITICO     = 180;
const int   PRESSAO_MIN_NORMAL  = 100;
const int   PRESSAO_MAX_NORMAL  = 160;
const int   PRESSAO_MAX_CRITICO = 200;
const float ACCEL_REPOUSO       = 1.5;
const float ACCEL_CAMINHADA     = 4.0;

// =========================
// ESTADO DO SISTEMA
// =========================
enum EstadoAlerta { ESTADO_OK, ESTADO_ALERTA, ESTADO_CRITICO };
EstadoAlerta estadoAtual = ESTADO_OK;

// =========================
// LEITURAS GLOBAIS
// =========================
int    g_bpm          = 0;
int    g_pressaoSis   = 0;
int    g_pressaoDias  = 0;
float  g_accelTotal   = 0.0;
int    g_atividade    = 0;
String g_descAtiv     = "repouso";
String g_alertaMotivo = "";
unsigned long g_uptime = 0;

unsigned long lastReadTime = 0;
const unsigned long INTERVALO_LEITURA = 2000;

// =========================
// FUNÇÕES DE LEITURA
// =========================

int lerBPM() {
  return map(analogRead(PIN_SENSOR_BPM), 0, 4095, 40, 200);
}

int lerPressao() {
  return map(analogRead(PIN_SENSOR_PRESSAO), 0, 4095, 80, 220);
}

float lerAceleracaoTotal() {
  mpu.update();
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();
  return sqrt(ax*ax + ay*ay + (az - 1.0)*(az - 1.0));
}

void classificarAtividade(float accel) {
  if (accel < ACCEL_REPOUSO) {
    g_atividade = map((int)(accel * 100), 0, (int)(ACCEL_REPOUSO * 100), 0, 30);
    g_descAtiv  = "repouso";
  } else if (accel < ACCEL_CAMINHADA) {
    g_atividade = map((int)(accel * 100), (int)(ACCEL_REPOUSO * 100), (int)(ACCEL_CAMINHADA * 100), 31, 60);
    g_descAtiv  = "caminhada";
  } else {
    g_atividade = min(100, (int)map((int)(accel * 100), (int)(ACCEL_CAMINHADA * 100), 1000, 61, 100));
    g_descAtiv  = "corrida";
  }
}

// =========================
// LÓGICA DE ALERTA
// =========================

EstadoAlerta avaliarEstado(int bpm, int pressao) {
  if (bpm > BPM_MAX_CRITICO || pressao > PRESSAO_MAX_CRITICO) return ESTADO_CRITICO;
  if (bpm < BPM_MIN_NORMAL  || bpm > BPM_MAX_NORMAL ||
      pressao < PRESSAO_MIN_NORMAL || pressao > PRESSAO_MAX_NORMAL) return ESTADO_ALERTA;
  return ESTADO_OK;
}

String motivoAlerta(int bpm, int pressao) {
  if (bpm > BPM_MAX_CRITICO)        return "Frequencia cardiaca critica";
  if (bpm > BPM_MAX_NORMAL)         return "Frequencia cardiaca elevada";
  if (bpm < BPM_MIN_NORMAL)         return "Frequencia cardiaca baixa";
  if (pressao > PRESSAO_MAX_CRITICO) return "Pressao arterial critica";
  if (pressao > PRESSAO_MAX_NORMAL)  return "Pressao arterial elevada";
  if (pressao < PRESSAO_MIN_NORMAL)  return "Pressao arterial baixa";
  return "";
}

void atualizarLEDs(EstadoAlerta estado) {
  digitalWrite(LED_OK,      estado == ESTADO_OK      ? HIGH : LOW);
  digitalWrite(LED_ALERTA,  estado == ESTADO_ALERTA  ? HIGH : LOW);
  digitalWrite(LED_CRITICO, estado == ESTADO_CRITICO ? HIGH : LOW);
}

void atualizarLeituras() {
  g_bpm         = lerBPM();
  g_pressaoSis  = lerPressao();
  g_pressaoDias = (int)(g_pressaoSis * 0.62);
  g_accelTotal  = lerAceleracaoTotal();
  g_uptime      = millis() / 1000;

  classificarAtividade(g_accelTotal);

  estadoAtual    = avaliarEstado(g_bpm, g_pressaoSis);
  g_alertaMotivo = motivoAlerta(g_bpm, g_pressaoSis);

  atualizarLEDs(estadoAtual);

  const char* estadoStr = estadoAtual == ESTADO_OK ? "OK" : estadoAtual == ESTADO_ALERTA ? "ALERTA" : "CRITICO";
  Serial.printf("[LEITURA] BPM:%d | Pressao:%d/%d | Accel:%.2f | Ativ:%s (%d%%) | Estado:%s\n",
    g_bpm, g_pressaoSis, g_pressaoDias,
    g_accelTotal, g_descAtiv.c_str(), g_atividade, estadoStr);
}

// =========================
// ROTAS HTTP
// =========================

void handleDados() {
  StaticJsonDocument<300> doc;
  doc["idDispositivo"]      = ID_DISPOSITIVO;
  doc["pet"]                = NOME_PET;
  doc["frequenciaCardiaca"] = g_bpm;
  doc["nivelAtividade"]     = g_atividade;
  doc["descAtividade"]      = g_descAtiv;
  doc["aceleracaoTotal"]    = serialized(String(g_accelTotal, 2));
  doc["pressaoSistolica"]   = g_pressaoSis;
  doc["pressaoDiastolica"]  = g_pressaoDias;
  doc["estado"]             = estadoAtual == ESTADO_OK ? "OK" : estadoAtual == ESTADO_ALERTA ? "ALERTA" : "CRITICO";
  doc["alerta"]             = g_alertaMotivo;
  doc["uptime"]             = g_uptime;

  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleStatus() {
  StaticJsonDocument<128> doc;
  doc["dispositivo"] = ID_DISPOSITIVO;
  doc["pet"]         = NOME_PET;
  doc["online"]      = true;
  doc["uptime"]      = g_uptime;
  doc["estado"]      = estadoAtual == ESTADO_OK ? "OK" : estadoAtual == ESTADO_ALERTA ? "ALERTA" : "CRITICO";

  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleRoot() {
  String cor   = estadoAtual == ESTADO_OK ? "#00c853" : estadoAtual == ESTADO_ALERTA ? "#ffd600" : "#ff5555";
  String label = estadoAtual == ESTADO_OK ? "NORMAL"  : estadoAtual == ESTADO_ALERTA ? "ALERTA"  : "CRITICO";

  String html = "<!DOCTYPE html><html lang='pt-BR'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PetPulse</title><style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:20px}";
  html += "h1{text-align:center;color:#58a6ff;margin-bottom:6px;font-size:1.6em}";
  html += ".sub{text-align:center;color:#8b949e;margin-bottom:20px;font-size:.9em}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px}";
  html += ".card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:20px;text-align:center}";
  html += ".label{font-size:.8em;color:#8b949e;margin-bottom:8px;text-transform:uppercase;letter-spacing:1px}";
  html += ".value{font-size:2.2em;font-weight:bold;color:#58a6ff}";
  html += ".unit{font-size:.8em;color:#8b949e;margin-top:4px}";
  html += ".badge{display:block;width:fit-content;padding:6px 24px;border-radius:20px;font-weight:bold;margin:16px auto;color:#000}";
  html += ".footer{text-align:center;color:#484f58;font-size:.75em;margin-top:24px}";
  html += "</style></head><body>";
  html += "<h1>&#128062; PetPulse</h1>";
  html += "<p class='sub'>Coleira Inteligente &mdash; " + String(NOME_PET) + " &mdash; FIAP 2026</p>";
  html += "<div class='badge' id='badge' style='background:" + cor + "'>" + label + "</div>";
  html += "<div class='grid'>";
  html += "<div class='card'><div class='label'>&#10084; Freq. Cardiaca</div><div class='value' id='bpm'>" + String(g_bpm) + "</div><div class='unit'>bpm</div></div>";
  html += "<div class='card'><div class='label'>&#128137; Pressao</div><div class='value' id='pressao'>" + String(g_pressaoSis) + "/" + String(g_pressaoDias) + "</div><div class='unit'>mmHg</div></div>";
  html += "<div class='card'><div class='label'>&#128062; Atividade</div><div class='value' id='ativ'>" + String(g_atividade) + "</div><div class='unit' id='descativ'>" + g_descAtiv + "</div></div>";
  html += "<div class='card'><div class='label'>&#128225; Aceleracao</div><div class='value' id='accel'>" + String(g_accelTotal, 2) + "</div><div class='unit'>m/s2</div></div>";
  html += "</div>";
  html += "<p class='footer'>Atualiza a cada 3s &middot; Uptime: <span id='up'>" + String(g_uptime) + "</span>s</p>";
  html += "<script>";
  html += "async function upd(){try{";
  html += "const d=await(await fetch('/dados')).json();";
  html += "document.getElementById('bpm').textContent=d.frequenciaCardiaca;";
  html += "document.getElementById('pressao').textContent=d.pressaoSistolica+'/'+d.pressaoDiastolica;";
  html += "document.getElementById('ativ').textContent=d.nivelAtividade;";
  html += "document.getElementById('descativ').textContent=d.descAtividade;";
  html += "document.getElementById('accel').textContent=d.aceleracaoTotal;";
  html += "document.getElementById('up').textContent=d.uptime;";
  html += "const c={OK:'#00c853',ALERTA:'#ffd600',CRITICO:'#ff5555'};";
  html += "const l={OK:'NORMAL',ALERTA:'ALERTA',CRITICO:'CRITICO'};";
  html += "const b=document.getElementById('badge');";
  html += "b.style.background=c[d.estado];b.textContent=l[d.estado];";
  html += "}catch(e){}}setInterval(upd,3000);";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
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
  byte mpuStatus = mpu.begin();
  if (mpuStatus == 0) {
    Serial.println("[MPU6050] OK — Calibrando (mantenha parado)...");
    delay(1000);
    mpu.calcOffsets(true, true);
    Serial.println("[MPU6050] Calibracao concluida!");
  } else {
    Serial.printf("[MPU6050] Erro ao inicializar (codigo %d)\n", mpuStatus);
  }

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("[WiFi] Conectando");
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }
  Serial.println();
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("[WiFi] Abra o IP acima no navegador do Wokwi para ver o dashboard!");

  // Rotas
  server.on("/",       handleRoot);
  server.on("/dados",  handleDados);
  server.on("/status", handleStatus);
  server.begin();

  Serial.println("[HTTP] Servidor na porta 80 — pronto!\n");
}

// =========================
// LOOP
// =========================
void loop() {
  server.handleClient();

  if (millis() - lastReadTime >= INTERVALO_LEITURA) {
    lastReadTime = millis();
    atualizarLeituras();
  }
}
