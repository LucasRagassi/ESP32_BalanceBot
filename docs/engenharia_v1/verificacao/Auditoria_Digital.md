# Comparação automatizada de netlists e ERC

| Projeto | Redes antes/depois | ERC inicial erros/avisos | ERC final erros/avisos |
|---|---:|---:|---:|
| pcb | 152 / 152 | 48 / 2 | 45 / 0 |
| seguranca_hibrida | 18 / 66 | 2 / 0 | 4 / 0 |
| seguranca_vm | 49 / 49 | 4 / 0 | 4 / 0 |

Checks de componentes anteriores, conjunto exato das redes alteradas na principal, caminhos críticos e DNP aprovados digitalmente. VM_FUSED possui apenas F1/J7 na principal e J201/K201 no módulo. Retornos B− e PACK− se mantêm distintos. O módulo histórico é idêntico byte a byte.

## Auditoria CAD dos pads

| Projeto | Resultado | Componentes |
|---|---|---:|
| pcb | Numeração OK | 44 |
| pcb | Pads extra | 1 |
| pcb | Sem footprint | 93 |
| seguranca_hibrida | Numeração OK | 86 |
| seguranca_hibrida | Sem footprint | 8 |
| seguranca_vm | Numeração OK | 4 |
| seguranca_vm | Sem footprint | 42 |

A verificação numérica usa os nós de netlist e ignora aberturas de pasta sem número. Geometria, pinos mecânicos e footprints herdados requerem revisão física/DFM.

## Alterações elétricas na árvore

- hardware/pcb/BalanceBot.kicad_sch
- hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_sch
- hardware/seguranca_hibrida/BalanceBot_Supervisao.kicad_sym
- hardware/seguranca_hibrida/sym-lib-table
