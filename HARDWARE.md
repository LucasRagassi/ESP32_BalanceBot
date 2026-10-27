# Hardware BalanceBot V1

## Componentes e status

| Item | Seleção atual | Status |
|---|---|---|
| MCU | ESP32 DevKit com módulo ESP32-WROOM-32 | confirmar marcação, revisão e esquema físicos; PlatformIO usa `esp32dev` genérico |
| IMU | MPU6050 | orientação, endereço e pull-ups devem ser conferidos no módulo |
| Motor esquerdo | Creality 42-34, NEMA 17 | datasheet, corrente de fase, resistência/indutância e pinagem pendentes |
| Motor direito | 42HB34F08AB, NEMA 17 | datasheet, corrente de fase, resistência/indutância e pinagem pendentes |
| Drivers | 2 × TMC2209 | módulo, Rsense, dissipação, UART e endereços pendentes |
| Estrutura | Impressa em 3D, cerca de 300 mm | medidas finais e material conforme [CAD_REQUIREMENTS.md](./CAD_REQUIREMENTS.md) |
| PCB | Personalizada em KiCad | requisitos de interface e segurança em [PCB_REQUIREMENTS.md](./PCB_REQUIREMENTS.md) |

**Não tratar nenhum valor provisório de corrente/velocidade do firmware como rating de motor.** Corrente de fase, corrente RMS, Vsense/Rsense, alimentação, microstepping, térmica e pinagem dependem dos componentes reais.

## Pinos provisionais

| Sinal | GPIO | Observação |
|---|---:|---|
| MPU6050 SDA/SCL | 21 / 22 | I2C 3,3 V; confirmar pull-ups integrados e evitar pull-up a 5 V |
| STEP/DIR esquerdo | 25 / 26 | Reservado; RMT canal 0 pretendido; não configurado pelo app |
| STEP/DIR direito | 27 / 14 | Reservado; RMT canal 1 pretendido; GPIO14 também é JTAG; não configurado pelo app |
| Intertravamento | 33 | Firmware usa pull-up interno, fechado = nível baixo; somente entrada de lógica, não é E-stop |
| ADC bateria reservado | 32 | ADC1; divisor, filtro, proteção e calibração ainda não implementados |
| Encoders | Não definidos | NEMA 17 selecionados não pressupõem encoder; perda física de passos não pode ser verificada |

Confirme o pinout específico do DevKit e os multiplexadores/straps antes de fechar placa. Nenhum GPIO STEP/DIR é inicializado no app e o backend RMT não é instanciado. O código de simulação não produz pulsos em pinos.

## TMC2209 e correntes por eixo

- A configuração independente contém current limit (A), sentido, velocidade máxima (steps/s) e aceleração (steps/s²) para cada eixo; ela pode ser editada no painel e salva em NVS.
- Corrente inicial `0.0 A` significa **não configurada**. Valores >0 são apenas dados persistidos para integração futura. V1 não programa UART nem ativa ENN, portanto nenhum desses valores chega a um TMC2209.
- Depois de obter datasheet do motor e módulo, decidir se campo representa RMS ou peak conforme API/documentação TMC2209, medir/confirmar Rsense do módulo e aplicar limite térmico seguro. Não assumir que os dois módulos têm o mesmo Rsense.
- Configurar endereços UART únicos de forma compatível com os módulos (MS1/MS2 e topologia), validar comunicação e tratar DIAG/OTP/short/open-load antes de escrever corrente.
- NEMA 17 não determina pinagem, torque, corrente ou passo apenas pelo tamanho. Identificar os dois pares de bobina com datasheet/medição sem alimentação.

## Entrada, níveis e desacoplamento

- ESP32 não é tolerante a 5 V. I2C, UART, STEP, DIR, ENN/DIAG e ADC têm de ser compatíveis com 3,3 V ou receber interface adequada.
- SDA/SCL precisam de pull-ups 3,3 V. Confirme se MPU6050 breakout possui pull-ups; evite resistores paralelos excessivos e qualquer pull-up ligado ao rail de 5 V.
- RMT gera STEP por periférico de hardware. Validar largura/polaridade/frequência de pulso nos TMC escolhidos, atualização de frequência, comportamento do pino no boot e limites de clock/tick do RMT. STEP deve permanecer inativo até interlock + driver habilitado por hardware.
- Usar capacitores de bulk junto de cada driver e desacoplamento VIO/VM conforme datasheet do módulo; dimensionar alimentação, cobre, conectores, trilhas, fusível, retorno e térmica pela corrente real.
- Alimentar lógica e motores por reguladores/fontes apropriados, com estratégia de retorno/terra e mitigação de ruído. Não alimentar motor pelo ESP32.
- Se breakouts TMC comuns tiverem ENN ativo baixo, dimensionar pull-up físico que mantenha desabilitado durante reset/boot/brownout. Confirmar polaridade específica; projetar sinal seguro mesmo com MCU removido ou resetado.
- GPIO33 pode monitorar intertravamento, mas não corta potência. O E-stop físico deve abrir o rail do driver independentemente do ESP32, Wi-Fi ou firmware, com componente apropriado para energia/corrente do conjunto.

## Ensaios necessários antes de motores

1. Identificar placa/módulo ESP32 e validar I2C, endereço MPU6050 e orientação.
2. Revisar PCB KiCad (ERC/DRC), polaridade, continuidade, curtos, reguladores e níveis lógicos.
3. Com potência de motor removida: confirmar ENN inativo em reset/boot, interlock e E-stop.
4. Validar UART e identidade/configuração do TMC sem motor, somente após confirmar Rsense e limites.
5. Ensaiar um motor de cada vez em suporte preso, fonte com limite de corrente, proteção mecânica e E-stop acessível; medir corrente, STEP/DIR e temperatura.
6. Validar ambos, EMI, reset/brownout, sensor desconectado, deadline, WebSocket perdido e corte físico antes de qualquer equilíbrio.

Consulte [PCB_REQUIREMENTS.md](./PCB_REQUIREMENTS.md) para lista de requisitos da placa e [CAD_REQUIREMENTS.md](./CAD_REQUIREMENTS.md) para estrutura impressa.
