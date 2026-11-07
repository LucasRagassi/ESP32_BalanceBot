#!/usr/bin/env python3
"""Modelo lógico digital de rearme. Não simula analógico, falhas ou temporização."""
from itertools import product


def transition(press_seen, armed, fault_clear, press_edge, release_edge):
    if not fault_clear:
        return False, False
    if press_edge and release_edge:
        raise ValueError("Sobreposição de bordas fora do modelo SPDT")
    return press_seen or press_edge, (press_seen if release_edge else armed)


for seen, arm, clear, press, release in product((False, True), repeat=5):
    if press and release:
        continue
    after = transition(seen, arm, clear, press, release)
    if not clear:
        assert after == (False, False)
    if clear and not arm and after[1]:
        assert seen and release
    if clear and not press and not release:
        assert after == (seen, arm)

scenarios = (
    ([(False, False, False), (True, False, True)], (False, False)),
    ([(True, True, False), (True, False, False)], (True, False)),
    ([(True, True, False), (True, False, True)], (True, True)),
    ([(True, True, False), (True, False, True), (False, False, False), (True, False, False)], (False, False)),
)
for events, expected in scenarios:
    state = (False, False)
    for event in events:
        state = transition(*state, *event)
    assert state == expected

pilot_rdown = 1 / (1 / 47000 + 1 / 10000)
pilot = 3.3 * pilot_rdown / (39000 + pilot_rdown)
assert 0.405 < pilot < 1.091
print("24 combinações digitais e quatro sequências passam. Sobreposição de bordas não modelada.")
print(f"Piloto nominal: {pilot:.3f} V. Sem validação física.")
