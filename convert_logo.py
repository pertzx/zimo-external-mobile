import png
import sys

# Read PNG
reader = png.Reader(filename='hesco_ov.png')
w, h, pixels, meta = reader.read()
print(f'Width: {w}, Height: {h}')
print(f'Meta: {meta}')
print(f'Bit depth: {meta.get("bitdepth")}')
print(f'Color type: {meta.get("greyscale", False)}, alpha: {meta.get("alpha", False)}')

# Convert to flat RGBA
rows = list(pixels)
if meta.get('alpha'):
    # Already has alpha
    flat = []
    for row in rows:
        flat.extend(row)
    data = bytes(flat)
else:
    # Add alpha
    flat = []
    for row in rows:
        for i in range(0, len(row), 3):
            flat.extend([row[i], row[i+1], row[i+2], 255])
    data = bytes(flat)

print(f'Total bytes: {len(data)}')
print(f'Expected: {w * h * 4}')

# Write C header
with open('app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp', 'w') as f:
    f.write('#pragma once\n\n')
    f.write(f'const int LogoWidth = {w};\n')
    f.write(f'const int LogoHeight = {h};\n\n')
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