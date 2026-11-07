# ESP32 BalanceBot · Fase 10 · engenharia e PCB v1.1

**Base edb2c13f8ae9a0ab319519d5842b31dfa5140024 · hardware/bms-2s-revisao-work · 07/11/2026 · KiCad 10.0.8**

**Parecer.** Três PCBs editáveis foram desenvolvidas, com componentes posicionados e roteamento parcial. O projeto continua bloqueado para montagem, fabricação e energização.

## Implementação desta revisão

| Bloco | Estado | Entrega e limite |
|---|---|---|
| Principal, shunt e interface | **A/B** | R16 Kelvin de quatro terminais, DNP, com rotas de medição separadas. J14 em GPIO32 leva TX futuro do quadro consumido. D3 SMBJ10CA DNP recebe footprint e C38/C42/C44/C48 passam a 50 V X7R. J1 recebe footprint USB4105. CIs e demais decisões de energia preservados. |
| BMS/carregador/fusíveis | **C/D** | Faixas e candidatos de ensaio para limiares, shunt, wake, temperatura, balanceamento e coordenação. F1 segue DNP. Origem e AUX aguardam corrente de falta e I²t. CE/ILIM e DNPs do carregador preservados. |
| Segurança VM | **A/B/D** | Pré-teste de VM_K e VM_MOTOR em duas bordas do rearme. K201/estágio eletrônico e dump continuam candidatos DNP. Contato soldado com VM_K baixo pode não ser detectado. UV em RUN ainda não atua. |
| Auxiliar 5 V e qualificador | **A/B/E** | Novo projeto com TPS62160, monitor de subtensão, Pico RP2040, taps bufferizados, UART e saída READY/WDI. A fonte é buck e exige margem de VIN no fim da descarga. Sem firmware do qualificador nem contrato implementado no ESP32, não autoriza operação. |
| Regeneração | **B/D** | Estrutura de dump já a jusante do corte e TVS candidata local. Resistência, SOA, energia e arranjo em J9 ainda dependem de ensaio. Não há proteção validada se o chicote de VM desconectar. |
| PCB e mecânica | **B/C** | Três PCBs parciais, com classes, posicionamento e rotas. Contorno de estudo apenas no auxiliar. Stackup, dimensões, conectores e térmica continuam abertos. |

A base foi confirmada na ponta da branch remota. A execução usou extração limpa e preservou as alterações locais já existentes. Firmware e módulo histórico foram mantidos. O patch foi conferido sobre a base e o pacote contém esquema, PCB, bibliotecas, BOM e verificações.

## Resultado medido no CAD
| Projeto | ERC inicial → final (erros/avisos) | Footprints atribuídos / pendentes | DNP | Redes completas / parciais / sem cobre |
|---|---:|---:|---:|---:|
| Principal | 45/0 → 44/0 | 73 / 66 | 11 | 14/0/86 |
| Híbrido | 4/0 → 4/0 | 97 / 8 | 26 | 9/2/43 |
| Auxiliar / qualificador | novo → 2/1 | 30 / 0 | 4 | 13/2/8 |
| Histórico | 4/0 → 4/0 | 4 / 42 | 46 | sem PCB |
| Projeto | DRC geométrico | Conexões abertas | Paridade | Segmentos / vias |
|---|---:|---:|---:|---:|
| Principal | 39 | 185 | 66 | 239 / 0 |
| Híbrido | 23 | 212 | 8 | 183 / 0 |
| Auxiliar / qualificador | 0 | 31 | 0 | 304 / 9 |

**200 footprints** atribuídos e **74 pendentes** nos três projetos ativos. No histórico, 4 atribuídos e 42 pendentes. Redes de um único pino e NC não entram na contagem de roteamento.

O auxiliar tem contorno de estudo de 76 × 71 mm e zona de retorno lógica. As placas principal e híbrida ainda não têm contorno. A base principal vazia reportava um erro de contorno. A comparação dos pads confirmou as ligações Kelvin, o TX, as interfaces do qualificador e a separação de B−/PACK− e SYS/VM. O DRC final não indicou curtos entre redes. Os resultados de paridade pendentes correspondem a footprints faltantes.

O ERC do auxiliar conserva dois erros de alimentação (VIN externo e VSYS após diodo) e um aviso porque o retorno PACK_RTN tem nome local. O híbrido conserva quatro erros de entrada/retorno de alimentação externa. O principal conserva erros de entradas sem driver e pinos sem conexão herdados. O modelo de protocolo e rearme passou com vetores sintéticos. O DRC e o modelo não validam temporização real, EMI, SOA, capacidade de corte ou segurança. Não houve simulação elétrica nem ensaio físico.

## Decisão de arquitetura

Manter a potência e drivers na placa principal, o corte híbrido em módulo separado e a supervisão/auxiliar em placa de estudo própria. Isso permite ensaiar e substituir os módulos sem retrabalhar a placa principal. O custo é a área do Pico e mais chicotes. Na versão mecânica final, avaliar consolidar auxiliar/qualificador com o híbrido ou migrar para MCU montado diretamente. O dump final deve ficar eletricamente local aos drivers quando J7 abrir. Não usar simultaneamente a arquitetura histórica e a híbrida. Não foi fixado um layout mecânico do robô sem medidas reais.

## Ensaios indispensáveis

1. Medir pack por célula, chicote, shunt, FETs, interrupção DC e fusíveis. Confirmar carregador com simulador, NTCs e USB/bateria ausente.
2. Medir corrente e torque dos motores, VM/VIO e energia de frenagem. Fechar clamp, R228, SOA, dissipação e proteção após abertura de J7.
3. Demonstrar latência e comportamento do watchdog/qualificador, VIO ausente, soldagem, falha comum, perda de energia, ausência de torque perigoso e ausência de rearme automático.
4. Fechar footprints, stackup, contornos, roteamento, térmica, EMI, montagem e DRC depois das decisões elétricas.

Os detalhes e critérios de saída estão nos documentos de proteções, Segurança VM, validação e pendências. A versão v1.1 é uma entrega de engenharia, sem Gerbers liberados.
