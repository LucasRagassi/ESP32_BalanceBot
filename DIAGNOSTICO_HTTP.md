# ESP32_BalanceBot — Fase 1.2: diagnóstico reproduzível HTTP

Data: 27/10/2026. Referência: AUDITORIA_CODEX.md, especialmente A11, A14 e interpretação dos sintomas.

Atualização Fase 1.4: AP+STA, portal `/network`, autorização própria, campos STA/canais/clientes em health/diagnostics e buffer health de 3072 bytes estão documentados em [REDE_AP_STA.md](./REDE_AP_STA.md). Os registros de compilação/identidade da Fase 1.2 abaixo são históricos.

## Página visual /diagnostics

O link **Diagnóstico** do painel agora abre `http://192.168.4.1/diagnostics`. A API `/health` continua retornando JSON com o mesmo contrato e códigos HTTP. A página visual é servida do LittleFS por `diagnostics.html`, `diagnostics.css` e `diagnostics.js`, usando também o style.css existente. Exibe Wi-Fi/IP, listeners HTTP/WebSocket, LittleFS/arquivos do painel, MPU6050, DEMO/estado/falha, motores físicos, uptime, heap, heartbeat FreeRTOS, versão, commit, build UTC e hash das fontes.

Apenas o navegador agenda consultas HTTP assíncronas: espera 3 s após concluir cada resposta, permite no máximo uma consulta por vez e aborta após 4 s sem conclusão. Pausa/aborta quando a página fica oculta ou é abandonada e retoma ao voltar. HTTP 503 com JSON continua exibindo os indicadores degradados. Em falha de rede/JSON, mantém a última leitura claramente marcada como desatualizada e tenta novamente. Campos ausentes aparecem como não informados, sem presumir motores seguros. Textos recebidos são inseridos com textContent, sem interpretação de HTML.

Não abre WebSocket, não envia comandos de movimento/armamento nem acrescenta esperas/timers à tarefa de controle ou ao servidor. Os listeners exibidos são os estados locais reportados pela API. Os três assets novos precisam estar na imagem LittleFS enviada manualmente; filesystem antigo pode retornar 404 para essas rotas. Não houve upload automático.

Além dos testes nativos, `gjs scripts/test_diagnostics.js` executa o JavaScript real com DOM/transporte/relógio simulados e cobre DEMO, indicadores, 503, erros, timeout, consulta única, dados desatualizados/recuperação, visibilidade e navegação. Isso não substitui renderização em navegador ou acesso na placa. A inspeção local das rotas verifica os links/assets registrados; os testes HTTP abaixo ainda dependem do firmware/filesystem gravados:

```sh
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/diagnostics
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/diagnostics.css
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/diagnostics.js
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/health
```

No Android, abrir Diagnóstico, conferir os indicadores em DEMO, alternar para outra aba/aplicativo e voltar; interromper/restabelecer a conexão para verificar a indicação de dados desatualizados e recuperação. Testes de build/native/buildfs da página visual: sucesso; 21/21 casos nativos. Os registros de identidade/tamanho mais abaixo são da etapa Fase 1.2 anterior; os assets e fontes novos geram outra identidade de build.

## Resultado e limites desta etapa

O projeto contém modo DEMO com IMU ausente e GET `/health`, `/`, `/app.js` e `/style.css`. Foram acrescentados identificação do firmware, diagnósticos de listeners TCP, heartbeat observado da tarefa e validação da resposta JSON. As alterações locais anteriores foram preservadas. A lógica de PID, estados, comandos de motor, rampas, pinagem e instâncias de drivers permaneceu como estava; a tarefa ganhou apenas campos de observabilidade no snapshot e checagem de formatação dos textos de diagnóstico.

Não houve upload, acesso à placa, alteração de hardware, configuração de STEP/DIR, instanciação de TMC2209 físico, apagamento de NVS, formatação de LittleFS ou commit. `LittleFS.begin(false)` foi preservado. Portanto, **a causa das falhas no Android e Fedora ainda não foi comprovada**. A instrumentação permite separar firmware antigo, filesystem ausente, rede/HTTP e falha de WebSocket com evidências da próxima execução em hardware.

## Alterações implementadas

- `scripts/build_metadata.py`, acionado pelo ambiente esp32dev: gera `.pio/build/esp32dev/generated/balancebot_build.hpp`, sem editar fontes ou consultar o remoto Git. Registra versão, commit local, árvore dirty/clean/unknown, timestamp UTC e SHA-256 dos caminhos/conteúdos de platformio.ini, src/, include/, lib/, data/ e scripts/. Esse hash identifica as entradas locais, incluindo mudanças sem commit; não é o hash do binário nem comprova qual filesystem está na placa. Ações exclusivas buildfs/uploadfs não mudam a identidade do firmware já compilado.
- Boot: linha `[BUILD]` e etapas de LittleFS/AP/listeners/filas/criação da tarefa com resultado e heap. IP, gateway e máscara são lidos da interface AP real via esp_netif, em vez de apenas repetir valores solicitados. A senha do AP não é registrada.
- LittleFS: verifica abertura, arquivo não vazio e ausência de diretório para os três assets; mostra bytes totais/usados. `/health` repete essa inspeção sem escrever no filesystem. Isso verifica disponibilidade, não checksum integral dos arquivos.
- HTTP/WebSocket: subclasses mínimas inspecionam o estado de escuta do WiFiServer protegido pelas bibliotecas instaladas, pois begin() retorna void. Os logs distinguem sucesso/falha do listener de teste externo ainda pendente. Nenhuma conexão de loopback é criada.
- HTTP: registra chegada dos pedidos, status e bytes de streams; adiciona `Cache-Control: no-store`, `X-BalanceBot-Version` e `X-BalanceBot-Build`. Fallback de `/` continua existindo sem LittleFS. Assets indisponíveis/vazios retornam 404.
- WebSocket: registra conexão, recusa por controlador ocupado, desconexão e evento de erro. Não muda a política de um controlador nem envia comandos físicos.
- Snapshot: contador por iteração e flag de publicação. A cada 10 s, o loop registra contador, idade do heartbeat, tarefa criada, heap, clientes AP, pedidos e endereço completo. O log existente de watermark da tarefa foi preservado. Incremento do contador até zero por rollover não é confundido com ausência, pois há flag separada.
- `include/balance/diagnostics.hpp` e `src/diagnostics.cpp`: contrato de saúde e serialização portáveis, compartilhados pelo firmware e testes nativos, sem Arduino/FreeRTOS. Isso cobre parte da lacuna A14; adapters de rede, filesystem e tarefas continuam dependendo de integração na placa.

## Contrato de GET /health

O endpoint continua independente de LittleFS para gerar a resposta. O JSON possui os seguintes grupos:

| Campos | Significado |
|---|---|
| `version`, `commit`, `tree_state`, `source_sha256`, `build_utc` | Identidade compilada; comparar com `[BUILD]` e saída do build/upload. |
| `uptime_ms`, `reset_reason`, `free_heap`, `min_free_heap` | Tempo desde boot, causa de reset e heap observado. |
| `ap_ssid`, `ap_ip`, `ap_gateway`, `ap_netmask`, `ap_clients` | SSID/endereço da interface AP e clientes associados. |
| `ap_config_ready`, `ap_ready` | Configuração de rede aceita; AP iniciado com modo AP e IP não nulo. Não comprovam DHCP/rota no cliente. |
| `http_ready`, `websocket_ready`, `http_port`, `ws_port` | Listeners TCP locais e portas 80/81. Não comprovam resposta HTTP/handshake vistos pelo cliente. |
| `littlefs_ready`, `index_ready`, `script_ready`, `style_ready` | Montagem e abertura de cada asset não vazio. |
| `control_task_ready` | Criação da tarefa retornou pdPASS; separada de execução recente. |
| `control_heartbeat_seen`, `control_heartbeat_count`, `control_heartbeat_ms`, `control_heartbeat_age_ms`, `control_healthy` | Publicação observada, contador, timestamp e idade. Antes da primeira publicação a idade é null. Saudável se tarefa criada e snapshot com até 2000 ms. É diagnóstico de vivacidade; não é garantia do deadline de 5 ms. |
| `command_queue_ready`, `config_queue_ready` | Filas alocadas. |
| `http_requests`, `ws_connections` | Pedidos HTTP contados, incluindo o próprio /health; conexões WS recebidas, inclusive as recusadas por vaga ocupada. |
| `mode`, `state`, `fault`, `imu_available`, `motors_enabled` | Estado observado; motores físicos sempre false nesta versão. |

`ok=true` e HTTP **200** exigem AP/configuração, os dois listeners, filesystem e três assets, tarefa com heartbeat recente e as duas filas. IMU ausente não torna esse contrato de infraestrutura inválido: com os demais itens disponíveis, DEMO responde 200, `imu_available=false`, `state=5`, `fault=1` e `motors_enabled=false`. Um estado de controle Fault também não é, por si só, falha da infraestrutura web; consultar state/fault separadamente.

HTTP **503** com JSON completo indica infraestrutura degradada, inclusive filesystem faltante ou heartbeat ainda não publicado/parado. Receber 503 **já comprova que o cliente alcançou o endpoint HTTP**; examinar os campos em vez de tratar isso como timeout de rede. O fallback de `/` pode continuar respondendo 200 nessa situação.

HTTP **500** com `{"ok":false,"error":"health_json_overflow"}` indica falha de serialização. O buffer parcial nunca é enviado. Health usa buffer de 2304 bytes e escape de aspas, barras invertidas e caracteres de controle em strings. Todas as chamadas snprintf/vsnprintf do aplicativo agora passam por checagem de resultado negativo ou comprimento >= capacidade. Telemetria conserva buffer de 640 bytes; em falha, descarta a mensagem e registra diagnóstico limitado em frequência, em vez de transmitir JSON truncado.

## Validação local executada

PlatformIO Core 6.2.0; Espressif32 6.12.0; Arduino ESP32 2.0.17 (pacote 3.20017.241212+sha.dcc1105b); WebSockets 2.7.3; Xtensa 8.4.0+2021r2-patch5. O estado dos listeners foi conferido contra o código dessas bibliotecas locais e compilado para ESP32. A dependência de membros protegidos deve ser revista quando as bibliotecas forem atualizadas.

| Comando | Resultado |
|---|---|
| `pio test -e native` | Sucesso, 21/21 casos: 16 anteriores + 5 de diagnóstico. |
| `pio run -e esp32dev` | Sucesso; RAM estática 47.180/327.680 bytes (14,4%); flash 883.901/1.310.720 bytes (67,4%). |
| `pio run -e esp32dev -t buildfs` | Sucesso; index.html, app.js e style.css empacotados. |

Neste sandbox foi necessário executar esses comandos com `PLATFORMIO_CORE_DIR=/tmp/balancebot-audit-pio PLATFORMIO_SETTING_ENABLE_TELEMETRY=no`, reutilizando o ambiente temporário da auditoria, pois o diretório global do PlatformIO é somente leitura. Não se alterou a instalação global.

Os novos testes verificam DEMO saudável, degradação de infraestrutura, heartbeat ausente/parado/rollover do relógio, escape de SSID, identidade, buffer exato versus truncado e erros de snprintf. Uma sonda temporária compilou o serializador real e validou o resultado com Python json.loads: 819 bytes no cenário inicial e 1217 num vetor extremo com SSID de 32 caracteres de controle e contadores máximos. Os casos couberam em 2304 bytes. Inspeção do formato da telemetria: 540 bytes com valores DEMO representativos e 614 com vetor ampliado de contadores/limites; o guard em runtime trata outros comprimentos. Não são provas de estabilidade em hardware.

Identidade do firmware validado nesta execução:

```text
version=1.2.0-diagnostics
commit=b72e760a5662
tree_state=dirty
build_utc=2026-10-27T17:47:39Z
source_sha256=1c9cbf9dd919148cf01ddb15d91ccca81559728e1aad66c4bd52194ee1321b56
```

Uma nova compilação/upload pode gerar outro build_utc. Compare a placa com a saída da compilação usada para gravá-la, não somente com este registro. Buildfs não grava nada no ESP32. Os testes nativos não executam HTTP, Wi-Fi, NVS, I2C ou FreeRTOS.

## Procedimento de upload manual — não executado

Na raiz do projeto, confirme primeiro a porta com `pio device list`. No exemplo abaixo substitua `/dev/ttyUSB0` pela porta identificada; mantenha a mesma placa e configuração de partições. Feche o monitor serial antes dos uploads.

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

Os dois comandos de upload acima são instruções para execução deliberada pelo operador. Uploadfs substitui o conteúdo da partição de arquivos pelo diretório data/; não é recuperação por formatação no boot. Não executar erase, erase_flash nem limpeza de NVS. NVS deve ser preservada pelo layout atual. Capture a saída do upload para registrar a identidade gerada. Após ambos os uploads, com o monitor aberto, reinicie deliberadamente a placa para capturar o boot completo. Não conectar/habilitar motores para esses testes.

Esperado no serial: `[BUILD]`, resultado LittleFS e três assets, AP com IP/gateway/máscara, listeners 80/81, resultado das filas/criação da tarefa, DEMO por ausência da IMU e logs `[TASK]`/`[LOOP]` a cada 10 s. Compare duas linhas de heartbeat: contador deve avançar e idade deve permanecer pequena. Se houver caracteres ilegíveis, confirmar 115200/8N1 e distinguir bytes iniciais isolados de corrupção persistente; não mudar baud/hardware por hipótese.

## Testes HTTP idênticos no Fedora e Android

Conecte ao AP `BalanceBot-Setup`, senha padrão atual `BalanceBot123`, e mantenha essa rede mesmo sem internet. No Android, confirme que o sistema não trocou para dados móveis/outra rede; abra a URL com `http://` explícito. O firmware não implementa DNS/captive portal nem HTTPS: não se espera abertura automática. Se houver terminal com curl no Android, repita exatamente os comandos abaixo; caso contrário, abra as quatro URLs no navegador e registre a mensagem mostrada, horário e logs seriais correspondentes.

```sh
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/health
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/app.js
curl --noproxy '*' --connect-timeout 3 --max-time 10 -i http://192.168.4.1/style.css
```

Use GET, não `curl -I` (HEAD). Os comandos desconsideram proxy, limitam espera e preservam corpo de erro; não usam --fail nem seguem redirecionamentos. Esperado: JSON de diagnóstico no health; HTML do painel ou fallback explícito em `/`; JavaScript e CSS reais com HTTP 200 nos assets. Headers de versão/build devem coincidir. Se uma resposta for redirecionamento ou conteúdo de outro serviço, isso é evidência de caminho de rede diferente do ESP32.

Para guardar evidências no Fedora:

```sh
mkdir -p /tmp/balancebot-http
curl --noproxy '*' --connect-timeout 3 --max-time 10 -sS -D /tmp/balancebot-http/health.headers -o /tmp/balancebot-http/health.json -w 'health HTTP=%{http_code} bytes=%{size_download} total=%{time_total}s\n' http://192.168.4.1/health
curl --noproxy '*' --connect-timeout 3 --max-time 10 -sS -D /tmp/balancebot-http/index.headers -o /tmp/balancebot-http/index.html -w 'root HTTP=%{http_code} bytes=%{size_download} total=%{time_total}s\n' http://192.168.4.1/
curl --noproxy '*' --connect-timeout 3 --max-time 10 -sS -D /tmp/balancebot-http/app.headers -o /tmp/balancebot-http/app.js -w 'app HTTP=%{http_code} bytes=%{size_download} total=%{time_total}s\n' http://192.168.4.1/app.js
curl --noproxy '*' --connect-timeout 3 --max-time 10 -sS -D /tmp/balancebot-http/style.headers -o /tmp/balancebot-http/style.css -w 'style HTTP=%{http_code} bytes=%{size_download} total=%{time_total}s\n' http://192.168.4.1/style.css
python3 -m json.tool /tmp/balancebot-http/health.json
sha256sum data/index.html /tmp/balancebot-http/index.html
sha256sum data/app.js /tmp/balancebot-http/app.js
sha256sum data/style.css /tmp/balancebot-http/style.css
```

Repita /health alguns segundos depois: uptime, heartbeat e contagem de pedidos devem avançar. Pares de hashes dos assets devem coincidir se a mesma versão de data/ foi gravada; root fallback terá hash diferente. Hash divergente também pode indicar uploadfs antigo, conteúdo inesperado ou transferência incompleta; examinar status/headers/tamanho antes de concluir.

Se não houver resposta no Fedora, registre sem alterar a rede:

```sh
ip -4 address
ip -4 route
ip -4 route get 192.168.4.1
nmcli -f GENERAL,IP4 device show
curl --noproxy '*' --connect-timeout 3 --max-time 10 -v http://192.168.4.1/health
```

Confirmar endereço do cliente em 192.168.4.0/24 e rota escolhida pela interface Wi-Fi do AP. VPN, outra interface com essa sub-rede ou proxy são hipóteses; não há evidência para culpar firewall/SELinux nem instrução para desabilitá-los. Um ping sem resposta, sozinho, não diagnostica HTTP.

## WebSocket — somente depois de HTTP e assets

Abra o painel em uma única aba e procure handshake 101/conexão a `ws://192.168.4.1:81/` e telemetria no navegador, correlacionando com `[WS]` no serial. Uma segunda aba pode ser recusada por controlador ocupado. Para verificar somente o handshake por curl, feche antes as abas do painel:

```sh
curl --noproxy '*' --http1.1 --connect-timeout 3 --max-time 3 -i -N \
  -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  -H 'Sec-WebSocket-Protocol: arduino' \
  http://192.168.4.1:81/
```

Esperado: 101 Switching Protocols e registro de conexão/desconexão. Curl pode encerrar com timeout 28 após o upgrade porque o socket permanece aberto; nesse caso o 101 é a evidência relevante. Esse comando não é cliente completo de telemetria WebSocket nem envia comandos de movimento/armamento.

## Hipóteses e decisões após coleta em hardware

| Evidência | Interpretação e próximo teste |
|---|---|
| Não há AP ou boot reinicia | Conferir etapas AP, heap, reset_reason e heartbeat antes de investigar filesystem/browser. |
| Associou ao AP, curl dá timeout e não aparece `[HTTP]` | Verificar IP/rota/proxy/troca de rede do cliente e listener local no boot; ainda não determina causa única. |
| `[HTTP]` aparece, mas cliente não recebe corpo completo | Comparar bytes enviados/esperados, timeout, heap e reset; investigar serviço síncrono/cliente lento ou conexão interrompida. |
| /health recebe 200 com identidade antiga | Firmware gravado diferente do esperado; comparar saída do upload. |
| /health recebe 503 com assets false e `/` abre fallback | HTTP está acessível; investigar montagem/partição/uploadfs. Não formatar automaticamente. |
| /health recebe 503 com task criada e heartbeat parado | Criação não garante execução. Correlacionar última publicação, `[TASK]`, logs I2C e reset; reproduzir sem motores. |
| Assets recebem 404 ou hashes diferentes | Conteúdo LittleFS ausente, vazio ou de outra versão; conferir imagem/uploadfs e bytes da transferência. |
| Quatro GET funcionam, navegador falha | Investigar cache/HTTPS automático, erro JavaScript e handshake na porta 81, inclusive vaga de controlador. |
| HTTP funciona nos dois, WS só falha em um | Isolar porta 81, navegador/rede e handshake com logs `[WS]`; não atribuir o problema à IMU. |
| I2C sem sensor reaparece apenas a cada 5 s | Retentativa prevista em DEMO. Fluxo a cada ciclo não corresponde ao tratamento de ausência deste checkout. |
| NOT_FOUND em Preferences.begin reaparece | Comparar identidade gravada: este checkout já faz probe read-only do namespace e usa defaults sem apagar NVS. |

Pendentes: execução dos quatro GET nos dois clientes; DHCP/rota real; log de boot em 115200; comparação de identidade/arquivos gravados; handshake WS; evolução de heap/heartbeat e estabilidade prolongada. A11 foi tratado como observabilidade local com contrato explícito; A14 foi reduzido com serialização/regra de saúde testáveis, sem alegar cobertura de integração física. Os demais problemas da auditoria, inclusive A01/A02 e os de controle, permanecem fora desta fase.
