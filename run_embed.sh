#!/bin/bash
cd "C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile"
xxd -i hesco_ov.png > /tmp/full_png.h
# Fix variable names
sed -i 's/unsigned char .*\[\]/const unsigned char LogoMenuRawRGBA[]/' /tmp/full_png.h
sed -i 's/unsigned int .*_len/const size_t LogoMenuRawRGBASize/' /tmp/full_png.h
# Add header
echo '#pragma once' > app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
echo '' >> app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
echo 'const int LogoWidth = 720;' >> app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
echo 'const int LogoHeight = 1600;' >> app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
echo '' >> app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
cat /tmp/full_png.h >> app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp
echo 'Done!'