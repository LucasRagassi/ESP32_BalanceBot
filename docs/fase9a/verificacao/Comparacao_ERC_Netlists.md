# Evidências digitais da Fase 9A

Base 42dc0a6859bb3a1f9071c9f7cae8f9c5512c51c8. KiCad 10.0.8. Comparação sem códigos de rede voláteis, por nome e pares referência/pino.

| Projeto | Redes antes/depois | ERC antes → depois (erros/avisos) |
|---|---:|---:|
| pcb | 152/152 | 48/2 → 48/2 |
| seguranca_vm | 49/49 | 4/0 → 4/0 |
| híbrido | novo / 18 | sem base → 2/0 |

Ambos os ERCs existentes são idênticos linha a linha, salvo o timestamp. A placa principal, a PCB, as configurações, as bibliotecas anteriores, o módulo histórico, o firmware e o restante da árvore de referência são idênticos byte a byte. F1, R43, D3 e J9 permanecem DNP. A rede GND da placa principal corresponde a PACK−. B− permanece em rede própria.

## Novo módulo

| Rede | Pinos |
|---|---|
| `/COIL_5V` | J202.1, J203.1 |
| `/COIL_N` | D201.1, J203.2, K201.2 |
| `/COIL_P` | D201.2, J202.2, K201.5 |
| `/DGATE` | Q201.4, U201.1 |
| `/EN_HW` | J202.4, R203.1, U201.6 |
| `/HGATE` | Q202.4, R204.1, U201.8 |
| `/OV_POST_K` | R201.2, R202.1, U201.5 |
| `/PACK_RTN` | C201.2, C202.2, C204.2, J201.4, J203.4, R202.2, R203.2, R205.2, U201.7 |
| `/PERMIT_HW` | J202.3, J203.3 |
| `/RAMP` | C204.1, R204.2 |
| `/VCAP` | C203.1, U201.11 |
| `/VM_FUSED` | J201.1, K201.1 |
| `/VM_K` | C201.1, K201.3, Q201.1, Q201.2, Q201.3, U201.2 |
| `/VM_MID` | C202.1, C203.2, Q201.5, Q201.6, Q201.7, Q201.8, Q201.9, Q202.5, Q202.6, Q202.7, Q202.8, Q202.9, R201.1, U201.10, U201.12, U201.3, U201.4 |
| `/VM_MOTOR` | J201.3, Q202.1, Q202.2, Q202.3, R205.1, U201.9 |
| `unconnected-(J201-Pin_2-Pad2)` | J201.2 |
| `unconnected-(K201-Pad4)` | K201.4 |
| `unconnected-(U201-RTN_(NC)-Pad13)` | U201.13 |

Os dois erros do módulo são `power_pin_not_driven` em U201.10 (VM_MID, fonte via contato e diodo de corpo de Q201) e U201.7 (PACK_RTN, retorno externo). Foram mantidos sem flags. A conectividade e a direção física dos diodos foram verificadas separadamente.

## Footprints do módulo

| Ref. | Footprint | Pinos/pads |
|---|---|---|
| C201 | Capacitor_SMD:C_0603_1608Metric | 1, 2 |
| C202 | Capacitor_SMD:C_0603_1608Metric | 1, 2 |
| C203 | Capacitor_SMD:C_0603_1608Metric | 1, 2 |
| C204 | Capacitor_SMD:C_0603_1608Metric | 1, 2 |
| D201 | Diode_SMD:D_SMB | 1, 2 |
| J201 | — | sem footprint, seleção mecânica pendente |
| J202 | — | sem footprint, seleção mecânica pendente |
| J203 | — | sem footprint, seleção mecânica pendente |
| K201 | — | sem footprint, seleção mecânica pendente |
| Q201 | BalanceBot:DNK0008A | 1, 2, 3, 4, 5, 6, 7, 8, 9 |
| Q202 | BalanceBot:DNK0008A | 1, 2, 3, 4, 5, 6, 7, 8, 9 |
| R201 | Resistor_SMD:R_0603_1608Metric | 1, 2 |
| R202 | Resistor_SMD:R_0603_1608Metric | 1, 2 |
| R203 | Resistor_SMD:R_0603_1608Metric | 1, 2 |
| R204 | Resistor_SMD:R_0603_1608Metric | 1, 2 |
| R205 | Resistor_SMD:R_0603_1608Metric | 1, 2 |
| U201 | BalanceBot_Hibrida:DRR0012E | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 |

## Arquivos adicionados (nenhum arquivo versionado da base foi alterado)

- `hardware/seguranca_hibrida/BalanceBot_Hibrida.kicad_sym`
- `hardware/seguranca_hibrida/BalanceBot_Hibrida.pretty/DRR0012E.kicad_mod`
- `hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_pro`
- `hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_sch`
- `hardware/seguranca_hibrida/fp-lib-table`
- `hardware/seguranca_hibrida/sym-lib-table`
