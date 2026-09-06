import sys
import os

input_png = 'hesco_ov.png'
output_hpp = 'app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp'

with open(input_png, 'rb') as f:
    data = f.read()

print(f'PNG size: {len(data)} bytes')

with open(output_hpp, 'w') as f:
    f.write('#pragma once\n\n')
    f.write('const int LogoWidth = 720;\n')
    f.write('const int LogoHeight = 1600;\n\n')
    f.write('const unsigned char LogoMenuRawRGBA[] = {\n')
    for i, b in enumerate(data):
        if i % 16 == 0:
            f.write('    ')
        f.write(f'0x{b:02X}, ')
        if i % 16 == 15:
            f.write('\n')
    f.write('\n};\n')
    f.write(f'const size_t LogoMenuRawRGBASize = {len(data)};\n')

print('Done!')