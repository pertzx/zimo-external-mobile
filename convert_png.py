import struct
import zlib

# Simple PNG reader using only standard library
def read_png(filename):
    with open(filename, 'rb') as f:
        data = f.read()

    # Check PNG signature
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Not a PNG file')

    pos = 8
    ihdr = None
    idat_chunks = []

    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        chunk_type = data[pos+4:pos+8]
        chunk_data = data[pos+8:pos+8+length]
        crc = data[pos+8+length:pos+12+length]

        if chunk_type == b'IHDR':
            ihdr = chunk_data
        elif chunk_type == b'IDAT':
            idat_chunks.append(chunk_data)
        elif chunk_type == b'IEND':
            break

        pos += 12 + length

    if not ihdr:
        raise ValueError('No IHDR chunk found')

    width = struct.unpack('>I', ihdr[0:4])[0]
    height = struct.unpack('>I', ihdr[4:8])[0]
    bit_depth = ihdr[8]
    color_type = ihdr[9]
    compression = ihdr[10]
    filter_method = ihdr[11]
    interlace = ihdr[12]

    print(f'Width: {width}, Height: {height}')
    print(f'Bit depth: {bit_depth}, Color type: {color_type}')

    # Decompress IDAT data
    compressed_data = b''.join(idat_chunks)
    try:
        raw = zlib.decompress(compressed_data)
    except Exception as e:
        print(f'Decompression failed: {e}')
        return None

    # Unfilter
    bytes_per_pixel = 4 if color_type == 6 else 3 if color_type == 2 else 1
    stride = width * bytes_per_pixel
    rows = []
    idx = 0
    for y in range(height):
        filter_type = raw[idx]
        idx += 1
        row = raw[idx:idx+stride]
        idx += stride

        # Simple unfilter (only type 0 - none)
        if filter_type == 0:
            rows.append(row)
        else:
            # For other filter types, we'd need proper unfiltering
            rows.append(row)

    # Convert to RGBA
    rgba = bytearray()
    for row in rows:
        if color_type == 6:  # RGBA
            rgba.extend(row)
        elif color_type == 2:  # RGB
            for i in range(0, len(row), 3):
                rgba.extend([row[i], row[i+1], row[i+2], 255])
        elif color_type == 0:  # Grayscale
            for v in row:
                rgba.extend([v, v, v, 255])
        elif color_type == 4:  # Grayscale + alpha
            for i in range(0, len(row), 2):
                rgba.extend([row[i], row[i], row[i], row[i+1]])

    return width, height, bytes(rgba)

w, h, rgba = read_png('hesco_ov.png')
if rgba:
    print(f'RGBA size: {len(rgba)} bytes (expected {w*h*4})')

    # Write C header
    with open('app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp', 'w') as f:
        f.write('#pragma once\n\n')
        f.write(f'const int LogoWidth = {w};\n')
        f.write(f'const int LogoHeight = {h};\n\n')
        f.write('const unsigned char LogoMenuRawRGBA[] = {\n')
        for i, b in enumerate(rgba):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{b:02X}, ')
            if i % 16 == 15:
                f.write('\n')
        f.write('\n};\n')
        f.write(f'const size_t LogoMenuRawRGBASize = {len(rgba)};\n')
    print('Done!')
else:
    print('Failed to read PNG')