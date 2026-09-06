#!/bin/bash
python3 << 'PYEOF'
with open('hesco_ov.png', 'rb') as f:
    data = f.read()
print(f'Size: {len(data)} bytes')
with open('app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp', 'w') as f:
    f.write('#pragma once\n\n')
    f.write(f'const int LogoWidth = 720;\n')
    f.write(f'const int LogoHeight = 1600;\n\n')
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
PYEOF