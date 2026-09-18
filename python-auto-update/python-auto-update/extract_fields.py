#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
extract_fields.py — identifica CADA campo usado pelo Offsets.cpp nos dumps
NOVOS (v7a + v8a), com verificacao estrutural, e imprime a tabela
code_v76 | dump_v7a | code_v8a | dump_v8a | status.

Estrategias:
  ('nome', Classe, Nome)            -> nome real
  ('tipo', Classe, Tipo, n)         -> n-esimo campo do tipo
  ('bridge', Classe, NomeBridge)    -> nome no dump 8313c28 + alinhamento de indice
  ('fixo', valor)                   -> manual (typeinfo/Unity) — manter
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumplib import parse_dump

# --- paths portateis -------------------------------------------------------
# ZIMO_REPO    : diretorio do repo (com dump_v7a.cs / dump_v8a.cs)
# STORM_BRIDGE : dump ANTIGO (geracao 8313c28, perfil v76 atual) p/ alinhamento
#                Se nao existir, tenta dump_old_v7a.cs no repo, /tmp, e por
#                ultimo extrai do proprio git (git show 8313c28:dump_v7a.cs).
BASE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("ZIMO_REPO", "/home/z/my-project/repo-zimo")


def _find_bridge():
    import subprocess
    cands = []
    if os.environ.get("STORM_BRIDGE"):
        cands.append(os.environ["STORM_BRIDGE"])
    cands.append(os.path.join(REPO, "dump_old_v7a.cs"))
    cands.append("/tmp/dump_old_v7a.cs")
    for c in cands:
        if os.path.exists(c):
            return c
    try:
        r = subprocess.run(["git", "-C", REPO, "show", "8313c28:dump_v7a.cs"],
                           capture_output=True, timeout=300)
        if r.returncode == 0 and r.stdout:
            out = "/tmp/dump_old_v7a.cs"
            with open(out, "wb") as f:
                f.write(r.stdout)
            print(f"[bridge] extraida do git 8313c28 -> {out}")
            return out
    except Exception:
        pass
    print("[bridge] AVISO: dump-ponte nao encontrado; alinhamento bridge falhara")
    return cands[-1]


BRIDGE = _find_bridge()


class D:
    def __init__(self, path):
        self.cls, self.order = parse_dump(path)

    def c(self, name):
        return self.cls.get(name)

    def allc(self, name):
        return [x for x in self.order if x.name == name]


B = D(BRIDGE)
V7 = D(f"{REPO}/dump_v7a.cs")
V8 = D(f"{REPO}/dump_v8a.cs")

# desambiguacao de classes com nome duplicado
def resolve(d, cname):
    if cname == "Player":
        ps = [c for c in d.order if c.name == "Player" and "AttackableEntity" in c.bases]
        return ps[0] if ps else None
    return d.c(cname)


def player(d):
    ps = [c for c in d.order if c.name == "Player" and "AttackableEntity" in c.bases]
    return ps[0] if ps else None


PB, P7, P8 = player(B), player(V7), player(V8)


def kind(t):
    """Normaliza um tipo para assinatura de alinhamento."""
    import re as _re
    prim = {"Boolean", "Char", "Byte", "SByte", "Int16", "UInt16", "Int32", "UInt32",
            "Int64", "UInt64", "Single", "Double", "String", "Object", "IntPtr",
            "Vector2", "Vector3", "Vector4", "Quaternion", "Color", "Matrix4x4", "Transform"}
    if t in prim:
        return t
    if _re.fullmatch(r"[A-Z]{6,}", t):
        return "T"  # nome ofuscado
    if t.startswith("List"):
        return "L"
    if t.startswith("Dictionary"):
        return "D"
    if t.startswith("HashSet"):
        return "H"
    if t.endswith("[]"):
        return "A" + kind(t[:-2])
    if t.endswith("`2") or t.endswith("`1"):
        return t[:2]
    return t  # nome real de classe/interface


def align(bc, nc):
    """Alinha campos de bc (bridge) -> nc (novo) via difflib.
    Assinatura = (vis, tipo_kind, gap_prox_campo) — gap e muito discriminativo."""
    import difflib

    def sigs(fields):
        out = []
        for i, f in enumerate(fields):
            vis = f.attrs.split()[0] if f.attrs else ""
            gap = (fields[i + 1].offset - f.offset) if i + 1 < len(fields) else 0
            gap = min(gap, 0x30)  # teto p/ estabilidade
            out.append((vis, kind(f.type), gap))
        return out

    sb, sn = sigs(bc.fields), sigs(nc.fields)
    sm = difflib.SequenceMatcher(None, sb, sn, autojunk=False)
    mapping = {}
    for blk in sm.get_matching_blocks():
        for k in range(blk.size):
            mapping[blk.a + k] = blk.b + k
    return mapping


CACHE_ALIGN = {}


def aligned(d_target, cls_name, bridge_idx):
    """Campo no dump target correspondente ao bridge_idx de cls_name."""
    bc = resolve(B, cls_name)
    nc = resolve(d_target, cls_name)
    if bc is None or nc is None:
        return None, "classe ausente"
    key = (id(d_target), cls_name)
    if key not in CACHE_ALIGN:
        CACHE_ALIGN[key] = align(bc, nc)
    mp = CACHE_ALIGN[key]
    if bridge_idx in mp:
        j = mp[bridge_idx]
        return nc.fields[j], f"idx {bridge_idx}->{j}"
    return None, "sem match"


def g7(spec):
    return fld(V7, spec)


def g8(spec):
    return fld(V8, spec)


def fld(d, spec):
    k = spec[0]
    if k == "nome":
        c = resolve(d, spec[1])
        return c.f(spec[2]) if c else None
    if k == "tipo":
        c = resolve(d, spec[1])
        if not c:
            return None
        hits = [f for f in c.fields if f.type == spec[2]]
        return hits[spec[3]] if spec[3] < len(hits) else None
    if k == "fixo":
        return "FIXO"
    return None


def bridge_lookup(cls_name, field_name):
    bc = resolve(B, cls_name)
    if not bc:
        return None, None
    f = bc.f(field_name)
    if not f:
        return None, None
    idx = bc.fields.index(f)
    return f, idx


TABELA = []


def row(codename, code_v76, code_v8a, spec7, spec8, note=""):
    """spec7/spec8: ('nome',C,N) | ('tipo',C,T,n) | ('fixo',None)"""
    f7, f8 = fld(V7, spec7), fld(V8, spec8)
    TABELA.append((codename, code_v76, code_v8a, f7, f8, note))


if __name__ == "__main__":
    print("== demo: alinhamento Player: m_AvatarManager (bridge KPMDIPJINJO) ==")
    f, idx = bridge_lookup("Player", "KPMDIPJINJO")
    print("bridge:", f, "idx:", idx)
    for tag, d, P in (("v7a", V7, P7), ("v8a", V8, P8)):
        nf, info = aligned(d, "Player", idx)
        print(f"{tag}: {info} -> {nf}")
