// module.cpp - Zygisk module for dumping Free Fire offsets
// Otimizado: so escaneia a libil2cpp.so (nao o processo todo) e usa memcpy direto.
// Isso resolve o crash e a lentidao (process_vm_readv no processo todocongelava o jogo).

#include "zygisk.hpp"
#include "Il2Cpp.h"
#include <android/log.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <inttypes.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "OffsetDumper", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OffsetDumper", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "OffsetDumper", __VA_ARGS__)

constexpr size_t PTR_SZ   = sizeof(void*);
constexpr size_t PTR_MASK = PTR_SZ - 1;

// ============================================================
// Utils
// ============================================================
struct MapEntry {
    uintptr_t start, end;
    char perms[5];
    std::string path;
};

static std::vector<MapEntry> GetMaps() {
    std::vector<MapEntry> out;
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return out;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long s, e, fo;
        char perms[5] = {0};
        char path[256] = {0};
        int n = sscanf(line, "%llx-%llx %4s %llx %*x:%*x %*u %255[^\n]",
                       &s, &e, perms, &fo, path);
        if (n >= 3) {
            MapEntry m{};
            m.start = (uintptr_t)s;
            m.end   = (uintptr_t)e;
            memcpy(m.perms, perms, 4);
            if (n >= 5) m.path = path;
            out.push_back(m);
        }
    }
    fclose(fp);
    return out;
}

static uintptr_t GetBaseAddress(const char* libName) {
    auto maps = GetMaps();
    uintptr_t best = UINTPTR_MAX;
    for (auto& m : maps) {
        if (m.path.find(libName) == std::string::npos) continue;
        if (m.start < best) best = m.start;
    }
    return (best == UINTPTR_MAX) ? 0 : best;
}

static void SaveToFile(const std::string& content) {
    const char* paths[] = {
        "/data/data/com.dts.freefireth/files/offsets_dump.txt",
        "/sdcard/Android/data/com.dts.freefireth/files/offsets_dump.txt",
        "/data/data/com.dts.freefiremax/files/offsets_dump.txt",
        "/sdcard/Android/data/com.dts.freefiremax/files/offsets_dump.txt",
        nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        // (V2) TRUNC: o arquivo fica sempre com a ULTIMA rodada completa
        // (append acumulava lixo de rodadas antigas com offsets velhos)
        std::ofstream f(paths[i], std::ios::trunc);
        if (f.is_open()) {
            f << content << "\n";
            f.close();
            return;
        }
    }
}

// ============================================================
// Scanner OTIMIZADO (so escaneia libil2cpp.so e usa memcpy)
// ============================================================
struct Hit {
    uintptr_t   address;
    uintptr_t   offset;
    int         neighborScore;
};

static bool LooksLikeIl2CppClassPtr(uintptr_t v, uintptr_t libStart, uintptr_t libEnd) {
    if (v == 0) return false;
    if (v < 0x1000) return false;
#if INTPTR_MAX == INT64_MAX
    if (v < 0x100000000ULL)         return false;
    if (v >= 0xff00000000000000ULL) return false;
#else
    if (v >= 0xFFFF0000) return false;
#endif
    // O ponteiro da classe fica no heap (geralmente ANTES da lib em v7a, ou DEPOIS em v8a)
    // Entao so rejeitamos se estiver DENTRO da lib.
    if (v >= libStart && v < libEnd) return false;
    return true;
}

// Usa memcpy direto (muito mais rapido que process_vm_readv e nao crasha)
static int CountNeighbors(uintptr_t addr, uintptr_t libStart, uintptr_t libEnd) {
    int score = 0;
    for (int delta = -(int)(PTR_SZ * 4); delta <= (int)(PTR_SZ * 4); delta += (int)PTR_SZ) {
        if (delta == 0) continue;
        uintptr_t nv = 0;
        memcpy(&nv, (void*)(addr + delta), PTR_SZ);
        if (LooksLikeIl2CppClassPtr(nv, libStart, libEnd)) score++;
    }
    return score;
}

static std::vector<Hit> FindAllSlots(uintptr_t libBase, uintptr_t target,
                                     uintptr_t libStart, uintptr_t libEnd) {
    std::vector<Hit> hits;
    auto maps = GetMaps();

    // SO ESCANEIA AS PAGINAS DA libil2cpp.so
    // O slot do TypeInfo sempre fica na .data da lib.
    for (auto& m : maps) {
        if (m.path.find("libil2cpp.so") == std::string::npos) continue;
        if (m.perms[0] != 'r') continue;

        uintptr_t start = (m.start + PTR_MASK) & ~(uintptr_t)PTR_MASK;
        uintptr_t end   =  m.end                 & ~(uintptr_t)PTR_MASK;

        size_t regionSize = end - start;
        if (regionSize == 0) continue;

        // Le direto com memcpy (a pagina ta mapeada como r-- ou rw-)
        for (uintptr_t a = start; a + PTR_SZ <= end; a += PTR_SZ) {
            uintptr_t v = 0;
            memcpy(&v, (void*)a, PTR_SZ);
            if (v != target) continue;

            Hit h{};
            h.address = a;
            h.offset  = a - libBase;
            h.neighborScore = CountNeighbors(a, libStart, libEnd);
            hits.push_back(h);
        }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b){
        if (a.neighborScore != b.neighborScore) return a.neighborScore > b.neighborScore;
        return a.offset < b.offset;
    });

    return hits;
}

// ============================================================
// Resolve classe IL2CPP
// ============================================================
static void* ResolveClassType(const std::string& dll,
                              const std::vector<std::string>& namespaces,
                              const std::string& cls)
{
    for (const auto& ns : namespaces) {
        void* p = Il2CppGetClassType(dll.c_str(), ns.c_str(), cls.c_str());
        if (p) return p;
    }
    return nullptr;
}

// ============================================================
// DoDump (V2 — rescan continuo)
//
// POR QUE O RESCAN: no il2cpp novo os slots de TypeInfo sao LAZY —
// o slot guarda o TOKEN encoded ate o jogo rodar o codigo que usa a
// classe. Um scan unico aos ~25s (só loading) pegava hits=0 ou pior:
// apenas slots de codigo de loading, que podem nunca mais refletir o
// estado real. Re-escaneando a cada 15s por 10 min, qualquer slot que
// o jogo resolver durante o login/lobby/match aparece.
// ============================================================
struct Target {
    std::string dll;
    std::vector<std::string> namespaces;
    std::string cls;
    std::string outName;
};

static const std::vector<Target>& GetTargets() {
    static const std::vector<Target> targets = {
        { "Assembly-CSharp.dll", { "COW", ""  }, "GameFacade", "GameFacade_TypeInfo" },
        { "Assembly-CSharp.dll", { "",   "COW"}, "GameVarDef", "GameVarDef_TypeInfo" },
        { "Assembly-CSharp.dll", { "", "COW"  }, "AvatarWardrobeDataManager", "AvatarWardrobeDataManager_TypeInfo" },
    };
    return targets;
}

// Escaneia os targets UMA vez; devolve o conteudo formatado e marca
// found[i]=1 quando o target tem pelo menos 1 hit.
static std::string ScanRound(uintptr_t libBase, uintptr_t libStart, uintptr_t libEnd, int* found) {
    char buf[512];
    std::string content;

    snprintf(buf, sizeof(buf), "base=0x%" PRIxPTR " ptr_size=%zu", libBase, PTR_SZ);
    content += std::string(buf) + "\n";

    for (size_t ti = 0; ti < GetTargets().size() && ti < 8; ++ti) {
        const Target& t = GetTargets()[ti];

        if (found[ti]) {
            snprintf(buf, sizeof(buf), "%-32s : OK (ja encontrado)", t.outName.c_str());
            content += std::string(buf) + "\n";
            continue;
        }

        void* classPtr = ResolveClassType(t.dll, t.namespaces, t.cls);
        if (!classPtr) {
            snprintf(buf, sizeof(buf), "%-32s : CLASS NOT FOUND", t.outName.c_str());
            LOGI("%s", buf); content += std::string(buf) + "\n"; continue;
        }

        uintptr_t target = (uintptr_t)classPtr;
        auto hits = FindAllSlots(libBase, target, libStart, libEnd);

        if (hits.empty()) {
            snprintf(buf, sizeof(buf), "%-32s : hits=0 (slot LAZY/token — codigo da classe ainda nao rodou; rescan continua)", t.outName.c_str());
            LOGI("%s", buf); content += std::string(buf) + "\n"; continue;
        }

        found[ti] = 1;

        auto& best = hits[0];
        snprintf(buf, sizeof(buf), "%-32s = 0x%" PRIxPTR " (score=%d hits=%zu)",
                 t.outName.c_str(), best.offset, best.neighborScore, hits.size());
        LOGI("%s", buf); content += std::string(buf) + "\n";

        snprintf(buf, sizeof(buf), "  ^ ReadPtr(LibIl2Cpp + 0x%" PRIxPTR ")", best.offset);
        LOGI("%s", buf); content += std::string(buf) + "\n";

        // (V2) TODOS os hits servem pro cheat: qualquer slot ja resolvido
        // guarda o MESMO ponteiro do klass. Cola o best no principal ou
        // qualquer hit nos TypeInfoAlt1..3 do config [Chain.Fix].
        size_t n = hits.size() < 8 ? hits.size() : 8;
        for (size_t h = 0; h < n; ++h) {
            snprintf(buf, sizeof(buf), "  hit[%zu] offset=0x%" PRIxPTR " score=%d",
                     h, hits[h].offset, hits[h].neighborScore);
            LOGI("%s", buf); content += std::string(buf) + "\n";
        }
    }

    return content;
}

static void DoDump() {
    LOGI("[DoDump] Esperando libil2cpp.so no maps...");
    int retries = 0;
    while (GetBaseAddress("libil2cpp.so") == 0 && retries < 240) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        retries++;
    }
    uintptr_t libBase = GetBaseAddress("libil2cpp.so");
    if (!libBase) { LOGE("[DoDump] libil2cpp.so NAO carregou."); return; }
    LOGI("[DoDump] libil2cpp.so carregou em 0x%" PRIxPTR, libBase);

    retries = 0;
    while (GetBaseAddress("libunity.so") == 0 && retries < 120) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        retries++;
    }
    LOGI("[DoDump] Esperando 10s pro Unity init...");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOGI("[DoDump] Chamando Il2CppAttach...");
    Il2CppAttach("libil2cpp.so");

    retries = 0;
    while (!Il2CppIsAssembliesLoaded() && retries < 120) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        retries++;
    }
    if (!Il2CppIsAssembliesLoaded()) { LOGE("[DoDump] Assemblies nao carregaram."); return; }
    LOGI("[DoDump] Assemblies carregados.");

    auto maps = GetMaps();
    uintptr_t libStart = UINTPTR_MAX, libEnd = 0;
    for (auto& m : maps) {
        if (m.path.find("libil2cpp.so") == std::string::npos) continue;
        if (m.start < libStart) libStart = m.start;
        if (m.end   > libEnd  ) libEnd   = m.end;
    }

    LOGI("========================================================");
    LOGI(" OffsetDumper | libil2cpp.so base = 0x%" PRIxPTR, libBase);
    LOGI("========================================================");

    const int kMaxRounds = 40;               // 40 x 15s = 10 min
    const int kTargetCount = (int)GetTargets().size();
    int found[8] = {0};

    for (int round = 1; round <= kMaxRounds; ++round) {
        LOGI("[DoDump] rodada %d/%d...", round, kMaxRounds);

        std::string content = ScanRound(libBase, libStart, libEnd, found);

        char stamp[64] = {0};
        time_t nowT = time(nullptr);
        struct tm* lt = localtime(&nowT);
        if (lt) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", lt);

        std::string fileContent = std::string("== OffsetDumper rodada ") +
                                  std::to_string(round) + "/" + std::to_string(kMaxRounds) +
                                  " " + stamp + " ==\n" + content;
        SaveToFile(fileContent);

        int total = 0;
        for (int i = 0; i < kTargetCount && i < 8; ++i) total += found[i];
        if (total == kTargetCount) {
            LOGI("[DoDump] todos os %d targets encontrados (rodada %d) — fim", kTargetCount, round);
            break;
        }

        std::this_thread::sleep_for(std::chrono::seconds(15));
    }

    LOGI("========================================================");
    LOGI("[DoDump] fim — offsets_dump.txt tem a ULTIMA rodada; cole os offsets no app (config [Chain.Fix])");
    LOGI("========================================================");
}

// ============================================================
// Zygisk Module (API v3)
// ============================================================
using zygisk::Api;
using zygisk::AppSpecializeArgs;

class OffsetDumperModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api_ = api;
        this->env_ = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *process = env_->GetStringUTFChars(args->nice_name, nullptr);
        // (V2) suporta FF normal e FF MAX
        is_game_ = (process && (strcmp(process, "com.dts.freefireth") == 0 ||
                                strcmp(process, "com.dts.freefiremax") == 0));
        env_->ReleaseStringUTFChars(args->nice_name, process);
        if (!is_game_) api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!is_game_) return;
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            DoDump();
        }).detach();
    }

private:
    Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool is_game_ = false;
};

REGISTER_ZYGISK_MODULE(OffsetDumperModule)