# Requisitos da PCB BalanceBot V1

## Situação

Documento de requisitos, não um esquemático liberado para fabricação. Placa, corrente, tensão de alimentação, Rsense dos módulos TMC2209 e especificações dos motores ainda precisam de datasheets e confirmação de compra. **Não dimensionar potência com base nos valores provisórios de software.**

## Interfaces e conectores

- ESP32 DevKit/WROOM-32 removível, com acesso a EN/reset, USB e serial de depuração.
- MPU6050 em I2C 3,3 V (SDA GPIO21, SCL GPIO22), conector polarizado, terra e pull-ups dimensionados após verificar os resistores já existentes no módulo IMU.
- Dois sockets/conectores TMC2209 identificados A/esquerdo (Creality 42-34) e B/direito (42HB34F08AB); identificar claramente VM, GND, VIO, UART, EN, STEP, DIR e pares A/B do motor.
- STEP/DIR planejados: esquerdo GPIO25/26; direito GPIO27/14. Conferir o DevKit e multiplexação de GPIO14 (JTAG) antes de fechar layout.
- UART TMC com endereços distintos selecionados por MS1/MS2 ou método confirmado para os módulos reais. Definir topologia de UART (barramento/daisy-chain), resistores e pinos ESP32 após escolha do DevKit.
- Intertravamento lógico GPIO33 com pull-up e contato normalmente aberto para GND, além de entrada de status do corte físico, se disponível.
- ADC de bateria GPIO32 somente por divisor/proteção calculados para a faixa real de bateria e faixa ADC; não ligar tensão de bateria direto ao ESP32.
- Conectores dos motores com chaveamento mecânico, pinagem e corrente adequados, retenção contra vibração e polaridade sinalizada.

## Potência e proteção

- Separar plano/rota de VM dos motores, retorno de alta corrente e alimentação lógica. Controlar loops de comutação e manter trilhas de sensor/IMU e I2C afastadas de STEP, motor e VM.
- Reservar capacitor de bulk próximo a cada driver, cerâmicos de bypass e desacoplamento local, calculados conforme datasheet TMC2209 e módulo escolhido.
- Prever entrada de bateria com fusível substituível, proteção de polaridade/transientes, medição e pontos de teste. Dimensionar cobre, conectores, trilhas e térmica pela corrente contínua/pico real, não pela corrente RMS configurada apenas no software.
- Cada driver precisa de ventilação/dissipação e acesso térmico. Validar temperatura em bancada; considerar sensor de temperatura e fault/DIAG por driver.
- Planejar retorno de corrente e terra em estrela/zonas, reguladores com margem de picos do ESP32, filtro da IMU e acesso a medições de VM, 3V3, GND e UART.
- TMC2209 usa ENN ativo em nível baixo nos módulos comuns; projetar pull-up físico que mantenha o driver desabilitado durante boot, reset, brownout, ESP32 removido e fio rompido. Confirmar polaridade no módulo específico.
- E-stop físico independente do firmware, abrindo o caminho de potência dos drivers. Dimensionar componente de corte para tensão/corrente e energia do conjunto; contato e sinal do GPIO não substituem o corte.
- Prever jumpers/test points para STEP, DIR, ENN, UART, DIAG e sinais de intertravamento, sem expor pontos de alta corrente ao usuário.

## Regras de layout e validação

- Definir footprint a partir do fabricante do módulo TMC2209 escolhido, não apenas do CI. Confirmar posição do dissipador, dissipação inferior, furos e altura.
- Manter ESP32 antena sem cobre/plano e longe de VM/motores conforme guia do módulo.
- Não compartilhar GPIO strap/boot sem verificar níveis nos ciclos de reset.
- Revisar ERC/DRC, creepage/clearance adequado à tensão, largura de trilha, retorno, aquecimento e montagem mecânica.
- Verificar inicialmente sem motores: rails, polaridade, ENN desabilitado, I2C, UART/endereço e corte físico. Em seguida usar fonte limitada e um único driver/motor em suporte, medir forma de onda STEP, corrente real e temperatura. Só integrar ambos após testes de reset, perda de Wi-Fi, falha de sensor e E-stop.

## Itens bloqueadores de fabricação

Confirmar revisão do DevKit e sua pinagem; datasheets/rotulagem dos dois motores; corrente/fase, resistência/indutância e pares de bobinas; módulos TMC2209 e Rsense reais; tensão/faixa de bateria; wheel/gear ratio; corrente de stall, conectores, fusível, dispositivo de corte e dissipação.
