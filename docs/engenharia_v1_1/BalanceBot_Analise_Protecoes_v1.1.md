# BalanceBot v1.1 · alimentação e proteções

**Base edb2c13 · 07/11/2026.** Pontos de cálculo e ensaio, sem aprovação de limites de operação.

## Pack 2S1P e caminhos de corrente

Duas Murata US18650VTC6 em série. 7,2 V e 3,12 Ah nominais, 22,46 Wh nominais, e 8,4 V como soma de dois pontos de caracterização de 4,2 V. Corrente/temperatura das curvas de laboratório não são especificação do pack. Obter folha de aprovação, autenticidade/lote, DCIR por célula/SOC/temperatura, corrente de curto prospectiva, chicote e acoplamento térmico do NTC. [Murata VTC6](https://www.murata.com/-/media/webrenewal/products/batteries/cylindrical/datasheet/us18650vtc6-product-datasheet.ashx)

B+ alimenta F1/VM, BAT/BATP do BQ25798 e o futuro ramo auxiliar próprio pós-fusível. Em descarga PACK− retorna por Q3/Q4 e R16 até B−. O sentido no shunt se inverte em carga. SYS alimenta a lógica principal e não alimenta VM. Com o pack desconectado ou BMS aberto, USB ainda pode manter a lógica ativa. J3/J4 do BMS usam B−. J12/J13 do carregador e o auxiliar usam PACK−. Não unir retornos via instrumentos, blindagens ou chicotes. Q3/Q4 são CSD18540Q5B em drenos comuns. CHG U5.10 vai a Q3 e DSG U5.9 a Q4. A interrupção no retorno não remove B+ contra B−.

## R16 Kelvin, BMS e coordenação

**Vishay WSK2512R0100FEA**, 10 mΩ, ±1%, 1 W na condição de catálogo, quatro terminais, DNP. A ficha diferencia TCR da liga (<20 ppm/°C) do comportamento do componente/terminais. A faixa de 5 a 200 mΩ especifica ±35 ppm/°C para o componente. Footprint KiCad T1.19mm cobre 5–200 mΩ. Pads 1/4 são força B−/SHUNT_OUT e pads 2/3 Kelvin vão a R18/R17. Não ligar por cobre os pads de força aos de medição. [Vishay WSK2512](https://www.vishay.com/docs/30108/wsk.pdf)

| Estímulo de cálculo | Queda / potência / energia |
|---|---|
| 2 A contínuos | 20 mV / 40 mW |
| 3 A contínuos | 30 mV / 90 mW |
| 10 A por 10 ms | 100 mV / 1 W no pulso / 10 mJ |
| 20 A por 1 ms | 200 mV / 4 W no pulso / 4 mJ |

Energias calculadas são solicitação. Verificar capacidade de pulso pelo fabricante, repetição, temperatura do terminal, cobre e deriva pós-ensaio. Aos 60 °C de excursão, ±35 ppm/°C agrega ±0,21%. Candidatos 6,8 e 12 mΩ preservam trade-off queda/resolução. R17/R18 100 Ω e C29 100 nF dão τ diferencial ideal de 20 µs. Degrau de 100 mV cruza 80 mV em ~32 µs antes do comparador. SRP−SRN é positivo em carga. A faixa do ADC ±200 mV não é especificação de comparador. [TI BQ76905](https://www.ti.com/lit/ds/symlink/bq76905.pdf) · [TRM SLUUCI8](https://www.ti.com/lit/ug/sluuci8/sluuci8.pdf)

| Função | Candidato para simulador com 10 mΩ | Fechamento |
|---|---|---|
| COV / CUV | 4200 mV e 3100/3200 mV. Faixa de campo TI 0–5500 mV, unidade 1 mV. | Aplicar erro, overshoot e limite individual aprovado. Medir ADSCAN em NORMAL/SLEEP e associar CUV à VM e torque mínimos. |
| OCC | Campo 8 × 2 mV = 16 mV (~1,6 A). TI admite códigos 2–62. | Confirmar offset, tolerância e rejeição dos pulsos normais. |
| OCD1/OCD2 | 30/40 mV em magnitude (~3/4 A). | Confirmar quantização/códigos e dispersão por injeção calibrada. Menor atraso para nível mais severo. |
| SCD | 80 mV como ponto de estudo (~8 A). | Confirmar código e tolerância reais, filtro, SOA e I²t. Sem curto direto em célula. |
| Tolerância | Itrip,min=(Vlim−erro)/Rmax e Itrip,max=(Vlim+erro)/Rmin. Exemplo com 16 mV, erro ilustrativo ±2 mV e R ±1,21% dá 1,38–1,82 A. | Usar o erro garantido na condição real, não os ±2 mV do exemplo. Incluir fixture e ripple. |
| Temperatura, wake e balanceamento | Simular NTC aberto/curto, verificar leitura de ambas as células, status, wake e recuperação com carga inibida. | Só habilitar balanceamento após verificar corrente, temperatura e filtros VC. Testar RAM/readback, sem OTP. Recuperação do BMS não rearma VM. |

Q3/Q4 têm 60 V VDS, ±20 V VGS e 3,3 mΩ máx. a 4,5 V/25 °C. A REGSRC=5,9 V, o mínimo de CHG referido a VSS pode ser ~4,15 V, antes da tensão de source. Nessa condição o RDS(on) de 4,5 V não é garantido. Medir VGS/VDS, SOA e dissipação com pior queda de pack e pulsos. [TI CSD18540Q5B](https://www.ti.com/lit/ds/symlink/csd18540q5b.pdf)

F1 continua DNP. Fusível de origem e do AUX aguardam I normal/pico, I²t admissível no menor chicote/MOSFET, curto prospectivo e capacidade de interrupção DC. Fechar coordenação antes de colocar os fusíveis na BOM de compra. Um fusível no ramo VM não protege o restante de B+.

## Carregador e USB

PROG 6,04 kΩ identifica 2S/1,5 MHz. CE polarizado a REGN e ILIM_HIZ em GND continuam. J12/J13/R52 DNP. Com R52/R53 5,23/30,1 kΩ e NTC 10 kΩ, VTS/REGN≈0,589. NTC aberto≈0,852 e curto≈0. Confirmar janela real da célula, flags TS, POR/WD de U4, OTG, VREG/ICHG, BATFET, bateria ausente e VBUS variável. J13 exige mestre open-drain referenciado a PACK−, com pull-ups e high-Z quando U4 sem alimentação. USB4105 de 16 vias recebeu footprint candidato. D+/D−, impedância, ESD e backfeed de UART0/EN/BOOT continuam pendentes. [TI BQ25798](https://www.ti.com/lit/ds/symlink/bq25798.pdf) · [GCT USB4105](https://gct.co/files/drawings/usb4105.pdf)

## Fonte auxiliar 5 V provisória

U301 TPS62160DGKR buck, L301 Coilcraft XAL4020-222MEC 2,2 µH, entrada 10 µF + 100 nF, saída 22 µF, VOS em AUX_5V. R301/R302 523k/100k e FB de 0,8 V dão 4,984 V nominais. J303 recebe B+ pós-fusível próprio antes de K201 e PACK−. D301 SS14-E3/61T com anodo em AUX_5V e catodo em Pico VSYS bloqueia retorno da USB do Pico. Se USB do Pico estiver presente e AUX cair, U303 TPS3808G01 força READY baixo. Divisor 105k/10k dá ~4,66 V de limiar nominal. R313 2,2 kΩ limita corrente no clamp open-drain. Perda de READY limpa o latch híbrido por U204. Chicotes DNP.

Orçamento de estudo 250 mA, incluindo bobina 72–88 mA a 5 V/23 °C, Pico e lógica. A 8,4 V, 5 V/0,25 A, 2,2 µH e 2,25 MHz, ripple ideal ≈0,41 A pico a pico e pico ≈0,46 A. A 6 V/80% hipotéticos, entrada ≈0,26 A, perda total ≈0,31 W. Validar Ceff, ESR, eficiência, inrush, curto, térmica, captura da bobina e limite baixo de VM. A faixa operacional de entrada do CI não garante 5 V na saída quando VIN cai perto/abaixo de 5 V. Ensaiar 5,5–8,4 V com carga variável, rampa lenta e perda de pack. Se a margem falhar, desligar sem rearme ou usar buck-boost como TPS63070 após revisão da topologia. [TI TPS62160](https://www.ti.com/lit/ds/symlink/tps62160.pdf) · [Coilcraft XAL4020](https://www.coilcraft.com/en-us/products/power/shielded-inductors/molded-inductor/xal/xal40xx/xal4020-222/) · [TI TPS3808](https://www.ti.com/lit/ds/symlink/tps3808.pdf) · [TI TPS63070](https://www.ti.com/product/TPS63070)
