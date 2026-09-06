#!/bin/bash
xxd -i "C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile/hesco_ov.png" > /tmp/png_header.txt
sed -i 's/unsigned char/logo_raw_png/' /tmp/png_header.txt
sed -i 's/unsigned int/logo_raw_png_len/' /tmp/png_header.txt
cat /tmp/png_header.txt