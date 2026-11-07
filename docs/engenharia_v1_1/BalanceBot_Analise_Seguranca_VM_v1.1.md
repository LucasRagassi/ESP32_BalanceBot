# BalanceBot v1.1 · Segurança VM, regeneração e qualificação

**B/D, candidatos e ensaio obrigatório.** Sem crédito de certificação ou de cobertura de falha única.

## Corte, pré-teste e falhas

K201 NA abre VM antes do LM74800. Q201/Q202 têm drenos comuns. NC A retira energia da bobina, NC B interrompe EN. Seu piloto ≈0,576 V permite leitura sem autorização de HGATE. Q203 tem pull-down, D201 segue DNP e D202 protege apenas o sense. Medir sobretensão negativa em COIL_P, clamp, tempo de release e arco. G5LE-1 DC5 é candidato resistivo de 8 A/30 VDC e permanece sem footprint. Verificar COM/NA/NC na peça, interrupção real com L/R e inrush. [Omron G5LE](https://components.omron.com/sites/default/files/datasheet_pdf/K100-E1.pdf) · [TI LM7480](https://www.ti.com/lit/ds/symlink/lm7480.pdf)

U220/U221 monitoram VM_K e VM_MOTOR com saída open-drain. CHECK_REF≈1,242×10/(33+10)=0,289 V. Divisores 100k/22k nas duas entradas dão limiar nominal ~1,60 V, antes de tolerância, histerese e transitórios. PRECHECK_OK só fica alto com ambos baixos. U210 o amostra no pressionamento e U211 amostra ARM_SAMPLE = PRESS_SEEN ∧ PRECHECK_OK na liberação. FAULT_N limpa ambos. A subida normal de VM em RUN não atua pelo pré-teste. U214 OV é distinto e UV em RUN segue telemetria. [TI TLV3011B/TLV3012B](https://www.ti.com/lit/ds/symlink/tlv3012b.pdf)

| Falha | Resposta candidata e lacuna |
|---|---|
| K201 soldado | Se VM_K permanecer alto, o pré-teste impede novo rearme. Com F1 aberto, fonte ausente ou caminho rompido, tensão baixa não comprova contato aberto. Q202 deve cortar. Não equivale a EDM de guia forçada. |
| Q202 curto | K201 precisa interromper a corrente DC real. SOA, arco, I²t e soldagem requerem ensaio. |
| Q201 curto | Possível retorno por diodo de Q202 até abrir K201. Medir Irev, Eret e coordenação. |
| Brownout, perda de VIO ou READY | U203/U204/WD limpam latch. Exigir novo press/release. Medir sequência de VIO/VM, injeção por STEP/DIR/UART e torque residual. |
| WDO preso, latch defeituoso, curto comum no chicote | NC A/NC B atuam em pontos físicos diferentes. Causas comuns e falhas simultâneas seguem abertas. Testar cada canal e curto cruzado. |
| Dump aberto ou FET em curto | D3/dump DNP, sem cobertura demonstrada. Monitorar disponibilidade do dissipador, sobretemperatura e fusível. |
| J7 aberto | O dump no híbrido pode deixar de enxergar VM dos drivers. Proteção local em J9 ou integração do dump no lado dos drivers deve ser fechada. |
| VM baixo em RUN | VM_PRESENT é telemetria. Falta janela de partida e corte retido por UV. VM_READY_TEST não substitui PWR_OK de J8. |

## Regeneração e residual

Topologia DNP preservada a jusante de K201/Q202. U217 TPS7A1633 e U218 TLV3012B se alimentam de VM_MOTOR. Os limiares nominais do dump são 10,81 V na subida e 9,99 V na descida. Q205 AO3400A candidato, 30 V/48 mΩ máx. a VGS 2,5 V nas condições da ficha. R228 TBD/DNP. C37/C43 100 µF cada, candidato de 25 V Panasonic EEEFK1E101P com footprint ainda pendente. D3 SMBJ10CA bidirecional, 10 V de stand-off, ruptura 11,1–12,3 V e clamp de 17 V a 35,3 A no pulso da ficha. D3 segue DNP, footprint SMB posicionado na principal sem trilhas VM. C38/C42/C44/C48 passam a 100 nF/50 V X7R, com Murata GRM188R71H104KA93 como candidato. A classificação anterior de 16 V em C42/C48 era inferior ao clamp de 17 V. Verificar Ceff, derating, overshoot e o menor rating dos demais componentes. Não atribuir ao TVS absorção repetitiva. [Littelfuse SMBJ](https://www.littelfuse.com/assetdocs/littelfuse_tvs_diode_smbj_datasheet.pdf?assetguid=09a6ae9a-73cb-4ac4-acac-e6dab92ab953) · [Panasonic EEEFK1E101P](https://industrial.panasonic.com/ww/products/pt/aluminum-cap-smd/models/EEEFK1E101P) · [AO3400A](https://www.aosmd.com/products/mosfets/low-voltage-mosfets-12v-30v/ao3400a)

Medir Eret pela integral da potência reversa com frenagem simultânea, inversão, abertura de VM e remoção do pack. 200 µF ideais de 8,4 a 12 V absorvem 7,34 mJ. Exemplo hipotético 0,10 J nessa faixa pede 2,72 mF efetivos. Exemplo 20 W a 12 V pede R≤7,2 Ω e I≥1,67 A. Cinco pulsos hipotéticos de 0,10 J/s totalizam 0,5 W médios. Margem de 0,5 V a 2 A implica ESR≤0,25 Ω sem ESL. Medir L·di/dt e escolher teto pelo menor rating derated, incluindo VM local e cabo. Comparar curva de pulso/temperatura de R228 e SOA VDS×ID de Q205. Bleed 10 kΩ/200 µF daria ~3,32 s de 8,4 a 1,60 V sem carga adicional. A medida de torque residual é indispensável.

## Qualificador independente e contrato futuro

Pico RP2040 com PIO para observar os três taps, UART e clock próprios. U302 SN74LVC125A não dirige o barramento e tem Ioff. Cada entrada usa 1 kΩ em série e 470 kΩ para retorno após a série. GPIO2/3/4 observam SCL/SDA/DRDY, GPIO1 recebe UART e GPIO16/17 emitem READY/WDI. U303 força READY baixo quando AUX cai. TPS3435 do híbrido permanece independente e de 20 ms nominais. O código atual tem timeout I²C de 20 ms, portanto essa configuração não garante margem com jitter. [Pico](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf) · [TI SN74LVC125A](https://www.ti.com/lit/ds/symlink/sn74lvc125a.pdf) · [TI TPS3435](https://www.ti.com/lit/ds/symlink/tps3435.pdf)

1. **Chicote.** J301.1–5 recebe SCL J11.4, SDA J11.3, DRDY J11.5, TX J14.1 e PACK−. J14.2 é PACK−. Não levar +3V3 de J11 ao J301. J302.1/4 vão a J204.1/2. J302.2/3 vão a J205.1/2. J10.2/.1 permanece em J205.3/.4. J205.5 usa PACK− comum. J303 recebe B+ com fusível externo e PACK−. J3 do BMS pertence a outro domínio.
2. **Quadro UART.** Candidato 460800 8N1, COBS delimitado por zero, CRC-32 ISO-HDLC. Corpo little-endian 23 bytes: versão u8, tipo u8, sessão u32, seq u32, índice DRDY u32, CRC da amostra u32, flags u8 e CRC do quadro u32. CRC do quadro cobre os 19 bytes anteriores. HELLO tipo 1, sessão não zero e demais campos zero, seguido de reconfiguração observável da IMU. CONSUMED tipo 2, versão 1, seq inicial 0, DRDY inicial 1, flags=1 só após consumo válido.
3. **Validação.** READY/WDI baixos em reset. O monitor exige HELLO, WHO_AM_I em 0x68 e configuração conhecida. Correlacionar cada DRDY a uma leitura completa de 14 bytes desde 0x3B, endereço 0x68, ACKs, repeated START, NACK final e STOP. Aceitar CONSUMED apenas se índice, CRC dos dados observados, seq e idade coincidirem. Contadores módulo 2³². Conteúdo igual pode pertencer a evento novo. Invalidar leitura truncada, duplicata, timeout, DRDY faltante, barramento preso, novo DRDY com evento pendente ou mudança de sessão. WDI só na validação do quadro, nunca temporizador livre.
4. **Recuperação e tempo.** Falha derruba READY, exige HELLO novo e o latch exige press/release. Fechar Tevento,max+margem < Twd,min e Twd,max+Tcorte+Tresidual ≤ Tseguro. Medir I²C, clocks, Wi-Fi e jitter. Um host defeituoso que fabrique quadros coerentes exige outra fonte de evidência. O modelo do ZIP testa a lógica com estímulos sintéticos. Nenhum firmware de produção foi criado ou modificado.
