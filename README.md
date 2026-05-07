# 🐾 PetPulse — Coleira Inteligente IoT
### Challenge FIAP 2026 · CLYVO VET · Disruptive Architectures

---

## 📌 Visão Geral

A **PetPulse** é uma coleira IoT que monitora continuamente os sinais vitais do pet e transmite os dados em tempo real para um dashboard. O dispositivo simula sensores de frequência cardíaca, pressão arterial e nível de atividade, publica os dados via protocolo **MQTT** e um dashboard **Node-RED** exibe as leituras e dispara alertas inteligentes quando os valores saem dos parâmetros normais.

---

## 🔧 Tecnologias Utilizadas

| Camada | Tecnologia |
|---|---|
| Hardware | ESP32 (simulado no Wokwi) |
| Sensores | 3× Potenciômetros (simula BPM, atividade, pressão) |
| Atuadores | 3× LEDs (verde / amarelo / vermelho) |
| Protocolo | MQTT via `PubSubClient` |
| Broker | `broker.hivemq.com` (público, sem autenticação) |
| Dashboard | Node-RED + `node-red-dashboard` |
| Serialização | ArduinoJson (JSON) |

---

## 📡 Tópicos MQTT

| Tópico | Direção | Descrição |
|---|---|---|
| `petpulse/coleira/telemetria` | ESP32 → Node-RED | Publica JSON com todos os sinais vitais |
| `petpulse/coleira/alerta` | ESP32 → Node-RED | Publica JSON de alerta quando fora do range normal |
| `petpulse/coleira/config` | Node-RED → ESP32 | Configura o intervalo de coleta em milissegundos |
| `petpulse/coleira/status` | ESP32 → Node-RED | Indica que o dispositivo está online |

### Exemplo de payload — Telemetria
```json
{
  "idDispositivo": "COLLAR-001",
  "pet": "Rex",
  "frequenciaCardiaca": 95,
  "nivelAtividade": 45,
  "descricaoAtividade": "caminhada",
  "pressaoSistolica": 130,
  "pressaoDiastolica": 80,
  "intervaloColetaMs": 5000,
  "uptime": 12345
}
```

### Exemplo de payload — Alerta
```json
{
  "dispositivo": "COLLAR-001",
  "pet": "Rex",
  "nivel": "ALERTA",
  "bpm": 155,
  "pressaoSis": 175,
  "motivo": "Pressao arterial elevada"
}
```

### Exemplo de payload — Config
```json
{ "intervaloMs": 10000 }
```

---

## 🖥️ Circuito (Wokwi)

| Componente | Pino ESP32 | Função |
|---|---|---|
| Potenciômetro 1 | GPIO 34 | Simula frequência cardíaca (40–200 bpm) |
| Potenciômetro 2 | GPIO 35 | Simula nível de atividade (0–100%) |
| Potenciômetro 3 | GPIO 32 | Simula pressão sistólica (80–220 mmHg) |
| LED Verde | GPIO 26 | Pet OK (valores normais) |
| LED Amarelo | GPIO 27 | Alerta (valores fora do range normal) |
| LED Vermelho | GPIO 14 | Crítico (valores perigosos) |

---

## 🚀 Como Executar

### 1. Simulação no Wokwi

1. Acesse [wokwi.com](https://wokwi.com) → **New Project** → **ESP32**
2. Cole o conteúdo de `petpulse_collar.ino` no editor principal
3. Clique em **"+"** para adicionar arquivo → nome `diagram.json` → cole o conteúdo
4. Clique em **▶ Start Simulation**
5. Gire os potenciômetros para simular diferentes leituras

> **Dica:** O Wokwi já possui Wi-Fi embutido com SSID `Wokwi-GUEST` — não precisa de configuração extra.

### 2. Dashboard Node-RED

**Pré-requisitos:**
```bash
# Instalar Node-RED (se não tiver)
npm install -g --unsafe-perm node-red

# Instalar o pacote de dashboard
cd ~/.node-red
npm install node-red-dashboard
```

**Importar o fluxo:**
1. Abra o Node-RED: `http://localhost:1880`
2. Menu ≡ → **Import** → Cole o conteúdo de `petpulse_nodered_flow.json`
3. Clique em **Import** → **Deploy**
4. Acesse o dashboard: `http://localhost:1880/ui`

---

## 📊 Parâmetros Normais (cão adulto)

| Sinal Vital | Normal | Alerta | Crítico |
|---|---|---|---|
| Frequência Cardíaca | 60–140 bpm | Fora desse range | > 180 bpm |
| Pressão Sistólica | 100–160 mmHg | Fora desse range | > 200 mmHg |
| Nível de Atividade | 0–100% | — | — |

---

## 🏗️ Arquitetura da Solução

```
[Coleira ESP32]                    [Broker MQTT]              [Dashboard]
  Pot1 → BPM         ─publish─►   broker.hivemq.com   ─sub─►  Node-RED
  Pot2 → Atividade                 :1883                        Gauges
  Pot3 → Pressão     ◄─subscribe─  topic: config               Charts
  LED Verde/Amarelo/Vermelho                                     Alertas (toast)
```

---

## 📁 Arquivos do Projeto

```
petpulse-iot/
├── petpulse_collar.ino        # Firmware ESP32
├── diagram.json               # Circuito Wokwi
├── petpulse_nodered_flow.json # Fluxo Node-RED
└── README.md                  # Este arquivo
```

---

## 🔗 Ligação com o Challenge CLYVO VET

Esta entrega IoT resolve o pilar de **monitoramento contínuo** do desafio:
- **Coleta passiva** de sinais vitais sem depender da interação do responsável
- **Alertas em tempo real** permitem intervenção clínica proativa
- Os dados publicados no MQTT podem ser consumidos pela **API Java/.NET** do grupo para persistência no banco e geração de score de risco do pet
- O histórico longitudinal de leituras alimenta o módulo de **inteligência** da plataforma CLYVO VET
