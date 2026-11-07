# BalanceBot · falhas, testes e critérios de aprovação

**Estado: análise digital. Nenhum teste físico foi realizado.** Os testes a seguir exigem bancada e procedimento próprios aprovados, energia limitada e carga simulada nas etapas iniciais.

| Evento | Detecção e resposta disponíveis | Lacuna e aceitação |
|---|---|---|
| NC A abre / cabo parte | Retira COIL_5V da bobina. U212 observa COIL_P através de R231/D202, limpa latch. | Ensaiar com clamp, fio aberto, carga e temperatura. Bobina não pode sustentar corrente após o limite definido e nenhum refechamento sozinho pode rearmar. |
| NC B abre / cabo parte | Interrompe EN_HW e U213 detecta queda do piloto. Latch corta também Q203. | Comprovar limiares, corrente de contato e tempo mínimo de abertura detectável. Curto paralelo ao contato pode mascarar falha. |
| K201 soldado | Caminho eletrônico com Q202 ainda íntegro retira VM. | VM_K_SENSE é diagnóstico passivo. Ensaiar soldagem simulada com impedância e descarga conhecidas. Implementar decisão de bloqueio de rearme. |
| Q202 D–S em curto | Canal K201 pode abrir alimentação. Diodo Q201 pode manter condução se K201 não abrir. | Verificar capacidade real de interrupção DC, temperatura e soldagem com pior L/R e I²t aprovados. |
| Q201 D–S em curto | Pode retornar energia via diodo Q202 enquanto K201 estiver fechado. | Verificar corrente reversa e energia durante a janela de abertura. Coordenar com BMS e fusível. |
| Gate/enable presos, latch defeituoso ou curto em FAULT_N | Dois contatos NC atuam em pontos físicos separados. | Causa comum, dois curtos ou bypass externo podem anular os dois canais. Injetar falhas separadamente. Sem alegação de certificação. |
| Brownout 5 V, SAFE_3V3 ou VIO | U203/U204 limpam o latch. Q203 tem pull-down e Q204 tem resistor G–S. | Variação de 5 V que ainda mantém 3,3 V pode deixar bobina abaixo de captura. Provar drop-out, sem rearmar após recuperação e sem torque com VM residual. |
| Reset ESP32 / QUAL perde atualização | ARM_SENSE ou QUAL_READY baixo limpa o latch. U207 monitora bordas qualificadas quando habilitado. | Testar WDO preso, WDI espúrio, handshake e ausência de amostra. Uma falha do qualificador pode simular progresso. |
| Rearme mantido ou chicote aberto | Flip-flops exigem press seguido de release por outro contato. | Injetar chatter, curto entre contatos, edges durante recuperação e troca de alimentação. Não permitir rearme sem ação nova deliberada. |
| VM baixo em RUN | U215 só fornece telemetria. | Implementar janela de partida e corte retido após UV. Medir tempo, torque e condição de rearme. |
| VM alto ou bateria desconectada em frenagem | U214 pode limpar latch se SAFE_3V3 válido. Topologia dump usa VM local. | Atuador e resistência DNP, sem proteção física creditada. Com J7 aberto o dump no módulo pode ficar desconectado da carga. Exigir clamp local, continuidade e resposta mesmo sem VIO. |
| Resistor do dump aberto, FET curto, TVS aberta/curta | Nenhuma mitigação completa nesta revisão. | Dimensionar fusível térmico/elétrico, medir energia e temperatura, implementar diagnóstico de perda de dissipação. |
| USB + bateria / perda parcial / instrumentos | Netlist preserva B− separado e VIO monitorado. | Medir fugas em B−/PACK−, UART0, sinais TMC e J11 em todas as combinações. Limitar corrente em bancada sem unir terras por instrumento. |

## Gates do primeiro protótipo

1. **Digital e componentes.** Conferir footprint físico de K201, desenhos dos conectores, orientação, EPs e montagem de DNP. Resolver ERC por evidência e fechar esquema de alimentação auxiliar, J8 e qualificador. Aprovação exige netlist pin-to-pin do chicote e demonstração de que nenhum conector equivalente pode ser montado no destino errado.
2. **Pack e motor separados.** Caracterizar DCIR e queda de cada célula por lote/SOC/temperatura. Medir R e L por fase e torque × velocidade × VM, corrente de fase e corrente de bateria. Aprovação exige envelope de pior caso documentado com incerteza e margens de VM/VIO. Selecionar R16, F1 e fusível da origem por limiar, filtro, curto prospectivo, I²t e temperatura.
3. **Carregador/BMS isolados.** Simular células, termistor, NTC aberto/curto, perda de alimentação e desconexão. Readback U4/U5 após POR e watchdog, checar OTG, PROG, CE, TS, comparadores, FETs e limites de célula. Aprovação exige ausência de carga indevida e cada célula dentro do envelope acordado. Não gravar OTP durante exploração.
4. **Supervisor sem potência nos motores.** Exercitar debounce, power cycle, E-stop individual, retorno de chicote, reset e entradas falsas. Medir FAULT_N, WDO, ARM_LATCH, EN e coil. Aprovação exige nenhum rearme em retorno de condição e latência menor que o orçamento validado.
5. **Corte de potência com simulador.** Carga R/C e energia limitadas, observar VGS/VDS, CAP−VS, corrente reversa e inrush. Abrir cada corte e simular falha única com procedimento aprovado. Aprovação exige SOA, tensão, temperatura, arco, durabilidade e I²t dentro dos limites aprovados. Sequência de pré-carga só libera PWR_OK após confirmação do VM real.
6. **Regeneração.** Injetar perfil de energia e potência medidos, medir VM junto a U6/U7, clamp, corrente e temperatura do dump. Injetar perda de bateria, abertura de J7/J9, queda de VIO e falha do dissipador. Aprovação exige pico abaixo do menor limite derated e dissipação/tempo residual adequados.
7. **PCB e integração.** Fechar stackup, espessura de cobre, retorno Kelvin, dimensões/mecânica, dissipação, EMI e desconexão. DRC com netlist revisada, inspeção cruzada de montagem e validação funcional com o robô retido. Evidência de bancada, documentação de desvios e revisão independente precedem qualquer liberação.

Registrar fonte, versão, configuração, carga, incerteza, temperatura, SOC e oscilogramas brutos. Usar os próprios limites aprovados no teste, sem promover os cenários de cálculo do relatório a requisitos de produção.
