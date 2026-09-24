#pragma once

/*
 * ============================================================================
 * BridgeProtocol.hpp
 * ============================================================================
 *
 * Protocolo de comunicação entre o CLIENT (libclient.so, processo do app)
 * e o DAEMON (executável root em /data/local/tmp/stormdaemon).
 *
 * O daemon é SOMENTE UMA PONTE de READ/WRITE. Ele não conhece offsets,
 * não conhece o jogo, não guarda config: ele recebe pedidos
 * (pid, endereço, tamanho, dados) e executa no alvo com privilégio root
 * via syscall direta (__NR_pread64/__NR_pwrite64) + cache de leitura.
 *
 * Transporte: AF_UNIX SOCK_STREAM no caminho
 *     /data/local/tmp/stormbridge.sock
 *
 * Framing: cada mensagem = header fixo + payload (PayloadSize bytes).
 * Pedido:  BridgeRequest  (+ payload quando WRITE / nomes p/ FIND_PID etc.)
 * Resposta: BridgeResponse (+ payload quando READ / nomes / nada)
 * ============================================================================
 */

#include <cstdint>

/*
 * "STOR" em ASCII. Identifica mensagens da ponte.
 */
static constexpr uint32_t BRIDGE_MAGIC = 0x53544F52;

static constexpr uint32_t BRIDGE_PROTO_VERSION = 1;

/*
 * Tamanho máximo do payload aceito pela ponte (1 MiB é mais que
 * suficiente: as leituras maiores são matrizes de skeleton ~4-8 KB).
 */
static constexpr uint32_t BRIDGE_MAX_PAYLOAD = 1024 * 1024;

enum BridgeCmd : uint32_t
{
    /*
     * Liveness/identidade. Resposta: Value = BRIDGE_PROTO_VERSION.
     */
    BRIDGE_CMD_PING = 1,

    /*
     * Ler Size bytes em Address/Pid. Payload de resposta = dados lidos.
     */
    BRIDGE_CMD_READ = 2,

    /*
     * Escrever Payload bytes em Address/Pid. Payload do pedido = dados.
     */
    BRIDGE_CMD_WRITE = 3,

    /*
     * Procurar PID cujo /proc/<pid>/cmdline contenha QUALQUER um dos nomes
     * passados (payload = nomes separados por '|').
     * Resposta: Value = pid (0 = não achou). Nome do jogo fica no CLIENT,
     * o daemon continua genérico.
     */
    BRIDGE_CMD_FIND_PID = 4,

    /*
     * Endereço base de um módulo no alvo (payload = nome do módulo, ex:
     * "libil2cpp.so"). Resposta: Value = base (0 = não achou).
     */
    BRIDGE_CMD_MODULE_BASE = 5,

    /*
     * O processo alvo é 32-bit? (lê /proc/<pid>/exe ELF class)
     * Resposta: Value = 1 (32-bit) ou 0 (64-bit).
     */
    BRIDGE_CMD_IS_32BIT = 6,

    /*
     * Encerra o daemon de forma limpa (usado pelo Java no stop).
     */
    BRIDGE_CMD_SHUTDOWN = 7,

    /*
     * LEITURA EM LOTE — o coração da velocidade do ESP.
     *
     * Payload do pedido: N itens de 12 bytes, cada um:
     *     uint64_t address;
     *     uint32_t size;
     *
     * Payload da resposta: os dados lidos CONCATENADOS na mesma ordem
     * (N leituras em UM único round-trip do socket). Item com falha de
     * leitura vem ZERADO no payload — o código chamante trata zero como
     * "leitura falhou", igual ao comportamento do READ individual.
     *
     * Status: BRIDGE_OK (tudo lido) ou BRIDGE_ERR_PARTIAL (alguns itens
     * falharam — o payload ainda assim contém os bytes de todos).
     */
    BRIDGE_CMD_READ_BATCH = 8,

    /*
     * (V8.7) ESTATISTICAS DO DAEMON — PROVA DE BYPASS.
     *
     * Sem payload de entrada. Payload de resposta = BridgeStatsPayload
     * (struct fixa, packed). O painel usa para mostrar EM TEMPO REAL:
     *   - quantas leituras o daemon fez por pread64 (directReads)
     *   - quantas caíram no fallback process_vm (vmFbReads — 0 = bypass
     *     100% pread64)
     *   - quantas escritas por pwrite64 (directWrites) vs vmFbWrites
     *   - cache hit/miss e kill-switch de escrita (wrKilled/writesOn)
     */
    BRIDGE_CMD_STATS = 9,

    /*
     * (V9) AUTO-RESOLVE DE TYPEINFO — cura o "para no AccessClass".
     *
     * Quando o jogo atualiza, os slots globais dos TypeInfo (GameFacade,
     * GameVarDef, AvatarWardrobeDataManager, ...) SE MOVEM dentro da
     * .data da libil2cpp.so e os valores manuais do Offsets.cpp ficam
     * velhos — a cadeia morre no AccessClass.
     *
     * O daemon varre os mapeamentos LEGÍVEIS da lib no alvo (pread64 em
     * blocos de 256 KB) procurando slots que apontam para um Il2CppClass
     * cujo campo `name` (offset 0x8 em 32-bit / 0x10 em 64-bit) é
     * EXATAMENTE o nome pedido. Cada slot achado = {RVA, klass}.
     *
     * Payload do pedido: "libName|ClassName" (ex: "libil2cpp.so|GameFacade").
     * Payload da resposta: uint32 count + count x TypeInfoHitPayload
     *     { uint64 rva; uint64 klass; } (16 bytes cada, count <= 16).
     *
     * Resultado é cacheado no daemon por (pid, lib, classe).
     */
    BRIDGE_CMD_FIND_TYPEINFO = 10,

    /*
     * (V10 KERNEL) ATIVAR/DESATIVAR O MODO KERNEL do daemon.
     *
     * Payload do pedido: uint32_t enable
     *     1 = usar SOMENTE o driver de kernel (RTmodules.h, ioctl
     *         OP_CMD_READ/OP_CMD_WRITE) para todo READ/WRITE — sem
     *         pread64/pwrite64, sem /proc/pid/mem;
     *     0 = voltar ao caminho padrao (syscall direta).
     *
     * Resposta: Value = 1 (modo ativado) ou 0 (nao ativado);
     *           Status = BRIDGE_OK ou BRIDGE_ERR_NOTFOUND quando o
     *           device do driver nao existe (modulo nao carregado).
     *
     * O toggle e estado RUNTIME do daemon: o client RE-APLICA sozinho
     * depois de qualquer reconexao (daemon respawnou).
     */
    BRIDGE_CMD_KERNEL_SET = 11,

    /*
     * (V10 KERNEL) ESTADO DO DRIVER DE KERNEL (usado pelo painel).
     *
     * Sem payload de entrada. Payload de resposta = KernelStatusPayload
     * (struct fixa, packed): disponibilidade do device, se o modo esta
     * ativo, self-test de escrita, contadores e o caminho do device
     * em uso (ex: "/dev/RTmodules").
     */
    BRIDGE_CMD_KERNEL_STATUS = 12,
};

enum BridgeStatus : uint32_t
{
    BRIDGE_OK = 0,
    BRIDGE_ERR_GENERIC = 1,
    BRIDGE_ERR_INVALID = 2,   /* pedido malformado                    */
    BRIDGE_ERR_NOTFOUND = 3,  /* pid/modulo/proc nao encontrado       */
    BRIDGE_ERR_PERM = 4,      /* EPERM/ptrace bloqueado               */
    BRIDGE_ERR_PARTIAL = 5,   /* leitura/escrita parcial              */
    BRIDGE_ERR_TOOBIG = 6,    /* payload/tamanho acima do limite      */
};

#pragma pack(push, 1)

struct BridgeRequest
{
    uint32_t Magic;         /* BRIDGE_MAGIC                            */
    uint32_t Version;       /* BRIDGE_PROTO_VERSION                    */
    uint32_t Cmd;           /* BridgeCmd                               */
    uint32_t Seq;           /* sequencia incremental (eco na resposta) */
    uint32_t Pid;           /* processo alvo                           */
    uint32_t PayloadSize;   /* bytes de payload apos este header       */
    uint64_t Address;       /* endereco alvo (READ/WRITE)              */
    uint32_t Size;          /* tamanho da operacao (READ/WRITE)        */
    uint32_t Reserved;
};

struct BridgeResponse
{
    uint32_t Magic;         /* BRIDGE_MAGIC                            */
    uint32_t Version;       /* BRIDGE_PROTO_VERSION                    */
    uint32_t Cmd;           /* eco do comando                          */
    uint32_t Seq;           /* eco da sequencia                        */
    uint32_t Status;        /* BridgeStatus                            */
    uint32_t PayloadSize;   /* bytes de payload apos este header       */
    uint64_t Value;         /* pid / base / flag 32bit / versao        */
};

#pragma pack(pop)

static_assert(sizeof(BridgeRequest) == 40, "BridgeRequest deve ter 40 bytes");
static_assert(sizeof(BridgeResponse) == 32, "BridgeResponse deve ter 32 bytes");

/*
 * (V8.7) Payload da resposta do BRIDGE_CMD_STATS. Struct FIXA (packed,
 * campos de largura definida) — versoes futuras só ADICIONAM campos no
 * fim; o cliente valida pelo Size recebido.
 */
#pragma pack(push, 1)
struct BridgeStatsPayload
{
    /* totals da ponte (g_Total*) */
    uint64_t bridgeReads;
    uint64_t bridgeWrites;
    uint64_t bridgeErrors;

    /* tempo de vida do daemon (segundos) */
    uint64_t uptimeSec;

    /* cache de blocos de 256 B */
    uint64_t cacheHits;
    uint64_t cacheNegHits;
    uint64_t cacheMisses;
    uint64_t syscalls;
    uint64_t retries;

    /* (V8.7) caminhos de leitura */
    uint64_t directReads;    /* pread64 direto (CAMINHO 1)              */
    uint64_t exactFb;        /* pread64 do range exato (CAMINHO 1b)     */
    uint64_t vmFbReads;      /* process_vm_readv (CAMINHO 2 — fallback) */

    /* (V8.7) caminhos de escrita */
    uint64_t directWrites;   /* pwrite64 direto (CAMINHO 1)             */
    uint64_t vmFbWrites;     /* process_vm_writev (CAMINHO 2 — fallback)*/

    /* misc de telemetria */
    uint64_t negCreated;
    uint64_t openFails;
    uint64_t wrRefused;
    uint64_t wrKilled;

    uint32_t writesOn;         /* canal de escrita habilitado           */
    uint32_t vmFallbackOn;     /* STORM_VM_FALLBACK com que o daemon
                                * foi compilado (0 = build 100%
                                * pread64/pwrite64)                     */
    uint32_t daemonProtoVersion;
    uint32_t reserved0;
};
#pragma pack(pop)

/*
 * (V10 KERNEL) campos ADICIONADOS NO FIM (politica do struct fixo:
 * versoes novas so acrescentam no fim; o cliente valida pelo Size
 * recebido). Contadores do modo kernel:
 *   kReads/kWrites/kErrs = ops servidas pelo driver (ioctl);
 *   kActive = modo kernel ligado agora; kAvail = device encontrado.
 */
#pragma pack(push, 1)
struct BridgeStatsPayloadKFields
{
    uint64_t kReads;
    uint64_t kWrites;
    uint64_t kErrs;
    uint32_t kActive;
    uint32_t kAvail;
};
#pragma pack(pop)

static_assert(sizeof(BridgeStatsPayload) == 160,
              "BridgeStatsPayload (parte base) deve ter 160 bytes");

static_assert(sizeof(BridgeStatsPayloadKFields) == 32,
              "BridgeStatsPayloadKFields deve ter 32 bytes");

/*
 * Struct completa (base + campos de kernel). O daemon SEMPRE responde
 * com ela; cliente antigo (que espera 160) continua funcionando porque
 * ele so copia o comeco.
 */
#pragma pack(push, 1)
struct BridgeStatsPayloadV2
{
    BridgeStatsPayload base;
    BridgeStatsPayloadKFields kernel;
};
#pragma pack(pop)

static_assert(sizeof(BridgeStatsPayloadV2) == 192,
              "BridgeStatsPayloadV2 deve ter 192 bytes");


/*
 * (V10 KERNEL) Payload da resposta do BRIDGE_CMD_KERNEL_STATUS.
 * Struct FIXA (packed).
 */
#pragma pack(push, 1)
struct KernelStatusPayload
{
    uint32_t supported;   /* daemon compilado com RTmodules.h (sempre 1)  */
    uint32_t available;   /* device do driver aberto + self-test READ ok  */
    uint32_t active;      /* modo kernel LIGADO neste momento             */
    uint32_t writeOk;     /* self-test de ESCRITA passou                  */

    uint64_t reads;       /* ops de leitura servidas pelo kernel          */
    uint64_t writes;      /* ops de escrita servidas pelo kernel          */
    uint64_t errs;        /* falhas de ioctl (endereco invalido conta)    */

    char     devPath[64]; /* device em uso, ex "/dev/RTmodules" (0 = -)   */
};
#pragma pack(pop)

static_assert(sizeof(KernelStatusPayload) == 104,
              "KernelStatusPayload deve ter 104 bytes");

/*
 * (V9) Hit de uma busca de TypeInfo (BRIDGE_CMD_FIND_TYPEINFO).
 * Struct FIXA (packed): rva = offset do slot dentro da lib,
 * klass = ponteiro do Il2CppClass lido NAQUELE instante (o cliente
 * ainda assim deve ler *(lib + rva) na hora de usar — o klass pode
 * mudar entre sessões do jogo, o RVA não).
 */
#pragma pack(push, 1)
struct TypeInfoHitPayload
{
    uint64_t rva;
    uint64_t klass;
};
#pragma pack(pop)

static_assert(sizeof(TypeInfoHitPayload) == 16,
              "TypeInfoHitPayload deve ter 16 bytes");
