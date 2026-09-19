#!/usr/bin/env python3
"""Gera SkinItems.h a partir do clothes.json (SkinChangerLogic) —
tabela compacta "itemID|descricao|rarityIdx" embutida no app."""
import json, sys, os

REPO = os.environ.get("ZIMO_REPO", "/home/z/my-project/repo-zimo")
SRC = os.path.join(REPO, "SkinChangerLogic/codes/clothes.json")
OUT = os.path.join(REPO, "app/src/main/cpp/Client/Skin/SkinItems.h")

RARITY = {
    "WHITE": 0, "GREEN": 1, "BLUE": 2, "PURPLE": 3, "PURPLE_PLUS": 4,
    "ORANGE": 5, "ORANGE_PLUS": 6, "RED": 7, "NONE": 8,
}

os.makedirs(os.path.dirname(OUT), exist_ok=True)

with open(SRC, "r", encoding="utf-8") as f:
    items = json.load(f)

rows = []
for it in items:
    if it.get("itemType") != "CLOTHES":
        continue
    try:
        item_id = int(it.get("itemID", "0"))
    except ValueError:
        continue
    if item_id <= 0:
        continue
    desc = (it.get("description") or "?").strip()
    desc = desc.replace("\\", " ").replace('"', "'").replace("\n", " ").replace("\r", "")
    if "|" in desc:
        desc = desc.replace("|", "/")
    if not desc:
        desc = "?"
    rar = RARITY.get(it.get("Rare", "NONE"), 8)
    rows.append((item_id, desc, rar))

rows.sort(key=lambda r: r[0])

with open(OUT, "w", encoding="utf-8") as f:
    f.write("// Gerado por scripts/gen_skin_header.py a partir do clothes.json\n")
    f.write("// NAO EDITE A MAO — rode o gerador de novo apos atualizar o clothes.json\n")
    f.write("#pragma once\n\n#include <cstdint>\n\n")
    f.write("namespace Skin\n{\n")
    f.write("    struct SkinItemRow\n    {\n        uint32_t Id;\n        const char* Name;\n        uint8_t Rarity; // 0=WHITE 1=GREEN 2=BLUE 3=PURPLE 4=PURPLE_PLUS 5=ORANGE 6=ORANGE_PLUS 7=RED 8=NONE\n    };\n\n")
    f.write("    static const SkinItemRow kSkinItems[] =\n    {\n")
    for item_id, desc, rar in rows:
        f.write('        { %uu, "%s", %d },\n' % (item_id, desc, rar))
    f.write("    };\n\n")
    f.write("    static const uint32_t kSkinItemCount = (uint32_t)(sizeof(kSkinItems) / sizeof(kSkinItems[0]));\n")
    f.write("}\n")

print("entries:", len(rows))
print("written:", OUT)
