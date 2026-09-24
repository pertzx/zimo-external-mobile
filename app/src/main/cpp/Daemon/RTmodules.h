/*
 * ============================================================================
 * RTmodules.h — READ/WRITE VIA DRIVER DE KERNEL (modo kernel da ponte)
 * ============================================================================
 * Baseado no KERNEL_H distribuido pelo fornecedor do driver, com as
 * correcoes obrigatorias pra funcionar dentro do stormdaemon:
 *
 *   [1] FATAL DO ORIGINAL: o construtor de c_driver() era VAZIO e o fd
 *       NUNCA era aberto (nenhum open() no arquivo inteiro) — todo ioctl
 *       saia com fd de lixo e falhava sem explicacao. Agora existe
 *       RT::Probe(): abre o device, valida com self-test de LEITURA e
 *       ESCRITA no proprio processo e guarda o fd.
 *
 *   [2] CORRIGIDO: fd e pid sem inicializacao (lixo de memoria).
 *
 *   [3] CORRIGIDO: MAX_MODIFY_REGS definido 2x no original.
 *
 *   [4] NOVO: LOGS CLAROS — tag "RTKernel" no logcat. Probe loga cada
 *       caminho testado com errno; cada ioctl de RW falho loga op +
 *       endereco + errno com rate-limit. Logs ativos quando o modo
 *       kernel esta LIGADO (opt-in) ou com --verbose do daemon.
 *
 *   [5] NOVO: RT::KernelRead/KernelWrite com pid EXPLICITO por chamada
 *       (o daemon atende varios clientes em threads — usar this->pid da
 *       classe original seria condicao de corrida entre conexoes).
 *
 *   [6] NOVO: RT::IsAvailable()/GetStats()/DevicePath() pra o daemon
 *       reportar o estado do driver pro painel (BRIDGE_CMD_KERNEL_STATUS).
 *
 * A ABI do driver NAO MUDOU: opcodes, COPY_MEMORY, MODULE_BASE, HW_BP_INFO,
 * TRACKING_DATA, REGS_INFO, HWBP_HIT_* e paradise_gyro_config_cmd sao
 * EXATAMENTE os do header original (o kernel compara byte a byte).
 * ============================================================================
 */

#ifndef RT_MODULES_H
#define RT_MODULES_H

/* ------------------------------------------------------------------------
 * Includes do header original (mantidos) + os novos da infra de log/probe
 * ------------------------------------------------------------------------ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/types.h>
#include <asm/ptrace.h>
#include <errno.h>

#include <android/log.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

/* ------------------------------------------------------------------------
 * (CORRECAO 3) MAX_MODIFY_REGS era definido 2x no header original
 * ------------------------------------------------------------------------ */
#ifndef MAX_MODIFY_REGS
#define MAX_MODIFY_REGS 10
#endif

#define OP_CMD_READ            601
#define OP_CMD_WRITE           602
#define OP_CMD_BASE            603
#define OP_CMD_GETPID          604
#define OP_CMD_HIDE_PROCESS    605
#define OP_CMD_UN_HOOK         606
#define OP_CMD_RECOVER_PROCESS 607
#define OP_CMD_GYRO            608
#define OP_CMD_HWBP_ADD        609
#define OP_CMD_HWBP_GET_HITS   610
#define OP_CMD_HWBP_ENABLE     611
#define OP_CMD_HWBP_CLEAR      612
#define OP_CMD_HWBP_DISABLE    613
#define OP_CMD_UPDATE_TRACKING 614

#define HW_BP_TYPE_R    1
#define HW_BP_TYPE_W    2
#define HW_BP_TYPE_RW   3
#define HW_BP_TYPE_X    4

#define PARADISE_GYRO_MASK_GYRO (1u << 0)
#define PARADISE_GYRO_MASK_UNCAL (1u << 1)
#define PARADISE_GYRO_MASK_ALL (PARADISE_GYRO_MASK_GYRO | PARADISE_GYRO_MASK_UNCAL)

/* ------------------------------------------------------------------------
 * Estruturas da ABI DO KERNEL — NAO ALTERAR (o driver compara campo a
 * campo via copy_from_user). Iguais as do header original.
 * ------------------------------------------------------------------------ */

struct paradise_gyro_config_cmd {
    int enable;
    uint32_t type_mask;
    float x;
    float y;
    float z;
};

typedef struct _TRACKING_DATA {
    bool is_active;    // 是否激活追踪
    uintptr_t bp_addr; // 追踪功能的断点地址（用来区分其他断点）
    float x;
    float y;
    float z;
} TRACKING_DATA;

typedef struct _HW_BP_INFO {
    pid_t pid;
    uintptr_t addr;
    int type;
    int len;

    bool is_write_gp_regs;                   // 是否开启通用寄存器修改
    int gp_reg_count;                        // 修改的通用寄存器数量
    int gp_reg_indices[MAX_MODIFY_REGS];     // 寄存器编号 (0~30代表X0~X30)
    uint64_t gp_reg_values[MAX_MODIFY_REGS]; // 对应的修改值
    bool is_write_fp_regs;
    int fp_reg_count;
    int fp_reg_indices[MAX_MODIFY_REGS];
    uint64_t fp_reg_values[MAX_MODIFY_REGS][2];
} HW_BP_INFO;

struct REGS_INFO {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

struct HWBP_HIT_ITEM {
    pid_t task_id;
    uintptr_t hit_addr;
    uint64_t hit_time;
    struct REGS_INFO regs_info;
};

typedef struct _HWBP_HIT_ARGS {
    pid_t pid;
    uintptr_t addr;
    void *out_buf;
    int out_len;
    int real_count;
} HWBP_HIT_ARGS;

typedef struct _COPY_MEMORY {
    pid_t pid;
    uintptr_t addr;
    void* buffer;
    size_t size;
} COPY_MEMORY, *PCOPY_MEMORY;

typedef struct _MODULE_BASE {
    pid_t pid;
    char* name;     // 这里的 name 是指针，内核态会通过 copy_from_user 去读字符串
    uintptr_t base;
    short index;
} MODULE_BASE, *PMODULE_BASE;

/* ------------------------------------------------------------------------
 * LOGS (novo) — tag "RTKernel". RTLOG* pra nao colidir com os LOG* do
 * daemon. Ligados por padrao quando o modo kernel e ativado; o daemon
 * controla via RT::SetLogVerbose().
 * ------------------------------------------------------------------------ */

#ifndef RT_LOG_TAG
#define RT_LOG_TAG "RTKernel"
#endif

namespace RT {

    static std::atomic<int> g_rtVerbose{ 1 };

    inline void SetLogVerbose(bool on) { g_rtVerbose.store(on ? 1 : 0); }
    inline bool LogVerbose() { return g_rtVerbose.load() != 0; }

    inline long long NowMsRT()
    {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    }
}

#define RTLOGI(...) \
    do { if (RT::LogVerbose()) __android_log_print(ANDROID_LOG_INFO, RT_LOG_TAG, __VA_ARGS__); } while (0)

#define RTLOGW(...) \
    do { if (RT::LogVerbose()) __android_log_print(ANDROID_LOG_WARN, RT_LOG_TAG, __VA_ARGS__); } while (0)

#define RTLOGE(...) \
    do { if (RT::LogVerbose()) __android_log_print(ANDROID_LOG_ERROR, RT_LOG_TAG, __VA_ARGS__); } while (0)

/*
 * LOG DE ERRO COM RATE-LIMIT: os primeiros 12 ocorrem sempre; depois,
 * no maximo 1 linha a cada 3s por ponto de chamada (o volume de reads
 * do ESP e alto — sem isso um device morto enchia o log em segundos).
 */
#define RTLOGE_RL(...)                                                        \
    do {                                                                      \
        static std::atomic<long long> s_rtLastMs{ -1000000 };                 \
        static std::atomic<uint64_t>  s_rtCount{ 0 };                         \
        const long long s_rtNow = RT::NowMsRT();                              \
        const uint64_t  s_rtN    = s_rtCount.fetch_add(1) + 1;                \
        if (s_rtN <= 12 || (s_rtNow - s_rtLastMs.load()) >= 3000) {           \
            s_rtLastMs.store(s_rtNow);                                        \
            RTLOGE(__VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

namespace RT {

    /* Nome legivel da operacao pros logs de erro de ioctl. */
    inline const char* OpName(unsigned int op)
    {
        switch (op)
        {
            case OP_CMD_READ:            return "READ";
            case OP_CMD_WRITE:           return "WRITE";
            case OP_CMD_BASE:            return "BASE";
            case OP_CMD_GETPID:          return "GETPID";
            case OP_CMD_HIDE_PROCESS:    return "HIDE_PROCESS";
            case OP_CMD_UN_HOOK:         return "UN_HOOK";
            case OP_CMD_RECOVER_PROCESS: return "RECOVER_PROCESS";
            case OP_CMD_GYRO:            return "GYRO";
            case OP_CMD_HWBP_ADD:        return "HWBP_ADD";
            case OP_CMD_HWBP_GET_HITS:   return "HWBP_GET_HITS";
            case OP_CMD_HWBP_ENABLE:     return "HWBP_ENABLE";
            case OP_CMD_HWBP_CLEAR:      return "HWBP_CLEAR";
            case OP_CMD_HWBP_DISABLE:    return "HWBP_DISABLE";
            case OP_CMD_UPDATE_TRACKING: return "UPDATE_TRACKING";
            default:                     return "OP?";
        }
    }

    /* prototipo usado por c_driver::initialize (definicao mais abaixo) */
    inline bool Probe();

    /* ========================================================================
     * c_driver — mesma classe do header original, CORRIGIDA:
     *   - fd abre de verdade (Open() + Probe), antes ficava lixo;
     *   - fd/pid inicializados;
     *   - toda falha de ioctl LOGA com op + errno (rate-limited).
     * Os metodos que usam this->pid ficam com a mesma assinatura do
     * original (compatibilidade com o ecossistema do fornecedor). O
     * DAEMON usa as funcoes RT::KernelRead/KernelWrite (pid explicito).
     * ======================================================================== */
    class c_driver {
    private:
        int   fd;
        pid_t pid;

    public:
        /* (CORRECAO 2) antes: construtor vazio deixando fd SEM INIT. */
        c_driver() : fd(-1), pid(-1) {}

        ~c_driver()
        {
            if (fd > 0)
                close(fd);
        }

        /* (NOVO) abre um device especifico do driver. */
        bool OpenDevice(const char* path)
        {
            if (!path || !path[0])
                return false;

            int nfd = open(path, O_RDWR | O_CLOEXEC);

            if (nfd < 0)
            {
                RTLOGW("[PROBE] open(%s) falhou: %s (errno=%d)",
                       path, strerror(errno), errno);
                return false;
            }

            if (fd > 0)
                close(fd);

            fd = nfd;

            RTLOGI("[PROBE] device aberto: %s (fd=%d)", path, fd);
            return true;
        }

        bool Opened() const { return fd > 0; }

        int  Fd() const { return fd; }

        void CloseDevice()
        {
            if (fd > 0)
                close(fd);
            fd = -1;
        }

        /* (CORRECAO 1) antes: so guardava o pid — o fd continuava lixo. */
        void initialize(pid_t p)
        {
            this->pid = p;

            if (fd <= 0)
                Probe();
        }

        /* ----------------------------------------------------------------
         * READ/WRITE do original (usam this->pid) — mantidos p/ o
         * ecossistema do fornecedor. threads: ioctl e syscall atomica,
         * as estruturas vao na stack.
         * ---------------------------------------------------------------- */
        bool read(uintptr_t addr, void* buffer, size_t size)
        {
            if (fd <= 0)
                return false;

            COPY_MEMORY cm{};

            cm.pid    = this->pid;
            cm.addr   = addr;
            cm.buffer = buffer;
            cm.size   = size;

            if (ioctl(fd, OP_CMD_READ, &cm) != 0)
            {
                RTLOGE_RL("[READ] pid=%d addr=0x%lx size=%zu FALHOU: %s (errno=%d)",
                          (int)this->pid, (unsigned long)addr, size,
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        bool write(uintptr_t addr, void* buffer, size_t size)
        {
            if (fd <= 0)
                return false;

            COPY_MEMORY cm{};

            cm.pid    = this->pid;
            cm.addr   = addr;
            cm.buffer = buffer;
            cm.size   = size;

            if (ioctl(fd, OP_CMD_WRITE, &cm) != 0)
            {
                RTLOGE_RL("[WRITE] pid=%d addr=0x%lx size=%zu FALHOU: %s (errno=%d)",
                          (int)this->pid, (unsigned long)addr, size,
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        template <typename T>
        T read(uintptr_t addr)
        {
            T res{};

            if (this->read(addr, &res, sizeof(T)))
                return res;

            return {};
        }

        template <typename T>
        bool write(uintptr_t addr, T value)
        {
            return this->write(addr, &value, sizeof(T));
        }

        uintptr_t get_module_base(char* module_name, short index = 0)
        {
            if (fd <= 0 || !module_name)
                return 0;

            MODULE_BASE mb{};

            mb.pid    = this->pid;
            mb.name   = module_name; // 传指针给内核，内核自行读取
            mb.index  = index;
            mb.base   = 0;

            if (ioctl(fd, OP_CMD_BASE, &mb) != 0)
            {
                RTLOGE_RL("[BASE] pid=%d '%s' FALHOU: %s (errno=%d)",
                          (int)this->pid, module_name,
                          strerror(errno), errno);
                return 0;
            }

            return mb.base;
        }

        void hide_process()
        {
            if (fd <= 0)
                return;

            if (ioctl(fd, OP_CMD_HIDE_PROCESS) != 0)
                RTLOGE_RL("[HIDE_PROCESS] FALHOU: %s (errno=%d)",
                          strerror(errno), errno);
        }

        void recover_process()
        {
            if (fd <= 0)
                return;

            if (ioctl(fd, OP_CMD_RECOVER_PROCESS) != 0)
                RTLOGE_RL("[RECOVER_PROCESS] FALHOU: %s (errno=%d)",
                          strerror(errno), errno);
        }

        bool AddHwBp(HW_BP_INFO* info)
        {
            if (fd <= 0 || !info)
                return false;

            if (ioctl(fd, OP_CMD_HWBP_ADD, info) != 0)
            {
                RTLOGE_RL("[HWBP_ADD] addr=0x%lx FALHOU: %s (errno=%d)",
                          (unsigned long)(info ? info->addr : 0),
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        bool UpdateAndEnableHwBp(HW_BP_INFO* info)
        {
            // 既能恢复断点，又能把最新的 xyz 坐标刷进内核
            if (fd <= 0 || !info)
                return false;

            if (ioctl(fd, OP_CMD_HWBP_ENABLE, info) != 0)
            {
                RTLOGE_RL("[HWBP_ENABLE] addr=0x%lx FALHOU: %s (errno=%d)",
                          (unsigned long)(info ? info->addr : 0),
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        bool DisableHwBp(int bp_pid, uintptr_t addr)
        {
            if (fd <= 0)
                return false;

            HW_BP_INFO info{};

            memset(&info, 0, sizeof(info));

            info.pid  = (pid_t)bp_pid;
            info.addr = addr;

            // 发送暂停指令
            if (ioctl(fd, OP_CMD_HWBP_DISABLE, &info) != 0)
            {
                RTLOGE_RL("[HWBP_DISABLE] pid=%d addr=0x%lx FALHOU: %s (errno=%d)",
                          bp_pid, (unsigned long)addr,
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        bool ClearHwBp()
        {
            if (fd <= 0)
                return false;

            if (ioctl(fd, OP_CMD_HWBP_CLEAR, NULL) != 0)
            {
                RTLOGE_RL("[HWBP_CLEAR] FALHOU: %s (errno=%d)",
                          strerror(errno), errno);
                return false;
            }

            return true;
        }

        bool UpdateTrackingData(bool active, uintptr_t bp_addr,
                                float x, float y, float z)
        {
            if (fd <= 0)
                return false;

            TRACKING_DATA data{};

            data.is_active = active;
            data.bp_addr   = bp_addr;
            data.x         = x;
            data.y         = y;
            data.z         = z;

            if (ioctl(fd, OP_CMD_UPDATE_TRACKING, &data) != 0)
            {
                RTLOGE_RL("[UPDATE_TRACKING] FALHOU: %s (errno=%d)",
                          strerror(errno), errno);
                return false;
            }

            return true;
        }
    };

    /* ========================================================================
     * INFRA DO DAEMON (novo): probe com self-test + estado global seguro
     * ======================================================================== */

    static std::mutex    g_rtProbeMutex;
    static std::atomic<int>   g_rtFd{ -1 };
    static std::atomic<int>   g_rtAvailable{ 0 };
    static std::atomic<int>   g_rtWriteOk{ 0 };
    static std::atomic<int>   g_rtEverProbed{ 0 };
    static std::atomic<long long> g_rtNextProbeMs{ 0 };
    static char          g_rtDevPath[96] = { 0 };
    static std::atomic<uint64_t> g_rtReads{ 0 };
    static std::atomic<uint64_t> g_rtWrites{ 0 };
    static std::atomic<uint64_t> g_rtErrors{ 0 };

    inline const char* DevicePath() { return g_rtDevPath; }
    inline bool IsAvailable() { return g_rtAvailable.load() != 0; }
    inline bool WriteSelfTestOk() { return g_rtWriteOk.load() != 0; }
    inline bool EverProbed() { return g_rtEverProbed.load() != 0; }

    inline uint64_t ReadsDone()   { return g_rtReads.load(); }
    inline uint64_t WritesDone()  { return g_rtWrites.load(); }
    inline uint64_t ErrorsTotal() { return g_rtErrors.load(); }

    struct Stats
    {
        uint64_t reads;
        uint64_t writes;
        uint64_t errors;
        bool     available;
        bool     writeOk;
        const char* devPath;
    };

    inline Stats GetStats()
    {
        Stats s{};

        s.reads     = g_rtReads.load();
        s.writes    = g_rtWrites.load();
        s.errors    = g_rtErrors.load();
        s.available = IsAvailable();
        s.writeOk   = WriteSelfTestOk();
        s.devPath   = g_rtDevPath;

        return s;
    }

    /* ------------------------------------------------------------------------
     * SELF-TEST DE LEITURA: le, via ioctl OP_CMD_READ, 8 bytes de uma
     * variavel estatica DESTE processo. Se o device responder os bytes
     * certos, ele E um driver de RW compativel (e nao um device qualquer
     * que aceitou o ioctl). 100% seguro: leitura na propria memoria.
     * ------------------------------------------------------------------------ */
    inline bool SelfTestRead(int fd)
    {
        static const char kMagic[8] =
            { 'R', 'T', 'M', 'o', 'd', 'u', 'l', 'e' };

        char back[8] = { 0 };

        COPY_MEMORY cm{};

        cm.pid    = getpid();          /* o PROPRIO processo        */
        cm.addr   = (uintptr_t)kMagic; /* endereco da variavel      */
        cm.buffer = back;
        cm.size   = sizeof(back);

        errno = 0;

        if (ioctl(fd, OP_CMD_READ, &cm) != 0)
        {
            RTLOGW("[PROBE] self-test READ: ioctl recusado (%s errno=%d) — device incompativel",
                   strerror(errno), errno);
            return false;
        }

        if (memcmp(back, kMagic, sizeof(back)) != 0)
        {
            RTLOGW("[PROBE] self-test READ: dados diferentes (device nao e driver RW) — ignorado");
            return false;
        }

        return true;
    }

    /* ------------------------------------------------------------------------
     * SELF-TEST DE ESCRITA: escreve via ioctl numa pagina anonima deste
     * processo e confere o padrao. 100% seguro (pagina propria, descartada
     * em seguida). Falha aqui NAO impede o uso p/ leitura — e reportada.
     * ------------------------------------------------------------------------ */
    inline bool SelfTestWrite(int fd)
    {
        void* pg = mmap(nullptr, 4096,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (pg == MAP_FAILED)
        {
            RTLOGW("[PROBE] self-test WRITE: mmap falhou (%s)", strerror(errno));
            return false;
        }

        memset(pg, 0, 4096);

        const uint32_t pat = 0x5A5AA5A5u;

        COPY_MEMORY cm{};

        cm.pid    = getpid();
        cm.addr   = (uintptr_t)pg;
        cm.buffer = (void*)&pat;
        cm.size   = sizeof(pat);

        errno = 0;

        bool ok = (ioctl(fd, OP_CMD_WRITE, &cm) == 0) &&
                  (memcmp(pg, &pat, sizeof(pat)) == 0);

        if (!ok)
            RTLOGW("[PROBE] self-test WRITE FALHOU (%s errno=%d) — driver pode ser so-leitura",
                   strerror(errno), errno);

        munmap(pg, 4096);

        return ok;
    }

    /* ------------------------------------------------------------------------
     * PROBE: procura o device do driver e valida com os self-tests.
     *
     * Ordem de busca:
     *   1. $RT_DEV            (override manual — sempre vem primeiro)
     *   2. /dev/RTmodules     (nome padrao desta integracao)
     *   3. /dev/rtmodules
     *   4. nomes comuns dos drivers dessa familia
     *   5. varredura de /proc/misc (todos os misc devices registrados;
     *      o self-test descarta os que nao sao drivers de RW)
     *
     * Probe falho e re-tentado no maximo 1x a cada 3s (chamado pelas
     * respostas de status / toggle do painel — nunca trava o daemon).
     * ------------------------------------------------------------------------ */
    inline void CollectMiscCandidates(std::vector<std::string>& out)
    {
        FILE* fp = fopen("/proc/misc", "r");

        if (!fp)
        {
            RTLOGW("[PROBE] /proc/misc ilegivel (%s) — sem varredura de misc devices",
                   strerror(errno));
            return;
        }

        char line[256];

        while (fgets(line, sizeof(line), fp))
        {
            char name[128] = { 0 };

            /* formato: "<minor> <nome>\n" */
            char* sp = strchr(line, ' ');

            if (!sp)
                continue;

            ++sp;

            size_t n = strcspn(sp, " \t\r\n");

            if (n == 0 || n >= sizeof(name))
                continue;

            memcpy(name, sp, n);
            name[n] = 0;

            /* devices de sistema conhecidos — nada a ver com driver RW */
            if (!strcmp(name, "loop")        ||
                !strcmp(name, "rfkill")      ||
                !strcmp(name, "binder")      ||
                !strcmp(name, "hw_random")   ||
                !strcmp(name, "tun")         ||
                !strcmp(name, "ashmem")      ||
                !strcmp(name, "mtp")         ||
                !strcmp(name, "ppp")         ||
                !strcmp(name, "device-mapper") ||
                !strcmp(name, "memory")      ||
                !strcmp(name, "null")        ||
                !strcmp(name, "urandom")     ||
                !strcmp(name, "random"))
                continue;

            out.push_back(std::string("/dev/") + name);
        }

        fclose(fp);
    }

    inline bool Probe()
    {
        std::lock_guard<std::mutex> lock(g_rtProbeMutex);

        g_rtEverProbed = 1;

        if (g_rtAvailable.load())
            return true;

        /* rate-limit de re-probe (1x / 3s) pra nao martelar o /dev */
        const long long now = NowMsRT();

        if (now < g_rtNextProbeMs.load())
            return false;

        g_rtNextProbeMs = now + 3000;

        /* device pode ter morrido numa sessao anterior: limpa fd velho */
        int oldFd = g_rtFd.load();

        if (oldFd > 0)
        {
            close(oldFd);
            g_rtFd = -1;
        }

        std::vector<std::string> candidates;

        /* 1. override por ambiente (tem prioridade total) */
        const char* env = getenv("RT_DEV");

        if (env && env[0])
            candidates.push_back(std::string(env));

        /* 2-4. nomes fixos: desta integracao + comuns da familia */
        candidates.push_back("/dev/RTmodules");
        candidates.push_back("/dev/rtmodules");
        candidates.push_back("/dev/kernel64");
        candidates.push_back("/dev/kernelRW");
        candidates.push_back("/dev/memdriver");
        candidates.push_back("/dev/memroy");
        candidates.push_back("/dev/rtk");

        /* 5. varredura de /proc/misc */
        CollectMiscCandidates(candidates);

        RTLOGI("[PROBE] procurando driver de kernel (%d caminhos)...",
               (int)candidates.size());

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const char* path = candidates[i].c_str();

            int fd = open(path, O_RDWR | O_CLOEXEC);

            if (fd < 0)
            {
                /* ENOENT e o normal quando nao ha device — so loga W */
                RTLOGW("[PROBE] %s: %s (errno=%d)",
                       path, strerror(errno), errno);
                continue;
            }

            RTLOGI("[PROBE] %s aberto — validando com self-test...", path);

            if (!SelfTestRead(fd))
            {
                close(fd);
                continue;
            }

            const bool wok = SelfTestWrite(fd);

            memset(g_rtDevPath, 0, sizeof(g_rtDevPath));
            strncpy(g_rtDevPath, path, sizeof(g_rtDevPath) - 1);

            g_rtFd        = fd;
            g_rtWriteOk   = wok ? 1 : 0;
            g_rtAvailable = 1;

            RTLOGI("[KERNEL] DRIVER OK em %s | self-test: leitura OK, escrita %s",
                   path, wok ? "OK" : "FALHOU (so-leitura)");

            return true;
        }

        g_rtAvailable = 0;

        RTLOGW("[KERNEL] NENHUM driver compativel encontrado — modo kernel indisponivel (carregue o modulo .ko do fornecedor)");

        return false;
    }

    /* ------------------------------------------------------------------------
     * Marca o device como MORTO (errno suspeito) — a proxima chamada
     * re-probe automaticamente. EFAULT/ESRCH NAO contam: sao falhas
     * NORMAIS de endereco/pid alvo.
     * ------------------------------------------------------------------------ */
    inline void MarkDeviceSuspicious(int err)
    {
        if (err == ENODEV || err == EBADF || err == ENOTTY || err == EPIPE)
        {
            RTLOGW("[KERNEL] device suspeito (errno=%d %s) — re-probe na proxima operacao",
                   err, strerror(err));

            g_rtAvailable = 0;
            g_rtNextProbeMs = 0;  /* libera o re-probe imediatamente */
        }
    }

    /* ------------------------------------------------------------------------
     * CORE DO DAEMON: READ/WRITE com pid EXPLICITO (sem estado compartilhado
     * entre threads — (CORRECAO 5)).
     * ------------------------------------------------------------------------ */
    inline bool KernelRead(pid_t pid, uintptr_t addr, void* buffer, size_t size)
    {
        if (!buffer || size == 0)
            return false;

        if (!IsAvailable() && !Probe())
            return false;

        COPY_MEMORY cm{};

        cm.pid    = pid;
        cm.addr   = addr;
        cm.buffer = buffer;
        cm.size   = size;

        errno = 0;

        if (ioctl(g_rtFd.load(), OP_CMD_READ, &cm) != 0)
        {
            const int e = errno;

            g_rtErrors++;

            RTLOGE_RL("[READ] pid=%d addr=0x%llx size=%zu FALHOU: %s (errno=%d)",
                      (int)pid, (unsigned long long)addr, size,
                      strerror(e), e);

            MarkDeviceSuspicious(e);

            return false;
        }

        g_rtReads++;
        return true;
    }

    inline bool KernelWrite(pid_t pid, uintptr_t addr, const void* buffer, size_t size)
    {
        if (!buffer || size == 0)
            return false;

        if (!IsAvailable() && !Probe())
            return false;

        COPY_MEMORY cm{};

        cm.pid    = pid;
        cm.addr   = addr;
        cm.buffer = const_cast<void*>(buffer);
        cm.size   = size;

        errno = 0;

        if (ioctl(g_rtFd.load(), OP_CMD_WRITE, &cm) != 0)
        {
            const int e = errno;

            g_rtErrors++;

            RTLOGE_RL("[WRITE] pid=%d addr=0x%llx size=%zu FALHOU: %s (errno=%d)",
                      (int)pid, (unsigned long long)addr, size,
                      strerror(e), e);

            MarkDeviceSuspicious(e);

            return false;
        }

        g_rtWrites++;
        return true;
    }

    /* Base de modulo no alvo via driver (OP_CMD_BASE) — o kernel le o
     * nome pelo ponteiro (copy_from_user), igual no header original. */
    inline uintptr_t KernelModuleBase(pid_t pid, const char* name, short index = 0)
    {
        if (!name || !name[0])
            return 0;

        if (!IsAvailable() && !Probe())
            return 0;

        MODULE_BASE mb{};

        mb.pid   = pid;
        /* o kernel le a string POR PONTEIRO: string viva na stack desta chamada */
        mb.name  = const_cast<char*>(name);
        mb.index = index;
        mb.base  = 0;

        errno = 0;

        if (ioctl(g_rtFd.load(), OP_CMD_BASE, &mb) != 0)
        {
            const int e = errno;

            g_rtErrors++;

            RTLOGE_RL("[BASE] pid=%d '%s' FALHOU: %s (errno=%d)",
                      (int)pid, name, strerror(e), e);

            MarkDeviceSuspicious(e);

            return 0;
        }

        if (mb.base == 0)
            RTLOGW("[BASE] pid=%d '%s': driver nao achou o modulo (index=%d)",
                   (int)pid, name, (int)index);

        return mb.base;
    }

    /* Utilidades de exposicao do driver (futuro: painel pode usar).
     * Escondem/REAPARECEM o PROPRIO daemon nas listas de processo. */
    inline void HideDaemon() { if (IsAvailable()) ioctl(g_rtFd.load(), OP_CMD_HIDE_PROCESS); }
    inline void ShowDaemon() { if (IsAvailable()) ioctl(g_rtFd.load(), OP_CMD_RECOVER_PROCESS); }
}

/* ============================================================================
 * BLOCO DE COMPATIBILIDADE COM O ECOSSISTEMA DO FORNECEDOR
 * ============================================================================
 * As helpers globais do header original (getPID, ReadValue, WriteFloat...)
 * ficam em RTClient com o mesmo comportamento — uteis se voce colar este
 * header num projeto client-side. O stormdaemon NAO USA nada daqui
 * (popen/fork dentro do daemon e risco desnecessario).
 * ============================================================================ */
namespace RTClient {

    using RT::c_driver;

    static c_driver* driver = new c_driver();

    typedef char PACKAGENAME;

    static pid_t global_pid = -1;

    static float Kernel_v()
    {
        const char* command = "uname -r | sed 's/\\.[^.]*$//g'";

        FILE* file = popen(command, "r");

        if (file == NULL)
            return 0.0f;

        static char result[512];

        if (fgets(result, sizeof(result), file) == NULL)
        {
            pclose(file);
            return 0.0f;
        }

        pclose(file);

        result[strlen(result) - 1] = '\0';

        return (float)atof(result);
    }

    static int getPID(char* PackageName)
    {
        FILE* fp;

        char cmd[0x100] = "pidof ";

        strcat(cmd, PackageName);

        fp = popen(cmd, "r");

        if (!fp)
            return -1;

        fscanf(fp, "%d", &global_pid);

        pclose(fp);

        if (global_pid > 0)
            driver->initialize(global_pid);

        return global_pid;
    }

    static long GetModuleBaseAddr_Maps(char* module_name)
    {
        long addr = 0;

        char filename[64];
        char line[1024];

        if (global_pid < 0)
            snprintf(filename, sizeof(filename), "/proc/self/maps");
        else
            snprintf(filename, sizeof(filename), "/proc/%d/maps", global_pid);

        FILE* fp = fopen(filename, "r");

        if (fp != NULL)
        {
            while (fgets(line, sizeof(line), fp))
            {
                if (strstr(line, module_name))
                {
                    sscanf(line, "%lx-%*lx", &addr);
                    break;
                }
            }

            fclose(fp);
        }

        return addr;
    }

    static long GetModuleBaseAddr(char* module_name, int index = 0)
    {
        long base = (long)driver->get_module_base(module_name, (short)index);

        if (base == 0 && index == 0)
            return GetModuleBaseAddr_Maps(module_name);

        return base;
    }

    static long ReadValue(long addr)
    {
        long he = 0;

        if (addr < 0xFFFFFFFF)
            driver->read((uintptr_t)addr, &he, 4);
        else
            driver->read((uintptr_t)addr, &he, 8);

        return he;
    }

    static long ReadDword(long addr)
    {
        long he = 0;

        driver->read((uintptr_t)addr, &he, 4);

        return he;
    }

    static float ReadFloat(long addr)
    {
        float he = 0;

        driver->read((uintptr_t)addr, &he, 4);

        return he;
    }

    static int WriteDword(long int addr, int value)
    {
        driver->write((uintptr_t)addr, &value, 4);

        return 0;
    }

    static int WriteFloat(long int addr, float value)
    {
        driver->write((uintptr_t)addr, &value, 4);

        return 0;
    }
}

#endif // RT_MODULES_H
