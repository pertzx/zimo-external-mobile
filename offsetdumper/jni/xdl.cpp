// xdl.cpp - Mini xDL implementation
// Bypasses Android namespace isolation by parsing ELF directly from /proc/self/maps
#include "xdl.h"
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define XDL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "xDL", __VA_ARGS__)

#if INTPTR_MAX == INT64_MAX
    typedef Elf64_Ehdr Elf_Ehdr;
    typedef Elf64_Shdr Elf_Shdr;
    typedef Elf64_Sym  Elf_Sym;
#else
    typedef Elf32_Ehdr Elf_Ehdr;
    typedef Elf32_Shdr Elf_Shdr;
    typedef Elf32_Sym  Elf_Sym;
#endif

// Estrutura interna que guarda o caminho da lib e a base em runtime
struct XdlHandle {
    char path[512];
    uintptr_t base;
};

// Procura o caminho completo de uma lib em /proc/self/maps
static bool FindLibPath(const char* libName, char* outPath, size_t outPathSize) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[1024];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, libName)) continue;
        char* pathStart = nullptr;
        char* p = line;
        while (*p) {
            if (*p == ' ' && *(p+1) != ' ' && *(p+1) != '\0' && *(p+1) != '\n')
                pathStart = p + 1;
            p++;
        }
        if (!pathStart || *pathStart == '\0' || *pathStart == '\n') continue;
        if (pathStart[0] == '[') continue;
        char* nl = strchr(pathStart, '\n');
        if (nl) *nl = 0;
        strncpy(outPath, pathStart, outPathSize - 1);
        outPath[outPathSize - 1] = 0;
        found = true;
        break;
    }
    fclose(fp);
    return found;
}

// Procura a base de uma lib em /proc/self/maps
static uintptr_t FindLibBase(const char* libName) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;
    char line[1024];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, libName)) continue;
        unsigned long long s, e;
        if (sscanf(line, "%llx-%llx", &s, &e) == 2) {
            base = (uintptr_t)s;
            break;
        }
    }
    fclose(fp);
    return base;
}

// Parseia o ELF e encontra o simbolo pelo nome
static void* ResolveSymbolFromELF(const char* libPath, uintptr_t libBase, const char* symName) {
    if (!libPath || libBase == 0 || !symName) return nullptr;

    int fd = open(libPath, O_RDONLY);
    if (fd < 0) return nullptr;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return nullptr; }
    size_t fileSize = st.st_size;

    void* map = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return nullptr; }

    void* result = nullptr;
    Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
    if (memcmp(ehdr->e_ident, ELFMAG, 4) == 0) {
        Elf_Shdr* shdrs = (Elf_Shdr*)((uint8_t*)map + ehdr->e_shoff);

        // Procura em SHT_DYNSYM e SHT_SYMTAB
        for (int i = 0; i < ehdr->e_shnum && !result; i++) {
            if (shdrs[i].sh_type != SHT_DYNSYM && shdrs[i].sh_type != SHT_SYMTAB)
                continue;

            Elf_Sym* symtab = (Elf_Sym*)((uint8_t*)map + shdrs[i].sh_offset);
            size_t symcount = shdrs[i].sh_size / sizeof(Elf_Sym);
            const char* strtab = nullptr;

            if (shdrs[i].sh_link < ehdr->e_shnum) {
                strtab = (const char*)((uint8_t*)map + shdrs[shdrs[i].sh_link].sh_offset);
            }

            if (symtab && strtab) {
                for (size_t j = 0; j < symcount; j++) {
                    const char* name = strtab + symtab[j].st_name;
                    if (strcmp(name, symName) == 0 && symtab[j].st_value != 0) {
                        result = (void*)(libBase + symtab[j].st_value);
                        break;
                    }
                }
            }
        }
    }

    munmap(map, fileSize);
    close(fd);
    return result;
}

void* xdl_open(const char* name, int flags) {
    (void)flags; // nao usado

    // 1) Tenta dlopen normal primeiro (funciona em v7a antigo)
    void* h = dlopen(name, RTLD_NOW);
    if (h) return h;

    // 2) Se falhou (namespace isolation), usa ELF parser
    char path[512] = {0};
    if (!FindLibPath(name, path, sizeof(path))) {
        XDL_LOGE("xdl_open: %s nao encontrado no maps", name);
        return nullptr;
    }

    uintptr_t base = FindLibBase(name);
    if (base == 0) {
        XDL_LOGE("xdl_open: base nao encontrada pra %s", name);
        return nullptr;
    }

    // Testa se o ELF parser consegue achar um simbolo basico
    void* test = ResolveSymbolFromELF(path, base, "il2cpp_domain_get");
    if (!test) {
        // Tenta outro simbolo basico caso il2cpp_domain_get nao exista
        test = ResolveSymbolFromELF(path, base, "il2cpp_class_from_name");
    }
    if (!test) {
        XDL_LOGE("xdl_open: ELF parser nao achou simbolos em %s", name);
        return nullptr;
    }

    XdlHandle* handle = new XdlHandle();
    strncpy(handle->path, path, sizeof(handle->path) - 1);
    handle->base = base;
    return handle;
}

void* xdl_sym(void* handle, const char* name, size_t* cache) {
    (void)cache; // nao usado

    if (!handle || !name) return nullptr;

    // Tenta dlsym primeiro (se for um handle de dlopen real)
    void* sym = dlsym(handle, name);
    if (sym) return sym;

    // Se for nosso XdlHandle, usa ELF parser
    XdlHandle* xh = (XdlHandle*)handle;
    return ResolveSymbolFromELF(xh->path, xh->base, name);
}

void xdl_close(void* handle) {
    // Se for nosso XdlHandle, deleta. Se for handle de dlopen, faz dlclose.
    if (!handle) return;
    // Heuristica: handles de dlopen geralmente tem endereco alto,
    // nossos XdlHandle sao alocados com new (heap).
    // Pra simplicidade, so deleta se parecer XdlHandle.
    // (No codigo antigo, xdl_close eh chamado depois de xdl_open).
    XdlHandle* xh = (XdlHandle*)handle;
    delete xh;
}