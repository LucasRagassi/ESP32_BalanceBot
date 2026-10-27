# Auditoria Codex — ESP32_BalanceBot

Data: 27/10/2026. Auditoria estática e validação local; nenhum acesso serial, upload, acionamento de motor, configuração de STEP/DIR ou commit foi realizado.

## Escopo e estado auditado

Leitura integral de README.md, HARDWARE.md, ROADMAP.md, platformio.ini e de todos os arquivos presentes em src/, include/, lib/, data/ e test/. lib/ contém apenas README.md. Base Git local: `b72e760` (`feat: estrutura inicial ESP32 BalanceBot com motores NEMA17`); origin configurado para `git@github.com:LucasRagassi/ESP32_BalanceBot.git`. Não foi consultado o remoto.

O checkout já tinha modificações em README.md, data/app.js, data/index.html, data/style.css, include/balance/safety.hpp, include/balance/types.hpp, src/esp32_app.cpp, src/safety.cpp e test/test_native/test_main.cpp, além de include/balance/mpu6050.hpp não rastreado. Elas foram preservadas. Os resultados descrevem **esses arquivos locais**, não comprovam o conteúdo atualmente gravado no ESP32 nem o estado de origin/main.

Único arquivo de projeto criado nesta auditoria: este relatório. Compilações também regeneraram artefatos em .pio/; a adaptação do ambiente de execução ficou em /tmp/balancebot-audit-pio.

## Conclusão

O firmware é uma base de simulação, não está pronto para equilíbrio físico. O aplicativo instancia somente SimulatedStepper: não instancia Esp32RmtStepPulseEngine nem inicializa STEP/DIR/ENN. TMC2209 UART, corrente física, bateria e feedback físico de posição ainda não estão implementados. Isso é compatível com a fase declarada e com os dois motores diferentes ainda sem parâmetros elétricos confirmados.

Não foi identificado no código atual um bloqueio permanente da interface causado simplesmente pela ausência da MPU6050. Já existem DEMO sem armamento, retentativa espaçada, fallback HTTP e /health independente do filesystem. A causa exata das falhas Android/Fedora continua não comprovada, pois faltam logs e observação da placa/rede. Há defeitos confirmados no temporizador de deadlines, fluxo de rearme, protocolo e edição do painel, além de lacunas relevantes de segurança para qualquer integração física futura.

Gravidade: **alta** = compromete controle/segurança ou funcionalidade central; **média** = falha funcional, diagnóstico enganoso ou robustez insuficiente; **baixa** = manutenção/documentação. Os riscos de atuação física abaixo são prospectivos: hoje os motores são modelos em RAM.

## Inicialização, serviços e concorrência

Sequência em src/esp32_app.cpp:681–826:

1. Serial em 115200, espera de 50 ms e registro da razão de reset.
2. GPIO33 como INPUT_PULLUP; nenhum pino de motor configurado pelo aplicativo.
3. Wire em GPIO21/22, 400 kHz, timeout de transação de 20 ms.
4. Carregamento/validação NVS, com defaults em caso de ausência/erro.
5. `LittleFS.begin(false)` e verificação da existência de /index.html; sem formatação automática.
6. Wi-Fi AP, sleep desabilitado, endereço solicitado 192.168.4.1/24 e criação do AP.
7. Registro de /health, /, /style.css e /app.js; início de HTTP na porta 80.
8. Filas de comandos (8 elementos) e persistência (1 elemento).
9. WebSocket na porta 81; um cliente controlador admitido.
10. Tarefa de controle: core 1, prioridade 3, stack solicitada de 6144 bytes. Detecção inicial da IMU ocorre nessa tarefa.

O loop Arduino atende HTTP/WebSocket, envia snapshots a 10 Hz e persiste NVS fora da tarefa de controle. Configuração é inicializada antes da criação da tarefa; depois disso o controle a modifica e envia cópias pela fila. Não identifiquei acesso concorrente desprotegido à configuração nesse fluxo. SnapshotStore protege somente a cópia com seção crítica no ESP32 e mutex no host. Wire é usado pela tarefa de controle após sua inicialização no setup. As filas usam timeout zero; falha de criação da fila de comandos impede a criação da tarefa e preserva os serviços de rede.

Limitações: WebServer é síncrono; cliente lento e stream de arquivos podem atrasar o loop de rede. NVS também pode atrasar esse loop. O controle executa I2C e Serial: a alegação de período de 5 ms não é garantia de execução em tempo real. O SDK instalado tem CONFIG_FREERTOS_HZ=1000, portanto pdMS_TO_TICKS(5) corresponde a cinco ticks neste build.

## Achados e plano de correção

### A01 — Deadline e jitter calculados contra o ciclo seguinte — alta

Evidência: src/esp32_app.cpp:445–457. `nextWake` começa no instante atual e é incrementado **antes** da primeira execução, que acontece imediatamente. Assim, em regime nominal, `before - nextWake` fica perto de -5000 µs e o jitter absoluto perto de 5000 µs. A condição `lateness > 2500` só acusa atraso quando a execução está aproximadamente 7500 µs além do início esperado do ciclo atual. A instrumentação subestima atrasos e pode deixar de gerar ControlDeadline no limiar pretendido.

Correção proposta: definir explicitamente o instante esperado do ciclo atual, alinhar a primeira espera e medir atraso de início e tempo total separadamente. Incluir publicação/diagnóstico no orçamento, definir política para overrun/catch-up e testar uma linha de tempo sintética com atraso conhecido. O execution_us atual é capturado antes da publicação e logs finais; não mede todo o trabalho da iteração.

### A02 — Desarmar impede novo armamento sem reiniciar — média

Evidência: src/safety.cpp:7–58 e src/esp32_app.cpp:425,501–511. `disarm()` leva Ready/Balancing a Disarmed; `requestBalance()` só aceita Ready. `beginCalibration()` e `finishCalibration()` são chamados apenas na entrada da tarefa. Logo, depois de desarmar, não existe comando de recalibração/rearme no aplicativo. Até desarmar estando Ready causa isso.

Correção proposta: definir fluxo deliberado Disarmed → Calibrating → Ready com validação atual da IMU e intertravamento; preservar Fault e Demo latched conforme política. Testar armamento, desarme e novo armamento completos.

### A03 — “Calibração” não calibra e a validade da IMU é insuficiente — alta antes de hardware

Evidência: src/esp32_app.cpp:182–239,425–431 e src/angle_estimator.cpp:15–34. A inicialização valida identidade/registros e uma leitura, mas não estima bias do giroscópio, estabilidade, orientação ou plausibilidade da aceleração. Todos os inteiros convertidos são finitos: um pacote de 14 bytes zerados passa por `readMpu6050()` e `atan2(0,0)` não é rejeitado pelo filtro. Ready é publicado antes de uma estimativa inicial de ângulo pelo filtro; comandos são processados antes da leitura de cada ciclo.

Correção proposta: manter Calibrating até haver amostras estáveis, plausíveis e recentes; estimar offsets, validar norma/faixas e orientação e estabelecer tempo de estabilização após despertar o sensor. Exigir leitura válida atual e ângulo seguro antes de aceitar armamento. requestBalance hoje só exige ângulo finito; o limite de inclinação é aplicado depois pelo health check, antes da atuação no fluxo atual.

### A04 — Timeout I2C maior que o período e logs no controle — alta antes de hardware

Evidência: src/esp32_app.cpp:518–548,630–635,696–698. Uma transação pode consumir 20 ms contra um período de 5 ms; inicialização/retentativa envolve várias transações. Logs Serial de detecção, erros e watermark também estão dentro da tarefa. Isso pode causar overrun e catch-up sem espera. Uma falha de leitura durante Balancing desabilita os modelos na própria iteração, mas o tempo até essa decisão depende do I2C.

Correção proposta: orçar aquisição, separar recuperação lenta do caminho crítico e transportar logs para tarefa de menor prioridade. Definir limite temporal total e comportamento de parada ao excedê-lo; medir em hardware depois de autorizado. Não basta validar ausência de espera infinita.

### A05 — Comandos de desarme/desconexão podem ser descartados — alta antes de hardware

Evidência: src/esp32_app.cpp:242–251,282–335,460–511. Todos os pedidos compartilham fila de oito entradas e `xQueueSend(...,0)`. Não há reserva/prioridade para Disarm/Disconnect nem confirmação ao cliente. Sob saturação eles podem ser perdidos. O timeout remoto zera movimento, mas intencionalmente não desarma o equilíbrio. O consumidor também drena a fila sem orçamento explícito por ciclo.

Correção proposta: canal/flag prioritário de parada, movimento por mailbox e configuração por fila distinta; limitar trabalho por ciclo e responder aceitação/rejeição. Testar inundação, desconexão e parada. O intertravamento GPIO33 é independente desses comandos no controle, mas não substitui corte físico.

### A06 — Parser de mensagens não valida JSON — média

Evidência: src/esp32_app.cpp:253–348. Identificação depende de substrings exatas como `"type":"motion"`: JSON válido com espaços entre campos é ignorado. strtof aceita prefixos sem validar o fim do token; hasTrueValue aceita prefixo `true`. Campos duplicados, estrutura e tipos não são validados. O painel atual usa JSON.stringify e produz o formato esperado, mas clientes externos e entradas malformadas não são tratados rigorosamente.

Correção proposta: parser JSON com tamanho/profundidade limitados, esquema e tipos explícitos, enum de comandos e resposta de erro. Testar mensagens truncadas, espaços, duplicatas, números não finitos, campos ausentes e saturação. O limite de 512 bytes e as verificações de finitude/faixa já existentes devem ser preservados.

### A07 — Controle WebSocket sem autenticação de aplicação e sem expiração da posse — média

Evidência: src/esp32_app.cpp:32–37,282–299,792–795; data/app.js:9–27. A senha padrão do AP é pública no repositório. Qualquer cliente associado pode ocupar a vaga única; não há autenticação/origin check configurados, heartbeat ou liberação do controllerClient pelo timeout de movimento. Uma conexão TCP que permaneça aberta e silenciosa mantém a posse mesmo após expirar o comando. Abas adicionais ficam reconectando a cada 1,5 s.

Correção proposta: credenciais de provisionamento adequadas, autenticação/origem do controlador, lease/heartbeat e distinção entre observador e controlador. Informar “controlador ocupado” no painel. Não há evidência de exposição à internet: o modo atual é apenas AP.

### A08 — Reconexão retoma referências antigas do painel — média

Evidência: data/app.js:10–18,64–66,103. Desconectar zera a referência no firmware, mas os sliders mantêm o valor anterior. onopen chama sendMotion e o timer continua transmitindo a cada 200 ms. Ao reconectar, a demanda antiga reaparece automaticamente; caso o controle continue Balancing, isso retoma o movimento simulado sem nova ação deliberada.

Correção proposta: zerar controles ao desconectar/reconectar, exigir nova intenção de movimento e tratar aba oculta/perda de foco conforme política de operação. Validar com testes de navegador e relógio controlado.

### A09 — Telemetria sobrescreve edição de vários campos — média

Evidência: data/app.js:45–52,67–72. A cada 100 ms, `setIfInactive()` substitui todo campo que não tenha foco. Alterar um valor e focar o próximo restaura o primeiro para o valor recebido do ESP32 antes de clicar em Salvar. Não há ACK de aplicação/persistência; a telemetria confirma RAM, não gravação NVS.

Correção proposta: formulário com rascunho/dirty por campo, carregamento inicial explícito e respostas distintas “aplicado” e “persistido”. Botão de iniciar deve refletir Ready: hoje onopen o habilita antes da primeira telemetria e, depois, só considera DEMO/disponibilidade da IMU, podendo exibir ação indisponível em Fault/Disarmed.

### A10 — Persistência parcial e sem versão de esquema — média

Evidência: src/esp32_app.cpp:141–165. As chaves são gravadas uma a uma com curto-circuito `&&`. Uma falha intermediária deixa mistura de valores anteriores/novos; no boot essa mistura pode continuar válida e ser aceita. Não há versão, checksum ou unidade atômica da configuração. Alguns campos de RobotConfig não são persistidos, como maximumTiltDeg e maximumVelocityReference: o contrato atual de persistência é parcial.

Correção proposta: formato versionado e consistente (por exemplo, blob único validado ou slots com marcador de commit), política de migração e testes com falha de gravação em cada etapa. Explicitar quais campos persistem e devolver falha ao painel. Não apagar NVS como solução genérica.

### A11 — /health informa início solicitado, não saúde comprovada — média

Evidência: src/esp32_app.cpp:737–775,776–778,792–795. `httpServerReady` e `webSocketReady` são definidos true após begin sem teste de funcionamento. `controlTaskReady` registra criação, não heartbeat/avanço; `ok` é sempre true, inclusive sem tarefa/AP. Falha de softAPConfig é só logada, sem refletir diretamente o estado agregado. LittleFS só verifica index.html, não JS/CSS ou versão do conjunto.

Correção proposta: distinguir iniciado, acessível e saudável; incluir idade do snapshot/heartbeat, erro de inicialização, integridade dos três assets e identificador de firmware/filesystem. Ajustar o contrato de ok e o código HTTP. A string de SSID é inserida no JSON sem escape e retornos de snprintf não são checados; strings customizadas/valores extensos precisam de tratamento.

### A12 — Falha do backend de eixo ignorada pelo controle — alta antes de hardware

Evidência: src/esp32_app.cpp:569–573 e src/stepper.cpp:41–64. Retornos de `leftAxis.update()` e `rightAxis.update()` são ignorados. Para dt > 0,1 s a atualização retorna false sem necessariamente zerar a velocidade anterior; falhas futuras de driver também não geram fault por esse caminho.

Correção proposta: transformar rejeição/falha de atuação em estado seguro explícito, zerar/desabilitar ambos os eixos e registrar a causa. Testar driver que recusa comando e dt inválido. Hoje os drivers são somente simulados.

### A13 — Backend RMT incompatível com velocidades baixas da rampa — média, latente

Evidência: src/esp32_rmt_step_pulse.cpp:42–65. Com tick de 1 µs e duration1 limitado a 32767, frequências de 1 a 30 steps/s são recusadas. A rampa padrão esquerda começa em aproximadamente 15 steps/s por ciclo de 5 ms. O backend ainda não está ligado aos eixos, portanto isso não explica a rede nem produz pulsos atualmente. setFrequency também para/reinicia a transmissão em cada chamada; direção não pode ser trocada durante transmissão.

Correção proposta: projetar intervalo completo de frequências, transição por zero, sentido, quantização, atualização sem glitches e confirmação de stop. Ensaios futuros devem validar onda e timing com potência dos motores removida. Não conectar esse backend ao aplicativo apenas porque compila.

### A14 — Cobertura nativa não testa integração ESP32/painel — média

Evidência: platformio.ini e test/test_native/test_main.cpp. Os 16 testes cobrem unidades de PID, configuração em memória, estado seguro, identidade, filtro, comandos, rampas e perda de passos simulada. Não cobrem setup/loop, Wire, NVS real, LittleFS, sockets, filas FreeRTOS, temporização do ciclo, concorrência do SnapshotStore, browser ou RMT. MockImu não é injetado no aplicativo: este chama funções Wire diretamente. O native_main apenas retorna 0; `pio run -e native` prova compilação, não execução do robô simulado.

Correção proposta: extrair orquestração do ciclo com relógio/IMU/transporte/persistência injetáveis, preservando adapters ESP32. Acrescentar regressões dos achados acima, teste de snapshot concorrente e integração HTTP/WS/UI. Reservar testes de bancada para o que depende da placa. As guards ARDUINO_ARCH_ESP32 e UNIT_TEST evitam corretamente conflito de main e dependências Arduino no host; RMT vira unidade vazia no build nativo.

### A15 — Builds não fixam versões e documentação de corrente diverge — baixa

Evidência: platformio.ini usa `platform = espressif32` e WebSockets `^2.4.1`; o ambiente instalado resolve 6.12.0 e 2.7.3. Builds futuros podem usar outro conjunto. data/index.html chama o valor de “corrente RMS”, enquanto HARDWARE.md deixa RMS/peak pendente de definição. `motorElectricalSpecsConfirmed` é informativo e não bloqueia requestBalance; isso é aceitável só enquanto não existir atuação física.

Correção proposta: fixar versões após homologação, registrar toolchain e alinhar unidade/semântica do campo antes da integração. Futuro caminho físico deve exigir validação dos componentes e corrente adequada por eixo, além do corte independente descrito em HARDWARE.md.

### A16 — Comparação de WHO_AM_I aceita bit alto indevido — baixa

Evidência: include/balance/mpu6050.hpp:7. A máscara 0x7e aceita também 0xe8/0xe9, além de 0x68/0x69. O teste “accepts_only_compatible_identity” não cobre esses valores. Não é suficiente como prova de identidade estrita.

Correção proposta: estabelecer valores/reservados aceitos conforme especificação do componente confirmado e testar todos os 256 bytes. Endereço I2C do aplicativo é fixo em 0x68: módulo configurado em 0x69 não será encontrado, ainda que o helper aceite essa identidade.

## Interpretação dos sintomas relatados

| Sintoma | O que o checkout atual permite concluir | Hipóteses/verificação futura |
|---|---|---|
| Android associa ao AP mas não abre página | AP, HTTP e WS são etapas distintas; não há DNS/captive portal. `/health` independe do LittleFS e `/` tem fallback. | Usar URL HTTP literal; confirmar permanência no AP sem internet, IP DHCP, rota e resposta de /health. Falta de abertura automática não prova falha HTTP. Firmware/filesystem antigos ou não gravados permanecem hipóteses. |
| Fedora falha ao acessar | Não há código específico de sistema operacional. Não foram coletados erro do navegador, IP, rota ou resposta HTTP. | Comparar acesso direto sem proxy/VPN, rota escolhida para 192.168.4.1, eventual conflito de sub-rede, HTTP versus HTTPS e portas 80/81. Não atribuir a firewall/SELinux sem evidência. |
| I2C repetido sem MPU6050 | Uma detecção inicial e retentativas a cada 5 s, sem leitura por ciclo enquanto ausente. Demo recusa armamento. Se Wire.begin falhar, i2cReady permanece false e não há reinicialização do barramento. | Erros em cada retentativa ainda são possíveis. Fluxo contínuo a 200 Hz sugere firmware diferente ou outro caminho a investigar. Capturar timestamps/log completo; não instalar sensor para mascarar a falha. |
| Preferences.begin: NOT_FOUND | Namespace inexistente em abertura read-only explica esse erro na implementação Preferences instalada. O checkout atual já faz probe nvs_open e trata namespace ausente antes de Preferences.begin, usando defaults. | Comparar firmware gravado com checkout e distinguir primeiro boot de erro NVS real. Não há evidência para apagar partição. O fallback atual também mascara diferença entre tipos de erro no log agregado. |
| Boot ilegível | Serial.begin e monitor_speed concordam em 115200. Não foi capturado boot serial; o código não demonstra causa. | Conferir baud/8N1, porta/monitor e captura desde reset. Distinguir bytes iniciais isolados de texto permanentemente corrompido; só investigar cabo/alimentação/reset com evidência. Não alterar baud/hardware por suposição. |

Triagem futura, sem motores e somente quando autorizada: coletar boot completo em 115200 e heartbeat, testar `http://192.168.4.1/health`, `/`, `/app.js` e `/style.css` em cada cliente; depois inspecionar handshake WS em `ws://192.168.4.1:81/`. Se health falhar, investigar rede/loop/reset; se health funcionar e root cair no fallback, investigar filesystem; se HTML/assets funcionarem sem telemetria, investigar JS/WS/controlador ocupado. Registrar status HTTP, corpo e tempo de resposta. Nenhum desses acessos à placa foi executado nesta auditoria.

## Compilação e testes executados

PlatformIO Core 6.2.0. Primeira tentativa literal de `pio run -e native` falhou porque /home/ragassi/.platformio/platforms.lock não podia ser escrito no sandbox; não foi falha do firmware. Para concluir, foram copiados os metadados das plataformas native/espressif32 para /tmp/balancebot-audit-pio e usados links de leitura para os pacotes já instalados, com `PLATFORMIO_CORE_DIR=/tmp/balancebot-audit-pio` e `PLATFORMIO_SETTING_ENABLE_TELEMETRY=no`. Nenhuma dependência precisou ser baixada.

| Comando, com esse ambiente | Resultado observado |
|---|---|
| `pio run -e native` | Sucesso |
| `pio test -e native` | Sucesso, 16/16 casos Unity |
| `pio run -e esp32dev` | Sucesso; RAM estática 47.164/327.680 bytes (14,4%); flash 877.213/1.310.720 bytes (66,9%) |
| `pio run -e esp32dev -t buildfs` | Sucesso; /index.html, /style.css e /app.js incluídos em littlefs.bin |

Conjunto resolvido: Espressif32 6.12.0; framework-arduinoespressif32 3.20017.241212+sha.dcc1105b (Arduino 2.0.17); toolchain Xtensa 8.4.0+2021r2-patch5; WebSockets 2.7.3. A compilação ESP32 recompilou as fontes e bibliotecas; não houve warning relevante visível nessa execução. RAM estática não inclui consumo dinâmico completo de Wi-Fi, filas, stacks e conexões.

Inspeção adicional dos formatos JSON com valores representativos: telemetria DEMO ocupou 540 bytes para buffer de 640; /health ocupou 379 para buffer de 512. Isso não comprova limite máximo, mas não sustenta a hipótese de truncamento inevitável com os valores padrão. O retorno de snprintf deve ser validado para garantir o contrato em outros cenários.

Buildfs só gera imagem local; não prova que a placa tenha filesystem gravado/montado. Compilação e Unity não validam AP, desempenho em FreeRTOS, estabilidade prolongada, MPU6050 real ou pulsos RMT.

## Ordem recomendada para a próxima etapa

1. **Diagnóstico reproduzível da interface:** identificar versão gravada e colher evidências de boot/health/HTTP/WS nos dois sistemas. Manter DEMO e saídas físicas sem inicialização. Não apagar NVS ou formatar LittleFS preventivamente.
2. **Correções de software com regressões:** A01/A02, formulário e reconexão (A08/A09), parser/ACK e filas prioritárias (A05/A06), health e persistência (A10/A11). Separar testes de unidade, integração e bancada.
3. **Robustez do controle ainda simulado:** calibração/plausibilidade, orçamento I2C/logs e falhas dos eixos (A03/A04/A12), com relógio e adapters testáveis. Medir heap/stack/jitter em ensaio prolongado somente quando autorizado.
4. **Preparação para futura integração:** resolver A07/A13/A15/A16 e fechar requisitos elétricos/mecânicos dos dois motores/TMC2209, orientação da IMU e corte físico. Não habilitar motores apenas com base em testes nativos ou build bem-sucedido.

Nenhuma correção foi aplicada nesta execução. Aguardando a próxima instrução.
