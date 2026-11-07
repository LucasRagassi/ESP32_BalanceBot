# BalanceBot v1.1 · pendências de fabricação e energização

| Prioridade | Bloqueio e efeito | Dado ou ensaio indispensável | Critério de saída |
|---|---|---|---|
| P0 | Pack, R16, Q3/Q4, F1, fusível principal e AUX. Sem liberação elétrica. | DCIR, corrente normal/pico/falta, SOC e temperatura. Medir shunt, FETs, I²t e atraso. | Limiar e térmica coordenados, margem por célula, fusíveis com interrupção DC e chicote protegido. |
| P0 | BQ25798 e USB. Carga permanece inibida. | NTC e limites da célula, readback POR/WD, backfeed, bateria ausente e VBUS instável. | Perfil seguro de célula, nenhuma rota de retorno indevida e inibição comprovada. |
| P0 | K201/Q201/Q202 e fonte auxiliar. | Corrente do relé, arco, clamp e release. Desempenho da fonte em baixa tensão, falha e curto. | Cortes interrompem nas condições aprovadas. Alimentação ausente não rearma. |
| P0 | Intertravamento J8/R43 e UV em RUN. | Perda de VIO com VM presente, queda de VM, tempo de pré-carga, GPIOs/ENN e torque residual. | Estados definidos em partida e falha, corte retido por UV e ausência de torque perigoso. |
| P0 | Qualificador físico sem firmware. | Implementar parser, observação, contrato e watchdog. Medir jitter e tempo de parada. | Evidência de rejeição de dados velhos, barramento preso, falhas no host e reinício manual. |
| P0 | Regeneração. R228/Q205/D3 e proteções DNP. | Medir energia real, ESR/ESL, corrente reversa, abertura de cabos e térmica. | Tensão e SOA abaixo de limites derated e proteção local quando J7 abre. |
| P1 | Conectores, relé, NTCs, L1/L2 e outros footprints incompletos. | Dimensões, pinagem, montagem, terminais, temperatura, disponibilidade. | MPN e footprint com desenho aprovado e BOM completa. |
| P1 | PCB principal e híbrida sem contorno. AUX somente de estudo. | Chassis, altura, centro de massa, furação, fixação, acesso ao USB e E-stop. | Envelope mecânico medido e interferências resolvidas. |
| P1 | ERC/DRC abertos, layouts parciais. | Resolver os TSVs de redes, DFM de passos/NPTH, vias térmicas, USB/EMI, cobre e retorno. | DRC/paridade sem erros inexplicados, zonas/rotas revisadas, inspeção de fabricação documentada. |

O arquivo de configuração do BMS não é candidato de produção. Os valores do relatório são estímulos de análise. Nenhum dos gates de bancada foi executado nesta entrega.
