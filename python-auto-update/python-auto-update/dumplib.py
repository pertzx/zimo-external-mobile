#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dumplib.py — parser para dump.cs do Il2CppDumper (Free Fire).

Extrai classes -> campos -> offsets no formato:
    internal class Foo : Bar
    {
        // Fields
        public Type fieldName; // 0x1C
        public static Type staticField; // 0x4
    }
Tambem captura propriedades/metodos (sem offset util) para busca contextual.
"""
import re
import sys


class Field:
    __slots__ = ("name", "type", "offset", "attrs", "line")

    def __init__(self, name, ftype, offset, attrs, line):
        self.name = name
        self.type = ftype
        self.offset = offset
        self.attrs = attrs
        self.line = line

    def __repr__(self):
        return f"<F {self.attrs} {self.type} {self.name} @ {self.offset:#x}>"


class Cls:
    __slots__ = ("name", "bases", "start", "end", "fields", "props", "methods", "dup")

    def __init__(self, name, bases, start):
        self.name = name
        self.bases = bases
        self.start = start
        self.end = None
        self.dup = False
        self.fields = []   # Field
        self.props = []    # nomes de propriedades
        self.methods = []  # nomes de metodos

    def f(self, name):
        for x in self.fields:
            if x.name == name:
                return x
        return None

    def __repr__(self):
        return f"<C {self.name} : {self.bases} fields={len(self.fields)}>"


FIELD_RE = re.compile(r"^\t+(.*?)([_\w`<>]+);\s*//\s*(?:0x)?([0-9A-Fa-f]+)\s*$")
MODIFIERS = {"public", "private", "internal", "protected", "static", "readonly",
             "const", "new", "override", "volatile", "extern", "unsafe", "abstract", "sealed"}
CLASS_RE = re.compile(r"^\s*(?:\[[^\]]*\]\s*)*(public|private|internal|protected|static|abstract|sealed| )*?(class|struct|interface|enum)\s+([_\w`]+)\s*(?::\s*(.+))?$")


def parse_dump(path):
    """Retorna (classes_by_name, classes_in_order)."""
    classes = {}
    order = []
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            if line.startswith("//") and not line.startswith("\t"):
                # comentario de cabecalho global (Image N, Namespace, etc)
                pass
            m = CLASS_RE.match(line)
            if m and not line.lstrip().startswith("//"):
                name = m.group(3)
                bases = []
                if m.group(4):
                    bases = [b.strip().split(".")[-1] for b in m.group(4).split(",")]
                cur = Cls(name, bases, lineno)
                # nomes duplicados (namespace diferentes): mantem o primeiro
                # no dict 'classes', mas TODOS entram em 'order'
                if name not in classes:
                    classes[name] = cur
                else:
                    cur.dup = True
                order.append(cur)
                continue
            if cur is None:
                continue
            s = line.strip()
            if s == "}":
                cur.end = lineno
                cur = None
                continue
            fm = FIELD_RE.match(line.rstrip())
            if fm:
                prefix, fname, off = fm.groups()
                toks = prefix.strip().split()
                attrs = []
                while toks and toks[0] in MODIFIERS:
                    attrs.append(toks.pop(0))
                ftype = " ".join(toks)
                cur.fields.append(Field(fname, ftype, int(off, 16), " ".join(attrs), lineno))
                continue
            if s.startswith("//"):
                continue
            if s.startswith("public ") or s.startswith("private ") or s.startswith("protected ") or s.startswith("internal "):
                # propriedade ou metodo
                mm = re.match(r"^(?:[\w ]+?)\s+([_\w`<>]+)\s*(\(|\{)", s)
                if mm:
                    if mm.group(2) == "{":
                        cur.props.append(mm.group(1))
                    else:
                        cur.methods.append(mm.group(1))
                continue
    return classes, order


def find_class(classes, names):
    """Procura a primeira classe existente numa lista de candidatos."""
    for n in names:
        c = classes.get(n)
        if c is not None:
            return c
    return None


def dump_class(c, max_fields=None):
    if c is None:
        return "<None>"
    out = [f"class {c.name} : {' , '.join(c.bases)}  (linhas {c.start}-{c.end})"]
    for i, fd in enumerate(c.fields):
        if max_fields and i >= max_fields:
            out.append(f"  ... +{len(c.fields)-max_fields} campos")
            break
        out.append(f"  {fd.attrs} {fd.type} {fd.name}; // {fd.offset:#x}")
    return "\n".join(out)


if __name__ == "__main__":
    cls, order = parse_dump(sys.argv[1])
    for name in sys.argv[2:]:
        print(dump_class(cls.get(name)))
        print()
