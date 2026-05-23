 🐾 PetPulse — Coleira Inteligente IoT
### Challenge FIAP 2026 · CLYVO VET · Disruptive Architectures

---

## 🐕 O Problema

Donos de pets e clínicos veterinários enfrentam um desafio crítico: **a maioria das doenças graves em cães só é detectada quando os sintomas já são visíveis** — o que frequentemente significa que o animal já está em sofrimento há horas ou dias.

Condições como **taquicardia, hipertensão e febre** são silenciosas no início. Um cão com frequência cardíaca de 170 bpm em repouso pode parecer normal para um tutor leigo. Infecções que começam com temperatura de 39.8°C passam despercebidas até o quadro se agravar.

O atendimento veterinário reativo — só quando o animal apresenta sinais externos — gera:
- Diagnósticos tardios e tratamentos mais custosos
- Maior sofrimento animal desnecessário
- Dificuldade do tutor em justificar consultas preventivas sem dados objetivos

**A PetPulse resolve isso com monitoramento contínuo e passivo**, fornecendo dados clínicos em tempo real diretamente do animal — sem depender da observação humana.

---

## 💡 Por que IoT?

O monitoramento contínuo de sinais vitais exige que o dispositivo esteja **no corpo do animal, o tempo todo, transmitindo dados automaticamente**. Isso é por definição o domínio de IoT:

- **Sensores embarcados** capturam dados fisiológicos sem intervenção humana
- **Processamento local no ESP32** aplica lógica clínica (faixas normais, cruzamento de dados) diretamente no dispositivo — sem depender de nuvem para decisões críticas
- **Conectividade Wi-Fi** permite que os dados cheguem ao tutor ou clínica em tempo real
- **Atuadores físicos** (buzzer, LEDs) garantem alertas mesmo sem acesso ao dashboard

Uma solução puramente manual (termômetro, esfigmomanômetro, consulta periódica) captura no máximo um instantâneo. IoT captura a **tendência** — que é onde o diagnóstico precoce acontece.

---

## 📌 Visão Geral da Solução

A **PetPulse** é uma coleira IoT embarcada em ESP32 que monitora continuamente os sinais vitais do pet e os disponibiliza via **HTTP/WebServer** embutido. O dashboard é servido diretamente pelo ESP32 na porta 80, acessível via navegador, com atualização automática a cada 3 segundos.

Não há dependência de broker externo, nuvem ou infraestrutura adicional — o dispositivo é autossuficiente.

---

## 🔧 Tecnologias Utilizadas e Como São Aplicadas

| Camada | Tecnologia | Aplicação no Projeto |
|---|---|---|
| Microcontrolador | ESP32 DevKit V1 | Processa sensores, executa lógica clínica, serve o dashboard HTTP |
| Atividade física | MPU6050 (I2C) | Mede aceleração 3 eixos → classifica repouso, caminhada e corrida |
| Temperatura corporal | DS18B20 (1-Wire) | Lê temperatura interna do animal com precisão de 0.5°C |
| Frequência cardíaca | Potenciômetro (GPIO 34) | Simula BPM (40–200) — substituível por sensor MAX30102 em produção |
| Pressão arterial | Potenciômetro (GPIO 35) | Simula pressão sistólica (80–220 mmHg) — PoC |
| Alerta visual | 3× LEDs (GPIO 18, 19, 5) | Verde/Amarelo/Vermelho conforme estado clínico |
| Alerta sonoro | Buzzer passivo (GPIO 13, PWM LEDC) | S.O.S morse no CRÍTICO, bipes no ALERTA, silêncio no OK |
| Display local | LCD I2C 16×2 (0x27) | Exibe BPM, temperatura, score e estado sem precisar de celular |
| Dashboard | HTML/CSS/JS + Chart.js | Servido pelo ESP32 — gráfico de vitais, gauge de saúde, alertas |
| Simulador | Wokwi (web e VS Code) | Permite validar o firmware sem hardware físico |

---

## 🧩 Funcionamento dos Componentes-Chave

### MPU6050 — Classificação de Atividade
O acelerômetro calcula a magnitude do vetor de aceleração líquida (descontando a gravidade no eixo Z). O resultado é mapeado em três estados:
- `< 1.5 m/s²` → repouso (0–30% de atividade)
- `1.5 – 4.0 m/s²` → caminhada (31–60%)
- `> 4.0 m/s²` → corrida (61–100%)

### DS18B20 — Temperatura com Validação
Leituras fora do range fisiológico possível (< 30°C ou > 45°C) são descartadas e substituídas pela última leitura válida — evitando falsos alertas por ruído do sensor.

### Cruzamento Inteligente de Dados
A funcionalidade mais relevante clinicamente:
> Se o pet estiver em **repouso** (atividade ≤ 30%) e a **temperatura ≥ 39.5°C**, o sistema dispara **CRÍTICO imediatamente** — independente dos outros vitais.

Isso detecta **processos infecciosos em animais quietos**, que são os mais difíceis de identificar visualmente.

### Score de Saúde (0–100)
Calculado localmente no ESP32 a cada leitura, sem depender de servidor externo:

| Vital | Peso | Critério |
|---|---|---|
| Frequência Cardíaca | 35 pts | Desconto proporcional ao desvio |
| Pressão Sistólica | 30 pts | Idem |
| Temperatura | 25 pts | Idem + penalidade para febre em repouso |
| Atividade | 10 pts | Bônus para caminhada, desconto para corrida extrema |

### Buzzer não bloqueante
O S.O.S morse é executado passo a passo usando `millis()` — o ESP32 nunca trava em `delay()` durante os alertas, mantendo o WebServer e os sensores funcionando normalmente.

---

## 📡 Rotas HTTP

| Rota | Método | Descrição |
|---|---|---|
| `/` | GET | Dashboard HTML completo com gráficos e gauge |
| `/api/dados` | GET | JSON com todas as leituras, score e estado |

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

## 🖥️ Circuito

| Componente | Pino ESP32 | Função |
|---|---|---|
| Potenciômetro 1 | GPIO 34 | Simula frequência cardíaca (40–200 bpm) |
| Potenciômetro 2 | GPIO 35 | Simula pressão sistólica (80–220 mmHg) |
| MPU6050 | SDA 21 / SCL 22 | Aceleração e classificação de atividade |
| DS18B20 | GPIO 4 (1-Wire) + pull-up 4.7kΩ | Temperatura corporal |
| LED Verde | GPIO 18 + resistor 220Ω | Estado OK |
| LED Amarelo | GPIO 19 + resistor 220Ω | Estado ALERTA |
| LED Vermelho | GPIO 5 + resistor 220Ω | Estado CRÍTICO |
| Buzzer passivo | GPIO 13 + resistor 100Ω | Alertas sonoros PWM |
| LCD I2C 16×2 | SDA 21 / SCL 22 (endereço 0x27) | Display local |

---

## 📈 Dashboard HTTP

Acessível em `http://<IP_DO_ESP32>/` ou `http://localhost:8280` no VS Code:

- **5 cards de vitais** com cor dinâmica por estado clínico
- **Gauge semicircular** do score de saúde (0–100) com classificação textual
- **Badge de estado** — NORMAL / ALERTA / CRÍTICO animado
- **Caixa de alertas** com motivo, vital e valores no momento
- **Gráfico de vitais** — Chart.js com 20 pontos rolantes, dois eixos Y
- **Painel do buzzer** — indicador visual do estado sonoro atual
- **Barra de temperatura** com gradiente de cor fisiológico

---

## ✅ Viabilidade Técnica — Prova de Conceito

Esta entrega é uma **PoC funcional** que valida a arquitetura e a lógica clínica do sistema. O que está implementado e funcionando:

| Funcionalidade | Status |
|---|---|
| Leitura de temperatura real (DS18B20) | ✅ Funcionando |
| Classificação de atividade via acelerômetro | ✅ Funcionando |
| Lógica de cruzamento febre + repouso | ✅ Funcionando |
| Score de saúde ponderado | ✅ Funcionando |
| Dashboard com gráfico em tempo real | ✅ Funcionando |
| Alertas sonoros não bloqueantes (S.O.S) | ✅ Funcionando |
| Display LCD local | ✅ Funcionando |
| API JSON consumível por sistemas externos | ✅ Funcionando |

---

## 🚀 Como Executar

### Opção 1 — Wokwi Web

1. Acesse [wokwi.com](https://wokwi.com) → **New Project** → **ESP32**
2. Cole o conteúdo de `PetPulse-Iot.ino` no editor principal
3. Clique em **"+"** → adicione `diagram.json` e cole o conteúdo
4. Em **Library Manager**, adicione: `MPU6050_light`, `ArduinoJson`, `LiquidCrystal_I2C`, `OneWire`, `DallasTemperature`
5. Clique em **▶ Start Simulation**
6. Clique no botão de navegador embutido para abrir o dashboard

### Opção 2 — Wokwi for VS Code

1. Instale a extensão **Wokwi Simulator** no VS Code
2. Compile no **Arduino IDE** com a placa **DOIT ESP32 DEVKIT V1**
3. Exporte o binário: `Sketch → Export Compiled Binary`
4. Confirme o `wokwi.toml`:

```toml
[wokwi]
version = 1
elf      = "build/esp32.esp32.esp32doit-devkit-v1/PetPulse-Iot.ino.elf"
firmware = "build/esp32.esp32.esp32doit-devkit-v1/PetPulse-Iot.ino.bin"

[[net.forward]]
from = "0.0.0.0:8280"
to   = "target:80"
```

5. `F1 → Wokwi: Start Simulator`
6. Acesse `http://localhost:8280`

> **Se o simulador carregar versão antiga:** feche a aba do Wokwi Simulator, reabra com `F1 → Wokwi: Start Simulator` e recarregue o browser.

---

## 📁 Arquivos do Projeto
PetPulse-Iot/
├── PetPulse-Iot.ino       # Firmware ESP32 completo
├── diagram.json           # Circuito Wokwi
├── libraries.txt          # Dependências Wokwi web
├── wokwi.toml             # Configuração simulador VS Code
└── README.md              # Este arquivo

### libraries.txt
MPU6050_light
ArduinoJson
LiquidCrystal_I2C
OneWire
DallasTemperature

---

## 🔗 Ligação com o Challenge CLYVO VET

| Pilar do Challenge | Como a PetPulse contribui |
|---|---|
| Monitoramento contínuo | Coleta passiva de sinais vitais 24/7 sem intervenção do tutor |
| Detecção precoce | Cruzamento febre + repouso detecta infecções antes dos sintomas visíveis |
| Métrica objetiva | Score 0–100 padroniza a comunicação entre tutor e veterinário |
| Integração com plataforma | `/api/dados` é consumível pela API Java/.NET para persistência e histórico |
| Inteligência clínica | Histórico longitudinal de leituras alimenta análise preditiva do estado do animal |