# ESP32 BalanceBot · Hardware integrado v1.0

**Engenharia em revisão. Sem liberação para energização, montagem ou fabricação.**

- Abra `hardware/pcb/BalanceBot.kicad_pro` para a placa principal.
- Abra `hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_pro` para o módulo híbrido.
- O projeto em `hardware/seguranca_vm/` é a referência histórica.
- Use KiCad 10 e bibliotecas oficiais de símbolos e footprints. As bibliotecas locais acompanham a árvore e usam caminhos relativos ao projeto.
- `BOM_consolidada.csv` usa UTF-8 e ponto e vírgula. É um inventário de engenharia com flags CAD e 278 referências, incluindo o módulo histórico.
- `verificacao/` contém ERCs e netlists da base e da entrega, deltas, checksums e as tentativas de DRC. Os logs de DRC registram a dependência ausente e não constituem aprovação.
- O relatório principal inclui cálculos, decisões e interfaces. `BalanceBot_Falhas_e_Testes_v1.0.md` registra a análise de falhas e os gates de bancada.

## Aplicação local no Git

A base exata é `1f745b5707b9ea3af3a6d6e763366db217baadb8` da branch `hardware/bms-2s-revisao-work`. Antes de aplicar, use uma cópia limpa e dedicada. Preserve alterações existentes.

```bash
git rev-parse HEAD
git status --short --branch
git apply --check /caminho/BalanceBot_Hardware_v1.0.patch
git apply /caminho/BalanceBot_Hardware_v1.0.patch
git status --short
git diff --check
```

Se o primeiro comando não retornar o hash acima ou houver alterações locais, crie uma cópia de trabalho separada nesse commit. A aplicação do patch foi conferida em extração limpa e comparada byte a byte com os arquivos editáveis deste ZIP.

## Inventário do patch

| Arquivo | Alteração |
|---|---|
| `hardware/pcb/BalanceBot.kicad_sch` | Polarização TS e interfaces J12/J13 do BQ25798. |
| `hardware/seguranca_hibrida/BalanceBot_Seguranca_Hibrida.kicad_sch` | Supervisor, interfaces, latch, monitor, driver de bobina e dissipador condicional. J203 retirado e interfaces novas J204–J207. |
| `hardware/seguranca_hibrida/BalanceBot_Supervisao.kicad_sym` | Biblioteca local dos CIs adicionados. |
| `hardware/seguranca_hibrida/sym-lib-table` | Registro da biblioteca local. |

Documentação, evidências e BOM acompanham o ZIP como arquivos de entrega. O PCB vazio, os footprints preexistentes e o módulo histórico foram preservados.
