#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

// Simple PNG to C array converter
// Compile: gcc convert_png.c -o convert_png -lz

int main() {
    FILE* f = fopen("hesco_ov.png", "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    if (sz < 8 || memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) {
        fprintf(stderr, "Not a PNG\n");
        return 1;
    }

    unsigned char* p = data + 8;
    unsigned char* ihdr = NULL;
    unsigned char* idat_chunks[100];
    int idat_count = 0;

    while (p < data + sz) {
        unsigned int len = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
        unsigned char* chunk_type = p + 4;
        unsigned char* chunk_data = p + 8;
        if (memcmp(chunk_type, "IHDR", 4) == 0) {
            ihdr = chunk_data;
        } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            idat_chunks[idat_count++] = chunk_data;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }
        p += 12 + len;
    }

    if (!ihdr) { fprintf(stderr, "No IHDR\n"); return 1; }

    unsigned int w = (ihdr[0]<<24)|(ihdr[1]<<16)|(ihdr[2]<<8)|ihdr[3];
    unsigned int h = (ihdr[4]<<24)|(ihdr[5]<<16)|(ihdr[6]<<8)|ihdr[7];
    unsigned char ct = ihdr[9];

    printf("Width: %u, Height: %u, Color type: %u\n", w, h, ct);

    // Combine IDAT chunks
    size_t total_idat = 0;
    for (int i = 0; i < idat_count; i++) {
        // Need to find length of each chunk - simplified
        total_idat += 100000; // placeholder
    }

    // Just output the raw PNG data as a C array for stb_image to decode
    FILE* out = fopen("app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp", "w");
    fprintf(out, "#pragma once\n\n");
    fprintf(out, "const int LogoWidth = %u;\n", w);
    fprintf(out, "const int LogoHeight = %u;\n\n", h);
    fprintf(out, "const unsigned char LogoMenuRawRGBA[] = {\n");
    for (long i = 0; i < sz; i++) {
        if (i % 16 == 0) fprintf(out, "    ");
        fprintf(out, "0x%02X, ", data[i]);
        if (i % 16 == 15) fprintf(out, "\n");
    }
    fprintf(out, "\n};\n");
    fprintf(out, "const size_t LogoMenuRawRGBASize = %ld;\n", sz);
    fclose(out);

    free(data);
    printf("Done\n");
    return 0;
}