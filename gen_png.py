with open('hesco_ov.png', 'rb') as f:
    data = f.read()
print('PNG size:', len(data))
with open('app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp', 'w') as f:
    f.write('#pragma once\n\n')
    f.write('const int LogoWidth = 720;\n')
    f.write('const int LogoHeight = 1600;\n\n')
    f.write('const unsigned char LogoMenuRawRGBA[] = {\n')
    for i, b in enumerate(data):
        if i % 16 == 0:
            f.write('    ')
        f.write('0x%02X, ' % b)
        if i % 16 == 15:
            f.write('\n')
    f.write('\n};\n')
    f.write('const size_t LogoMenuRawRGBASize = %d;\n' % len(data))
print('Done')