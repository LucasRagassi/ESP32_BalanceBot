# ESP32 BalanceBot · Fase 11A · v1.2

Base verificada na branch `hardware/bms-2s-revisao-work`, commit `8bf82c8aae334e745d4af5b431e9699294eff77f`. As verificações iniciais e finais usaram KiCad 10.0.8 com as configurações versionadas. O patch passou no `git diff --check` e foi reaplicado em uma extração limpa do commit. Os arquivos resultantes são idênticos aos entregues. O histórico `hardware/seguranca_vm/` e o firmware foram preservados.

| Projeto | ERC erros/avisos | DRC geométrico | Abertas no DRC | Paridade | Footprints atribuídos / pendentes | Redes completas / parciais / sem trilha ou zona |
|---|---:|---:|---:|---:|---:|---:|
| Principal | 44/0 → 15/0 | 39 → 17 | 185 → 153 | 66 → 58 | 73/66 → 81/58 | 14/0/86 → 31/5/64 |
| Segurança híbrida | 4/0 → 4/0 | 23 → 1 | 212 → 183 | 8 → 8 | 97/8 → 97/8 | 9/2/43 → 15/11/28 |
| Auxiliar | 2/1 → 2/1 | 0 → 0 | 31 → 0 | 0 → 0 | 30/0 → 30/0 | 13/2/8 → 23/0/0 |

Foram resolvidos 29 diagnósticos ERC e 44 violações geométricas. Oito footprints novos reduziram a pendência total de 74 para 66. As três placas ganharam 33 redes completas de dois ou mais pinos. Após acrescentar os oito footprints, a principal passou por um ponto intermediário com 201 conexões abertas. O roteamento reduziu essa contagem em 48, com redução líquida de 32 frente à base. O roteamento fechou 29 indicações na híbrida e 31 na auxiliar, incluindo as conexões pela zona de retorno. Uma rede completa tem todos os pads previstos e nenhuma indicação de abertura no DRC.

## O que mudou

| Arquivos | Trabalho efetivo |
|---|---|
| `hardware/pcb/BalanceBot.kicad_sch` e `.kicad_pcb` | 26 marcações NC em pinos já isolados de U1, U3 e saídas STAT/INT de U4. Campos de simulação e textos de fornecedor saíram da serigrafia de Q1–Q4. Entraram oito passivos e rotas de CC, detecção VBUS, RESET, BOOT, recepção do ESP32 por UART0, ARM/ENN e UART dos TMC. Divisores VREF foram aproximados dos drivers, com as redes VREF completas. Parte de STEP/DIR, TX de UART0, retornos locais e alimentações segue aberta. |
| `hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_sch` e `.kicad_pcb`, mais três arquivos `.kicad_mod` em `BalanceBot_Hibrida.pretty/` | Land patterns revisados para U207, U210/U211 e U217. As rotas completaram QUAL_WDI, QUAL_BUF, COIL_SENSE, VM_RESID_S e as referências OV/UV. FAULT_N, pré-teste, alimentação lógica e intertravamentos ainda têm trechos abertos. |
| `hardware/aux_qualificador/BalanceBot_Aux_Qualificador.kicad_pcb` | Fechados taps HOST, OBS_DRDY, 3V3 lógica, supervisão e retornos. VOS de U301 recebe o sense na saída após L301, junto a C303. SW permaneceu no cobre local existente. AGND/PGND e os capacitores ganharam retorno em PACK_RTN, com zona B.Cu e recorte sob SW. FB continua separado em F.Cu. O contorno de estudo de 76 × 71 mm foi preservado. |

Na principal, as rotas Kelvin de R16 permaneceram intactas. A comparação das netlists antes/depois retornou os mesmos conjuntos de pinos para todas as redes. A conferência pad a pad cruzou numeração, rede, biblioteca e DNP. Na principal, B− e o retorno protegido identificado como GND/PACK− continuam em redes distintas, sem novo cobre em B−, no caminho de corte ou no barramento VM. A auxiliar usa somente PACK_RTN como retorno. As flags DNP originais, inclusive F1, R43, K201, R228 e o Pico U304, foram preservadas. Os DRCs finais ficaram sem curtos ou cruzamentos de redes.

## Footprints conferidos

| Referências | Seleção ou desenho |
|---|---|
| R1/R2, R5, R6, R8 | Vishay CRCW0603 e3 de 5,10 kΩ, 22,1 kΩ, 47,5 kΩ e 1 kΩ, respectivamente. Dois terminais sem polaridade em 1608 métrico. Land KiCad SMD de 0,80 × 0,95 mm por terminal. R5/R6 seguem o divisor de VBUS da Silicon Labs. [Vishay 20035](https://www.vishay.com/docs/20035/dcrcwe3.pdf) · [Silicon Labs CP2102N, figura 2.5](https://www.silabs.com/documents/public/data-sheets/cp2102n-datasheet.pdf) |
| C6, C49, C50 | Murata GRM188R71H104KA93D, 100 nF, 50 V, X7R, 0603. Dois terminais sem polaridade. Land KiCad de 0,90 × 0,95 mm. [Murata SimSurfing](https://ds.murata.com/simsurfing/mlcc.html?partnumbers=%5B%22GRM188R71H104KA93%22%5D) |
| U207, U210/U211, U217 | Exemplos TI DDF0008A, DCU0008A e DGN0008C, com pad 1 e sequência preservados. Pads periféricos de 1,05 × 0,45 mm a passo 0,65 mm, 0,85 × 0,30 mm a 0,50 mm e 1,40 × 0,45 mm a 0,65 mm, respectivamente. U217 preserva EP9 em PACK_RTN e o exemplo de abertura central de stencil de 0,125 mm. Vias térmicas do estágio DNP dependem do processo escolhido. [TI TPS3435](https://www.ti.com/lit/ds/symlink/tps3435.pdf) · [TI SN74LVC2G74](https://www.ti.com/lit/ds/symlink/sn74lvc2g74.pdf) · [TI TPS7A16](https://www.ti.com/lit/ds/symlink/tps7a16.pdf) |

A auditoria das 74 referências originalmente pendentes está em `verificacao/Footprints_*.tsv`. Os 66 restantes carecem de seleção física ou dados do nó, especialmente conectores, passivos do carregador/BMS, indutores, fusível, relé e dissipação regenerativa. Paridade pendente equivale a referências sem footprint. A numeração de pads mecânicos foi conferida sem convertê-los em pinos elétricos.

## Bloqueios e próximos ensaios

- **Principal.** Um contorno ausente, 12 furos de 0,20 mm de U1 abaixo da regra de 0,30 mm e quatro folgas do USB4105 entre cobre e NPTH de 0,1944 mm frente à regra de 0,25 mm. Fechar chassi, antena, processo de furação e máscara, tolerâncias do USB e stackup. Dimensionar o par USB e seu retorno e verificar ESD e retroalimentação antes de roteá-lo. ERC restante registra seis fontes de potência não reconhecidas, sete diagnósticos de entradas de modem de U3 e dois em D+/D− do carregador. U4 segue inibido por CE/ILIM_HIZ na arquitetura herdada. Habilitação exige fechar limite de entrada, detecção USB, controle e proteção térmica. [Espressif, antena](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/pcb-layout-design.html) · [GCT USB4105](https://gct.co/files/drawings/usb4105.pdf) · [TI BQ25798](https://www.ti.com/lit/ds/symlink/bq25798.pdf)
- **Híbrida.** O DRC geométrico restante é o contorno ausente. Os quatro ERC dizem respeito a alimentações ou retorno sem fonte reconhecida. Fechar chicotes, lógica, alimentação e layout antes de validar watchdog, rearme, E-stop, corte sob carga, arco, falha dos dispositivos de corte e dump regenerativo. As folgas nominais de 0,20 mm nos novos land patterns ainda requerem aprovação do fabricante da PCB e do montador.
- **Auxiliar.** Dois ERC de fonte e um aviso de identificação do GND do Pico em PACK_RTN. Confirmar corrente e picos do AUX, UV/brownout, ripple e transientes do TPS62160, Ceff e saturação de L301, temperatura e continuidade do retorno com o stackup real. Validar watchdog, perda de amostras e falhas do qualificador. [TI TPS62160, layout](https://www.ti.com/lit/ds/symlink/tps62160.pdf)

**Energização e fabricação bloqueadas.** Faltam medições de corrente dos dois motores, resposta do pack e proteção BMS, energia regenerativa, tolerâncias do chicote, ensaios de perda de alimentação e validação do corte. O roteamento é de engenharia. A ausência de aberturas na auxiliar demonstra continuidade no CAD.

## Reproduzir

Aplique `BalanceBot_Fase11A_8bf82c8.patch` sobre o commit indicado com `git apply --check` seguido de `git apply`. O ZIP traz somente os arquivos KiCad alterados e as novas bibliotecas, que dependem dos demais arquivos desse commit. No diretório de cada projeto, substitua `NOME` pelo nome base do arquivo.

```sh
kicad-cli sch erc --format json -o NOME.erc.json NOME.kicad_sch
kicad-cli sch export netlist --format kicadxml -o NOME.xml NOME.kicad_sch
kicad-cli pcb drc --format json --schematic-parity -o NOME.drc.json NOME.kicad_pcb
```

Em `verificacao/`, os TSVs `Abertas_*` listam os extremos físicos reportados pelo DRC. `Redes_*` inclui o estado antes/depois e os pinos da netlist para as redes com mais de um pino, inclusive quando falta footprint. O JSON de auditoria registra os números e a lista DNP por projeto.
