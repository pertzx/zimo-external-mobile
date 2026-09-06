#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE* in = fopen("C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile/hesco_ov.png", "rb");
    if (!in) { perror("fopen input"); return 1; }
    fseek(in, 0, SEEK_END);
    long sz = ftell(in);
    fseek(in, 0, SEEK_SET);
    unsigned char* data = malloc(sz);
    fread(data, 1, sz, in);
    fclose(in);

    FILE* out = fopen("C:/Users/Márcio Ribeiro/Desktop/Pyerre/zimo-external-mobile/app/src/main/cpp/Panel/Fonts/Bytes/BytesImg.hpp", "w");
    fprintf(out, "#pragma once\n\n");
    fprintf(out, "const int LogoWidth = 720;\n");
    fprintf(out, "const int LogoHeight = 1600;\n\n");
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
    printf("Done! Size: %ld bytes\n", sz);
    return 0;
}