import struct, zlib
with open('hesco_ov.png','rb') as f: d=f.read()
assert d[:8]==b'\x89PNG\r\n\x1a\n'
p=8; ihdr=None; idats=[]
while p<len(d):
    l=struct.unpack('>I',d[p:p+4])[0]; t=d[p+4:p+8]; c=d[p+8:p+8+l]; p+=12+l
    if t==b'IHDR': ihdr=c
    elif t==b'IDAT': idats.append(c)
    elif t==b'IEND': break
w=struct.unpack('>I',ihdr[:4])[0]; h=struct.unpack('>I',ihdr[4:8])[0]; ct=ihdr[9]
print(f'{w}x{h} ct={ct}')
raw=zlib.decompress(b''.join(idats))
bpp=4 if ct==6 else 3 if ct==2 else 1; stride=w*bpp; rows=[]; i=0
for y in range(h):
    ft=raw[i]; i+=1; rows.append(raw[i:i+stride]); i+=stride
rgba=bytearray()
for r in rows:
    if ct==6: rgba.extend(r)
    elif ct==2:
        for j in range(0,len(r),3): rgba.extend([r[j],r[j+1],r[j+2],255])
    elif ct==0:
        for v in r: rgba.extend([v,v,v,255])
    elif ct==4:
        for j in range(0,len(r),2): rgba.extend([r[j],r[j],r[j],r[j+1]])
print(f'{len(rgba)} bytes')
with open('app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp','w') as f:
    f.write(f'#pragma once\n\nconst int LogoWidth={w};\nconst int LogoHeight={h};\n\nconst unsigned char LogoMenuRawRGBA[] = {{\n')
    for i,b in enumerate(rgba):
        if i%16==0: f.write('    ')
        f.write(f'0x{b:02X}, ')
        if i%16==15: f.write('\n')
    f.write('\n};\nconst size_t LogoMenuRawRGBASize=%d;\n'%len(rgba))
print('Done')
