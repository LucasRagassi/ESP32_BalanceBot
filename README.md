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

**Fase 1.4.3 — SoftAP:** `status` agora compara eventos e driver reais (modo/configuração, canal, BSSID, clientes e scans). IP 192.168.4.1 ou sucesso no boot não comprovam beacons RF. Não há restart automático; a causa do desaparecimento em repouso segue dependente de captura física. Veja [DIAGNOSTICO_SOFTAP.md](./DIAGNOSTICO_SOFTAP.md). O scan continua manual/autenticado e exige intervalo mínimo de 15 s.

**Fase 1.4.7 — configuração de rede simplificada:** no Android conectado ao AP `BalanceBot-Setup`, abra `http://192.168.4.1/network`, preencha SSID/senha do roteador **2,4 GHz** e toque **Conectar e salvar**. Não há token, login, cookies ou sessões administrativas. Depois de obter DHCP válido, o ESP32 salva e verifica automaticamente a configuração em NVS `bb_wifi`; falhas preservam a configuração anterior. O portal mostra o IP STA para acesso pelo Fedora na mesma LAN e mantém os campos/foco durante as consultas. O reboot recupera a configuração gravada e reconecta.

**Use somente em rede confiável:** qualquer cliente com acesso ao AP ou à LAN pode consultar/alterar a rede. A senha WPA2 e o IP `192.168.4.1/24` do AP foram preservados. HTTP não cifra a senha STA em trânsito; ela não é devolvida em JSON/telemetria/logs. Não publique as portas 80/81 na Internet. A configuração de rede não concede autorização para acionar motores: DEMO, intertravamentos e motores físicos desabilitados permanecem. Veja [REDE_AP_STA.md](./REDE_AP_STA.md) para arquitetura, testes e limitações.

**Console:** NORMAL continua padrão. Use `pio device monitor -b 115200`, comandos `status`, `help` e `log quiet|normal|debug`. O comando `admin` e a geração/validação de token foram removidos; QR administrativo das fases anteriores não é necessário. Falhas críticas, FAULT e watchdog continuam visíveis.

O link **Diagnóstico** abre `/diagnostics`, uma página responsiva de consulta que atualiza os indicadores a partir da API JSON `/health`. Ela não ocupa a conexão WebSocket do controlador. Seus arquivos `diagnostics.html`, `diagnostics.css` e `diagnostics.js` devem acompanhar o upload manual do LittleFS. Em falhas de consulta, a página sinaliza os dados desatualizados. Consulte [DIAGNOSTICO_HTTP.md](./DIAGNOSTICO_HTTP.md) para testes e instruções de gravação.

Depois de gravar firmware e filesystem, conecte ao AP `BalanceBot-Setup` (senha padrão `BalanceBot123`) e abra `http://192.168.4.1/`. Altere a senha antes de qualquer ensaio. É possível substituir credenciais com `BALANCEBOT_AP_SSID` e `BALANCEBOT_AP_PASSWORD` em `build_flags`.

Para diagnosticar acesso pelo Android, abra primeiro `http://192.168.4.1/health`. Este endpoint não usa LittleFS e retorna JSON com estado do AP/HTTP/WebSocket/tarefa, disponibilidade da IMU, heap e razão de reset. Se LittleFS não estiver montado, `/` ainda responde com uma página HTML curta de diagnóstico; os recursos completos podem exigir `pio run -e esp32dev -t uploadfs`. Prefira digitar o endereço HTTP explícito e desative temporariamente a troca automática para dados móveis/rede sem internet, caso o Android abandone o AP.

O painel expõe perfis independentes por motor. Corrente 0 indica não configurada; configure-a somente após validar corrente de fase/RMS, Rsense e dissipação para os módulos reais. Um cliente WebSocket controlador é aceito; perda de conexão/timeout zera a referência remota, sem desabilitar abruptamente o laço de equilíbrio. A configuração de motor pode ser alterada apenas fora de `EQUILIBRANDO`.

## Estados e segurança

`DESARMADO → CALIBRANDO → PRONTO → EQUILIBRANDO`; falhas latched levam a `FALHA`. Com a MPU6050 ausente no boot, o sistema entra em `DEMO`, permanece servindo a interface e não aceita armamento. A detecção inicial ocorre uma vez; tentativas de recuperação são separadas por 5 s e não há leituras I2C a cada ciclo enquanto o sensor estiver ausente. Se a IMU falhar durante equilíbrio, o controle entra em falha e os modelos são desabilitados. A detecção posterior do sensor não rearma automaticamente: requer reinicialização deliberada. São também tratados atraso de controle, inclinação excessiva, intertravamento aberto e divergência de passos simulada.

**Não conecte motores aos pinos STEP/DIR e não use este firmware para equilibrar fisicamente o robô.** Antes de uma futura habilitação, é obrigatório um E-stop que corte a potência independentemente do software e hardware que mantenha ENN dos dois TMC2209 inativo no boot/reset/brownout. Consulte [HARDWARE.md](./HARDWARE.md).

## Diagnóstico do boot e concorrência

Em 115200 baud, NORMAL exibe um resumo organizado e transições importantes; `status` repete o estado. Em DEBUG, as etapas registram início/resultado, timestamp monotônico, heap e NVS/defaults; o loop e a tarefa produzem diagnósticos de heartbeat/watermark a cada 10 s, impressos pelo loop por filas não bloqueantes. Esses diagnósticos periódicos não aparecem em NORMAL/QUIET. Watermark abaixo de 256 unidades gera alerta crítico em qualquer nível. A razão de reset aparece no resumo; resets panic/watchdog/brownout geram aviso crítico. O loop continua atendendo HTTP/WS e não há limpeza automática de NVS.

Não há espera infinita explícita: as filas usam timeout zero, o controle usa `vTaskDelayUntil` em 5 ms e o I2C tem timeout de 20 ms. O snapshot protege apenas a cópia curta por seção crítica/mutex RAII; a persistência NVS ocorre no loop, fora da tarefa de controle. Diagnóstico de watermark não substitui um ensaio prolongado nem prova ausência de stack overflow/deadlock em hardware.

## Validação registrada

Execução local em 27/10/2026, PlatformIO Core 6.2.0:

| Comando | Resultado |
|---|---|
| `pio run -e native` | Sucesso |
| `pio test -e native` | Sucesso — 38/38 casos Unity (Fase 1.4.3) |
| `pio run -e esp32dev` | Sucesso — firmware, incluindo backend RMT compilado |
| `pio run -e esp32dev -t buildfs` | Sucesso — painel V1 empacotado |

O AP, o barramento MPU6050 e as formas de onda RMT não foram testados em placa física. A variante do módulo, correntes, pinagem real dos módulos TMC2209, Rsense e características elétricas dos dois motores permanecem pendentes.
