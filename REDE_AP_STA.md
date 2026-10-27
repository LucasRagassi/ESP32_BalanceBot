# ESP32_BalanceBot — AP+STA e configuração de rede

## Fase 1.4.7 — regra vigente: acesso direto em rede confiável

Firmware **`1.4.7-network-open`**. A administração de rede foi simplificada por decisão de prototipagem: `/network` e todos os endpoints `/api/network/*` dispensam token/login/sessão. Campos e mensagens de token, geração/validação no backend, comando serial `admin` e classes exclusivas de autenticação foram removidos. Não há cookies, sessão administrativa ou credenciais de navegador. Ferramentas QR antigas existentes em `tools/` foram preservadas como arquivos locais históricos, sem integração ao firmware ou necessidade no fluxo atual.

### Uso no Android e Fedora

1. Conectar o Android ao AP **BalanceBot-Setup**, usando a senha WPA2 existente, e abrir `http://192.168.4.1/network`.
2. Digitar o SSID e a senha de um roteador **2,4 GHz**, ou tocar em **Buscar redes** e selecionar uma rede. Para SSID oculto, digitar o nome manualmente. O ESP32-WROOM clássico não suporta SSID exclusivo de 5 GHz. WPA/WPA2 Personal e rede aberta explicitamente selecionada continuam suportados; não há provisionamento Enterprise/captive portal.
3. Tocar **Conectar e salvar**. O ESP32 conecta e aguarda DHCP por até 25 s. Não há confirmação manual, estado awaiting_confirmation ou expiração de confirmação.
4. Em sucesso, aparece **Configuração salva e verificada na NVS**, o IPv4 recebido e instrução de acesso pela LAN. No Fedora por Ethernet ao mesmo roteador, abrir `http://<IP_STA>/`, `/network` ou `/diagnostics`. O endereço `192.168.100.26` observado anteriormente é exemplo de DHCP, não endereço fixo.
5. Se o Android desconectar durante troca de canal, reconectar ao AP e reabrir `/network`. A operação continua no ESP32, mesmo fechando a aba. O resultado permanece em RAM até nova operação ou reboot. No reboot, a configuração persistida reconecta automaticamente; o resultado deste boot começa em `idle`.

### Arquitetura preservada e transação

- `WIFI_AP_STA`, SSID/senha WPA2 do AP e `192.168.4.1/24` foram preservados. Falha ou ausência de STA não desliga o AP. HTTP 80 e WebSocket 81 usam as mesmas instâncias nas duas interfaces. `/health`, `/diagnostics`, painel e assets continuam no LittleFS.
- Só a operação explícita POST `/api/network/connect` arma a candidata em RAM. Um evento de IP isolado, consulta de status ou reconexão não inicia persistência. Retirar autenticação de rede não muda permissões/intertravamentos de movimento.
- Cada tentativa recebe epoch não nulo. Associação deve corresponder ao SSID esperado e BSSID real; GOT_IP exige a geração atual, interface STA, DHCP ativo e lease igual ao estado atual do driver. Há barreira não bloqueante de desassociação e limpeza do lease anterior antes da próxima tentativa. O core rejeita epoch atrasado, falta de associação, eventos duplicados e lease após o prazo.
- IPv4/máscara/gateway devem ser válidos, unicast e consistentes; sem endereço de rede/broadcast, máscara não contígua/nula, loopback/link-local ou /31 e /32. A sub-rede STA inteira não pode sobrepor `192.168.4.0/24`. Não se exige conectividade à Internet.
- NVS própria `bb_wifi`, registros `BW01`, versão 1, CRC32, dois slots de 110 bytes e seletor ativo continuam compatíveis com configurações anteriores. Grava no slot inativo, faz commit, relê/compara integralmente e só então troca/verifica o seletor. Slot anterior permanece intacto. Candidata não selecionada nunca é adotada no boot.
- Autenticação Wi-Fi, DHCP, timeout, conflito, cancelamento ou NVS falhando preservam/restauram a configuração anterior. Sem configuração anterior, permanece apenas AP. Erros específicos: `authentication_failed`, `dhcp_failed`, `connection_timeout`, `subnet_conflict`, `storage_failed`, `cancelled`. O resultado da operação não é ocultado pela reconexão posterior.
- DHCP duplicado não regrava. Segunda solicitação durante provisionamento ou scan recebe 409. Cancelamento exige `trial_id` da operação ativa para impedir que uma resposta antiga cancele outra tentativa; esse identificador não é token de autenticação nem sessão. Cancelamento após commit não desfaz a gravação. Um novo clique após conclusão é uma nova operação.
- Reconexão de credenciais gravadas usa backoff de 1, 2, 4, 8, 16 e máximo 30 s, sem apagar credenciais. Scan só inicia por ação explícita; é assíncrono, limitado a 16 resultados 2,4 GHz e cooldown de 15 s. Scan e associação não se sobrepõem.
- Persistência ocorre no loop Arduino, nunca no callback Wi-Fi ou na tarefa PID. Não foram adicionadas tarefas de rede de alta prioridade, reinícios periódicos de Wi-Fi nem recriação de servidores. PID, FreeRTOS de controle, watchdog, intertravamentos/paradas e invalidação de comandos antigos permanecem; DEMO e motores físicos desabilitados continuam obrigatórios. Nenhum STEP/DIR/TMC2209 físico foi habilitado.

### Formulário estável

A abertura inicia leitura automática de status e resultados de scan já armazenados, sem iniciar varredura. Polling de status a cada 3 s, timeout de 5 s, uma requisição por vez e backoff até 30 s em falhas; pausa em segundo plano e retoma automaticamente, inclusive após retorno à página/reboot, sem reautorizar.

Estado de formulário separado da telemetria: polling só atualiza elementos de status e botões. Nunca recarrega página, recria inputs, reescreve valores digitados ou desabilita campos em edição. Dirty/editing, IME, foco, cursor/seleção e edição durante requisições permanecem preservados. Lista de scan é atualizada somente quando não interfere com edição/foco; não altera SSID manual ou rede selecionada. Respostas canceladas/atrasadas são descartadas por geração e sequência. Senha fica apenas na memória da página até navegação; não usa localStorage, sessionStorage, cookies ou logs.

### APIs sem autenticação adicional

| Método/rota | Resultado |
|---|---|
| GET `/network`, `/network.css`, `/network.js` | Assets LittleFS, sem formulário de token. |
| GET `/api/network/status` | Estado AP/STA, erro, retry, presença de credenciais, epoch, trial_id e result/result_error/result_ip; sem senha ou SSID STA salvo. |
| POST `/api/network/connect` | Form URL-encoded `ssid`, `password`, `open=0/1`; 202 inicia conexão/salvamento. Não colocar senha na URL. |
| POST `/api/network/scan` | Form vazio; 202 inicia scan; 409 se ocupado/AP inconsistente/cooldown. |
| GET `/api/network/scan` | Resultados de scan, incluindo SSIDs detectados; nunca senha salva. |
| POST `/api/network/cancel` | Form `trial_id`; 200 cancela candidata; 409 se nonce incorreto ou operação concluída. |
| GET `/health`, `/diagnostics`, `/` e assets | Funcionam nos IPs AP e STA; sem senha ou token. |

Formato/tamanho do POST e campos duplicados/desconhecidos continuam validados. Rotas antigas `/api/network/test` e `/api/network/confirm` não existem (404). Assets antigos e firmware novo devem ser substituídos juntos em eventual upload manual. mDNS continua opcional; acesso por IP é obrigatório e independe dele.

Consultas reproduzíveis, sem token:

```sh
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/api/network/status
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/health
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/network
```

Para configurar senha, preferir o formulário; não colocar credenciais em shell history/process list ou capturas públicas. Depois do DHCP, repetir consultas com o IP STA recebido para validar LAN.

### Segurança e riscos residuais

**Usar somente em rede confiável.** Qualquer cliente com acesso ao AP ou à LAN pode ler o scan, iniciar conexão, mudar as credenciais e cancelar uma operação ativa. WPA2 do AP protege a associação ao AP, mas não restringe os clientes da LAN. Páginas maliciosas podem tentar enviar formulários HTTP ao dispositivo; não há mecanismo de administração/CSRF autenticado nesta fase. HTTP não cifra credenciais; flash/NVS não está criptografada. Senhas Wi-Fi não são devolvidas em JSON/telemetria/logs da aplicação. Nenhuma porta foi aberta na Internet e não foi adicionado port forwarding/UPnP; não encaminhar portas no roteador.

A autorização futura de motores deve ser independente e avaliada antes de qualquer habilitação física. Remover token da rede não remove intertravamentos, estado FAULT ou restrições DEMO. O controle de simulação previamente disponível na LAN não foi expandido por esta fase.

AP+STA compartilha rádio/canal; associação ou scan pode provocar interrupções temporárias no Android. Nenhuma rotina agressiva foi introduzida para reiniciar AP. Estado consistente do driver não comprova beacons RF: a investigação de desaparecimento do SSID continua em [DIAGNOSTICO_SOFTAP.md](./DIAGNOSTICO_SOFTAP.md). O SDK não fornece epoch próprio em GOT_IP; a correlação usa a geração local e evidências atuais do driver e requer ensaio de eventos intercalados na placa. NVS/flash/rádio podem causar stalls e jitter mesmo fora da tarefa PID; deadline de 5 ms não foi comprovado por testes locais. Falha dupla ao selecionar/restaurar NVS ou corte de energia exige validação física específica.

### Validação e roteiro de bancada

Comandos executados:

```sh
pio test -e native
gjs scripts/test_network.js
gjs scripts/test_diagnostics.js
gjs scripts/test_panel_safety.js
python3 scripts/test_web_routes.py
python3 scripts/test_ap_idle.py
python3 scripts/test_console_integration.py
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

No sandbox, PlatformIO usa `PLATFORMIO_CORE_DIR=/tmp/balancebot-audit-pio PLATFORMIO_SETTING_ENABLE_TELEMETRY=no` para reaproveitar pacotes instalados sem escrita no diretório global. Nenhuma biblioteca de terceiros foi alterada.

Resultados finais desta fase:

- **36/36 testes nativos passaram**. Os dois testes exclusivos de token foram removidos por não haver autenticação administrativa; preservados os testes de console, filtragem crítica, rede, persistência e segurança.
- **6 grupos JavaScript de rede passaram**, incluindo abertura/alteração sem token, ausência de headers/sessões, fragmento legado não transmitido, formulário/IME estável, eventos HTTP atrasados, falhas, reconexão e persistência automática. Testes JS de diagnóstico e WebSocket também passaram.
- **15 registros de rotas/ativos validados**; verificações de console/I2C e AP sem restart periódico passaram. São verificações estáticas, não execução do servidor ESP32 no host.
- **Build ESP32 e buildfs passaram**; nove assets LittleFS. RAM estática **47.996/327.680 bytes (14,6%)**, flash **917.697/1.310.720 bytes (70,0%)**.
- Identidade: `1.4.7-network-open`, build `2026-10-27T23:00:02Z`, base `b72e760a5662` dirty, source SHA-256 `3aef6bb832bcf5663d02f29b702fc273512f9399b0f27e4e2d376dec7d656edf`.

Testes JS usam o código real da página com DOM/fetch/relógio/IME simulados; não substituem Android/rádio/DHCP/NVS reais.

Validação física pendente, sempre em DEMO/motores OFF:

1. Android: abrir `/network` sem login, digitar SSID/senha lentamente durante vários polls e scan; verificar teclado, seleção e foco. Conferir sucesso automático e IP, inclusive após mais de 60 s.
2. Fedora na LAN: `/network`, `/health`, `/diagnostics`, assets e WebSocket por IP STA, sem headers administrativos; conferir que formulário não expõe senha já salva.
3. Configuração anterior válida: senha errada, roteador ausente, DHCP indisponível, máscara/gateway inválidos e sub-rede conflitante devem falhar e restaurar anterior, mantendo AP.
4. Scan/nova conexão simultâneos, cancelamento e DHCP atrasado/duplicado: recusar concorrência e não efetuar gravação indevida. Reboot deve carregar configuração selecionada e reconectar sem token.
5. Desligar/religar roteador depois de salvar: reconexão com backoff sem apagar credenciais. Observar canal/beacons AP e coletar `status` se sumir.
6. Ensaiar flash/NVS/power cut com preservação dos dados e medir heap/heartbeat/jitter durante tráfego e gravação. Não apagar NVS nem formatar LittleFS como recuperação automática.

Não houve upload ou commit nesta etapa. Não foram apagadas NVS nem formatado LittleFS.

<details>
<summary>Histórico até a Fase 1.4.6 — instruções de token/QR abaixo estão obsoletas</summary>


Implementação em 27/10/2026. Versão de firmware: `1.4.0-ap-sta`. Não houve upload, commit, apagamento de NVS, formatação de LittleFS ou acionamento físico de motores.

## Fase 1.4.6 — Conectar e salvar automaticamente

Firmware atual: **`1.4.6-sta-autosave`**. O comportamento relatado (DHCP `192.168.100.26`, canal 5, seguido de expiração) correspondia ao fluxo antigo de confirmação manual. Essa etapa foi removida do firmware, portal e API: não existem mais os estados `awaiting_confirmation`/`confirmation_expired` nem handlers de teste/confirmar. As seções de fases anteriores registram histórico; o fluxo abaixo é o vigente.

No Android: conecte ao AP, abra `http://192.168.4.1/network`, autorize com o token deste boot (comando serial `admin` ou QR local), preencha a rede **2,4 GHz** e toque **Conectar e salvar**. A operação continua no ESP32 mesmo se a aba fechar, o canal mudar ou o Android desconectar. Não há confirmação posterior: ao comprovar DHCP válido, o firmware grava, relê e ativa a configuração. Reconecte ao AP e autorize com o mesmo token para consultar o resultado. Em sucesso, o painel mostra **Configuração salva e verificada na NVS**, o IP atribuído e o endereço para o Fedora pela Ethernet, por exemplo `http://192.168.100.26/`. O endereço real pode mudar no DHCP; o exemplo não é IP fixo.

### Transação e associação dos eventos

- Só POST autenticado `/api/network/connect` arma a candidata em RAM e a autorização de persistência. Eventos de rede, endpoints públicos e reconexão de credenciais salvas nunca armam uma nova gravação.
- Cada tentativa recebe epoch monotônico não nulo. O core exige epoch atual, fase de provisionamento/conexão, associação comprovada e prazo inferior a 25 s. Um booleano de conexão, evento duplicado, epoch anterior, cancelamento ou lease posterior ao timeout não pode gravar.
- O adapter invalida a geração anterior antes de desconectar **somente STA**. Aguarda pelo menos 250 ms sem espera bloqueante e confirma desassociação no driver antes de iniciar a próxima tentativa; limpa o lease anterior e inicia DHCP. Confere SSID esperado e BSSID da associação contra o driver. GOT_IP exige associação da geração corrente, interface STA correta, DHCP ativo, BSSID correspondente e IPv4/máscara/gateway iguais aos valores atuais do stack. Captura o epoch junto ao lease sob mutex; o loop verifica novamente a geração, associação e IP atuais antes de entregar ao core.
- O SDK não fornece um ID de transação próprio em GOT_IP. A correlação usa essa barreira de desassociação, geração local e evidências atuais do driver; não consiste apenas em marcar qualquer evento recebido com o último ID. Cenários de eventos rapidamente intercalados, mudança de BSSID e reconexões continuam no roteiro físico; testes nativos verificam a recusa de epochs atrasados no core, não reproduzem o event loop de rádio do SDK.
- IPv4/gateway devem ser unicast, diferentes, na mesma sub-rede; endereços de rede/broadcast, loopback, link-local, máscara nula/não contígua ou prefixos /31 e /32 são recusados. A faixa STA inteira não pode sobrepor `192.168.4.0/24`. Não há ping, DNS nem teste de Internet como condição de salvamento.
- Somente o loop Arduino executa a gravação transacional. HTTP/cancelamento/scan/reconexão são serializados nesse loop; callbacks não acessam NVS e a tarefa PID não participa. Eventos DHCP repetidos na mesma geração online são idempotentes e não repetem escrita. Uma segunda solicitação enquanto há candidata recebe 409. Um novo clique após término representa uma nova operação autorizada.
- O último resultado (`result`, `result_error`, `result_ip`) fica em RAM até nova operação ou reboot. Reconexão da configuração anterior não esconde falha de provisionamento; desconexão futura não apaga configuração salva nem o resultado de sucesso. No reboot, carrega apenas o slot selecionado/validado e conecta automaticamente, sem regravar. O resultado deste boot começa em `idle`.

### Falhas e segurança

`authentication_failed` identifica motivos explícitos de falha de autenticação/handshake do SDK; `connection_timeout` indica falta de associação em 25 s; `dhcp_failed` indica associação sem lease válido, falha ao preparar DHCP ou lease inválido; `subnet_conflict` rejeita sobreposição; `storage_failed` indica falha de persistência/verificação. Na candidata, todos restauram a configuração anterior (reconexão após 1 s), ou permanecem só com AP se não havia configuração. Uma falha transitória cujo SDK não forneça motivo específico pode terminar como timeout. Cancelamento só é aceito com nonce da operação ativa antes do commit; depois de salvar, retorna 409 e não desfaz a configuração.

SSID/senha STA são persistidos **somente no journal privado NVS**, necessário para reconexão. Não são salvos em armazenamento do navegador nem escritos nos logs da aplicação. O status autenticado informa presença de credenciais, IP e resultado, sem devolver SSID/senha salvos; `/health` mantém `sta_ssid` vazio por compatibilidade e `/diagnostics` mostra identidade omitida. Scan autenticado continua exibindo SSIDs detectados para seleção. O token continua aleatório por boot e só é revelado via comando serial local explícito. HTTP não cifra credenciais em trânsito e a NVS não usa criptografia: mantenha rede e acesso físico confiáveis.

O AP preserva nome, senha e `192.168.4.1/24`; não há desligamento/restart/recovery agressivo de AP, troca de servidores ou tarefa de rede de alta prioridade. AP+STA compartilha rádio/canal: associação/scan pode interromper temporariamente clientes AP. A disponibilidade RF contínua não pode ser comprovada por testes locais; a investigação de desaparecimento do SSID continua em [DIAGNOSTICO_SOFTAP.md](./DIAGNOSTICO_SOFTAP.md). NVS/flash pode causar stalls globais apesar de não rodar no PID; medir heartbeat/jitter na bancada. Nenhuma alteração em PID, GPIO de motores ou drivers físicos.

### Validação 1.4.6 e roteiro físico

Executar localmente:

```sh
pio test -e native
gjs scripts/test_network.js
gjs scripts/test_diagnostics.js
gjs scripts/test_panel_safety.js
python3 scripts/test_web_routes.py
python3 scripts/test_ap_idle.py
python3 scripts/test_console_integration.py
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

Resultados e métricas finais são registrados na seção de validação local abaixo. Testes nativos cobrem autorização, sucesso automático, associação obrigatória, IP inválido/conflito, timeout/DHCP/autenticação, eventos atrasados/duplicados, concorrência, cancelamento, falhas de escrita/leitura/seletor, rollback, reboot e backoff. JavaScript cobre progresso/sucesso/erros persistentes, campo em edição, foco/IME/cursor, atraso de HTTP, varredura e reconexão. Verificações estáticas conferem 15 registros de rotas/ativos e ausência de reinício periódico do rádio; não executam um servidor ESP32 no host.

Após eventual upload **manual de firmware e LittleFS compatíveis**, sem apagar NVS:

1. Conectar e salvar uma rede válida; sem tocar em mais nada, aguardar sucesso. Conferir IP e acesso LAN às páginas existentes e WebSocket. Aguardar mais de 60 s: a configuração deve continuar salva.
2. Reboot após sucesso: STA reconecta e AP permanece configurado; token antigo recebe 401 e `admin` fornece novo token. Não há confirmação pendente.
3. Com configuração válida anterior, testar senha errada, AP ausente, roteador sem DHCP e sub-rede sobreposta: conferir erro, rollback e acesso AP/LAN anterior. Não desligar AP para recuperar.
4. Durante tentativa, tentar scan/nova conexão/cancelamento com nonce errado: 409; cancelamento correto antes do commit preserva anterior. DHCP atrasado/duplicado não pode mudar a configuração após timeout/cancelamento/nova tentativa.
5. Reiniciar roteador depois de salvar: manter credenciais e reconectar com backoff. Validar troca de canal e reconexão Android; medir continuidade dos beacons com outro receptor e coletar `status` se SSID sumir.
6. Falhas reais de flash/NVS e corte de energia durante commit exigem bancada específica; preservar os slots/dados e não apagar NVS para simular. Confirmar recuperação do seletor e integridade do controle.
7. Medir heap/heartbeat/deadline durante gravação, scans e tráfego HTTP/WS, em DEMO e motores físicos desabilitados. Confirmar ausência de SSID/senha STA e token em logs públicos/diagnósticos.

Não houve upload, commit, formatação ou apagamento de NVS nesta execução.

## Fase 1.4.3 — investigação do SoftAP

Versão `1.4.3-ap-observer`: corrige a indicação AP ativo baseada apenas no boot/IP e passa a observar AP_START/AP_STOP, modo/configuração reais, canal/BSSID/clientes e scans. O comando `status` inclui retornos do driver e contadores. Sem STA configurada não são iniciados scans/conexões pela aplicação. Não há watchdog de rede ou restart periódico. Scans explícitos têm exclusão/cooldown de 15 s. A causa da perda de beacons **ainda não foi comprovada em hardware**; consulte [DIAGNOSTICO_SOFTAP.md](./DIAGNOSTICO_SOFTAP.md) para auditoria, correções, distinção entre estado do driver e RF, testes e roteiro de coleta sem apagar evidências.

## Fase 1.4.2 — console serial e acesso explícito ao token

Versão da revisão 1.4.2: **`1.4.2-serial-console`**. O console usa **NORMAL** por padrão. A partir desta revisão o token **não aparece automaticamente no boot**: abrir o monitor local a 115200, digitar `admin` e Enter o revela sob demanda, inclusive quando o monitor foi aberto depois da inicialização. O token é o mesmo até o próximo reboot; repetir `admin` não gera outro.

### Investigação do token ausente

No código anterior, `balanceNetworkSetup()` era chamado após iniciar o AP, antes de iniciar o listener HTTP, criar filas e iniciar a tarefa de controle. Ele obtinha 32 bytes usando o byte baixo de chamadas a esp_random, convertia para 64 caracteres hexadecimais e imprimia uma única linha `[NET ADMIN]`. Não havia recuperação por comando, flag de geração validada ou acesso posterior ao valor. Um monitor conectado depois do boot perderia a linha. Firmware gravado diferente, baud/reset/porta e falha real de inicialização também permanecem hipóteses: não houve leitura da placa nesta revisão para determinar qual ocorreu no hardware do usuário.

Agora `AdminToken` tem estado ready explícito, usa `esp_fill_random` para 32 bytes após configurar o rádio, valida o resultado e só permite autenticação após inicialização bem-sucedida. Verifica disponibilidade de modo Wi-Fi no adapter e rejeita falha da fonte/padrão constante óbvio, produzindo erro crítico e mantendo configuração bloqueada. Isso não é prova estatística de entropia: a fonte é o RNG do ESP32 nas condições do driver. O token fica exclusivamente em RAM; não é salvo em NVS. Acesso HTTP faz comparação limitada de 64 posições e exige ready; token vazio ou não gerado nunca autoriza. Não há geração nem impressão na tarefa PID.

O único ponto que lê o valor para saída é `balanceNetworkPrintAdmin()`, invocado pelo dispatcher serial para o comando exato `admin`. Não é usado pelo boot, logger, /health, /diagnostics, handlers HTTP, status público ou WebSocket. A impressão é feita diretamente na UART local, sem colocar o segredo nas filas de log. Nenhuma senha Wi-Fi é impressa.

### Console organizado e comandos

Após a primeira publicação da tarefa de controle (ou timeout de inicialização de 2 s, sem espera bloqueante), NORMAL imprime um resumo: versão/build/commit/reset, estado real DEMO/FAULT, motores físicos OFF, sensor/I2C, AP/STA e IPs, HTTP/WS, LittleFS/painel/portal e instrução para abrir `/network`. Não presume DEMO antes do primeiro snapshot; sem publicação avisa que ainda está inicializando. Modo FAULT tem precedência sobre sensor ausente, também no campo mode da API e no diagnóstico visual.

```sh
pio device monitor -b 115200
```

Se houver várias portas, selecionar a correta com `--port /dev/ttyUSB0` (substituir pela porta identificada em `pio device list`). No monitor, enviar texto terminado por Enter; LF, CR e CRLF são aceitos:

```text
help
status
admin
log quiet
log normal
log debug
```

| Comando | Resultado |
|---|---|
| `status` | Repete resumo atual, incluindo heap/uptime/heartbeat sob solicitação, sem token/senhas. |
| `help` | Ajuda dos comandos seguros. |
| `admin` | Exibe `[NET ADMIN]` e o token atual somente na serial física; se indisponível, explica a falha sem chave parcial. |
| `log quiet` | Só eventos críticos automáticos; respostas a comandos explícitos continuam visíveis. |
| `log normal` | Resumo de boot e transições relevantes; padrão. |
| `log debug` | Etapas, heap, heartbeat, HTTP, sensor/retentativas e detalhes de rede; sem senhas ou token automático. |

Não há comandos de movimento, armamento, GPIO ou apagamento/gravação de NVS nesse console. Parser usa buffer fixo de 32 bytes, leitura de até 32 bytes por iteração e nenhum readString/readStringUntil/espera de linha. Comandos longos, inválidos ou com controles/NUL são descartados até terminar a linha, sem ecoar o texto (pode ser um segredo colado por engano). Backspace é aceito dentro do limite. Comandos são minúsculos; não incluir argumentos depois de `admin`. `status` não requer reiniciar nem perder a rede.

Os níveis podem ser definidos inicialmente pelo build_flag `-DBALANCEBOT_LOG_LEVEL=0` (QUIET), `=1` (NORMAL) ou `=2` (DEBUG), mantendo os flags C++17 existentes. O comando altera só o boot atual, sem persistência e sem recriar servidores. NORMAL não imprime novamente ausência da MPU6050 a cada 5 s, nem heartbeat/heap/requests HTTP ou ciclos Connecting/Backoff. Mostra conexão STA com IP/canal/RSSI, desconexão uma vez por episódio e falhas importantes de rede/storage.

### Segurança, logs e Wire/I2C

Falhas de segurança latched são observadas no snapshot e reportadas como `[SEGURANCA] ... FAULT / FALHA` em **todos** os níveis, incluindo intertravamento/deadline/sensor/passos simulados. Logs críticos de configuração, filas, stack, storage e resets panic/watchdog/brownout não são filtrados por QUIET. Não existe E-stop físico implementado por este console; a exigência de corte independente em HARDWARE.md permanece. A interface não esconde estado/fault quando o log está quieto.

Produtores de log, inclusive a tarefa de controle, só formatam registros limitados e enviam para filas FreeRTOS com timeout zero. Apenas o loop Arduino escreve UART. Há fila crítica independente e mailbox de último crítico; em saturação extrema o último é mantido e um contador avisa coalescência, sem garantir histórico ilimitado de cada mensagem. O estado FAULT continua latched no snapshot e acessível via status/health/interface independentemente da fila. Logs DEBUG podem ser descartados sob carga. Impressão explícita de status/admin pode atrasar o loop HTTP, mas não é feita na tarefa PID. I2C síncrono preexistente, timeout de 20 ms, NVS/rádio e demais limitações temporais da auditoria permanecem; não se promete tempo real rígido.

Investigação no Arduino ESP32 **2.0.17** instalado: Wire usa macros `log_e` de esp32-hal-log, não uma API de nível por instância. Essas macros padrão escrevem diretamente e não obedecem ao filtro ESP-IDF por tag. O driver IDF tem a API suportada `esp_log_level_set("i2c", ...)`, confirmada no esp_log.h instalado. A aplicação usa esse ajuste **somente para a tag i2c**, no setup/loop, enquanto a IMU está indisponível em NORMAL/QUIET; restaura o nível anterior quando disponível. DEBUG solicita detalhes da tag, dentro do limite compilado do SDK. Não se troca o sink global, não se altera tag watchdog/panic e não se modifica biblioteca de terceiros nem se desliga globalmente CORE_DEBUG_LEVEL para esconder falhas.

Para evitar o caminho do Wire que gerava mensagens repetidas em sensor ausente, a detecção agora começa com sondagem de endereço e STOP em 0x68. NACK esperado encerra a detecção antes de requestFrom/repeated-start/WHO_AM_I. As retentativas continuam separadas por 5 s, com estado DEMO seguro. Erros inesperados do próprio Wire (mutex, buffer, barramento) podem continuar aparecendo diretamente; não há silenciamento seletivo suportado dessas macros nesta versão. A eliminação do spam exato do hardware precisa ser confirmada com os logs reais e esse build identificado. Falha/perda real da IMU continua mudando o estado de segurança, não apenas o nível de log.

### Configurar Wi-Fi no Android e QR

1. Conectar o Android a `BalanceBot-Setup` e abrir `http://192.168.4.1/network`.
2. No monitor serial local a 115200, enviar `admin` e Enter.
3. Digitar o token retornado na página e clicar **Autorizar**, ou fornecê-lo ao gerador local abaixo, sem eco:

```sh
python3 tools/network_admin_qr.py --output /tmp/balancebot-network-admin.png
xdg-open /tmp/balancebot-network-admin.png
```

Usar nome novo se o PNG já existir. O QR continua contendo fragmento removido no navegador; nenhum token é publicado pelo ESP32. QR antigo deixa de valer após reboot: enviar `admin` novamente. Não salvar/publicar captura do monitor após esse comando; o token solicitado explicitamente é credencial sensível. Não há impressão automática de chaves, senhas em logs, retorno de token em APIs ou mudança nas regras de autorização do portal. HTTP sem TLS e riscos do PNG/flash descritos abaixo permanecem.

### Validação e pendências

Novos testes nativos verificam geração/ready/falha de fonte/padrão constante, validade/tamanho/comparação, idempotência por boot, rejeição após novo boot simulado, comandos/CRLF/buffer/NUL/backspace e matriz de filtro garantindo críticos em QUIET/NORMAL/DEBUG. `scripts/test_console_integration.py` verifica estaticamente a fronteira serial-only do segredo, guard de autenticação, produtor de log sem UART e timeout zero, NORMAL padrão e política I2C por tag. Teste JS confirma FAULT visível mesmo sem IMU. Esses testes não substituem UART/RNG/barramento reais.

Resultados locais finais: **34/34 casos nativos passaram**, compilação ESP32 e buildfs passaram; testes JS de rede/diagnóstico/painel, verificações estáticas de console/16 rotas e **5/5 testes Python do QR** passaram. Build `2026-10-27T20:38:34Z`, base `b72e760a5662` dirty, source SHA-256 `5648e1614281b9d3c7959ab8d741cc5e435a33a9236f575aee7207e2a79bc67d`. RAM estática 47.796/327.680 bytes (14,6%), flash 911.553/1.310.720 (69,5%); filas do console consomem memória dinâmica adicional. Os checksums do PID, máquina de segurança e máquina Wi-Fi foram preservados. PlatformIO foi executado com o diretório temporário de pacotes já usado nas fases anteriores, pois o diretório global é somente leitura no sandbox.

Na bancada, depois de gravar manualmente firmware e LittleFS compatíveis: abrir monitor **depois** do boot e recuperar token via admin; repetir admin e comparar igualdade, reiniciar e comparar mudança; testar token antigo (401), status/help/níveis, vários minutos sem IMU em NORMAL, timeout/FAULT sem ocultação na web e autenticação no Android/QR. Testar serial desconectada e DEBUG sob tráfego HTTP/scan, observando stack/heartbeat pela interface. Não habilitar motores. Não houve upload ou commit nesta execução.

## Fase 1.4.1 — formulário estável e QR administrativo

Registro da revisão anterior: **`1.4.1-network-qr`**. Naquela revisão foram preservados os arquivos C++ do backend AP+STA/autorização/NVS, HTTP/WS e controle. A Fase 1.4.2 acima atualiza especificamente console, logs e disponibilização do token. Os números de build anteriores são históricos.

### Causa encontrada na auditoria do JavaScript

Não havia `location.reload`, atribuição a innerHTML nem recriação do formulário durante polling. O defeito principal era `execute()` marcar toda consulta como busy e `buttons()` aplicar disabled aos inputs SSID/senha/opção de rede aberta. Isso ocorria a cada 3 s e podia retirar foco e fechar o teclado Android mesmo sem mudar a propriedade value. Havia ainda limpeza do token após submit, limpeza da senha ao concluir teste/selecionar rede e recriação da lista de scan durante edição. Os testes anteriores não modelavam foco/cursor/teclado e por isso não detectaram a regressão.

Agora:

- Draft de formulário separado da telemetria, com flags dirty/editing, revisão de edição e seleção explícita de rede. Polling não escreve value, checked, disabled ou readOnly dos inputs; não move foco, cursor ou seleção e não recria inputs/formulários.
- SSID, senha e token podem ser digitados antes da autorização. Somente botões de ações dependem da autorização/estado da operação. Scan, conexão, gravação, falha de rede e 401 não limpam campos. Selecionar rede altera SSID/opção aberta por ação explícita, sem limpar senha/token.
- A lista de scan só muda quando os resultados mudam; sua atualização é adiada enquanto há input em edição/IME ou foco na própria lista. Resultado atrasado não seleciona SSID nem desfaz seleção/manual. Status e mensagens de operação têm elementos separados.
- Requisições HTTP serializadas, sem sobreposição. Uma ação explícita pode cancelar a leitura de status e aguarda sua conclusão/abort antes de enviar outra. Geração de sessão e sequência descartam respostas antigas, inclusive 401 de token anterior após reautorizar e resposta após bloquear/navegar. Edição durante teste preserva o draft e informa que o teste usa o snapshot enviado, sem enviar as alterações novas automaticamente.
- Polling autorizado de `/api/network/status` continua a cada 3 s após conclusão, timeout de 5 s e backoff de 6, 12, 24 e no máximo 30 s após falhas; sucesso retorna a 3 s. Não repete POSTs automaticamente. Em segundo plano pausa/aborta, preservando o draft; ao voltar consulta novamente.
- Token/senha permanecem apenas em RAM/DOM desta página até **Bloquear** ou navegar para fora, que explicitamente os limpam. Não há localStorage, sessionStorage, cookies, console/log de credenciais ou preenchimento pela telemetria. Manter os campos depois de testar é deliberado para respeitar a edição; bloquear quando terminar.

### Gerar QR offline no Fedora

Ferramenta: `tools/network_admin_qr.py`. Usa Python padrão e a biblioteca local **qrencode-libs** via ctypes; não acessa rede, API externa, serial ou serviços de QR. A biblioteca estava instalada no ambiente de validação (4.1.1, Fedora 44). Se ausente, instalar manualmente `sudo dnf install qrencode-libs`. Referências: [pacote Fedora](https://packages.fedoraproject.org/pkgs/qrencode/qrencode-libs/) e [API oficial libqrencode](https://fukuchi.org/works/qrencode/manual/).

```sh
python3 tools/network_admin_qr.py --output /tmp/balancebot-network-admin.png
xdg-open /tmp/balancebot-network-admin.png
```

O comando solicita o token deste boot **sem eco** no terminal. Não aceita token em argumento CLI, variável de ambiente, URL de argumento ou stdin não seguro; se getpass não puder desabilitar eco, falha. A saída não imprime token nem URL secreta. O destino deve ser novo, fora do repositório (principalmente fora de data/); não sobrescreve arquivos/symlinks e grava PNG com modo 0600, quiet zone de quatro módulos e sem metadados de texto com credencial. O diretório precisa existir. Se já houver PNG anterior, remova-o deliberadamente ou escolha outro nome. Geração de novo QR não renova nem altera o token do ESP32.

Para usar um IP STA privado já conhecido, opcionalmente:

```sh
python3 tools/network_admin_qr.py --base-url http://192.168.1.20/network --output /tmp/balancebot-network-sta.png
```

Substituir pelo IP real. A ferramenta aceita URL HTTP local com IPv4 privado ou BalanceBot.local, caminho `/network`, sem usuário/senha/query/fragmento. O padrão é `http://192.168.4.1/network`. Nenhum token é enviado para esse endereço pela ferramenta: ele só é codificado no PNG.

### Consumir QR e autorizar no Android

O conteúdo é `http://192.168.4.1/network#admin=<TOKEN>`, com fragmento construído por codificação de URL. Conectar o Android ao AP, abrir o PNG local no Fedora e ler com a câmera. O fragmento não faz parte do GET HTTP. Ao executar, o JavaScript valida o token, remove imediatamente o fragmento com `history.replaceState` sem reload e o mantém no campo password/memória da página. Só clicar **Autorizar** inicia chamadas protegidas com `X-Network-Token`; não há envio do token em GET query, `/health`, assets ou endpoints públicos. A política de referência do documento usa meta no-referrer como proteção adicional do navegador.

Um QR recebido depois de começar a editar o token não sobrescreve a edição: o fragmento é removido e surge **Usar token do QR recebido** para substituição deliberada. Fragmentos inválidos/duplicados são removidos e recusados. Se o navegador impedir replaceState, o QR não é aplicado e a página orienta limpar a URL e usar entrada manual. O HTML/JS público nunca contém um token específico nem publica o PNG gerado.

A entrada manual continua disponível. Depois de reboot o backend mantém sua geração aleatória e validação anteriores; QR antigo recebe 401 e a página indica **token inválido ou expirado após reboot**, preservando os campos e exigindo novo token/QR. Um token digitado errado é indistinguível de expirado no backend, portanto não se atribui todo 401 a reboot.

**O QR é uma credencial administrativa sensível.** Quem fotografá-lo ou ler o PNG poderá administrar a rede enquanto o token valer. Não publicar, anexar ao repositório, servir pelo ESP32 nem compartilhar captura de tela. Permissão 0600 protege de outros usuários locais comuns, mas não de root, aplicativo da mesma conta, backups/sincronização ou câmera/clipboard/histórico do leitor de QR. Remover o fragmento limita exposição posterior, não garante apagamento de registros mantidos pelo sistema/leitor. HTTP não protege token/senha contra interceptação; usar apenas AP/LAN confiáveis. Na Fase 1.4.2, a emissão `[NET ADMIN]` ocorre somente após `admin` na serial local; não guardar/divulgar captura dessa resposta. A ferramenta e o cliente não criam novos logs de credenciais.

### Validação da revisão

- `gjs scripts/test_network.js`: simula DOM estável, foco/cursor/seleção, teclado/IME móvel, dirty, polling enquanto digita nos três campos, scan tardio, seleção/manual, edição durante test/confirm/cancel, 401 antigo após nova autorização, lock com resposta atrasada, timeout/backoff/recuperação, navegação e QR válido/inválido/duplicado. Verifica ausência de alterações dos inputs e máximo de uma requisição em voo.
- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools -p 'test_*.py' -v`: cinco testes do gerador, incluindo libqrencode real, PNG/CRC/quiet zone/pixels, fragmento sem query, destinos locais, permissão 0600, recusa de sobrescrita/publicação, saída sem segredo e subprocesso com PTY verificando que ECHO está desabilitado durante o prompt. Só usa tokens sintéticos em arquivos temporários removidos ao final.
- Validação concluída: **30/30 testes nativos**, testes JS de rede/diagnóstico/painel, inspeção das **16 rotas GET/POST**, **5/5 testes Python** do QR, compilação ESP32 e buildfs passaram. O buildfs inclui os nove assets existentes; nenhum PNG administrativo é incluído na imagem.
- Build desta revisão: `2026-10-27T20:12:57Z`; RAM estática 47.684/327.680 bytes (14,6%); flash 904.353/1.310.720 bytes (69,0%). Os checksums dos arquivos C++ de aplicação, rede, estado Wi-Fi, PID e segurança foram comparados ao início desta revisão e permaneceram iguais. Não houve upload ou commit.

Limites: os testes JS usam DOM/relógio simulados, não um navegador Android real. Ainda é necessário gravar manualmente firmware e LittleFS compatíveis e testar teclado aberto por vários ciclos, composição/seleção de texto, perda/retorno da rede, scan durante digitação, reautorização após reboot e leitura do PNG pela câmera. O backend do controle, AP+STA, rollback e NVS permanece igual; riscos de HTTP, flash sem criptografia, controle de simulação aberto na LAN e temporalidade descritos abaixo continuam aplicáveis. Nenhum PID, tarefa FreeRTOS ou GPIO de motor foi alterado.

## Objetivo e caminho de acesso

O Android pode continuar conectado ao AP `BalanceBot-Setup`, senha existente `BalanceBot123`, em `192.168.4.1/24`. O ESP32 também se conecta como STA ao Wi-Fi **2,4 GHz** de um roteador. O Fedora conectado por Ethernet à mesma LAN acessa `http://<IP_STA>/`, `/diagnostics`, `/network` e `/health`; o WebSocket usa `ws://<IP_STA>:81/`. Não há necessidade de o Fedora autenticar com o SoftAP. Isso não corrige nem diagnostica o driver MediaTek MT7902: oferece outro caminho de acesso.

O roteador precisa oferecer banda 2,4 GHz e permitir tráfego entre Wi-Fi e Ethernet. SSID exclusivo de 5 GHz não funciona no ESP32 clássico. SSID compartilhado entre bandas só funciona se também estiver anunciado em 2,4 GHz. Redes guest, isolamento de clientes e VLANs diferentes podem impedir o acesso pela Ethernet mesmo com STA conectada. O robô não é roteador/NAT e não compartilha internet com clientes AP.

## Arquitetura

- `src/wifi_manager.cpp` e `include/balance/wifi_manager.hpp`: validação de credenciais, verificação de sub-redes, journal de duas posições e máquina de estados portável.
- `src/esp32_network.cpp`: adapter Wi-Fi/NVS, autorização, endpoints e varredura. Namespace NVS exclusivo **`bb_wifi`**; não abre nem modifica o namespace `balancebot` do controle.
- `balanceNetworkPrepare()` configura `WiFi.persistent(false)` antes de `WIFI_AP_STA`, desliga a reconexão automática do Arduino e registra eventos. Configurações de driver ficam em RAM; persistência STA é feita apenas pelo journal confirmado.
- AP preserva SSID/senha/IP/gateway/máscara anteriores. Somente a STA é desconectada em retentativa/rollback. Nunca há chamada de troca para modo STA exclusivo.
- HTTP e WebSocket continuam sendo criados uma única vez, com listeners nas interfaces disponíveis (endereço não restrito ao AP). Reconectar STA não recria filas, tarefas, objetos de servidor ou filesystem.
- A máquina avança no loop Arduino antes de atender HTTP; não há espera por associação/DHCP, `while` de conexão, `delay` longo ou tarefa de rede nova. O PID mantém prioridade/core/período anteriores. Os callbacks da tarefa Wi-Fi existente só publicam flags/época; no evento DHCP conflitante também solicitam desconexão imediata ao driver.
- Arquivos `network.html`, `network.css` e `network.js` ficam no LittleFS e reutilizam os estilos existentes. Páginas `/` e `/diagnostics` têm navegação para `/network`.

AP+STA usa um rádio e um canal principal compartilhado; a STA tem prioridade sobre o canal do AP. Associar ao roteador pode migrar o AP para o canal dele e provocar desconexão/reconexão de clientes. Varredura também pode interromper brevemente tráfego. Não se fixa um canal independente para o AP enquanto a STA está conectada. Esse comportamento está descrito na [documentação Wi-Fi da Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.3.4/esp32/api-guides/wifi.html).

O Arduino ESP32 2.0.17 instalado contém ainda um retry interno único da primeira falha de associação, mesmo com AutoReconnect=false; ele ocorre dentro da janela da tentativa. As tentativas posteriores da aplicação seguem o backoff descrito abaixo. Não foi modificado o framework para remover esse comportamento.

## Autorização e segredos

1. A cada boot, com o rádio configurado, o firmware gera um token aleatório de 256 bits e valida a inicialização. Na Fase 1.4.2, só o exibe na serial física após o comando `admin` e Enter: `[NET ADMIN] token deste boot ...`. Não há impressão automática no boot.
2. O operador digita esse token no formulário de `/network`. Ele fica somente na memória da página e é enviado no header `X-Network-Token`, nunca em URL, cookie ou localStorage. Navegar para fora/bloquear a página remove essa autorização no navegador; boot novo invalida o token anterior.
3. **Todos** os endpoints `/api/network/*`, inclusive leitura e scan, exigem esse header tanto pelo AP quanto pela STA. Ausência/token incorreto retorna 401. Comparação do token acumula diferenças em 64 posições, sem saída antecipada por prefixo.
4. POSTs exigem formulário URL-encoded, comprimento declarado até 512 bytes e campos conhecidos sem duplicatas. Não há CORS habilitado; o token não é uma credencial automaticamente anexada pelo navegador. Um formulário de outro site não consegue autorizar mudanças sem conhecer o token.
5. Conectar/salvar/cancelar não reutiliza a conexão de controle aberta nem a senha pública do AP. Cancelar exige ainda o identificador aleatório da operação ativa. Não há endpoint que devolva token de administração ou senha STA salva; logs e respostas nunca incluem senha STA. SSID STA salvo é omitido; scan autorizado expõe nomes detectados para seleção.

**Limites de segurança:** HTTP não oferece TLS; um observador da rede pode capturar token e senha transmitidos. Usar AP/LAN confiáveis, sem encaminhamento externo de portas. Token serial é um segredo de administração: não publicar esse trecho dos logs. O firmware não implementa NVS/flash encryption; credenciais e cópias anteriores de wear levelling ficam em flash, acessíveis a atacante com acesso físico. Na revisão 1.4.1 a senha digitada é preservada durante a edição e removida ao bloquear/sair; strings de requisição do navegador/Arduino podem permanecer em RAM até liberação, portanto não há promessa de apagamento forense. A [documentação NVS da Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html) descreve suas garantias e limites.

O painel/telemetria e WebSocket de simulação existentes continuam abertos na rede local. Essa decisão preserva a comunicação anterior, mas ampliar a presença para a LAN amplia quem pode acessar a simulação. Essa interface **não** autoriza configuração de rede. Antes de habilitar qualquer motor real, será necessária autorização própria para o controle e revisão dos achados de segurança da auditoria. Não usar em rede pública ou expor portas 80/81 à internet.

## Fluxo de provisionamento

1. Gravar manualmente firmware e LittleFS, quando desejado pelo operador. Nenhuma gravação foi feita nesta fase.
2. No monitor serial a 115200, enviar `admin` e Enter para obter o token atual. No Android, conectar ao AP existente e abrir `http://192.168.4.1/network`.
3. Autorizar com o token. O estado mostra AP/STA, presença de credenciais, IP/gateway/máscara, clientes AP, canais reais, RSSI, último erro e código de desconexão.
4. **Buscar redes** inicia varredura assíncrona com redes ocultas habilitadas. GET de resultados é consultado posteriormente. São exibidos no máximo 16 resultados, canais 1–14; canais autorizados dependem do domínio regulatório do rádio. Isso não pressupõe descoberta de 5 GHz.
5. Para SSID oculto, digitar o nome exato manualmente (até 32 bytes UTF-8); resultado sem nome não pode ser selecionado. A varredura não é pré-requisito para um teste. Seleção da lista copia somente o SSID, não fixa BSSID/canal: APs com o mesmo SSID continuam livres para associação pelo driver.
6. Informar senha WPA/WPA2 Personal de 8–63 caracteres ASCII imprimíveis, ou PSK hexadecimal de 64 caracteres. Rede aberta exige checkbox explícito e senha vazia. Enterprise/EAP, WEP e login de captive portal não são provisionados. A interface não permite selecionar tipos de autenticação não suportados da lista; entrada manual não garante compatibilidade do roteador.
7. **Conectar e salvar** guarda candidata em RAM, invalida movimento remoto e inicia associação/DHCP, com prazo de 25 s.
8. DHCP válido e sem conflito, associado à geração atual, dispara persistência transacional e sucesso automático.
9. Falha de autenticação/DHCP/timeout/NVS/conflito ou cancelamento antes da gravação preserva/restaura a configuração anterior.
10. Consultar resultado e abrir `http://<IP_STA>/network` ou `/diagnostics` pelo Fedora na mesma LAN. Não há botão de confirmação nem prazo de 60 s.

Varredura retorna 409 durante associação, backoff ou teste; não disputa uma tentativa em andamento. É permitida com STA online ou sem associação em andamento. Falha de scan não altera credenciais. Scan é cancelado se falhar/expirar e seus resultados são liberados após cópia limitada. Retentativa aguardando um scan termina após ele, sem bloquear o loop. O navegador consulta estado a cada 3 s, com uma requisição por vez, timeout de 5 s e pausa em segundo plano.

## Persistência, rollback e estados

`bb_wifi` contém `slot0`, `slot1` e seletor `active`. Cada slot é um blob de 110 bytes com magic `BW01`, versão 1 em little-endian, SSID, senha e CRC32. Não é serialização de struct com padding. O CRC detecta corrupção acidental; não é autenticação criptográfica.

Somente após comprovar DHCP da candidata autorizada: gravar/commit no slot inativo → reler e comparar integralmente → trocar e verificar o seletor ativo. O slot anterior permanece intacto. Slot preparado sem seletor ativo **nunca é adotado no boot**, mesmo se tiver CRC válido. Escrita/leitura/ativação falhando deixa o anterior selecionado; o adapter tenta restaurar o seletor anterior se sua atualização falhar. Não é feito `erase`, `clear` ou reparo implícito de NVS corrompida. Ausência de namespace/seletor é primeiro uso normal; falha/corrupção detectável é `storage_failed`. Se a configuração ativa estiver corrompida, requer investigação de serviço antes de nova persistência, mantendo AP para diagnóstico.

Em perda de energia durante a gravação automática, o seletor NVS pode conservar o valor anterior ou o novo já verificado; os blobs completos são validados ao carregar. Dois erros físicos consecutivos (gravação do seletor e restauração) podem impedir garantir qual seletor ficou persistido; nesse caso há log de rollback falho e os slots anteriores são preservados para recuperação. Testes nativos simulam falhas lógicas de armazenamento; ensaio de power cut em flash real continua pendente.

| Estado | Comportamento |
|---|---|
| `unconfigured` | Só AP; aguardando configuração ou erro de storage sem credenciais utilizáveis. |
| `connecting` | Tentativa da rede confirmada; prazo 25 s. |
| `online` | Rede confirmada com IP aceito. |
| `backoff` | Retenta após 1, 2, 4, 8, 16 e no máximo 30 s; sucesso reinicia o backoff. |
| `connecting_and_saving` | Candidata autorizada em RAM; DHCP válido dispara salvamento, até 25 s. |
| `subnet_conflict` | Rede confirmada conflita com AP: desconecta e suspende retentativas dessa configuração até novo teste/reboot. |

Conflito de candidata restaura a rede anterior. Conflito da própria rede confirmada não é repetido em loop, para preservar o fallback. Não se muda silenciosamente o AP para outra sub-rede. Comparação considera toda a faixa do prefixo STA, inclusive roteador /16 ou /23 que englobe `192.168.4.0/24`, não somente igualdade do endereço. DHCP sem máscara válida é rejeitado por segurança.

Não é possível conhecer uma sub-rede DHCP antes da oferta. Ao receber IP sobreposto, o callback captura lease/generation para rejeição; antes de atender o próximo HTTP, o loop interrompe DHCP STA, limpa o IP conflitante e executa rollback. Assim a lease não é deliberadamente utilizada para servir a aplicação. Há uma janela de processamento entre instalação do IP pelo stack e rejeição pelo loop; sua duração/remoção efetiva de rotas precisam ser medidas na placa. Uma falha ao limpar lease é registrada. Não se promete ausência absoluta de estado transitório dentro do stack.

## Segurança de movimento e temporalidade

Um contador atômico de época é incrementado em troca de rede, conexão/perda de IP/desconexão STA, scan iniciado, saída de cliente AP e perda do controlador WebSocket. Pedidos remotos são etiquetados com a época. A tarefa de controle invalida a referência e reseta o PID externo ao observar mudança, descarta pedidos de época antiga e verifica novamente antes de obter a demanda. Essa invalidação não depende de uma entrada livre na fila.

O loop fecha somente o cliente WebSocket controlador ao mudar época, preservando o servidor. Mensagens de uma sessão com época antiga também são recusadas no callback. O painel zera sliders ao abrir/perder conexão: reconexão não reaplica demanda antiga; movimento novo requer ação posterior do usuário. Não se trata de E-stop físico nem substitui o corte independente de HARDWARE.md.

TMC2209/RMT físicos continuam sem instância; nenhum STEP/DIR/ENN é inicializado pelo aplicativo. DEMO sem MPU6050 e bloqueio de armamento continuam presentes. Esta fase não resolve os achados A01/A02 da auditoria nem habilita equilíbrio físico.

Nenhuma tarefa extra de alta prioridade foi criada. I2C, logs preexistentes, flash/NVS, drivers do Wi-Fi e servidor síncrono ainda podem afetar latência; estar fora da tarefa PID não garante deadline de 5 ms. Medir jitter/stack/heap durante scans, gravação e reconexões, com motores físicos desabilitados.

## APIs

| Método/rota | Autorização e resultado |
|---|---|
| GET `/network`, `/network.css`, `/network.js` | Assets públicos do LittleFS; não contêm segredos. |
| GET `/api/network/status` | Token obrigatório; estados AP/STA, has_saved, erro, retry, epoch, trial_id, result/result_error/result_ip; sem SSID/senha STA salvos. |
| POST `/api/network/scan` | Token + formulário vazio; 202 em início assíncrono; 409 se ocupado. |
| GET `/api/network/scan` | Token; `idle`, `running`, `complete` ou `failed`, redes e contagem. |
| POST `/api/network/connect` | Token + `ssid`, `password`, `open=0/1`; 202 ao aceitar conexão e salvamento automático. Falhas posteriores aparecem em GET status. |
| POST `/api/network/cancel` | Token + `trial_id`; 200 após cancelar antes do commit; 409 se já terminou/nonce incorreto. |
| GET `/health`, `/diagnostics`, `/`, assets existentes | Continuam acessíveis por AP e STA. |

`/health` adiciona `wifi_mode`, `ap_channel`, `sta_connected`, `sta_ssid` (sempre vazio), `sta_ip`, `sta_gateway`, `sta_netmask`, `sta_state`, `sta_error`, `sta_rssi`, `sta_channel`, `sta_disconnect_reason`, `ws_clients` e `mdns_ready`. `ap_clients` permanece a contagem de estações no AP; STA liga-se a um roteador, não fornece outra lista de clientes. `/diagnostics` mostra os estados e canais. `ok` avalia a infraestrutura e fallback AP; STA desconectada/não configurada não causa 503 por si só. DEMO não é falha da rede. Buffer health ampliado para 3072 bytes, com checagem de truncamento preservada.

Eventos seriais identificam associação, desconexão, ganho/perda de IP e entrada/saída de cliente AP; mostram IP STA, canal, RSSI, motivo numérico e mudanças da máquina. Exemplos de códigos do SDK instalado: 201 AP não encontrado, 202 autenticação, 203 associação, 204 handshake; 15 timeout de handshake WPA. Esses motivos auxiliam o diagnóstico, não comprovam sozinhos senha incorreta ou defeito físico. `connection_timeout` também pode indicar SSID só 5 GHz, sinal ou DHCP.

## mDNS opcional

Desabilitado por padrão. Pode ser habilitado com `-DBALANCEBOT_ENABLE_MDNS=1` nos build_flags existentes; registra `BalanceBot.local` e serviços HTTP/WS sem alterar o acesso por IP. Depende de multicast/resolução mDNS no cliente e da topologia da LAN; pode não atravessar VLANs ou funcionar em todos os Android. O caminho obrigatório é IP STA. O modo opcional precisa de ensaio no roteador real; não deve ser usado como única descoberta. Uma reserva DHCP no roteador pode estabilizar o endereço sem introduzir IP estático no firmware.

## Validação local e limites

- `pio test -e native`: **38/38 casos passaram**. Cobertura descrita na seção 1.4.6, preservando testes de controle, segurança, console e observação AP.
- `gjs scripts/test_network.js`: **6 grupos passaram**, incluindo salvamento automático, resultados de falha e regressões de foco/IME/cursor, autorização, QR e polling.
- `gjs scripts/test_diagnostics.js` e `gjs scripts/test_panel_safety.js`: **passaram**, incluindo FAULT visível e WebSocket sem reaplicar movimento ao reconectar.
- `python3 scripts/test_web_routes.py`: **15 registros GET/POST**, dependências HTML, MIME types e assets validados estaticamente. As rotas antigas test/confirm foram removidas; uso por assets antigos resulta em 404.
- `python3 scripts/test_ap_idle.py` e `python3 scripts/test_console_integration.py`: **passaram**.
- Testes Python do QR local: **5/5 passaram** (`PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s tools -p 'test_*.py'`).
- `pio run -e esp32dev`: **sucesso**, firmware `1.4.6-sta-autosave`, build `2026-10-27T22:26:33Z`, base `b72e760a5662` dirty, source SHA-256 `a9f3c45a28e93f984f5bfd4f770ca6b3e2f37e0ab61d8dc74597ca7be3f2cb73`. RAM estática **48.076/327.680 bytes (14,7%)**; flash **919.109/1.310.720 bytes (70,1%)**.
- `pio run -e esp32dev -t buildfs`: **sucesso**, nove assets empacotados. Nenhum upload foi executado.

No sandbox, comandos PlatformIO usam `PLATFORMIO_CORE_DIR=/tmp/balancebot-audit-pio PLATFORMIO_SETTING_ENABLE_TELEMETRY=no`, para reutilizar pacotes instalados sem escrita no diretório global somente leitura. Nenhuma dependência foi baixada.

## Teste manual de bancada — pendente

Firmware e filesystem novos precisam ser gravados juntos, deliberadamente pelo operador. Identificar a porta real antes de executar (exemplo /dev/ttyUSB0):

```sh
pio device list
BALANCEBOT_PORT=/dev/ttyUSB0
pio test -e native
pio run -e esp32dev
pio run -e esp32dev -t buildfs
pio run -e esp32dev -t upload --upload-port "$BALANCEBOT_PORT"
pio run -e esp32dev -t uploadfs --upload-port "$BALANCEBOT_PORT"
pio device monitor --port "$BALANCEBOT_PORT" -b 115200
```

Não usar erase/erase_flash, não formatar LittleFS no boot e não limpar NVS do controle. Uploadfs substitui a imagem de arquivos, sem alterar o namespace de controle no layout atual. Conferir versão/build/source hash no boot e health; upload pode recompilar e gerar novo timestamp.

Após provisionar pelo Android e anotar IP STA, no Fedora pela Ethernet (substituir `192.168.1.20` pelo IP real):

```sh
BALANCEBOT_STA_IP=192.168.1.20
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/health"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/diagnostics"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/network"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/network.js"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/network.css"
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i "http://$BALANCEBOT_STA_IP/api/network/status"
```

A última chamada deve retornar **401** e não expor configuração autorizada. Testar POST sem token para scan/connect/cancel também deve retornar 401 e não mudar estado/NVS. Para consultar autenticado sem colocar o token na linha de comando/histórico, em bash:

```sh
read -r -s -p 'Token NET ADMIN deste boot: ' BALANCEBOT_NETWORK_TOKEN
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i \
  --header @<(printf 'X-Network-Token: %s\n' "$BALANCEBOT_NETWORK_TOKEN") \
  "http://$BALANCEBOT_STA_IP/api/network/status"
unset BALANCEBOT_NETWORK_TOKEN
```

Não incluir token/senha em URLs ou capturas públicas. Preferir o formulário para testes de senha, evitando shell history/process list. Testes em ambos os IPs devem preservar `/app.js`, `/style.css`, `/diagnostics.js` e `/diagnostics.css`, tipos de conteúdo e WebSocket em porta 81. Abrir uma única aba do painel para não disputar o controlador.

Matriz mínima de hardware ainda pendente:

1. Primeiro boot sem credenciais: AP e DEMO acessíveis, STA unconfigured; configuração do controle inalterada.
2. WPA2 Personal 2,4 GHz válida: Conectar e salvar, sucesso automático com DHCP, acesso Ethernet e reboot retomando STA. Verificar que nenhuma senha aparece em health/status/scan/logs.
3. Rede oculta: entrada manual; SSID 32 bytes e UTF-8; senha errada, AP ausente, 5 GHz exclusivo e DHCP indisponível: timeout/rollback, AP preservado.
4. Cancelamento e falhas durante conexão: preservar anterior; reiniciar antes de selecionar o slot novo não adota candidata.
5. Roteador reiniciado/perda de sinal: backoff limitado e reconexão sem recriar servidores; medir heap, heartbeat e jitter.
6. Roteador em 192.168.4.0/24 ou prefixo maior sobreposto: rejeitar STA, limpar lease, preservar AP; não salvar candidata. Verificar remoção real de IP/rotas e mensagens de erro.
7. AP no canal inicial diferente do roteador: observar canal real e reconexão Android. Confirmar que IP/senha AP não mudam.
8. Token ausente/incorreto, header/nonce de boot anterior, duplicatas e cancelamento repetido: recusar sem gravação. Power cut/NVS cheia/injeção de falha de flash requerem ensaio específico, preservando backup do projeto e dados.
9. Cliente WebSocket com movimento simulado: trocar rede/perder socket e conferir referência zerada, pedidos antigos descartados e reconexão sem movimento automático. Nenhum motor físico conectado/habilitado.
10. Regressão das páginas em Android e Fedora, perda de conexão/reautorização, scan, navegador em background e consulta do resultado pelo IP STA. mDNS, se habilitado, deve ser adicional ao teste por IP.

Riscos residuais: token/senha em HTTP sem TLS, flash sem criptografia, controle de simulação aberto na LAN, limite do WebServer síncrono (body é recebido antes da validação do handler), latência de flash/rádio, janela transitória de DHCP conflitante e falhas duplas de armazenamento. Nenhum teste nativo garante RF, DHCP, NVS físico, deadline ou desempenho sustentado na placa.

</details>
