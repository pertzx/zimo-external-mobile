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
 * via process_vm_readv/process_vm_writev com fallback para /proc/pid/mem.
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
