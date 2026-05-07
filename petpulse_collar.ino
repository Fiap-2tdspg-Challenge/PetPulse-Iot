/**
 * =========================================
 *  PetPulse - Coleira Inteligente IoT
 *  Disciplina: Disruptive Architectures
 *  Challenge FIAP 2026 - CLYVO VET
 * =========================================
 *
 * Simula os sensores da coleira:
 *   - Frequência cardíaca (bpm)
 *   - Pressão (mmHg sistólica/diastólica)
 *   - Nível de atividade (repouso, caminhada, corrida)
 *
 * Protocolo: MQTT (broker.hivemq.com)
 * Tópicos:
 *   petpulse/coleira/telemetria  → publica JSON com leituras
 *   petpulse/coleira/alerta      → publica alerta quando fora do range
 *   petpulse/coleira/config      → recebe intervalo de coleta em ms
 *
 * Simulação no Wokwi:
 *   - Potenciômetro 1 (GPIO 34): simula frequência cardíaca
 *   - Potenciômetro 2 (GPIO 35): simula nível de atividade
 *   - Potenciômetro 3 (GPIO 32): simula pressão sistólica
 *   - LED Verde  (GPIO 26): pet OK
 *   - LED Amarelo (GPIO 27): alerta leve
 *   - LED Vermelho (GPIO 14): alerta crítico
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// =========================
// CONFIGURACAO DO WIFI
// =========================
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6

// =========================
// CONFIGURACAO DO BROKER MQTT
// =========================
const char* MQTT_BROKER    = "broker.hivemq.com";
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "petpulse-coleira-001";

// Tópicos
const char* TOPIC_TELEMETRY = "petpulse/coleira/telemetria";
const char* TOPIC_ALERT     = "petpulse/coleira/alerta";
const char* TOPIC_CONFIG    = "petpulse/coleira/config";
const char* TOPIC_STATUS    = "petpulse/coleira/status";

// =========================
// HARDWARE - PINOS
// =========================
const int PIN_SENSOR_BPM        = 34;  // Potenciômetro 1 → frequência cardíaca
const int PIN_SENSOR_ATIVIDADE  = 35;  // Potenciômetro 2 → nível de atividade
const int PIN_SENSOR_PRESSAO    = 32;  // Potenciômetro 3 → pressão sistólica

const int LED_OK       = 26;  // verde
const int LED_ALERTA   = 27;  // amarelo
const int LED_CRITICO  = 14;  // vermelho

// =========================
// IDENTIFICAÇÃO DO DISPOSITIVO
// =========================
const char* ID_DISPOSITIVO = "COLLAR-001";
const char* NOME_PET       = "Rex";

// =========================
// PARÂMETROS NORMAIS DO PET (cão adulto)
// =========================
const int BPM_MIN_NORMAL  = 60;
const int BPM_MAX_NORMAL  = 140;
const int BPM_MAX_CRITICO = 180;

const int PRESSAO_SIS_MIN_NORMAL  = 100;
const int PRESSAO_SIS_MAX_NORMAL  = 160;
const int PRESSAO_SIS_MAX_CRITICO = 200;

// =========================
// CONTROLE DE TEMPO
// =========================
unsigned long lastPublishTime   = 0;
unsigned long intervaloColeta   = 5000;  // 5 segundos (configurável via MQTT)

// =========================
// OBJETOS
// =========================
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// =========================
// ESTADO DO SISTEMA
// =========================
enum EstadoAlerta { ESTADO_OK, ESTADO_ALERTA, ESTADO_CRITICO };
EstadoAlerta estadoAtual = ESTADO_OK;

// =========================
// FUNÇÕES AUXILIARES
// =========================

/**
 * Mapeia leitura do ADC (0-4095) para faixa de frequência cardíaca (40-200 bpm)
 * No Wokwi o ADC do ESP32 vai de 0 a 4095 (12 bits)
 */
int lerFrequenciaCardiaca() {
  int raw = analogRead(PIN_SENSOR_BPM);
  return map(raw, 0, 4095, 40, 200);
}

/**
 * Mapeia leitura do ADC para nível de atividade (0-100)
 * 0-30  → repouso
 * 31-60 → caminhada
 * 61-100 → corrida
 */
int lerNivelAtividade() {
  int raw = analogRead(PIN_SENSOR_ATIVIDADE);
  return map(raw, 0, 4095, 0, 100);
}

/**
 * Retorna string descritiva do nível de atividade
 */
String descreverAtividade(int nivel) {
  if (nivel <= 30)  return "repouso";
  if (nivel <= 60)  return "caminhada";
  return "corrida";
}

/**
 * Mapeia leitura do ADC para pressão sistólica (80-220 mmHg)
 * Pressão diastólica estimada como ~60-65% da sistólica
 */
int lerPressaoSistolica() {
  int raw = analogRead(PIN_SENSOR_PRESSAO);
  return map(raw, 0, 4095, 80, 220);
}

/**
 * Avalia o estado de saúde com base nos dados coletados
 */
EstadoAlerta avaliarEstado(int bpm, int pressaoSis) {
  if (bpm > BPM_MAX_CRITICO || pressaoSis > PRESSAO_SIS_MAX_CRITICO) {
    return ESTADO_CRITICO;
  }
  if (bpm < BPM_MIN_NORMAL || bpm > BPM_MAX_NORMAL ||
      pressaoSis < PRESSAO_SIS_MIN_NORMAL || pressaoSis > PRESSAO_SIS_MAX_NORMAL) {
    return ESTADO_ALERTA;
  }
  return ESTADO_OK;
}

/**
 * Atualiza os LEDs conforme o estado atual
 */
void atualizarLEDs(EstadoAlerta estado) {
  digitalWrite(LED_OK,      estado == ESTADO_OK      ? HIGH : LOW);
  digitalWrite(LED_ALERTA,  estado == ESTADO_ALERTA  ? HIGH : LOW);
  digitalWrite(LED_CRITICO, estado == ESTADO_CRITICO ? HIGH : LOW);
}

// =========================
// FUNÇÕES DE REDE
// =========================

void conectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.print("[WiFi] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WiFi] Conectado! IP: ");
  Serial.println(WiFi.localIP());
}

void garantirWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconectando...");
    conectarWiFi();
  }
}

void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Conectando ao broker...");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println(" OK!");

      // Assina o tópico de configuração
      mqttClient.subscribe(TOPIC_CONFIG);
      Serial.print("[MQTT] Inscrito em: ");
      Serial.println(TOPIC_CONFIG);

      // Publica status de online
      mqttClient.publish(TOPIC_STATUS, "{\"status\":\"online\",\"dispositivo\":\"COLLAR-001\",\"pet\":\"Rex\"}");

    } else {
      Serial.print(" falhou (código ");
      Serial.print(mqttClient.state());
      Serial.println("). Tentando em 3s...");
      delay(3000);
    }
  }
}

void garantirMQTT() {
  if (!mqttClient.connected()) {
    conectarMQTT();
  }
}

// =========================
// MQTT - CALLBACK
// =========================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }

  Serial.print("[MQTT] Mensagem recebida em [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(mensagem);

  // Processa configuração de intervalo
  if (String(topic) == TOPIC_CONFIG) {
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, mensagem);

    if (!err && doc.containsKey("intervaloMs")) {
      unsigned long novoIntervalo = doc["intervaloMs"].as<unsigned long>();
      if (novoIntervalo >= 1000 && novoIntervalo <= 60000) {
        intervaloColeta = novoIntervalo;
        Serial.print("[CONFIG] Intervalo atualizado para ");
        Serial.print(intervaloColeta);
        Serial.println("ms");
      } else {
        Serial.println("[CONFIG] Intervalo fora do range permitido (1000-60000ms)");
      }
    }
  }
}

// =========================
// MQTT - PUBLICA TELEMETRIA
// =========================
void publicarTelemetria() {
  int bpm         = lerFrequenciaCardiaca();
  int atividade   = lerNivelAtividade();
  int pressaoSis  = lerPressaoSistolica();
  int pressaoDias = (int)(pressaoSis * 0.62);  // estimativa diastólica

  EstadoAlerta novoEstado = avaliarEstado(bpm, pressaoSis);

  // Monta JSON da telemetria
  StaticJsonDocument<256> doc;
  doc["idDispositivo"]          = ID_DISPOSITIVO;
  doc["pet"]                    = NOME_PET;
  doc["frequenciaCardiaca"]     = bpm;
  doc["nivelAtividade"]         = atividade;
  doc["descricaoAtividade"]     = descreverAtividade(atividade);
  doc["pressaoSistolica"]       = pressaoSis;
  doc["pressaoDiastolica"]      = pressaoDias;
  doc["intervaloColetaMs"]      = intervaloColeta;
  doc["uptime"]                 = millis();

  char buffer[256];
  serializeJson(doc, buffer);

  mqttClient.publish(TOPIC_TELEMETRY, buffer);
  Serial.print("[PUB] ");
  Serial.println(buffer);

  // Publica alerta se necessário
  if (novoEstado != ESTADO_OK) {
    StaticJsonDocument<200> alertDoc;
    alertDoc["dispositivo"]  = ID_DISPOSITIVO;
    alertDoc["pet"]          = NOME_PET;
    alertDoc["nivel"]        = (novoEstado == ESTADO_CRITICO) ? "CRITICO" : "ALERTA";
    alertDoc["bpm"]          = bpm;
    alertDoc["pressaoSis"]   = pressaoSis;

    if (bpm > BPM_MAX_CRITICO || bpm > BPM_MAX_NORMAL) {
      alertDoc["motivo"] = "Frequencia cardiaca elevada";
    } else if (bpm < BPM_MIN_NORMAL) {
      alertDoc["motivo"] = "Frequencia cardiaca baixa";
    } else if (pressaoSis > PRESSAO_SIS_MAX_NORMAL) {
      alertDoc["motivo"] = "Pressao arterial elevada";
    } else {
      alertDoc["motivo"] = "Pressao arterial baixa";
    }

    char alertBuffer[200];
    serializeJson(alertDoc, alertBuffer);
    mqttClient.publish(TOPIC_ALERT, alertBuffer);
    Serial.print("[ALERTA] ");
    Serial.println(alertBuffer);
  }

  // Atualiza LEDs e estado
  estadoAtual = novoEstado;
  atualizarLEDs(estadoAtual);
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n===== PetPulse - Coleira IoT =====");

  // Configura pinos
  pinMode(LED_OK,      OUTPUT);
  pinMode(LED_ALERTA,  OUTPUT);
  pinMode(LED_CRITICO, OUTPUT);

  // LEDs de boot - acende todos brevemente
  digitalWrite(LED_OK,      HIGH);
  digitalWrite(LED_ALERTA,  HIGH);
  digitalWrite(LED_CRITICO, HIGH);
  delay(500);
  digitalWrite(LED_OK,      LOW);
  digitalWrite(LED_ALERTA,  LOW);
  digitalWrite(LED_CRITICO, LOW);

  // Conecta WiFi
  conectarWiFi();

  // Configura MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("[SETUP] Pronto! Iniciando coleta...\n");
}

// =========================
// LOOP
// =========================
void loop() {
  garantirWiFi();
  garantirMQTT();
  mqttClient.loop();

  unsigned long agora = millis();
  if (agora - lastPublishTime >= intervaloColeta) {
    lastPublishTime = agora;
    publicarTelemetria();
  }
}
