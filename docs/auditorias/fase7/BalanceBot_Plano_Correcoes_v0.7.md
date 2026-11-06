# ESP32 BalanceBot — plano de correções após a auditoria

**Revisão 0.7 · 05/11/2026 · Base f67c388 na branch hardware/bms-2s-revisao-work.**
Plano proposto. O projeto permanece bloqueado para energização e fabricação. As fases seguintes exigem revisão de engenharia e aprovação dos critérios abaixo. A matriz de pendências é o registro por item.

## Ordem e critérios de passagem

| Etapa proposta | Entregas e ordem | Critério de passagem |
|---|---|---|
| **8A · Fechar dados e arquitetura** | Selecionar pack 2S e chicotes (P01), fechar configuração BMS e partida (P02), dimensionar R16 e coordenar F1/proteção de pack (P04/P10). Levantar corrente, torque e requisitos de parada do robô, e definir a estratégia de medir Eret. | Especificação do pack e modelos de corrente/temperatura aprovados. Planilhas de margem para VM, shunt, FET, fusíveis e tensão-limite de regeneração com dados rastreáveis. Qualquer valor ainda sem dado fica pendente. |
| **8B · Fechar circuitos e interfaces** | Corrigir polaridade USB e validar alimentação parcial (P06); completar TS, I²C e política de carga (P07); incluir IMU e sinais de amostra (P12). Projetar fonte, janelas, temporizador, latch, permissões, monitor a jusante e tratamento de falhas (P11–P15). Fechar chicote e corrigir representação CAD de SW101/SW102 (P16/P22). Classificar reservas e opcionais (P20). | Netlist revisada com a documentação de pinagem e conectores. Modo de teste seguro, de corte independente e sem rearme automático desenhado e revisado. ERC reaplicado com diferenças justificadas. Cada DNP crítico segue bloqueado até aceitação em bancada. |
| **8C · Qualificar protótipos e falhas** | Testar primeiro lógica, power-good, BMS e corte com simuladores e fontes limitadas. Medir shunt, FET, VM, VIO, brownout, inrush, regeneração e parâmetros dos dois eixos (P03/P05/P08/P09/P14/P15/P19). Validar USB, carga e watchdog após fechar circuitos. | Registro de formas de onda, condições e margens. Critérios de parada cumpridos com perda de sensor, cabo, VIO e retorno de energia. Aprovação elétrica formal antes de avaliar remoção de DNPs. |
| **9 · Fechar BOM, PCB e fabricação** | Fechar MPN/footprints e opções DNP, desenhar as duas PCBs ou os desenhos do módulo/chicote, revisar layout e fabricação (P17/P18/P22). | Footprints conferidos com desenhos dos fabricantes e primeira inspeção de pad 1/EP; DRC e DFM fechados; pacote Gerber, furação, BOM e posicionamento coerentes. Liberação de fabricação em revisão independente. |
| **10 · Integrar funcionalidades adicionais** | Atualizar painel e documentação, GPIOs opcionais, diagnósticos e interfaces de manutenção (P20/P21), após definir o escopo de firmware. | Documentação e interface correspondem a dois BJ42D15-26V12 e aos TMC2209 discretos. Nenhum diagnóstico opcional sustenta uma função de proteção por pressuposto. |

## Sequência de bancada a preparar na 8B

| Passo | Condição de ensaio | Registros e aprovação |
|---|---|---|
| 1. Continuidade e isolamento | Conjunto desenergizado, sem células, todos os chicotes e depois instrumentos de serviço. Exercitar estado aberto da proteção e da cadeia. | Pinagem e polaridade registradas. Sem união B−/PACK− ou caminho de retorno paralelo não previsto. NC e DNP conferidos visualmente. |
| 2. Fontes lógicas e BMS | Simulador de duas células e fontes limitadas. Motores desconectados. Testar POR, wake, perda de lógica, USB isoladamente e pack isoladamente. | Corrente de repouso, tensões por célula, REGOUT, SYS, +3V3, reset, VGS/VDS e resultados de OV/UV/OCC/OCD/SCD. Confirmar que uma condição de falha leva ao estado previsto. |
| 3. Cadeia de parada | Módulo supervisionado e carga de teste. Injetar E-stop, contato colado, curto entre canais, botão preso, perda de 24 V/3V3_AUX, cabo aberto e saída presa. | Medir tempo até corte e descarga, diagnosticar contatos e demonstrar rearme somente por atuação local válida. Verificar ENN quando VIO está válida e medir a condição residual quando VIO cai. |
| 4. Dinâmica do sensor | Gerador ou sensor em bancada e relógio independente. Injetar amostras repetidas, NACK, perda de DRDY, mensagens sem progresso e travamento do ESP32. | Medir maior intervalo válido e pior tempo de parada. Aprovar timeout dentro da janela de frescor e orçamento de parada, sem rearme automático. |
| 5. VM e regeneração | Fonte limitada e absorção conhecida, carga controlada, depois motores em dispositivo contido. Cobrir dois eixos, parada, inversão e abertura sob carga. | Registrar corrente, energia devolvida, VM pico/mínima, tempo de descarga, VIO, temperaturas, VDS/VGS, falha de J9 e operação de clamp/TVS. Comparar com limites e margens aprovados. |

Não liberar etapas de ensaio com potência até aprovar o circuito específico, o procedimento e os meios de contenção. A redução dos erros ERC é evidência de consistência do modelo CAD, com a validação elétrica e de segurança feita pelos ensaios previstos.

## Itens que podem avançar antes das medições de potência

A correção D+/D− de U3 e a classificação CAD de SW101/SW102 têm evidência suficiente para correção dirigida. A implementação da IMU, da interface de carga e do circuito de supervisão pode ser detalhada em paralelo com a coleta de requisitos, mantendo os elementos críticos em DNP. R16, fusíveis, contatos, D101, Q101/R118, limiares e a retirada de DNP dependem da envoltória elétrica, dos estados de falha e da validação descrita na matriz.
