#pragma once

/*
 * ============================================================================
 * BridgeClient.hpp
 * ============================================================================
 *
 * Lado CLIENT da ponte root de read/write.
 *
 * O client (libclient.so, processo do app, sem privilégios) fala com o
 * daemon executável em /data/local/tmp/stormdaemon por um Unix socket.
 * Toda leitura/escrita de memória do jogo passa por aqui.
 *
 * Thread-safe: um mutex serializa os pedidos (a thread de leitura do jogo,
 * a thread de render e os exploits todos chamam Read/Write ao mesmo tempo).
 * ============================================================================
 */

#include <Shared/Bridge/BridgeProtocol.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace BridgeClient
{
    /*
     * Conecta ao daemon (se ainda não conectado). Retorna true quando
     * há socket ativo. Não lança.
     */
    bool EnsureConnected();

    /*
     * Fecha a conexão atual (Memory::Shutdown chama).
     */
    void Disconnect();

    /*
     * true se há socket ativo neste momento.
     */
    bool IsConnected();

    /*
     * Envia um pedido completo e devolve a resposta.
     * Internamente reconecta UMA vez em caso de falha (daemon reiniciou).
     */
    bool Request(
        BridgeRequest& req,               /* Seq é preenchido aqui     */
        const uint8_t* payloadIn,         /* pode ser nullptr          */
        uint32_t payloadInSize,
        BridgeResponse& resp,
        std::vector<uint8_t>& payloadOut
    );

    /*
     * Conveniências usadas por Memory.
     */
    bool ReadMem(
        uint32_t pid,
        uint64_t address,
        void* buffer,
        uint32_t size
    );

    bool WriteMem(
        uint32_t pid,
        uint64_t address,
        const void* buffer,
        uint32_t size
    );

    /*
     * Procura o processo do alvo por nomes de pacote.
     * Retorna pid > 0 ou -1.
     */
    int64_t FindPid(
        const std::vector<std::string>& packageNames
    );

    /*
     * Base de um módulo no processo alvo (0 se não achar).
     */
    uint64_t ModuleBase(
        uint32_t pid,
        const char* moduleName
    );

    /*
     * Processo alvo é 32-bit? Retorna false se não conseguiu descobrir.
     */
    bool Is32Bit(
        uint32_t pid,
        bool& out32
    );

    /*
     * Log de operação individual (verbose). Util para diagnosticar se
     * uma função específica do painel está lendo/gravando de verdade.
     */
    void EnableOpLogging(
        bool enabled
    );

    struct Stats
    {
        uint64_t Reads = 0;
        uint64_t Writes = 0;
        uint64_t Errors = 0;
        uint64_t Reconnects = 0;
    };

    Stats GetStats();

    /*
     * Caminho do socket (pode ser sobrescrito pela env
     * STORM_BRIDGE_SOCK antes do primeiro uso).
     */
    const char* GetSocketPath();
}
