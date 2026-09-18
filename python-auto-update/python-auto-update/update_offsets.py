#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
update_offsets.py — compara os offsets do Offsets.cpp com os dumps NOVOS
(v7a + v8a) e gera os novos blocos FFTHV7A76()/FFTHV8A().

Estrategias de identificacao por campo:
  ('nome', Classe, NomeCampo)              -> nome real no dump
  ('tipo', Classe, TipoCampo, n)           -> n-esimo campo da classe com esse TIPO
  ('back', Classe, NomeBackingField)       -> <XXX>k__BackingField
  ('idx',  Classe, indice)                 -> posicao na lista de campos (estavel)

Saida: tabela de comparacao + bloco C++ gerado.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumplib import parse_dump, Cls, Field

REPO = os.environ.get("ZIMO_REPO", "/home/z/my-project/repo-zimo")


class DL:
    """Dump carregado de um ABI."""

    def __init__(self, tag):
        self.tag = tag
        self.cls, self.order = parse_dump(f"{REPO}/dump_{tag}.cs")

    # ---------- resolucao de classes ----------
    def c(self, name, bases=None, idx=0):
        cs = [x for x in self.order if x.name == name]
        if bases is not None:
            cs = [x for x in cs if all(b in x.bases for b in bases)]
        if not cs:
            return None
        return cs[idx]

    def c_by_field(self, parent, ftype, fname=None):
        """Classe cujo nome == tipo do campo 'fname' do 'parent' (type-chase)."""
        f = parent.f(fname) if isinstance(parent, Cls) else parent
        if f is None:
            return None
        return self.cls.get(f.type)

    # ---------- resolucao de campos ----------
    def fld(self, spec):
        kind = spec[0]
        try:
            if kind == "nome":
                _, cn, fn = spec
                c = self.cls.get(cn)
                return c.f(fn) if c else None
            if kind == "back":
                _, cn, fn = spec
                c = self.cls.get(cn)
                if not c:
                    return None
                return c.f(f"<{fn}>k__BackingField")
            if kind == "tipo":
                _, cn, ftype, n = spec
                c = self.cls.get(cn)
                if not c:
                    return None
                hits = [f for f in c.fields if f.type == ftype]
                return hits[n] if n < len(hits) else None
            if kind == "idx":
                _, cn, i = spec
                c = self.cls.get(cn)
                if not c or i >= len(c.fields):
                    return None
                return c.fields[i]
        except Exception:
            return None
        return None


d7 = DL("v7a")
d8 = DL("v8a")


def both(spec):
    return d7.fld(spec), d8.fld(spec)


# ============================================================
#  Resolucao das classes obfuscadas (type-chase)
# ============================================================
def resolve_classes():
    r = {}
    mg7, mg8 = d7.cls.get("MatchGame"), d8.cls.get("MatchGame")
    r["Match"] = (mg7.f("m_Match").type, mg8.f("m_Match").type)
    M7 = d7.cls.get(mg7.f("m_Match").type)
    M8 = d8.cls.get(mg8.f("m_Match").type)
    r["M7"], r["M8"] = M7, M8
    # Observer: campo @0xb4 (antigo m_LocalObserver) -> tipo
    r["Observer"] = (M7.f("DGDPMNMOAFP").type, M8.f("DGDPMNMOAFP").type)
    # ShadowState: tipo de PlayerNetwork.m_ShadowState
    pn7, pn8 = d7.cls.get("PlayerNetwork"), d8.cls.get("PlayerNetwork")
    r["ShadowState"] = (pn7.f("m_ShadowState").type, pn8.f("m_ShadowState").type)
    # Profile: tipo do campo PlayerNetwork apos m_ShadowState (BaseProfileInfo?)
    return r


if __name__ == "__main__":
    R = resolve_classes()
    for k, v in R.items():
        if not isinstance(v, tuple):
            continue
        print(f"{k:14s} v7a={v[0]}  v8a={v[1]}")
    M7, M8 = R["M7"], R["M8"]
    print()
    print("=== Match (JMAGGLCNGIG/J...) campos-chave ===")
    for name in ("LOEPAKMFNJO", "MHJCLOPOBAA", "DGDPMNMOAFP", "JONHEMLEHHL"):
        f7, f8 = M7.f(name), M8.f(name)
        print(f"  {name}: v7a={f7} | v8a={f8}")
