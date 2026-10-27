# Roadmap BalanceBot V1

## Fase atual — arquitetura e simulação segura

- ESP32/PlatformIO + Arduino/FreeRTOS, AP Wi-Fi, painel LittleFS e WebSocket.
- MPU6050, PID a 200 Hz, instrumentação de deadlines/jitter.
- Perfis independentes para Creality 42-34 e 42HB34F08AB com dois TMC2209 previstos.
- Rampas de velocidade/aceleração, backend RMT isolado e mocks nativos de steppers/perda de passos.
- Nenhum GPIO STEP/DIR/ENN iniciado e nenhum pulso/motor físico.

## Fase seguinte — fechar requisitos físicos

- Confirmar DevKit/WROOM/revisão; reunir datasheets e medidas dos dois motores e módulos TMC2209 (incluindo Rsense).
- Definir bateria, rodas, razão mecânica, velocidade/torque requeridos, microstep, corrente RMS e limites térmicos.
- Fechar pinagem/UART, revisão PCB KiCad, corte independente e pulls de ENN.
- Finalizar CAD para 300 mm com peças reais, massa, centro de gravidade, tolerâncias e dissipação.
- Revisar ERC/DRC, fabricação/assemblagem e análise de risco antes de potência.

## Fase integração de bancada

- Implementar backend TMC2209 UART para current/diagnósticos e conectar StepperAxisController a cada RMT.
- Ensaiar disable-on-boot/reset/brownout, interlock e E-stop com drivers sem motores; depois motor individual em suporte.
- Validar temporalidade de STEP e PID em hardware, recalibrar aceleração/máximos e adicionar encoder/feedback para detectar passos perdidos fisicamente.
- Testar IMU, polaridade de roda, limite de inclinação, sensor desconectado, deadman remoto, perda de passos, timeout e sobretemperatura.

## Ensaios de equilíbrio

- Não equilibrar até os motores/drivers e dados elétricos serem confirmados, controle revisado e corte físico ensaiado.
- Ensaiar com suporte/amarração, área de contenção, fonte limitada e E-stop acessível; subir limites progressivamente.
- Documentar estabilidade, correntes, temperaturas, jitter, comportamento sob falhas e critérios de abortar.
- Só então considerar liberação móvel em estrutura de 300 mm.
