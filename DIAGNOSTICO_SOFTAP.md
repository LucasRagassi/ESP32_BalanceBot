# Fase 1.4.3 — desaparecimento do SoftAP

Firmware: `1.4.3-ap-observer`. Auditoria e testes locais, sem upload/commit. O usuário confirmou que o SSID desaparece **mesmo após reboot, sem abrir /network nem tocar em Buscar redes**. Não há /dev/ttyUSB* ou /dev/ttyACM* disponível neste ambiente; não foi possível ler eventos reais da placa ou observar beacons RF.

## Conclusão e grau de certeza

**Defeito confirmado e corrigido:** o firmware confundia o sucesso inicial de WiFi.softAP e a manutenção do IP 192.168.4.1 com AP ainda iniciado. Não registrava AP_START/AP_STOP e mantinha `accessPointReady` com o resultado do setup indefinidamente. Assim, podia informar “AP ativo” depois de uma parada real. O IP da interface pode continuar atribuído sem comprovar transmissão ou recepção de beacons.

**Causa do desaparecimento RF ainda não confirmada:** não foi encontrada rotina da aplicação que inicie scan ou conexão STA automaticamente no estado unconfigured, nem chamada periódica que desligue/recrie AP/radio. O relato sem uso do portal exclui atribuir o caso reproduzido a scans explicitamente solicitados. Não é correto afirmar que mudar o diagnóstico corrigiu a perda de beacons. A garantia física de disponibilidade contínua depende da coleta de status/eventos abaixo; permanece pendente.

Nesta revisão corrigiu-se a indicação incorreta, acrescentou-se evidência do driver e reforçou-se o controle de scans explícitos. Não há recuperação automática, alternância AP-only/AP+STA, limpeza de NVS, reinicialização periódica ou tentativa de esconder a falha. AP+STA, endereço/SSID/senha do AP, serviços e modo DEMO são preservados.

## Auditoria de chamadas e fluxo

| Chamada | Onde/quando ocorre | Observação |
|---|---|---|
| `WiFi.mode(WIFI_AP_STA)` | Uma vez no setup de esp32_app.cpp | Não há mudança periódica de modo ou chamada WIFI_OFF/STA exclusivo. |
| `WiFi.softAPConfig` / `WiFi.softAP` | Uma vez no setup | 192.168.4.1/24, SSID/senha existentes, visível, canal inicial padrão 1. Nenhum restart no loop. |
| `WiFi.setSleep(false)` | Setup | Mantido; status agora lê também esp_wifi_get_ps, em vez de presumir estado do driver pelo retorno do wrapper. |
| `WiFi.begin(ssid,password)` | EspWifiRadio::connect | Somente rede confirmada carregada do journal, teste explícito ou reconexão dessa rede. WifiManager::begin sem credenciais não chama rádio; tick em Unconfigured não agenda tentativa. |
| `WiFi.scanNetworks(true,true,false,120)` | Única ocorrência em POST autenticado `/api/network/scan` | Nenhum GET, polling, autorização, carregamento de página ou timer inicia scan. Apenas Buscar redes solicita POST. |
| `WiFi.scanComplete` | finishScan | Só quando existe scan explícito em andamento. Sem scan, retorna sem consultar/alterar scan no driver. |
| `WiFi.scanDelete` | Antes do POST aceito ou ao finalizar scan próprio | Libera resultados do wrapper Arduino; não inicia scan nem equivale a desligar o rádio. |
| `esp_wifi_scan_stop` | Falha/timeout de scan próprio | Retorno registrado. Não é esp_wifi_stop e não reinicia o rádio. |
| `WiFi.disconnect(false,false)` | Tentativa/backoff/rollback da STA | Não pede modo OFF nem apagamento de configuração. Sem credenciais e sem teste não é chamado pelo gerenciador. |
| `esp_wifi_disconnect` | Conflito DHCP STA com sub-rede AP | Desassocia STA; não é usado como recuperação do AP. Não entra nesse caminho em Unconfigured sem lease. |
| `WiFi.softAPdisconnect` | Ausente na aplicação | Nenhum timer esconde/desliga o AP. |
| `esp_wifi_start` / `esp_wifi_stop` | Sem chamadas diretas na aplicação | O SDK/Arduino inicia o rádio no setup através de mode. Não há ciclo stop/start próprio. |
| AP_START/AP_STOP/cliente/SCAN_DONE | Callback de eventos do SDK | Registra fatos, contadores e invalida movimento ao perder AP. Não chama mode/softAP/start/stop para recuperar AP. |

Foram também inspecionados os wrappers instalados em Arduino ESP32 2.0.17. `scanNetworks` habilita a interface STA se necessário; já estamos em AP+STA. `scanDelete` limpa resultados e flag de conclusão. `getMode`, `channel`, `SSID`, `softAPgetStationNum` e getters usados nos diagnósticos não iniciam scans. O SDK pode fazer scans internos ao conectar uma STA: isso é distinto do POST explícito e não é medido pelo contador da aplicação como uma nova chamada begin. O retry interno único de primeira conexão descrito em REDE_AP_STA.md continua sendo uma limitação do framework; AutoReconnect permanece false.

Referências primárias: [driver Wi-Fi Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4-rc1/esp32/api-guides/wifi.html), [API Wi-Fi Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32/api-reference/network/esp_wifi.html), além das implementações/headers locais do SDK utilizado no build. A documentação não substitui a evidência do driver desta placa; referências de versões próximas fundamentam o comportamento das APIs, não um diagnóstico conclusivo deste sintoma.

## Instrumentação e correções

- `ApObservation` registra AP_START e AP_STOP com timestamp monotônico, número de inícios/paradas e associações/desassociações. O acesso do callback/loop é protegido por seção crítica curta. Logs de AP_STOP são críticos; AP_START/clientes/canal são transições em NORMAL.
- Um observador de **somente leitura** consulta o driver uma vez por segundo, detecta alterações de canal e mudanças da consistência observada. Ele não tenta restaurar estado nem invoca funções de inicialização do rádio. Mudança de canal inclui estado do scan/STA no log para correlação; durante scan o valor é uma observação, não prova de migração permanente.
- `ap_ready` agora exige evento AP_START mais recente que STOP, modo com AP habilitado, getters de configuração/canal/clientes bem-sucedidos, interface realmente up/AP_STARTED_BIT, IP atribuído, canal 1–14, SSID não oculto e igual ao esperado. Falha em qualquer critério retorna false. `wifi_mode` vem do getter real, não da string AP_STA fixa.
- Console distingue `setup_aceito` (histórico do boot) de driver consistente/iniciado (atual). `/health`, `/diagnostics` e `/network` usam a mesma avaliação. A legenda deixa explícito que consistência do driver **não comprova beacons RF**. AP_STOP com IP ainda atribuído não mantém indicador verde.
- `status` lê esp_wifi_get_mode, esp_wifi_get_config(AP e STA), esp_wifi_get_channel, esp_wifi_get_mac(AP), esp_wifi_ap_get_sta_list e esp_wifi_get_ps, mostrando retornos numéricos/esp_err_to_name e valores seguros. SSID STA configurado no driver pode ser comparado a app_STA=unconfigured para identificar configuração residual inesperada do SDK. Nunca imprime structs wifi_config_t inteiras nem suas senhas; as cópias temporárias são zeradas.
- `status` também mostra BSSID AP, canal configurado versus observado, beacon interval, hidden, autenticação, clientes, último evento AP/timestamp/contadores, alterações de canal, chamadas begin/disconnect da aplicação, scan nunca/running/complete/failed, início/fim/resultado, tentativas, recusas, SCAN_DONE e retorno de scan_stop.
- Scans continuam exclusivamente explícitos/autenticados. Novo guard recusa AP não confirmado, associação/backoff/teste, scan em andamento e tentativas com menos de **15 s** desde o último início, inclusive falho. Não há agendamento automático nem repetição de POST pelo navegador. O limite reduz disputa por rádio; não foi estabelecido como correção do desaparecimento em repouso.

AP+STA compartilha canal entre interfaces, podendo seguir o roteador quando há STA conectada. Não foi imposto outro canal nem mudada potência/país/SSID/senha para mascarar o problema. A presença de IP e os getters retornarem ESP_OK também não garantem que outro dispositivo esteja recebendo beacons.

## Testes locais e limites

Novos casos nativos:

1. IP atribuído/mode APSTA sem AP_START não basta; AP_STOP, driver inválido, hidden, SSID inesperado ou interface down tornam ap_ready falso, sem recuperação automática.
2. **Sete dias simulados**, a passos de 1 s, com STA unconfigured: nenhum begin/disconnect, nenhuma escrita NVS ou scan; AP modelado permanece iniciado/consistente e não há evento STOP fabricado.
3. Scan explícito: elegibilidade não inicia scan, bloqueio de sobreposição, AP indisponível, cooldown e rollover de millis.
4. Teste STA sem rede confirmada falha e retorna a Unconfigured: por mais dois dias simulados não há novas chamadas de rádio.

JavaScript: seis minutos simulados do polling do portal não enviam nenhum POST de scan, apenas GETs. Regressões de formulário/QR/timeout/seleção e FAULT continuam cobertas. Diagnóstico recebe AP_STOP com IP anterior e exibe indisponibilidade/evento.

`scripts/test_ap_idle.py` verifica estaticamente fronteiras dos mutadores: modo só no setup, scan no POST autenticado e nenhuma rotina periódica de restart/AP. `scripts/test_web_routes.py` preserva 16 registros GET/POST. São verificações de software; não rodam o driver RF no host. Os testes nativos usam evidência de driver modelada e não comprovam sete dias de disponibilidade física.

Validação final: **38/38 testes nativos passaram**; compilação ESP32 e buildfs passaram; testes JS de rede/diagnóstico/painel, verificações estáticas AP/console/16 rotas e **5/5 testes Python do QR** passaram. Build `2026-10-27T21:44:22Z`, versão `1.4.3-ap-observer`, base `b72e760a5662` dirty, source SHA-256 `3f192e9ade9f43b10c74956b6b5372aed01e02a6dbdf856dc706d5fcb93c1181`. RAM estática 47.908/327.680 bytes (14,6%); flash 917.641/1.310.720 bytes (70,0%). Usado o ambiente temporário PlatformIO das fases anteriores, sem instalar dependências ou escrever no diretório global somente leitura.

PID, máquina de segurança, máquina STA/NVS e trecho completo da tarefa FreeRTOS de controle foram preservados. Não se inicializa STEP/DIR/TMC2209; não houve mudança de prioridade/core/período. Os getters/observadores ficam no loop/console/eventos, sem tarefa adicional. Código público não recebe token ou senha.

## Roteiro físico para estabelecer a causa

Usar o novo firmware e filesystem **somente após gravação manual deliberada**, conforme REDE_AP_STA.md. Não apagar NVS: isso destruiria evidência de eventual configuração residual. Não abrir `admin` durante a captura compartilhável: status não precisa de token e não o imprime.

### A — repouso sem portal (reprodução prioritária)

1. Abrir monitor a 115200 e reiniciar deliberadamente uma vez para capturar versão/build, AP_START e setup. Depois não reiniciar para “recuperar” antes de coletar evidências.
2. Sem abrir portal, scan ou teste de rede, enviar `status` logo após o boot e novamente em 30 s, 60 s e quando o SSID desaparecer. Conservar uptime para provar que foi o mesmo boot. Não é necessário DEBUG; AP_START/STOP/clientes/canal aparecem em NORMAL.
3. Verificar no Android e em varredura do Fedora, anotando horário e BSSID/canal. No Fedora, `nmcli -f SSID,BSSID,CHAN,SIGNAL device wifi list --rescan yes` solicita scan **do cliente**, não do ESP32. A falha de autenticação do MT7902 não deve ser confundida com visibilidade de beacons; comparar também o Android.
4. No caso sem credenciais/testes espera-se `app_STA=unconfigured`, begin_calls=0, disconnect_calls=0, scan starts=0/never. Espera-se AP_START, sem AP_STOP posterior e nenhuma mudança de canal causada pela aplicação. Isso é um critério de verificação, não um log real já coletado.
5. Comparar modo/config rc, AP_STARTED_BIT, interface, SSID/hidden, canal_cfg/real, beacon interval, PS e BSSID antes/depois. Valores cujo getter falhou são defaults/indeterminados e não devem ser interpretados como configuração válida.

### B — scan explícito separado do teste A

Somente depois de capturar a falha em repouso: abrir `/network`, autorizar, solicitar uma única busca e observar início/SCAN_DONE/fim, canal e clientes. Tentativa sobreposta ou antes de 15 s deve ser recusada sem novo início. Após scan concluído, deixar parado e repetir status. GETs de health/status/scan-result não podem aumentar scan starts. Não misturar esse ensaio com o baseline sem portal.

### C — STA conectada e desconectada

Com credenciais confirmadas em rede 2,4 GHz não conflitante, testar AP e STA, HTTP/WS/páginas e perda/retorno do roteador. Espera-se reconexão STA existente com backoff, sem AP_STOP/restart provocado pela aplicação; canal pode acompanhar o roteador. Testar AP por pelo menos 30 min e além do tempo de falha relatado. Não habilitar motores.

| Evidência após sumir SSID | Interpretação / próximo passo |
|---|---|
| AP_STOP registrado, modo sem AP ou getters falhando | Parada/divergência real observada. Correlacionar instante com eventos STA/scan, versão e retorno do driver. Não reiniciar automaticamente. |
| Driver SSID mudou, hidden=1 ou canal inesperado | Há mudança real de configuração. Comparar chamadas/logs e configuração SDK, sem atribuir ao Android por suposição. |
| app unconfigured mas SSID STA no driver/associação inesperada | Investigar configuração residual/framework; não apagar NVS antes de registrar o estado. |
| begin=0, scan starts=0, nenhum AP_STOP e driver consistente, mas ausente nos dois clientes | Causa continua abaixo da evidência de aplicação. Próxima etapa precisa observar beacons/canal com equipamento apropriado e investigar SDK/RF/alimentação com dados; não é correto prometer correção por software ou culpar alimentação sem medição. |
| Só um cliente não vê o BSSID enquanto outro recebe beacons | Investigar cliente/banda/canal/região/cache em separado, sem mudar firmware cegamente. |

Critério de aceite final: o BSSID/SSID deve permanecer detectável e o portal acessível no cenário A durante período superior ao da falha, além de manter os cenários B/C. **Esse aceite físico ainda não foi realizado.** Nenhuma rotina agressiva de recuperação foi introduzida para simular estabilidade.
