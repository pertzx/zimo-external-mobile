#!/usr/bin/env bash
# restore_bridge.sh — extrai o dump ANTIGO (ponte de alinhamento, geracao
# 8313c28) do git history do repo zimo-external-mobile.
#
# Uso:  bash restore_bridge.sh [caminho/do/repo-zimo]
#
# O dump-ponte so e necessario para rodar build_table.py (alinhamento de
# campos ofuscados por indice). Se ja existir, nada e feito.
set -euo pipefail

REPO="${1:-${ZIMO_REPO:-/home/z/my-project/repo-zimo}}"
OUT="${STORM_BRIDGE:-/tmp/dump_old_v7a.cs}"

if [ -s "$OUT" ]; then
  echo "[restore_bridge] dump-ponte ja existe: $OUT"
  exit 0
fi

echo "[restore_bridge] extraindo 8313c28:dump_v7a.cs de $REPO -> $OUT"
git -C "$REPO" show 8313c28:dump_v7a.cs > "$OUT"
echo "[restore_bridge] ok: $(wc -c < "$OUT") bytes"
