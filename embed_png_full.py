import base64

# Read the PNG file
with open('C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile/hesco_ov.png', 'rb') as f:
    data = f.read()

print(f'PNG size: {len(data)} bytes')

# Write the full C header
with open('C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile/app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp', 'w') as f:
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