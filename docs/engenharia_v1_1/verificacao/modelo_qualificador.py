#!/usr/bin/env python3
"""Modelagem digital de protocolo e rearme. Nenhum firmware, hardware ou tempo real."""
from dataclasses import dataclass
from zlib import crc32
from struct import pack, unpack

@dataclass
class Qualifier:
    session: int | None = None
    identity_ok: bool = False
    event: tuple | None = None
    event_index: int = 0
    sequence: int = 0
    ready: bool = False
    deadline: float | None = None
    ttl: float = 9  # parâmetro sintético dos testes em ms

    def reset(self):
        self.session = None
        self.identity_ok = False
        self.event = None
        self.event_index = self.sequence = 0
        self.ready = False
        self.deadline = None

    def hello(self, session):
        self.reset()
        self.session = session

    def config(self, who, address, complete):
        if not self.session or who != 0x68 or address != 0x68 or not complete:
            self.reset()
        else:
            self.identity_ok = True

    def drdy(self, t):
        if not self.identity_ok or self.event:
            self.reset()
        else:
            self.event_index = (self.event_index + 1) & 0xffffffff
            self.event = (self.event_index, t, None)

    def transaction(self, address, register, payload, complete):
        if not self.event or self.event[2] is not None or address != 0x68 or register != 0x3B or not complete or len(payload) != 14:
            self.reset()
        else:
            i, t, _ = self.event
            self.event = i, t, crc32(payload)

    def consumed(self, message, now):
        if len(message) != 23 or crc32(message[:-4]) != int.from_bytes(message[-4:], 'little') or not self.event:
            self.reset()
            return False
        version, kind, sid, seq, index, digest, status = unpack('<BBIIIIB', message[:-4])
        i, t, expected = self.event
        if (version, kind, sid, seq, index, digest, status) != (1, 2, self.session, self.sequence, i, expected, 1) or not 0 <= now - t <= self.ttl:
            self.reset()
            return False
        self.sequence = (self.sequence + 1) & 0xffffffff
        self.event = None
        self.deadline = now + self.ttl
        self.ready = True
        return True  # WDI pulse request

    def tick(self, now, bus_stuck=False):
        if bus_stuck or (self.deadline is not None and now >= self.deadline) or (self.event and now - self.event[1] > self.ttl):
            self.reset()

def packet(data, seq=0, index=1, sid=9, status=1):
    v=pack('<BBIIIIB',1,2,sid,seq,index,crc32(data),status)
    return v+pack('<I',crc32(v))

sample=bytes(range(14))
def pending(data=sample):
    q=Qualifier()
    q.hello(9);q.config(0x68,0x68,True);q.drdy(2);q.transaction(0x68,0x3B,data,True)
    return q

q=pending()
assert q.consumed(packet(sample),5)
assert q.ready
q.drdy(7);q.transaction(0x68,0x3B,sample,True)
assert q.consumed(packet(sample,1,2),8)  # conteúdo igual é válido quando o evento muda
assert not q.consumed(packet(sample,1,2),9) and not q.ready
for msg, t in [(packet(sample,sid=8),5),(packet(sample,seq=2),5),(packet(sample,index=2),5),
               (packet(b'changed'),5),(packet(sample,status=0),5),(packet(sample),12),
               (packet(sample)[:-1],5),(packet(sample)[:-1]+b'\0',5)]:
    q=pending()
    assert not q.consumed(msg,t) and not q.ready
q=pending();q.drdy(3)
assert not q.consumed(packet(sample),5) and not q.ready
q=pending();q.tick(3,bus_stuck=True)
assert not q.ready and q.session is None
q=pending();assert q.consumed(packet(sample),5)
q.tick(15); assert not q.ready and q.session is None
q=Qualifier();q.hello(9);q.config(0x69,0x68,True)
assert not q.identity_ok
q=Qualifier();q.hello(9);q.config(0x68,0x68,True);q.drdy(1);q.transaction(0x68,0x3B,sample[:-1],False)
assert q.session is None

def transition(state, clear=True, press=False, release=False, precheck=True):
    seen, armed=state
    if not clear:return False,False
    if press and release:raise ValueError('contatos SPDT sobrepostos: falha fora do modelo')
    return (precheck if press else seen), ((seen and precheck) if release else armed)

state=transition((False,False),press=True,precheck=False)
assert transition(state,release=True)==(False,False)
state=transition((False,False),press=True)
assert transition(state,release=True,precheck=False)==(True,False)
assert transition(state,release=True)==(True,True)
state=transition((True,True),clear=False)
assert transition(state)==(False,False)
assert transition(state,release=True)==(False,False)
assert transition((True,True),precheck=False)==(True,True)
print('Qualificador: quadros novos, conteúdo repetido com evento novo, sessão, CRC, idade, ordem, DRDY, I²C preso e truncado verificados.')
print('Rearme: pré-teste nas duas bordas, limpeza assíncrona e ausência de rearme por retorno verificadas no modelo.')
print('Sem ensaio físico, firmware de produção, simulação SPICE ou validação dos limites de tempo.')
