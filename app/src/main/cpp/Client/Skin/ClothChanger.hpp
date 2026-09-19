#pragma once

/*
 * ============================================================================
 * ClothChanger.hpp — (V9) SKIN CHANGER (porta do SkinChangerLogic)
 * ============================================================================
 *
 * Porta da lógica do SkinChanger (ClothChanger do BR) para o painel:
 *   - Resolve o singleton AvatarWardrobeDataManager (TypeInfo
 *     auto-resolvido pelo scan do daemon — imune a update do jogo);
 *   - Aplica a skin sobrescrevendo o bloco visual (4 recipe hashes)
 *     dos itens da MESMA categoria no banco do armário + reescreve o
 *     iID, e empurra o recipe hash direto na UMA do player local;
 *   - ANTI-REVERT: a cada tick verifica se o jogo reverteu um item
 *     patchado e regrava (o jogo NÃO reverte sem isso).
 *
 * Segurança (aprendida do original):
 *   - NUNCA escrever lastBuildNotFinish (loader trava em 99%);
 *   - Match ativa: só escreve RECIPES (full dirty = crash/travamento);
 *   - Lobby: full dirty (mesh/texture) é o que faz o boneco rebuildar.
 * ============================================================================
 */

#include <cstdint>
#include <string>
#include <vector>

namespace Skin
{
    /*
     * Categorias da UI — a ordem TEM que casar com a tabela
     * kCategoryDefs do ClothChanger.cpp.
     */
    enum SkinCategory
    {
        CAT_TOP = 0,        // 203 — Top (wardrobe slot 3)
        CAT_BOTTOM,         // 204 — Calça (slot 4)
        CAT_SHOES,          // 205 — Sapato (slot 5)
        CAT_HAIR,           // 211 — Cabelo/Cabeça (slots 11 + 1)
        CAT_MASK,           // 211 — Máscara/óculos (slot 8)
        CAT_FACE,           // 214 — Pintura facial (slot 14)
        CAT_COUNT
    };

    struct AppliedItem
    {
        uint32_t ClothID = 0;
        int Category = 0;
        std::string Name;
    };

    /*
     * Chamado UMA vez (idempotente). Carrega/valida a tabela estática.
     */
    bool Initialize();

    /*
     * Tick por frame (vindo de Data::Draw). matchActive = estado real
     * do jogo (m_State em [1..3]). localPlayer = player local atual
     * (0 = fora de partida). Barato: com nada aplicado ele retorna
     * imediatamente.
     */
    void Tick(bool n32, bool matchActive, uintptr_t localPlayer);

    /*
     * Aplica (ou remove, se já aplicada) uma skin. IDs vêm da tabela
     * kSkinItems (SkinItems.h). Retorna true se o estado mudou.
     */
    bool ApplyOrToggle(uint32_t clothID, const char* name, int category);

    /*
     * Remove tudo de uma categoria (restaura os originais patchados).
     */
    bool RemoveCategory(int category);

    const std::vector<AppliedItem>& GetApplied();

    bool IsApplied(uint32_t clothID);

    /*
     * Status curto para a UI ("DB ok · 2 aplicados", "sem DB", ...).
     */
    const char* GetStatusText();
}
