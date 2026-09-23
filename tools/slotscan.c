/* ============================================================================
 *  slotscan.c — scanner de slots TypeInfo v8a (root CLI, roda via adb shell su)
 *  StormMemory / zimo-external-mobile — V8A-SLOT-DIAG
 *
 *  SINTOMA QUE ELE INVESTIGA:
 *    O app le lib+0xAC1E768 e acha o TOKEN 0x2001B431 (lazy, nao resolvido),
 *    depois de MUITO tempo de jogo — mas o offsetdumper provou que esse RVA
 *    vira ponteiro ~12s apos o load. Possiveis causas que este scanner separa:
 *
 *      (a) JOGO ATUALIZOU: o RVA mudou de build -> o dumper precisa rodar
 *          de novo; o scanner lista os slots TypeInfo JA RESOLVIDOS desta
 *          build (ponteiros cercados por tokens) para comparar.
 *      (b) MAIS DE UMA COPIA do libil2cpp mapeada (tradutor de emulador,
 *          anti-cheat, remap): o app le a copia PRISTINE (tokens) enquanto o
 *          runtime patcheou OUTRA. O scanner mostra o valor de G+0xAC1E768
 *          em CADA copia e varre memoria pela janela de tokens.
 *      (c) PID ERRADO: imprime o cmdline do processo para conferir.
 *
 *  USO (root no aparelho/emulador):
 *    slotscan <pid> [token_hex]        ex.: slotscan 10818 2001B431
 *    (sem token, usa 0x2001B431 = o que o seu app leu hoje)
 *
 *  BUILD (NDK no Windows, cmd):
 *    %NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android33-clang slotscan.c -O2 -o slotscan
 *    adb push slotscan /data/local/tmp/
 *    adb shell "chmod 755 /data/local/tmp/slotscan"
 *    adb shell "su -c '/data/local/tmp/slotscan <PID>'"
 * ========================================================================== */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct { uint64_t start, end; char perms[8]; char path[512]; } MapRange;
typedef struct { MapRange *v; int n, cap; } RangeVec;

static int g_memfd = -1;

static int IsToken(uint64_t v)        { return v >= 0x20000000ull && v < 0xE0000000ull; }
static int PlausiblePtr(uint64_t v)   { return v >= 0x100000000ull && v < 0x800000000000ull; }

static void PushRange(RangeVec *rv, const MapRange *r)
{
    if (rv->n == rv->cap)
    {
        rv->cap = rv->cap ? rv->cap * 2 : 256;
        rv->v = (MapRange*)realloc(rv->v, sizeof(MapRange) * rv->cap);
        if (!rv->v) { fprintf(stderr, "oom\n"); exit(1); }
    }
    rv->v[rv->n++] = *r;
}

static void ParseMaps(int pid, RangeVec *out)
{
    char p[64]; snprintf(p, sizeof(p), "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) { fprintf(stderr, "ERRO: nao abriu %s (root? pid existe?)\n", p); exit(1); }
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        MapRange r; memset(&r, 0, sizeof(r));
        char pathbuf[512] = "";
        int got = sscanf(line, "%lx-%lx %7s %*s %*s %*s %511[^\n]",
                         &r.start, &r.end, r.perms, pathbuf);
        if (got < 3) continue;
        if (got == 4) { /* tira espacos iniciais do path */ char *s = pathbuf; while (*s == ' ' || *s == '\t') s++; snprintf(r.path, sizeof(r.path), "%s", s); }
        if (r.perms[0] != 'r') continue;
        PushRange(out, &r);
    }
    fclose(f);
}

/* le range inteiro de /proc/pid/mem em chunks; retorna 0=ok */
static int ReadChunk(uint64_t addr, uint8_t *buf, size_t len)
{
    size_t done = 0;
    while (done < len)
    {
        ssize_t r = pread(g_memfd, buf + done, len - done, (off_t)(addr + done));
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

/* grupos de mapeamento do libil2cpp.so: runs contiguos com o mesmo path */
typedef struct { uint64_t base, end; char path[512]; int nranges; } LibGroup;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "uso: slotscan <pid> [token_hex]\n  ex.: slotscan 10818 2001B431\n");
        return 2;
    }
    int pid = atoi(argv[1]);
    uint64_t token = 0x2001B431ull;
    if (argc >= 3) token = strtoull(argv[2], NULL, 16);

    printf("== slotscan 0.1 | pid=%d | token=0x%llX ==\n", pid, (unsigned long long)token);

    /* (c) pid certo? */
    {
        char p[64]; snprintf(p, sizeof(p), "/proc/%d/cmdline", pid);
        FILE *f = fopen(p, "rb");
        char cmd[256] = "?";
        if (f) { size_t n = fread(cmd, 1, sizeof(cmd) - 1, f); cmd[n ? n : 1] = 0; for (size_t i = 0; i < n; i++) if (cmd[i] == 0) cmd[i] = ' '; fclose(f); }
        printf("cmdline: %s\n", cmd);
    }

    RangeVec ranges = {0};
    ParseMaps(pid, &ranges);
    printf("ranges legiveis: %d\n", ranges.n);

    char mempath[64]; snprintf(mempath, sizeof(mempath), "/proc/%d/mem", pid);
    g_memfd = open(mempath, O_RDONLY);
    if (g_memfd < 0) { fprintf(stderr, "ERRO: nao abriu %s (rode como root: su -c '...')\n", mempath); return 1; }

    /* ---- lista TODOS os mapeamentos do libil2cpp.so (dupla copia?) ---- */
    printf("\n-- mapeamentos libil2cpp.so --\n");
    LibGroup groups[16]; int ngroups = 0;
    for (int i = 0; i < ranges.n; i++)
    {
        if (!strstr(ranges.v[i].path, "libil2cpp.so")) continue;
        printf("  0x%llx-0x%llx %s %s\n", (unsigned long long)ranges.v[i].start,
               (unsigned long long)ranges.v[i].end, ranges.v[i].perms, ranges.v[i].path);
        /* agrupa: path igual e contiguo (ou colado com folga <=4KB) ao grupo atual */
        int g = -1;
        for (int k = 0; k < ngroups; k++)
            if (strcmp(groups[k].path, ranges.v[i].path) == 0 &&
                ranges.v[i].start <= groups[k].end + 0x1000)
            {
                if (ranges.v[i].start < groups[k].base) groups[k].base = ranges.v[i].start;
                if (ranges.v[i].end   > groups[k].end)  groups[k].end  = ranges.v[i].end;
                groups[k].nranges++;
                g = k; break;
            }
        if (g < 0 && ngroups < 16)
        {
            groups[ngroups].base = ranges.v[i].start;
            groups[ngroups].end  = ranges.v[i].end;
            snprintf(groups[ngroups].path, sizeof(groups[0].path), "%s", ranges.v[i].path);
            groups[ngroups].nranges = 1;
            ngroups++;
        }
    }
    printf("grupos distintos: %d %s\n", ngroups,
           ngroups > 1 ? "<<< MAIS DE UMA COPIA MAPEADA!" : "");

    /* ---- valor do slot G+0xAC1E768 em CADA copia ---- */
    const uint64_t kSlotRva = 0xAC1E768ull;
    printf("\n-- slot TypeInfo (G+0x%llX) por copia --\n", (unsigned long long)kSlotRva);
    for (int g = 0; g < ngroups; g++)
    {
        uint64_t a = groups[g].base + kSlotRva;
        uint64_t v = 0;
        if (a + 8 > groups[g].end) { printf("  [G%d] 0x%llx: FORA do mapeamento\n", g, (unsigned long long)a); continue; }
        if (ReadChunk(a, (uint8_t*)&v, 8) != 0) { printf("  [G%d] 0x%llx: leitura falhou\n", g, (unsigned long long)a); continue; }
        printf("  [G%d] 0x%llx = 0x%016llX  %s\n", g, (unsigned long long)a, (unsigned long long)v,
               IsToken(v) ? "(TOKEN — copia nao patcheada)" :
               PlausiblePtr(v) ? "(PONTEIRO — copia RESOLVIDA!)" : "(outro)");
    }

    /* ---- varre pela janela de tokens (acha TODAS as copias do array) ---- */
    printf("\n-- varredura janela de tokens (token em -0x08 e +0x08) --\n");
    int whits = 0;
    uint64_t scanned = 0;
    for (int i = 0; i < ranges.n && whits < 64; i++)
    {
        MapRange *r = &ranges.v[i];
        int isLib = (strstr(r->path, "libil2cpp.so") != NULL);
        int isAnon = (r->path[0] == 0) && r->perms[1] == 'w';
        uint64_t size = r->end - r->start;
        /* so varre: libil2cpp inteiro + anonimas rw (copia remapeada?), pula o resto */
        if (!isLib && !isAnon) continue;
        if (size > 768ull << 20) { printf("  (pulado range 0x%llx grande demais: %llu MB)\n", (unsigned long long)r->start, (unsigned long long)(size >> 20)); continue; }

        uint8_t *buf = (uint8_t*)malloc(size);
        if (!buf) continue;
        if (ReadChunk(r->start, buf, size) != 0) { free(buf); continue; }
        scanned += size;

        for (uint64_t off = 8; off + 16 <= size; off += 8)
        {
            uint64_t prev, cur, next;
            memcpy(&prev, buf + off - 8, 8);
            memcpy(&cur,  buf + off,      8);
            memcpy(&next, buf + off + 8,  8);
            if (cur != token) continue;
            if (!IsToken(prev) || !IsToken(next)) continue;
            whits++;
            /* offset relativo ao grupo do lib, se cair dentro de um */
            char rel[96] = "(fora do lib)";
            for (int g = 0; g < ngroups; g++)
                if (r->start >= groups[g].base && r->end <= groups[g].end + 0x1000)
                {
                    snprintf(rel, sizeof(rel), "G%d+0x%llX", g,
                             (unsigned long long)(r->start + off - groups[g].base));
                    break;
                }
            printf("  hit@0x%llx %s  centro=0x%016llX %s | -0x10=0x%08llX +0x10=0x%08llX\n",
                   (unsigned long long)(r->start + off), rel, (unsigned long long)cur,
                   IsToken(cur) ? "TOKEN" : (PlausiblePtr(cur) ? "PONTEIRO" : "outro"),
                   (unsigned long long)(prev), (unsigned long long)(next));
            if (whits >= 64) { printf("  (limite de 64 hits atingido)\n"); break; }
        }
        free(buf);
        if ((scanned >> 30) != ((scanned - size) >> 30))
            printf("  ...varridos %llu GB\n", (unsigned long long)(scanned >> 30));
    }
    printf("  hits de janela: %d (varridos ~%.2f GB)\n", whits, (double)scanned / 1073741824.0);

    /* ---- acha slots TypeInfo JA RESOLVIDOS desta build (dentro do lib) ---- */
    printf("\n-- slots RESOLVIDOS (ponteiro cercado por tokens) dentro do libil2cpp --\n");
    int found = 0;
    for (int g = 0; g < ngroups; g++)
    {
        uint64_t gs = groups[g].base, ge = groups[g].end;
        uint64_t size = ge - gs;
        if (size == 0 || size > (2ull << 30)) continue;
        uint8_t *buf = (uint8_t*)malloc(size);
        if (!buf) continue;
        if (ReadChunk(gs, buf, size) != 0) { free(buf); printf("  [G%d] leitura falhou\n", g); continue; }
        for (uint64_t off = 8; off + 16 <= size; off += 8)
        {
            uint64_t prev, cur, next;
            memcpy(&prev, buf + off - 8, 8);
            memcpy(&cur,  buf + off,      8);
            memcpy(&next, buf + off + 8,  8);
            if (!PlausiblePtr(cur)) continue;
            int ntok = (IsToken(prev) ? 1 : 0) + (IsToken(next) ? 1 : 0);
            if (ntok < 2) continue;
            found++;
            if (found <= 200)
                printf("  G%d+0x%llX = 0x%016llX  <- TypeInfo JA resolvido nesta build\n",
                       g, (unsigned long long)off, (unsigned long long)cur);
        }
        free(buf);
    }
    printf("  total de slots resolvidos encontrados: %d\n", found);

    printf("\nRESUMO:\n");
    printf("  - copias do libil2cpp: %d %s\n", ngroups,
           ngroups > 1 ? "<<< o app pode estar lendo a copia ERRADA" : "");
    printf("  - hits da janela de tokens: %d\n", whits);
    printf("  - slots TypeInfo resolvidos: %d\n", found);
    if (ngroups <= 1 && whits <= 1 && found == 0)
        printf("  => UNICA copia, slot ainda TOKEN: o RVA 0xAC1E768 nao e desta build\n"
               "     (jogo atualizou?) — rode o offsetdumper DE NOVO nesta sessao e\n"
               "     compare com a lista de slots resolvidos acima.\n");
    close(g_memfd);
    return 0;
}
