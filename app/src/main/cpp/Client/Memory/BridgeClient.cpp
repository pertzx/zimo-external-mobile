#include "BridgeClient.hpp"

#include <android/log.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#define LOG_TAG "StormBridge"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/*
 * ESPELHO DO ERRO DE CONNECT NA TAG StormMemory:
 * o filtro de log do usuario so mostra StormMemory/StormDiag - a causa
 * real da "PONTE INDISPONIVEL" (errno do connect) vivia na tag
 * StormBridge e nunca aparecia. O espelho abaixo (rate-limit 5s) garante
 * que o motivo apareca no filtro que o usuario ja usa.
 */
#define LOG_TAG_DIAG "StormMemory"

#define LOGW_DIAG(...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG_DIAG, __VA_ARGS__)

namespace
{
    static constexpr const char* DEFAULT_SOCKET_PATH =
        "/data/local/tmp/stormbridge.sock";

    static std::mutex g_SocketMutex;
    static int g_Socket = -1;
    static uint32_t g_Seq = 0;

    static std::atomic<uint64_t> g_StatsReads{ 0 };
    static std::atomic<uint64_t> g_StatsWrites{ 0 };
    static std::atomic<uint64_t> g_StatsErrors{ 0 };
    static std::atomic<uint64_t> g_StatsReconnects{ 0 };
    static std::atomic<bool> g_OpLogging{ false };

    /*
     * (PONTEFIX-V5) Status da ultima resposta recebida (BridgeStatus) ou
     * BRIDGE_STATUS_NONE quando a ultima operacao nao teve resposta
     * (falha de send/recv/timeout — problema de transporte, nao do jogo).
     * Exposto via BridgeClient::LastStatusText().
     */
    static constexpr uint32_t BRIDGE_STATUS_NONE = 0xFFFFFFFFu;

    static std::atomic<uint32_t> g_LastRespStatus{ BRIDGE_STATUS_NONE };

    /*
     * PID alvo atual, memorizado das operações que o recebem por
     * parâmetro (ReadMem/WriteMem/ModuleBase/Is32Bit).
     *
     * CORREÇÃO DO "ESP não aparece / lista=N ok=0": o ReadBatch NÃO
     * recebe pid por parâmetro e antes enviava req.Pid = 0. O daemon
     * tentava ler /proc/0/mem, falhava em TODOS os itens e devolvia
     * o payload todo zerado — todas as entidades eram descartadas
     * silenciosamente (nenhum contador [ENTITY] se mexia).
     */
    static std::atomic<uint32_t> g_ActivePid{ 0 };

    /*
     * Traduz o status da ponte pra texto — usado nos logs de erro de
     * WRITE (antes o write podia falhar 100% SILENCIOSO, sem uma linha
     * sequer no logcat, e ficava impossível diagnosticar de fora).
     */
    const char* DecodeStatus(uint32_t status)
    {
        switch (status)
        {
            case BRIDGE_OK:            return "OK";
            case BRIDGE_ERR_GENERIC:   return "GENERIC";
            case BRIDGE_ERR_INVALID:   return "INVALID";
            case BRIDGE_ERR_NOTFOUND:  return "NOTFOUND";
            case BRIDGE_ERR_PERM:      return "PERM";
            case BRIDGE_ERR_PARTIAL:   return "PARTIAL";
            case BRIDGE_ERR_TOOBIG:    return "TOOBIG";
            case BRIDGE_STATUS_NONE:   return "SEM RESPOSTA (socket)";
            default:                   return "DESCONHECIDO";
        }
    }

    /*
     * Depois de uma falha de conexão, não martela o socket a cada
     * operação: espera este intervalo antes de tentar de novo. Durante
     * a espera, os pedidos falham na hora (caem no fallback local).
     */
    static std::atomic<long long> g_NextConnectAttemptMs{ 0 };
    static constexpr long long CONNECT_RETRY_INTERVAL_MS = 750;

    /*
     * Ultimo erro de connect em texto humano (+ dica de causa raiz).
     * Exposto via BridgeClient::LastConnectError() e impresso nas linhas
     * [INIT] Ponte=... do Memory (tag StormMemory). Sucesso limpa.
     */
    static char g_LastConnectErr[192] =
        "(nenhuma tentativa de connect ainda)";

    static std::atomic<long long> g_NextDiagLogMs{ 0 };

    void SetConnectErr(const char* fmt, ...)
    {
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(g_LastConnectErr, sizeof(g_LastConnectErr), fmt, ap);
        va_end(ap);
    }

    /*
     * Traduz o errno do connect() pra CAUSA RAIZ provavel:
     *   ENOENT         -> socket nao existe   = daemon root nao subiu/morreu no boot
     *   ECONNREFUSED   -> socket existe, ninguem escuta = daemon stale (morreu depois do bind)
     *   EACCES / EPERM -> SELinux bloqueando o acesso do app ao /data/local/tmp
     *   outros         -> dica generica apontando pro gerenciador (StormDaemonMgr)
     */
    const char* ConnectHintForErrno(int e)
    {
        switch (e)
        {
            case ENOENT:
                return "socket NAO EXISTE = daemon root nao subiu (root negado? erro de linker?) - veja: logcat -s StormDaemonMgr";

            case ECONNREFUSED:
                return "socket existe mas ninguem ESCUTA = daemon morreu depois do bind (stale) - watchdog deve recriar";

            case EACCES:
            case EPERM:
                return "acesso NEGADO = SELinux bloqueando o app no /data/local/tmp (o gerenciador aplica regras + plano-B)";

            case ETIMEDOUT:
                return "timeout no connect = daemon travado";

            default:
                return "veja o gerenciador: logcat -s StormDaemonMgr";
        }
    }

    /*
     * ====================================================================
     * STEALTH (Task 12): o caminho do socket NAO e mais fixo. O Java
     * (DaemonService) gera um nome ALEATORIO por instalacao, passa pro
     * daemon via --socket e grava o caminho em files/stormbridge.path.
     * Nada de "stormbridge.sock" estatico em /data/local/tmp pra
     * varredura de nomes conhecidos do anti-cheat achar.
     *
     * CORRECAO DA "PONTE INDISPONIVEL":
     *  - o arquivo .path e RELIDO enquanto ele nao existir (o C++ pode
     *    iniciar ANTES do Java gravar — cache de leitura unica travava
     *    no nome velho pra sempre);
     *  - o cache e INVALIDADO quando o connect falha (daemon respawnou
     *    com outro socket => proxima tentativa re-le o arquivo);
     *  - o pacote vem de /proc/self/cmdline (sem nome hardcoded) com
     *    fallback pro caminho do pacote conhecido.
     * ====================================================================
     */
    static std::mutex g_PathMutex;
    static char       s_PathFromFile[160] = { 0 };
    static bool       s_PathResolved      = false;

    bool ReadPathFileInto(
        const char* file,
        char*       out,
        size_t      cap
    )
    {
        FILE* fp =
            fopen(file, "rb");

        if (!fp)
            return false;

        char buf[192] = { 0 };

        const size_t n =
            fread(
                buf,
                1,
                sizeof(buf) - 1,
                fp
            );

        fclose(fp);

        if (n == 0)
            return false;

        buf[strcspn(buf, "\r\n")] = '\0';

        if (buf[0] != '/')
            return false;

        snprintf(
            out,
            cap,
            "%s",
            buf
        );

        return true;
    }

    /*
     * Pacote do proprio app via /proc/self/cmdline (conteudo:
     * "com.pkg\0args..." — strlen para no primeiro NUL).
     */
    void GetOwnPackage(
        char*  out,
        size_t cap
    )
    {
        out[0] = '\0';

        FILE* fp =
            fopen("/proc/self/cmdline", "rb");

        if (!fp)
            return;

        char buf[128] = { 0 };

        const size_t n =
            fread(
                buf,
                1,
                sizeof(buf) - 1,
                fp
            );

        fclose(fp);

        if (n > 0 && buf[0] != '\0')
            snprintf(
                out,
                cap,
                "%s",
                buf
            );
    }

    void ResolveSocketPath()
    {
        char pkg[128];

        GetOwnPackage(
            pkg,
            sizeof(pkg)
        );

        char cand[256];

        if (pkg[0] != '\0')
        {
            snprintf(
                cand,
                sizeof(cand),
                "/data/user/0/%s/files/stormbridge.path",
                pkg
            );

            if (ReadPathFileInto(
                    cand,
                    s_PathFromFile,
                    sizeof(s_PathFromFile)))
            {
                s_PathResolved = true;

                return;
            }

            snprintf(
                cand,
                sizeof(cand),
                "/data/data/%s/files/stormbridge.path",
                pkg
            );

            if (ReadPathFileInto(
                    cand,
                    s_PathFromFile,
                    sizeof(s_PathFromFile)))
            {
                s_PathResolved = true;

                return;
            }
        }

        if (ReadPathFileInto(
                "/data/data/com.stormcheats/files/stormbridge.path",
                s_PathFromFile,
                sizeof(s_PathFromFile)))
        {
            s_PathResolved = true;

            return;
        }

        /*
         * Arquivo ainda nao existe (Java ainda nao gravou): NAO marca
         * como resolvido — proxima chamada tenta de novo.
         */
    }

    const char* SocketPath()
    {
        const char* env =
            getenv("STORM_BRIDGE_SOCK");

        if (env && *env)
            return env;

        {
            std::lock_guard<std::mutex> lock(g_PathMutex);

            if (!s_PathResolved)
                ResolveSocketPath();

            if (s_PathFromFile[0])
                return s_PathFromFile;
        }

        return DEFAULT_SOCKET_PATH;
    }

    /*
     * Connect falhou => o caminho pode ter ficado velho (daemon respawnou
     * ou arquivo apareceu depois). Invalida pra re-ler no proximo acesso.
     */
    void InvalidateResolvedSocketPath()
    {
        std::lock_guard<std::mutex> lock(g_PathMutex);

        s_PathResolved = false;
    }

    long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    void CloseSocketLocked()
    {
        if (g_Socket >= 0)
        {
            close(g_Socket);

            g_Socket = -1;
        }
    }

    bool RecvAll(
        int fd,
        void* buffer,
        size_t size
    )
    {
        char* p =
            static_cast<char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            ssize_t n =
                recv(
                    fd,
                    p + total,
                    size - total,
                    0
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            if (n == 0)
                return false;

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    bool SendAll(
        int fd,
        const void* buffer,
        size_t size
    )
    {
        const char* p =
            static_cast<const char*>(buffer);

        size_t total = 0;

        while (total < size)
        {
            ssize_t n =
                send(
                    fd,
                    p + total,
                    size - total,
                    MSG_NOSIGNAL
                );

            if (n < 0)
            {
                if (errno == EINTR)
                    continue;

                return false;
            }

            total +=
                static_cast<size_t>(n);
        }

        return true;
    }

    bool ConnectLocked()
    {
        CloseSocketLocked();

        const char* path =
            SocketPath();

        int fd =
            socket(
                AF_UNIX,
                SOCK_STREAM,
                0
            );

        if (fd < 0)
        {
            SetConnectErr(
                "socket() falhou: %s",
                strerror(errno)
            );

            LOGE(
                "socket() falhou: %s",
                strerror(errno)
            );

            return false;
        }

        sockaddr_un addr{};

        addr.sun_family =
            AF_UNIX;

        strncpy(
            addr.sun_path,
            path,
            sizeof(addr.sun_path) - 1
        );

        if (connect(
                fd,
                reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)
            ) < 0)
        {
            const int savedErrno =
                errno;

            close(fd);

            /*
             * Guarda a causa raiz (visivel nas linhas [INIT] do Memory,
             * tag StormMemory - o filtro que o usuario usa) e espelha a
             * linha completa na mesma tag a cada 5s.
             */
            SetConnectErr(
                "%s - %s",
                strerror(savedErrno),
                ConnectHintForErrno(savedErrno)
            );

            LOGW(
                "connect(%s) falhou: %s (daemon rodando?)",
                path,
                strerror(savedErrno)
            );

            const long long agoraDiagMs =
                NowMs();

            if (agoraDiagMs >= g_NextDiagLogMs.load())
            {
                g_NextDiagLogMs =
                    agoraDiagMs + 5000;

                LOGW_DIAG(
                    "[PONTE] connect(%s) falhou: %s - %s",
                    path,
                    strerror(savedErrno),
                    ConnectHintForErrno(savedErrno)
                );
            }

            InvalidateResolvedSocketPath();

            return false;
        }

        struct timeval tv{};

        tv.tv_sec = 3;
        tv.tv_usec = 0;

        setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv,
            sizeof(tv)
        );

        setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &tv,
            sizeof(tv)
        );

        g_Socket = fd;

        SetConnectErr("");

        LOGI(
            "conectado a ponte em %s",
            path
        );

        return true;
    }

    /*
     * Um pedido completo por um socket já conectado.
     */
    bool RequestOnSocket(
        int fd,
        BridgeRequest& req,
        const uint8_t* payloadIn,
        uint32_t payloadInSize,
        BridgeResponse& resp,
        std::vector<uint8_t>& payloadOut
    )
    {
        g_LastRespStatus.store(
            BRIDGE_STATUS_NONE,
            std::memory_order_relaxed
        );

        if (!SendAll(
                fd,
                &req,
                sizeof(req)
            ))
        {
            return false;
        }

        if (payloadInSize > 0 && payloadIn)
        {
            if (!SendAll(
                    fd,
                    payloadIn,
                    payloadInSize
                ))
            {
                return false;
            }
        }

        if (!RecvAll(
                fd,
                &resp,
                sizeof(resp)
            ))
        {
            return false;
        }

        if (resp.Magic != BRIDGE_MAGIC ||
            resp.Version != BRIDGE_PROTO_VERSION ||
            resp.Seq != req.Seq)
        {
            LOGE(
                "resposta invalida da ponte (magic=0x%X seq=%u/%u)",
                resp.Magic,
                resp.Seq,
                req.Seq
            );

            return false;
        }

        g_LastRespStatus.store(
            resp.Status,
            std::memory_order_relaxed
        );

        payloadOut.clear();

        if (resp.PayloadSize > 0)
        {
            if (resp.PayloadSize > BRIDGE_MAX_PAYLOAD)
            {
                LOGE(
                    "payload de resposta %u acima do limite",
                    resp.PayloadSize
                );

                return false;
            }

            payloadOut.resize(resp.PayloadSize);

            if (!RecvAll(
                    fd,
                    payloadOut.data(),
                    payloadOut.size()
                ))
            {
                return false;
            }
        }

        return true;
    }
}

namespace BridgeClient
{

bool EnsureConnected()
{
    std::lock_guard<std::mutex> lock(g_SocketMutex);

    if (g_Socket >= 0)
        return true;

    long long now =
        NowMs();

    if (now < g_NextConnectAttemptMs.load())
        return false;

    g_NextConnectAttemptMs =
        now + CONNECT_RETRY_INTERVAL_MS;

    if (!ConnectLocked())
        return false;

    /*
     * Handshake: garante que é realmente a nossa ponte.
     */
    BridgeRequest req{};

    req.Magic =
        BRIDGE_MAGIC;

    req.Version =
        BRIDGE_PROTO_VERSION;

    req.Cmd =
        BRIDGE_CMD_PING;

    req.Seq =
        ++g_Seq;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    if (!RequestOnSocket(
            g_Socket,
            req,
            nullptr,
            0,
            resp,
            payload
        ) ||
        resp.Status != BRIDGE_OK)
    {
        LOGE("ping inicial da ponte falhou");

        CloseSocketLocked();

        return false;
    }

    LOGI(
        "ponte OK (protocolo v%llu)",
        (unsigned long long)resp.Value
    );

    return true;
}

void Disconnect()
{
    std::lock_guard<std::mutex> lock(g_SocketMutex);

    CloseSocketLocked();
}

bool IsConnected()
{
    std::lock_guard<std::mutex> lock(g_SocketMutex);

    return g_Socket >= 0;
}

const char* LastConnectError()
{
    return g_LastConnectErr;
}

/*
 * (PONTEFIX-V5) Texto do status da ultima resposta (ou "SEM RESPOSTA
 * (socket)"). Thread-safe (atomic + texto estatico).
 */
const char* LastStatusText()
{
    return DecodeStatus(
        g_LastRespStatus.load(std::memory_order_relaxed)
    );
}

bool Request(
    BridgeRequest& req,
    const uint8_t* payloadIn,
    uint32_t payloadInSize,
    BridgeResponse& resp,
    std::vector<uint8_t>& payloadOut
)
{
    std::lock_guard<std::mutex> lock(g_SocketMutex);

    req.Magic =
        BRIDGE_MAGIC;

    req.Version =
        BRIDGE_PROTO_VERSION;

    req.Seq =
        ++g_Seq;

    /*
     * BLINDAGEM DO WRITE: o header PRECISA declarar exatamente quantos
     * bytes de payload vêm depois dele. Forçar aqui torna a coerência
     * ESTRUTURAL — nenhum comando consegue mais enviar header com
     * PayloadSize=0 e bytes órfãos no stream (era isso que deixava o
     * canal de write morto: o daemon lia 0 bytes, respondia INVALID
     * silencioso e o stream dessincronizava).
     */
    req.PayloadSize =
        (payloadIn && payloadInSize > 0) ? payloadInSize : 0;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        if (g_Socket < 0)
        {
            if (attempt == 0)
            {
                /*
                 * Primeira tentativa respeita o rate-limit global de
                 * reconexão. A segunda (retry interno após queda no
                 * meio do pedido) conecta direto: o daemon acabou de
                 * voltar e não pode esperar.
                 */
                long long now =
                    NowMs();

                if (now < g_NextConnectAttemptMs.load())
                    return false;

                g_NextConnectAttemptMs =
                    now + CONNECT_RETRY_INTERVAL_MS;
            }

            if (!ConnectLocked())
                return false;

            g_StatsReconnects++;
        }

        if (RequestOnSocket(
                g_Socket,
                req,
                payloadIn,
                payloadInSize,
                resp,
                payloadOut
            ))
        {
            return true;
        }

        g_StatsErrors++;

        /*
         * Socket morreu no meio (daemon reiniciou, timeout).
         * Fecha e tenta mais uma vez.
         */
        LOGW(
            "pedido seq=%u cmd=%u falhou (%s) - reconectando",
            req.Seq,
            req.Cmd,
            strerror(errno)
        );

        CloseSocketLocked();
    }

    return false;
}

bool ReadMem(
    uint32_t pid,
    uint64_t address,
    void* buffer,
    uint32_t size
)
{
    if (!buffer || size == 0)
        return false;

    g_ActivePid.store(pid);

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_READ;

    req.Pid = pid;
    req.Address = address;
    req.Size = size;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    bool ok =
        Request(
            req,
            nullptr,
            0,
            resp,
            payload
        ) &&
        resp.Status == BRIDGE_OK &&
        resp.PayloadSize == size;

    if (!ok)
        return false;

    memcpy(
        buffer,
        payload.data(),
        size
    );

    g_StatsReads++;

    if (g_OpLogging.load())
    {
        LOGI(
            "[READ] pid=%u addr=0x%llX size=%u OK",
            pid,
            (unsigned long long)address,
            size
        );
    }

    return true;
}

bool ReadBatch(
    const BatchItem* items,
    uint32_t count,
    std::vector<uint8_t>& outBlob
)
{
    outBlob.clear();

    if (!items || count == 0)
        return false;

    /*
     * Monta o payload: N x 12 bytes (endereco + tamanho), packed.
     * O protocolo do daemon espera exatamente esse layout.
     */
    std::vector<uint8_t> payload(count * 12);

    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t* p = payload.data() + (size_t)i * 12;

        uint64_t addr = items[i].address;
        uint32_t sz = items[i].size;

        memcpy(p, &addr, sizeof(addr));
        memcpy(p + 8, &sz, sizeof(sz));
    }

    /*
     * CORREÇÃO: usa o pid memorizado das leituras individuais.
     * Antes req.Pid ficava 0 e o daemon falhava em TODOS os itens
     * (tudo voltava zerado — "lista=N ok=0, descartados tudo 0").
     */
    uint32_t pid = g_ActivePid.load();

    if (pid == 0)
    {
        /*
         * Nenhuma leitura individual aconteceu ainda (nenhum pid
         * conhecido). Falha explícita — NÃO enviar pid=0 pro daemon,
         * senão o blob volta zerado e o ESP "some" sem nenhum log.
         */
        LOGE("READBATCH sem pid conhecido (nenhuma leitura individual antes)");

        return false;
    }

    BridgeRequest req{};

    req.Cmd = BRIDGE_CMD_READ_BATCH;
    req.Pid = pid;
    req.PayloadSize = static_cast<uint32_t>(payload.size());

    BridgeResponse resp{};
    std::vector<uint8_t> respPayload;

    if (!Request(
            req,
            payload.data(),
            static_cast<uint32_t>(payload.size()),
            resp,
            respPayload
        ))
    {
        return false;
    }

    if (resp.Status != BRIDGE_OK && resp.Status != BRIDGE_ERR_PARTIAL)
        return false;

    outBlob = std::move(respPayload);

    return true;
}

bool WriteMem(
    uint32_t pid,
    uint64_t address,
    const void* buffer,
    uint32_t size
)
{
    if (!buffer || size == 0)
        return false;

    g_ActivePid.store(pid);

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_WRITE;

    req.Pid = pid;
    req.Address = address;
    req.Size = size;

    /*
     * FIX DO WRITE (canal morto): o header precisa declarar o tamanho
     * do payload. Antes req.PayloadSize ficava 0, então o daemon lia
     * ZERO bytes -> payloadIn.size() (0) != req.Size ->
     * BRIDGE_ERR_INVALID SILLENCIOSO (sem log nenhum), e os bytes do
     * valor ficavam órfãos no stream dessincronizando a conexão.
     * Era isso que deixava o log do daemon sem NENHUM write e todos
     * os exploits que escrevem sem funcionar.
     */
    req.PayloadSize = size;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    const bool transportOk =
        Request(
            req,
            static_cast<const uint8_t*>(buffer),
            size,
            resp,
            payload
        );

    const bool ok =
        transportOk &&
        resp.Status == BRIDGE_OK;

    g_StatsWrites++;

    if (ok && g_OpLogging.load())
    {
        LOGI(
            "[WRITE] pid=%u addr=0x%llX size=%u OK",
            pid,
            (unsigned long long)address,
            size
        );
    }

    /*
     * DIAGNÓSTICO DO WRITE (nunca mais falha silenciosa): se a escrita
     * não foi aceita, loga UMA vez por segundo com o motivo decodificado.
     * - transporte falhou  -> daemon morto/socket caiu (veja reconexões)
     * - status INVALID     -> header/payload dessincronizado (não deve
     *                         acontecer: Request() força PayloadSize)
     * - status PERM        -> SELinux/ptrace bloqueando o daemon
     * - status GENERIC     -> process_vm_writev + /proc/pid/mem falharam
     */
    if (!ok)
    {
        static long long s_LastWriteFailLogMs = 0;

        const long long nowMs = NowMs();

        if (nowMs - s_LastWriteFailLogMs > 1000)
        {
            s_LastWriteFailLogMs = nowMs;

            if (transportOk)
            {
                LOGE(
                    "[WRITE FALHOU] pid=%u addr=0x%llX size=%u status=%s (ponte respondeu erro)",
                    pid,
                    (unsigned long long)address,
                    size,
                    DecodeStatus(resp.Status)
                );
            }
            else
            {
                LOGE(
                    "[WRITE FALHOU] pid=%u addr=0x%llX size=%u transporte indisponivel (daemon morto? reconexoes=%llu)",
                    pid,
                    (unsigned long long)address,
                    size,
                    (unsigned long long)g_StatsReconnects.load()
                );
            }
        }
    }

    return ok;
}

int64_t FindPid(
    const std::vector<std::string>& packageNames
)
{
    if (packageNames.empty())
        return -1;

    std::string joined;

    for (
        size_t i = 0;
        i < packageNames.size();
        i++
    )
    {
        if (i > 0)
            joined += '|';

        joined +=
            packageNames[i];
    }

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_FIND_PID;

    req.PayloadSize =
        static_cast<uint32_t>(
            joined.size()
        );

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    if (!Request(
            req,
            reinterpret_cast<const uint8_t*>(
                joined.data()
            ),
            static_cast<uint32_t>(
                joined.size()
            ),
            resp,
            payload
        ))
    {
        return -1;
    }

    if (resp.Status != BRIDGE_OK ||
        resp.Value == 0)
    {
        LOGW(
            "FIND_PID: processo nao encontrado pela ponte (status=%u)",
            resp.Status
        );

        return -1;
    }

    LOGI(
        "FIND_PID: pid=%lld",
        (long long)resp.Value
    );

    return static_cast<int64_t>(resp.Value);
}

uint64_t ModuleBase(
    uint32_t pid,
    const char* moduleName
)
{
    if (!moduleName || !*moduleName)
        return 0;

    g_ActivePid.store(pid);

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_MODULE_BASE;

    req.Pid = pid;

    req.PayloadSize =
        static_cast<uint32_t>(
            strlen(moduleName)
        );

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    if (!Request(
            req,
            reinterpret_cast<const uint8_t*>(moduleName),
            static_cast<uint32_t>(strlen(moduleName)),
            resp,
            payload
        ))
    {
        return 0;
    }

    if (resp.Status != BRIDGE_OK)
        return 0;

    LOGI(
        "MODULE_BASE pid=%u '%s' = 0x%llX",
        pid,
        moduleName,
        (unsigned long long)resp.Value
    );

    return resp.Value;
}

bool Is32Bit(
    uint32_t pid,
    bool& out32
)
{
    g_ActivePid.store(pid);

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_IS_32BIT;

    req.Pid = pid;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    if (!Request(
            req,
            nullptr,
            0,
            resp,
            payload
        ) ||
        resp.Status != BRIDGE_OK)
    {
        return false;
    }

    out32 =
        (resp.Value == 1);

    return true;
}

/*
 * (V9) AUTO-RESOLVE DE TYPEINFO: pede ao daemon a varredura dos slots
 * de Il2CppClass pelo nome. O scan é pesado UMA vez (0.5-2 s) e fica
 * cacheado no daemon por (pid, lib, classe).
 */
bool FindTypeInfo(
    uint32_t pid,
    const char* libName,
    const char* className,
    std::vector<TypeInfoHit>& out
)
{
    out.clear();

    if (!libName || !className ||
        !libName[0] || !className[0] ||
        pid == 0)
        return false;

    std::string reqStr(libName);
    reqStr += '|';
    reqStr += className;

    BridgeRequest req{};

    req.Cmd = BRIDGE_CMD_FIND_TYPEINFO;
    req.Pid = pid;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    if (!Request(
            req,
            reinterpret_cast<const uint8_t*>(reqStr.data()),
            static_cast<uint32_t>(reqStr.size()),
            resp,
            payload
        ) ||
        resp.Status != BRIDGE_OK ||
        payload.size() < sizeof(uint32_t))
    {
        LOGW(
            "FindTypeInfo('%s' em '%s') falhou: status=%s payload=%zu",
            className,
            libName,
            LastStatusText(),
            payload.size()
        );

        return false;
    }

    uint32_t count = 0;

    memcpy(
        &count,
        payload.data(),
        sizeof(uint32_t)
    );

    if (count > 16)
        count = 16;

    const size_t need =
        sizeof(uint32_t) +
        count * sizeof(TypeInfoHitPayload);

    if (payload.size() < need)
        return false;

    out.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        TypeInfoHitPayload hp{};

        memcpy(
            &hp,
            payload.data() +
                sizeof(uint32_t) +
                i * sizeof(TypeInfoHitPayload),
            sizeof(hp)
        );

        TypeInfoHit h{};

        h.Rva   = hp.rva;
        h.Klass = hp.klass;

        out.push_back(h);
    }

    LOGI(
        "FindTypeInfo('%s') -> %zu hits (primeiro rva=0x%llX)",
        className,
        out.size(),
        out.empty()
            ? 0ULL
            : (unsigned long long)out[0].Rva
    );

    return true;
}

void EnableOpLogging(
    bool enabled
)
{
    g_OpLogging =
        enabled;

    LOGI(
        "log de operacoes individuais: %s",
        enabled ? "ON" : "OFF"
    );
}

Stats GetStats()
{
    Stats s{};

    s.Reads =
        g_StatsReads.load();

    s.Writes =
        g_StatsWrites.load();

    s.Errors =
        g_StatsErrors.load();

    s.Reconnects =
        g_StatsReconnects.load();

    return s;
}

/*
 * (V8.7) PROVA DE BYPASS — stats internas do daemon, com cache de 500 ms.
 * O overlay PERF do painel chama isto por frame; no maximo 2 round-trips
 * por segundo sao gerados.
 */
RemoteStats GetRemoteStats()
{
    static std::mutex s_CacheMutex;
    static RemoteStats s_Cached{};
    static long long s_CachedAtMs = -1000;

    const long long now = NowMs();

    {
        std::lock_guard<std::mutex> lk(s_CacheMutex);

        if (now - s_CachedAtMs < 500)
            return s_Cached;
    }

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_STATS;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    RemoteStats out{};

    if (Request(
            req,
            nullptr,
            0,
            resp,
            payload
        ) &&
        resp.Status == BRIDGE_OK &&
        payload.size() >= sizeof(BridgeStatsPayload))
    {
        BridgeStatsPayload sp{};

        memcpy(
            &sp,
            payload.data(),
            sizeof(sp)
        );

        out.BridgeReads  = sp.bridgeReads;
        out.BridgeWrites = sp.bridgeWrites;
        out.BridgeErrors = sp.bridgeErrors;
        out.UptimeSec    = sp.uptimeSec;

        out.CacheHits    = sp.cacheHits;
        out.CacheNegHits = sp.cacheNegHits;
        out.CacheMisses  = sp.cacheMisses;
        out.Syscalls     = sp.syscalls;
        out.Retries      = sp.retries;

        out.DirectReads  = sp.directReads;
        out.ExactFb      = sp.exactFb;
        out.VmFbReads    = sp.vmFbReads;

        out.DirectWrites = sp.directWrites;
        out.VmFbWrites   = sp.vmFbWrites;

        out.NegCreated   = sp.negCreated;
        out.OpenFails    = sp.openFails;
        out.WrRefused    = sp.wrRefused;
        out.WrKilled     = sp.wrKilled;

        out.WritesOn     = sp.writesOn;
        out.VmFallbackOn = sp.vmFallbackOn;

        out.Ok = true;

        std::lock_guard<std::mutex> lk(s_CacheMutex);

        s_Cached = out;
        s_CachedAtMs = now;
    }

    return out;
}

const char* GetSocketPath()
{
    return SocketPath();
}

void InvalidateSocketPath()
{
    InvalidateResolvedSocketPath();
}

} // namespace BridgeClient