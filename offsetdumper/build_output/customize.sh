#!/system/bin/sh
# customize.sh - instalador do modulo Zygisk
# Executado pelo Magisk durante a instalacao

SKIPUNZIP=0

ui_print "- Instalando OffsetDumper Zygisk Module"

if [ "$MAGISK_VER_CODE" -lt 24000 ]; then
  abort "! Magisk 24.0+ requerido. Voce tem: $MAGISK_VER_CODE"
fi

if [ ! -d "/data/adb/zygisk" ] && [ ! -d "/data/adb/modules/zygisksu" ]; then
  ui_print "! Aviso: Zygisk pode nao estar ativo"
  ui_print "! Habilite Zygisk nas configuracoes do Magisk"
fi

SO32="$MODPATH/zygisk/armeabi-v7a.so"
SO64="$MODPATH/zygisk/arm64-v8a.so"

if [ ! -f "$SO32" ] && [ ! -f "$SO64" ]; then
  ui_print "! Nao encontrei zygisk/*.so apos extracao"
  abort "! Instalacao abortada"
fi

SO64_SIZE=$(stat -c %s "$SO64" 2>/dev/null || echo 0)
ui_print "- .so 64-bit size: $SO64_SIZE bytes"

# Aceita .so a partir de 8KB (modulo minimo e pequeno mesmo)
if [ "$SO64_SIZE" -lt 8000 ]; then
  ui_print "! .so 64-bit muito pequena ($SO64_SIZE bytes)"
  abort "! Abortando pra evitar zygote crash"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644

ui_print "- Instalacao concluida!"
ui_print "- Reboot e abra Free Fire"
ui_print "- Logcat tag: OffsetDumper"
