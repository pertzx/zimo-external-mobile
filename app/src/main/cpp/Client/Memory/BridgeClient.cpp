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
#include <cstring>
#include <mutex>

#define LOG_TAG "StormBridge"

#define LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
     * Depois de uma falha de conexão, não martela o socket a cada
     * operação: espera este intervalo antes de tentar de novo. Durante
     * a espera, os pedidos falham na hora (caem no fallback local).
     */
    static std::atomic<long long> g_NextConnectAttemptMs{ 0 };
    static constexpr long long CONNECT_RETRY_INTERVAL_MS = 750;

    const char* SocketPath()
    {
        const char* env =
            getenv("STORM_BRIDGE_SOCK");

        return (env && *env)
            ? env
            : DEFAULT_SOCKET_PATH;
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
            close(fd);

            LOGW(
                "connect(%s) falhou: %s (daemon rodando?)",
                path,
                strerror(errno)
            );

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

bool WriteMem(
    uint32_t pid,
    uint64_t address,
    const void* buffer,
    uint32_t size
)
{
    if (!buffer || size == 0)
        return false;

    BridgeRequest req{};

    req.Cmd =
        BRIDGE_CMD_WRITE;

    req.Pid = pid;
    req.Address = address;
    req.Size = size;

    BridgeResponse resp{};
    std::vector<uint8_t> payload;

    bool ok =
        Request(
            req,
            static_cast<const uint8_t*>(buffer),
            size,
            resp,
            payload
        ) &&
        resp.Status == BRIDGE_OK;

    g_StatsWrites++;

    if (g_OpLogging.load() && ok)
    {
        LOGI(
            "[WRITE] pid=%u addr=0x%llX size=%u OK",
            pid,
            (unsigned long long)address,
            size
        );
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

const char* GetSocketPath()
{
    return SocketPath();
}

} // namespace BridgeClient
