# 🐾 PetPulse — Coleira Inteligente IoT
### Challenge FIAP 2026 · CLYVO VET · Disruptive Architectures

---

## 📌 Visão Geral

A **PetPulse** é uma coleira IoT que monitora continuamente os sinais vitais do pet e transmite os dados em tempo real via **HTTP/WebServer** embutido no ESP32. O dispositivo integra sensores de frequência cardíaca, pressão arterial, temperatura corporal e nível de atividade física, além de atuadores sonoros e visuais para alertas imediatos — sem depender de broker externo ou infraestrutura adicional.

O dashboard é servido diretamente pelo ESP32 na porta 80, acessível via navegador, com atualização automática a cada 3 segundos.

---

## 🔧 Tecnologias Utilizadas

| Camada | Tecnologia |
|---|---|
| Hardware | ESP32 DevKit V1 (simulado no Wokwi) |
| Sensores | MPU6050 (aceleração/atividade), DS18B20 (temperatura), 2× Potenciômetros (BPM e pressão) |
| Atuadores | 3× LEDs, Buzzer passivo (PWM via LEDC), LCD I2C 16×2 |
| Protocolo | HTTP (WebServer na porta 80) |
| Dashboard | HTML/CSS/JS + Chart.js — servido pelo próprio ESP32 |
| Serialização | JSON manual (sem biblioteca externa) |
| Simulador | Wokwi (web ou VS Code com extensão) |

---

## 📡 Rotas HTTP

| Rota | Método | Descrição |
|---|---|---|
| `/` | GET | Dashboard HTML completo com gráficos e gauge de saúde |
| `/api/dados` | GET | JSON com todas as leituras e score de saúde em tempo real |

### Exemplo de payload — GET /api/dados
```json
{
  "idDispositivo": "COLLAR-001",
  "pet": "Rex",
  "frequenciaCardiaca": 95,
  "nivelAtividade": 45,
  "descAtividade": "caminhada",
  "aceleracaoTotal": 1.23,
  "pressaoSistolica": 130,
  "pressaoDiastolica": 80,
  "temperatura": 38.5,
  "score": 87,
  "estado": "OK",
  "alerta": "",
  "uptime": 12345
}
```

---

## 🖥️ Circuito (Wokwi)

| Componente | Pino ESP32 | Função |
|---|---|---|
| Potenciômetro 1 | GPIO 34 | Simula frequência cardíaca (40–200 bpm) |
| Potenciômetro 2 | GPIO 35 | Simula pressão sistólica (80–220 mmHg) |
| MPU6050 | SDA 21 / SCL 22 | Aceleração e classificação de atividade |
| DS18B20 | GPIO 4 (1-Wire) | Temperatura corporal (°C) |
| LED Verde | GPIO 18 | Estado OK |
| LED Amarelo | GPIO 19 | Estado ALERTA |
| LED Vermelho | GPIO 5 | Estado CRÍTICO |
| Buzzer passivo | GPIO 13 | Alertas sonoros estruturados |
| LCD I2C 16×2 | SDA 21 / SCL 22 | Exibe BPM, score e estado |

---

## 🔊 Alertas Sonoros (Buzzer)

| Situação | Padrão Sonoro | Comportamento |
|---|---|---|
| Boot | 3 bipes crescentes (800→1200→1600 Hz) | Uma vez, na inicialização |
| ALERTA | 2 bipes curtos (1800 Hz) | A cada 5 segundos |
| CRÍTICO | S.O.S morse `... --- ...` | Contínuo e não bloqueante |
| OK | Silêncio | — |

---

## 🧠 Score de Saúde (0–100)

Calculado no ESP32 a cada leitura, cruzando todos os vitais com pesos ponderados:

| Vital | Peso | Lógica |
|---|---|---|
| Frequência Cardíaca | 35 pts | Desconto proporcional ao desvio da faixa normal |
| Pressão Sistólica | 30 pts | Idem |
| Temperatura Corporal | 25 pts | Idem + penalidade extra para febre em repouso |
| Nível de Atividade | 10 pts | +5 se caminhada saudável, -5 se corrida intensa |

| Score | Classificação |
|---|---|
| 80–100 | ✅ Excelente |
| 60–79 | 🟢 Bom |
| 40–59 | 🟡 Atenção |
| 20–39 | 🟠 Ruim |
| 0–19 | 🔴 Crítico |

---

## 🌡️ Lógica de Cruzamento de Dados

A funcionalidade mais importante do sistema é o **cruzamento de febre com repouso**:

> Se o pet estiver em **repouso** (MPU6050 com aceleração baixa, atividade ≤ 30%) e a **temperatura corporal ≥ 39.5°C**, o sistema dispara estado **CRÍTICO imediatamente** — independente dos outros vitais.

Isso detecta infecções e processos febris que passariam despercebidos em animais quietos.

---

## 📊 Parâmetros Normais (cão adulto)

| Sinal Vital | Normal | Alerta | Crítico |
|---|---|---|---|
| Frequência Cardíaca | 60–140 bpm | Fora do range | > 180 bpm |
| Pressão Sistólica | 100–160 mmHg | Fora do range | > 200 mmHg |
| Temperatura Corporal | 37.5–39.2°C | Fora do range | > 40.5°C ou < 36.0°C |
| Febre em Repouso | — | > 39.2°C | ≥ 39.5°C + atividade baixa |

---

## 📈 Dashboard HTTP

Acessível em `http://<IP_DO_ESP32>/` (Wokwi web) ou `http://localhost:8280` (VS Code):

- **5 cards de vitais** — BPM, pressão, atividade, aceleração e temperatura com barra de cor dinâmica
- **Gauge de score de saúde** — semicircular, muda de cor conforme o score
- **Badge de estado** — NORMAL / ALERTA / CRÍTICO com transição animada
- **Caixa de alertas** — descreve o motivo, vital e valores no momento do alerta
- **Gráfico de vitais em tempo real** — Chart.js com 20 pontos rolantes, dois eixos Y (BPM/pressão e temperatura)
- **Painel do buzzer** — indicador visual animado com estado atual (SILENCIOSO / 2 BIPES / S.O.S ATIVO)
- **Atualização automática** a cada 3 segundos via `fetch('/api/dados')`

---

## 🚀 Como Executar

### Opção 1 — Wokwi Web

1. Acesse [wokwi.com](https://wokwi.com) → **New Project** → **ESP32**
2. Cole o conteúdo de `PetPulse-Iot.ino` no editor principal
3. Clique em **"+"** → adicione `diagram.json` e cole o conteúdo
4. Em **Library Manager**, adicione: `MPU6050_light`, `ArduinoJson`, `LiquidCrystal_I2C`, `OneWire`, `DallasTemperature`
5. Clique em **▶ Start Simulation**
6. Clique no botão de navegador embutido do Wokwi para abrir o dashboard

### Opção 2 — Wokwi for VS Code

1. Instale a extensão **Wokwi Simulator** no VS Code
2. Compile o projeto no **Arduino IDE** com a placa **DOIT ESP32 DEVKIT V1**
3. Exporte o binário: `Sketch → Export Compiled Binary`
4. Confirme que o `wokwi.toml` aponta para o `.bin` correto:

```toml
[wokwi]
version = 1
elf      = "build/esp32.esp32.esp32doit-devkit-v1/PetPulse-Iot.ino.elf"
firmware = "build/esp32.esp32.esp32doit-devkit-v1/PetPulse-Iot.ino.bin"

[[net.forward]]
from = "0.0.0.0:8280"
to   = "target:80"
```

5. Inicie a simulação com `F1 → Wokwi: Start Simulator`
6. Acesse o dashboard em `http://localhost:8280`

> **Atenção:** Se o simulador carregar uma versão antiga, feche a aba do Wokwi Simulator, reabra com `F1 → Wokwi: Start Simulator` e recarregue o browser.

---

## 📁 Arquivos do Projeto

PetPulse-Iot/
├── PetPulse-Iot.ino       # Firmware ESP32 completo
├── diagram.json           # Circuito Wokwi
├── libraries.txt          # Dependências do Wokwi web
├── wokwi.toml             # Configuração do simulador VS Code
└── README.md              # Este arquivo

---

### libraries.txt
MPU6050_light
ArduinoJson
LiquidCrystal_I2C
OneWire
DallasTemperature

---

## 🔗 Ligação com o Challenge CLYVO VET

Esta entrega IoT resolve o pilar de **monitoramento contínuo** do desafio:

- **Coleta passiva** de sinais vitais sem depender da interação do responsável
- **Cruzamento inteligente** de dados (febre + repouso) permite detecção precoce de infecções
- **Score de saúde 0–100** fornece métrica objetiva e padronizada para triagem veterinária
- **Alertas em tempo real** permitem intervenção clínica proativa
- O endpoint `/api/dados` pode ser consumido pela **API Java/.NET** do grupo para persistência em banco de dados e geração de histórico longitudinal
- O histórico de leituras alimenta o módulo de **inteligência** da plataforma CLYVO VET para análise preditiva do estado de saúde do animal
