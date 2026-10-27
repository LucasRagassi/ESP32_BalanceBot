# ESP32 BalanceBot V1

Base segura para o BalanceBot de aproximadamente 30 cm, com ESP32 DevKit/WROOM-32 a confirmar, MPU6050, um motor de passo Creality 42-34 no lado esquerdo e um 42HB34F08AB no lado direito, cada um com um TMC2209. Estrutura impressa em 3D e PCB KiCad são requisitos do projeto; consulte [CAD_REQUIREMENTS.md](./CAD_REQUIREMENTS.md) e [PCB_REQUIREMENTS.md](./PCB_REQUIREMENTS.md).

**Fase atual: simulação e arquitetura; nenhuma saída STEP/DIR ou driver físico é inicializado ou habilitado.** Valores elétricos dos motores e módulos permanecem pendentes de datasheet/medição. `esp32dev` é só o alvo genérico para compilar e não comprova qual DevKit está conectado.

## Arquitetura

- `include/balance` e `src`: PID de equilíbrio, filtro complementar, estado seguro, validação/persistência NVS, interfaces StepPulseEngine/StepperDriver, rampas de aceleração independentes e monitor de passos perdidos.
- `src/esp32_rmt_step_pulse.cpp`: backend de pulsos RMT para ESP32 isolado por interface. Não é instanciado pelo firmware V1. `src/esp32_app.cpp` usa somente modelos de motor em RAM; o PID de 200 Hz produz demandas limitadas/rampadas simuladas.
- `StepperMotorConfig` de cada lado inclui limite de corrente, sentido, velocidade máxima em steps/s e aceleração em steps/s². Os limites de software são genéricos e conservadores, não substituem o limite seguro do conjunto motor/módulo/PCB.
- A configuração WebSocket é validada na tarefa de controle, aplicada aos simuladores e persistida pela tarefa principal, fora do caminho crítico. Valores de corrente são guardados mas ainda **não são enviados por UART aos TMC2209**.
- A tarefa de controle solicita período de 5 ms, registra execução, jitter, deadlines e desativa os modelos ao entrar em falha. Não faz rede, LittleFS ou NVS.
- Encoders físicos ainda não foram especificados. A perda de passos exibida/testada é exclusivamente simulada; não existe detecção física de posição nesta versão.

## Build e testes

Instale PlatformIO Core e execute na raiz:

```sh
pio run -e native
pio test -e native
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

Para hardware, somente depois da revisão elétrica e mecânica: `pio run -e esp32dev -t uploadfs`, `pio run -e esp32dev -t upload` e `pio device monitor -b 115200`. Upload físico não foi executado.

Os testes nativos cobrem PID e configuração, estados/falhas, comandos remotos, rampas distintas por eixo, sentido/limites de motor, detecção simulada de passos perdidos e falhas. Build e resultados mais recentes estão em “Validação registrada”.

## Wi-Fi e painel

Depois de gravar firmware e filesystem, conecte ao AP `BalanceBot-Setup` (senha padrão `BalanceBot123`) e abra `http://192.168.4.1/`. Altere a senha antes de qualquer ensaio. É possível substituir credenciais com `BALANCEBOT_AP_SSID` e `BALANCEBOT_AP_PASSWORD` em `build_flags`.

O painel expõe perfis independentes por motor. Corrente 0 indica não configurada; configure-a somente após validar corrente de fase/RMS, Rsense e dissipação para os módulos reais. Um cliente WebSocket controlador é aceito; perda de conexão/timeout zera a referência remota, sem desabilitar abruptamente o laço de equilíbrio. A configuração de motor pode ser alterada apenas fora de `EQUILIBRANDO`.

## Estados e segurança

`DESARMADO → CALIBRANDO → PRONTO → EQUILIBRANDO`; falhas latched levam a `FALHA`. São tratados indisponibilidade/leitura inválida da IMU, atraso de controle, inclinação excessiva, intertravamento aberto e divergência de passos simulada. Reinicialização começa sem motores reais: o backend RMT existe como ponto de integração, mas não é construído em instância nem chamado pelo app.

**Não conecte motores aos pinos STEP/DIR e não use este firmware para equilibrar fisicamente o robô.** Antes de uma futura habilitação, é obrigatório um E-stop que corte a potência independentemente do software e hardware que mantenha ENN dos dois TMC2209 inativo no boot/reset/brownout. Consulte [HARDWARE.md](./HARDWARE.md).

## Validação registrada

Execução local em 27/10/2026, PlatformIO Core 6.2.0:

| Comando | Resultado |
|---|---|
| `pio run -e native` | Sucesso |
| `pio test -e native` | Sucesso — 14/14 casos Unity |
| `pio run -e esp32dev` | Sucesso — firmware, incluindo backend RMT compilado |
| `pio run -e esp32dev -t buildfs` | Sucesso — painel V1 empacotado |

O AP, o barramento MPU6050 e as formas de onda RMT não foram testados em placa física. A variante do módulo, correntes, pinagem real dos módulos TMC2209, Rsense e características elétricas dos dois motores permanecem pendentes.
