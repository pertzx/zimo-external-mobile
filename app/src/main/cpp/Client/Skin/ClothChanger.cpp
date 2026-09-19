/*
 * ============================================================================
 * ClothChanger.cpp — (V9) SKIN CHANGER — lógica portada do SkinChangerLogic
 * (Client/ClothChanger.cpp do BR) para a ponte pread64/pwrite64.
 * ============================================================================
 */

#include "ClothChanger.hpp"
#include "SkinItems.h"

#include <Globals.hpp>
#include <Memory/Memory.hpp>
#include <Offsets/Offsets.hpp>
#include <Shared/WindowsCompat.hpp>
#include <android/log.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#define SKIN_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "StormSkin", __VA_ARGS__)
#define SKIN_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "StormSkin", __VA_ARGS__)

namespace Skin
{
    namespace
    {
        // ====================Defs de categoria ====================

        struct CategoryDef
        {
            const char* Name;
            uint32_t IdPrefix;      // filtro da UI pelo prefixo do iID
        };

        /*
         * Wardrobe types do dump: Head=1, Chest=3, Legs=4, Feet=5,
         * Face/máscara=8, Hair=11, Set=12, HeadAdd=14. Cabelo e Máscara
         * compartilham o prefixo 211 — o tipo REAL do item vem do DB vivo
         * (AvatarWardrobeData.wardrobeType) no ApplyOrToggle, e o patch
         * casa vítimas pelo wardrobeType do ALVO (robusto contra
         * prefixo enganoso).
         */
        const CategoryDef kCategoryDefs[CAT_COUNT] =
        {
            { "Top",        203 },
            { "Calca",      204 },
            { "Sapato",     205 },
            { "Cabeca",     211 },
            { "Mascara",    211 },
            { "Rosto",      214 },
        };

        /*
         * Índices no m_Recipes[] derivados do wardrobeType do alvo:
         *   wt 11 (cabelo) -> também escreve o slot 1 (head), como o
         *   original. Demais tipos -> o próprio wt.
         */
        inline void DeriveRecipeSlots(uint8_t wt, uint8_t out[2])
        {
            if (wt == 11)
            {
                out[0] = 11;
                out[1] = 1;
            }
            else if (wt != 0)
            {
                out[0] = wt;
                out[1] = 0xFF;
            }
            else
            {
                out[0] = 0xFF;
                out[1] = 0xFF;
            }
        }

        // ==================== Estado ====================

        struct Victim
        {
            uintptr_t Ptr = 0;              // endereço do AvatarWardrobeData
            int32_t OrigVisual[4] = { 0, 0, 0, 0 };
            uint32_t OrigIID = 0;
        };

        struct CategoryPatch
        {
            bool Active = false;
            bool Female = false;
            uint32_t TargetID = 0;
            uint8_t TargetWt = 0;           // wardrobeType REAL do alvo
            uint8_t RecipeSlots[2] = { 0xFF, 0xFF };
            uintptr_t TargetPtr = 0;
            int32_t TargetVisual[4] = { 0, 0, 0, 0 };
            std::vector<Victim> Victims;
        };

        std::mutex s_Mutex;
        std::vector<AppliedItem> s_Applied;
        CategoryPatch s_Patches[CAT_COUNT];

        char s_Status[96] = { 0 };

        /*
         * Player local injetado pelo Tick (a cadeia da partida já foi
         * resolvida lá pelo Data::Draw antes de chamar).
         */
        uintptr_t g_ContextLocalPlayer = 0;

        // ==================== Primitivas ====================

        inline uintptr_t ReadP(uintptr_t addr, bool n32)
        {
            if (addr == 0)
                return 0;

            return n32
                ? (uintptr_t)g_FreeFireMemory.Read<uint32_t>(addr)
                : (uintptr_t)g_FreeFireMemory.Read<uint64_t>(addr);
        }

        inline bool IsGuestPtr(uintptr_t v, bool n32)
        {
            if (v == 0)
                return false;

            if (n32)
                return v >= 0x01000000ull && v <= 0xB0000000ull;

            return v >= 0x100000000ull;
        }

        inline int ListItemsStride(bool n32) { return n32 ? 4 : 8; }
        inline uintptr_t ListItemsOff(bool n32) { return n32 ? 0x8 : 0x10; }
        inline uintptr_t ListCountOff(bool n32) { return n32 ? 0xC : 0x18; }
        inline uintptr_t ArrayDataOff(bool n32) { return n32 ? 0x10 : 0x20; }
        inline uintptr_t ArrayCountOff(bool n32) { return n32 ? 0xC : 0x18; }

        inline bool ReadVisual(uintptr_t ptr, int32_t out[4])
        {
            return g_FreeFireMemory.Read(
                ptr + Offsets::AvatarWardrobeData::VisualBase,
                out,
                sizeof(int32_t) * 4);
        }

        inline bool WriteVisual(uintptr_t ptr, const int32_t v[4])
        {
            return g_FreeFireMemory.Write(
                ptr + Offsets::AvatarWardrobeData::VisualBase,
                v,
                sizeof(int32_t) * 4);
        }

        // ==================== Resolução de cadeias ====================

        /*
         * statics[0] = instância do AvatarWardrobeDataManager.
         * Cache de 500 ms (a instância só morre se o jogo fechar o modo).
         */
        uintptr_t ResolveWardrobeInstance(bool n32)
        {
            static uintptr_t s_Cached = 0;
            static LONGLONG s_At = 0;

            const LONGLONG now = (LONGLONG)GetTickCount64();

            if (s_Cached != 0 && now - s_At < 500)
                return s_Cached;

            s_Cached = 0;
            s_At = now;

            if (Offsets::LibIl2Cpp == 0 ||
                Offsets::AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo == 0 ||
                Offsets::AccessClass == 0)
                return 0;

            const uintptr_t klass = ReadP(
                Offsets::LibIl2Cpp +
                Offsets::AvatarWardrobeDataManager::AvatarWardrobeDataManager_TypeInfo,
                n32);

            if (klass == 0)
                return 0;

            const uintptr_t statics = ReadP(klass + Offsets::AccessClass, n32);

            if (statics == 0)
                return 0;

            const uintptr_t inst = ReadP(statics, n32);   // statics[0]

            if (inst == 0)
                return 0;

            s_Cached = inst;

            return inst;
        }

        struct WardrobeList
        {
            uintptr_t Items = 0;   // base do array de dados (data[0])
            int Count = 0;
            int Stride = 4;
        };

        /*
         * instância -> m_dictIdToWardrobeDataTree (IntervalTreeDic) ->
         * m_Data (List) -> _items -> data.
         */
        bool ResolveWardrobeList(bool n32, WardrobeList& out)
        {
            if (Offsets::AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree == 0 ||
                Offsets::IntervalTreeDic::m_Data == 0)
                return false;

            const uintptr_t inst = ResolveWardrobeInstance(n32);

            if (inst == 0)
                return false;

            const uintptr_t tree = ReadP(
                inst + Offsets::AvatarWardrobeDataManager::m_dictIdToWardrobeDataTree,
                n32);

            if (tree == 0)
                return false;

            const uintptr_t list = ReadP(tree + Offsets::IntervalTreeDic::m_Data, n32);

            if (list == 0)
                return false;

            const uintptr_t items = ReadP(list + ListItemsOff(n32), n32);

            if (items == 0)
                return false;

            const int count = g_FreeFireMemory.Read<int>(list + ListCountOff(n32));

            if (count < 1 || count > 300000)
                return false;

            out.Items = items + ArrayDataOff(n32);
            out.Count = count;
            out.Stride = ListItemsStride(n32);

            return true;
        }

        // ==================== Patch de categoria ====================

        /*
         * Vítima = item do MESMO wardrobeType do alvo (com fallback por
         * prefixo quando o wt veio 0). ID do item é ignorado de propósito:
         * a categoria de verdade é o tipo do alvo lido do DB vivo.
         */
        bool MatchesVictim(uint32_t iID, uint8_t wt, const CategoryPatch& cp)
        {
            if (wt == cp.TargetWt)
                return true;

            if (wt == 0 && cp.TargetWt == 0)
                return true;

            return false;
        }

        bool FindTargetByID(const WardrobeList& wl, bool n32,
                            uint32_t clothID, uintptr_t& outPtr,
                            int32_t outVisual[4], uint8_t& outWt)
        {
            for (int i = 0; i < wl.Count; ++i)
            {
                const uintptr_t slot = wl.Items + (uintptr_t)i * wl.Stride;

                const uintptr_t ptr = ReadP(slot, n32);

                if (ptr == 0)
                    continue;

                uint32_t iID = 0;

                if (!g_FreeFireMemory.Read<uint32_t>(ptr + Offsets::AvatarWardrobeData::iID, iID))
                    continue;

                if (iID != clothID)
                    continue;

                if (!ReadVisual(ptr, outVisual))
                    continue;

                outWt = g_FreeFireMemory.Read<uint8_t>(ptr + Offsets::AvatarWardrobeData::wardrobeType);
                outPtr = ptr;

                return true;
            }

            return false;
        }

        /*
         * Varre o DB inteiro sobrescrevendo o bloco visual de TODOS os
         * itens da categoria (menos o alvo). É o PatchCategory do
         * original — é o que faz a skin valer no lobby/match e sobreviver
         * a re-equip.
         */
        bool PatchCategory(int category, bool n32)
        {
            CategoryPatch& cp = s_Patches[category];

            if (!cp.Active || cp.TargetID == 0)
                return false;

            WardrobeList wl;

            if (!ResolveWardrobeList(n32, wl))
                return false;

            cp.Victims.clear();

            int patched = 0;

            for (int i = 0; i < wl.Count; ++i)
            {
                const uintptr_t slot = wl.Items + (uintptr_t)i * wl.Stride;
                const uintptr_t ptr = ReadP(slot, n32);

                if (ptr == 0 || ptr == cp.TargetPtr)
                    continue;

                uint32_t iID = 0;

                if (!g_FreeFireMemory.Read<uint32_t>(ptr + Offsets::AvatarWardrobeData::iID, iID))
                    continue;

                if (iID == 0 || iID > 300000000u)
                    continue;

                const uint8_t wt =
                    g_FreeFireMemory.Read<uint8_t>(ptr + Offsets::AvatarWardrobeData::wardrobeType);

                if (!MatchesVictim(iID, wt, cp))
                    continue;

                Victim v;
                v.Ptr = ptr;
                v.OrigIID = iID;

                if (!ReadVisual(ptr, v.OrigVisual))
                    continue;

                if (!WriteVisual(ptr, cp.TargetVisual))
                    continue;

                g_FreeFireMemory.Write<uint32_t>(
                    ptr + Offsets::AvatarWardrobeData::iID,
                    cp.TargetID);

                cp.Victims.push_back(v);
                ++patched;

                if (patched >= 512)      // cinto de segurança
                    break;
            }

            SKIN_LOGI("PatchCategory[%s] alvo=%u vítimas=%d",
                      kCategoryDefs[category].Name, cp.TargetID, patched);

            return true;
        }

        // ==================== UMA (empurrar recipes) ====================

        /*
         * UMA do player local via cadeia da partida:
         * localPlayer -> m_AvatarManager -> m_Avatar.
         */
        uintptr_t ResolveLocalUma(bool n32)
        {
            if (Offsets::Player::m_AvatarManager == 0 || Offsets::AvatarManager::m_Avatar == 0)
                return 0;

            const uintptr_t mgr = ReadP(g_ContextLocalPlayer, n32);

            if (mgr == 0)
                return 0;

            const uintptr_t uma = ReadP(mgr + Offsets::AvatarManager::m_Avatar, n32);

            if (uma == 0)
                return 0;

            /*
             * Validação barata: m_Recipes tem que ser um array com len
             * plausível (8..64) — evita escrever em lixo quando o offset
             * não serve pra esse build.
             */
            const uintptr_t arr = ReadP(uma + Offsets::UmaAvatarSimple::m_Recipes, n32);

            if (arr == 0)
                return 0;

            const int len = g_FreeFireMemory.Read<int>(arr + ArrayCountOff(n32));

            if (len < 8 || len > 64)
                return 0;

            return uma;
        }

        void PushRecipesToUma(uintptr_t uma, bool n32)
        {
            if (uma == 0)
                return;

            const uintptr_t arr =
                ReadP(uma + Offsets::UmaAvatarSimple::m_Recipes, n32);

            if (arr == 0)
                return;

            const int len =
                g_FreeFireMemory.Read<int>(arr + ArrayCountOff(n32));

            if (len < 8 || len > 64)
                return;

            const uintptr_t data = arr + ArrayDataOff(n32);

            for (int cat = 0; cat < CAT_COUNT; ++cat)
            {
                const CategoryPatch& cp = s_Patches[cat];

                if (!cp.Active || cp.TargetID == 0)
                    continue;

                /*
                 * Hash de IN-GAME (índice 1 = masculino, 3 = feminino).
                 * A female detectada em ApplyOrToggle escolhe o índice.
                 */
                const int hashIdx = cp.Female ? 3 : 1;

                for (int i = 0; i < 2; ++i)
                {
                    const uint8_t slot = cp.RecipeSlots[i];

                    if (slot == 0xFF)
                        break;

                    if ((int)slot >= len)
                        continue;

                    g_FreeFireMemory.Write<int32_t>(
                        data + (uintptr_t)slot * sizeof(int32_t),
                        cp.TargetVisual[hashIdx]);
                }
            }
        }

        // ==================== Tick internals ====================

        void RefreshPatches(bool n32)
        {
            /*
             * Anti-revert: verifica os primeiros N vítimas por tick
             * (round-robin) e regrava quem o jogo reverteu.
             */
            static int s_Rot = 0;
            const int kMaxChecksPerTick = 16;

            int checked = 0;

            for (int cat = 0; cat < CAT_COUNT && checked < kMaxChecksPerTick; ++cat)
            {
                CategoryPatch& cp = s_Patches[cat];

                if (!cp.Active || cp.Victims.empty())
                    continue;

                const size_t sz = cp.Victims.size();
                const int start = s_Rot % (int)sz;

                for (size_t k = 0; k < sz && checked < kMaxChecksPerTick; ++k)
                {
                    const Victim& v =
                        cp.Victims[(start + (int)k) % (int)sz];

                    ++checked;

                    if (v.Ptr == 0)
                        continue;

                    int32_t cur[4] = { 0, 0, 0, 0 };

                    if (!ReadVisual(v.Ptr, cur))
                        continue;

                    if (cur[0] != cp.TargetVisual[0] ||
                        cur[1] != cp.TargetVisual[1] ||
                        cur[2] != cp.TargetVisual[2] ||
                        cur[3] != cp.TargetVisual[3])
                    {
                        WriteVisual(v.Ptr, cp.TargetVisual);
                        g_FreeFireMemory.Write<uint32_t>(
                            v.Ptr + Offsets::AvatarWardrobeData::iID,
                            cp.TargetID);
                    }
                }

                ++s_Rot;
            }
        }

        void RestoreCategory(int category, bool n32)
        {
            CategoryPatch& cp = s_Patches[category];

            if (!cp.Active)
                return;

            WardrobeList wl;

            if (ResolveWardrobeList(n32, wl))
            {
                for (const Victim& v : cp.Victims)
                {
                    if (v.Ptr == 0)
                        continue;

                    WriteVisual(v.Ptr, v.OrigVisual);
                    g_FreeFireMemory.Write<uint32_t>(
                        v.Ptr + Offsets::AvatarWardrobeData::iID,
                        v.OrigIID);
                }
            }

            cp.Active = false;
            cp.TargetID = 0;
            cp.TargetPtr = 0;
            cp.Victims.clear();

            SKIN_LOGI("RestoreCategory[%s]", kCategoryDefs[category].Name);
        }

        void UpdateStatus(bool n32)
        {
            int active = 0;

            for (int c = 0; c < CAT_COUNT; ++c)
                if (s_Patches[c].Active) ++active;

            snprintf(
                s_Status, sizeof(s_Status),
                "DB %u skins · %d aplicada(s) · %s",
                (unsigned)kSkinItemCount,
                active,
                n32 ? "32-bit" : "64-bit");
        }
    }

    // ==================== API pública ====================

    bool Initialize()
    {
        static bool s_Done = false;

        if (!s_Done)
        {
            s_Done = true;
            SKIN_LOGI("SkinChanger init: %u skins na tabela", (unsigned)kSkinItemCount);
        }

        return s_Done;
    }

    bool ApplyOrToggle(uint32_t clothID, const char* name, int category)
    {
        std::lock_guard<std::mutex> lk(s_Mutex);

        if (category < 0 || category >= CAT_COUNT)
            return false;

        const bool n32 = g_Globals.General.N32;

        // Já aplicada? -> remove (toggle).
        for (size_t i = 0; i < s_Applied.size(); ++i)
        {
            if (s_Applied[i].ClothID == clothID)
            {
                const int cat = s_Applied[i].Category;

                s_Applied.erase(s_Applied.begin() + (long)i);
                RestoreCategory(cat, n32);
                UpdateStatus(n32);

                return true;
            }
        }

        WardrobeList wl;

        if (!ResolveWardrobeList(n32, wl))
        {
            SKIN_LOGW("ApplyOrToggle: DB do armário não resolveu (lobby sem dados? TypeInfo errado?)");
            UpdateStatus(n32);
            return false;
        }

        CategoryPatch& cp = s_Patches[category];

        uintptr_t targetPtr = 0;
        int32_t targetVisual[4] = { 0, 0, 0, 0 };
        uint8_t wt = 0;

        if (!FindTargetByID(wl, n32, clothID, targetPtr, targetVisual, wt))
        {
            SKIN_LOGW("ApplyOrToggle: id=%u nao achado no DB vivo", clothID);
            UpdateStatus(n32);
            return false;
        }

        // Restaura a categoria anterior antes de aplicar a nova.
        RestoreCategory(category, n32);

        cp.Active = true;
        cp.TargetID = clothID;
        cp.TargetWt = wt;
        DeriveRecipeSlots(wt, cp.RecipeSlots);
        cp.TargetPtr = targetPtr;
        cp.TargetVisual[0] = targetVisual[0];
        cp.TargetVisual[1] = targetVisual[1];
        cp.TargetVisual[2] = targetVisual[2];
        cp.TargetVisual[3] = targetVisual[3];

        // Detecta female pelo player local (hash _F obrigatório na UMA F).
        cp.Female = false;

        if (g_ContextLocalPlayer != 0 && Offsets::Player::IsFemale != 0)
        {
            cp.Female = g_FreeFireMemory.Read<bool>(
                g_ContextLocalPlayer + Offsets::Player::IsFemale);
        }

        PatchCategory(category, n32);

        AppliedItem ai;
        ai.ClothID = clothID;
        ai.Category = category;
        ai.Name = (name != nullptr) ? name : "?";

        s_Applied.push_back(ai);

        UpdateStatus(n32);

        return true;
    }

    bool RemoveCategory(int category)
    {
        std::lock_guard<std::mutex> lk(s_Mutex);

        if (category < 0 || category >= CAT_COUNT)
            return false;

        const bool n32 = g_Globals.General.N32;

        RestoreCategory(category, n32);

        for (auto it = s_Applied.begin(); it != s_Applied.end(); )
        {
            if (it->Category == category)
                it = s_Applied.erase(it);
            else
                ++it;
        }

        UpdateStatus(n32);

        return true;
    }

    const std::vector<AppliedItem>& GetApplied()
    {
        return s_Applied;
    }

    bool IsApplied(uint32_t clothID)
    {
        for (const AppliedItem& a : s_Applied)
            if (a.ClothID == clothID)
                return true;

        return false;
    }

    const char* GetStatusText()
    {
        return s_Status;
    }

    void Tick(bool n32, bool matchActive, uintptr_t localPlayer)
    {
        g_ContextLocalPlayer = localPlayer;

        /*
         * Com nada aplicado, custo zero.
         */
        bool any = false;

        for (int c = 0; c < CAT_COUNT; ++c)
            if (s_Patches[c].Active) { any = true; break; }

        if (!any)
            return;

        const LONGLONG now = (LONGLONG)GetTickCount64();

        static LONGLONG s_LastTick = 0;
        static bool s_LastMatch = false;

        /*
         * Transição de match (loading -> dentro): re-patch com ponteiros
         * frescos (o DB do armário sobrevive, mas a UMA é nova).
         */
        if (matchActive != s_LastMatch)
        {
            s_LastMatch = matchActive;

            const bool n = n32;

            for (int c = 0; c < CAT_COUNT; ++c)
            {
                if (s_Patches[c].Active)
                {
                    // ponteiros do DB são estáveis — só reconfirma
                    (void)n;
                }
            }

            SKIN_LOGI("match gate: %s", matchActive ? "dentro" : "fora");
        }

        /*
         * Ritmo do anti-revert: a cada ~100 ms é suficiente (o jogo só
         * reverte em eventos: re-equip, loading, match end).
         */
        if (now - s_LastTick < 100)
            return;

        s_LastTick = now;

        RefreshPatches(n32);

        /*
         * Empurra os recipes na UMA do player local (a cada pulse — é
         * o "LivePushAll" do original, versão enxuta).
         */
        const uintptr_t uma = ResolveLocalUma(n32);

        if (uma != 0)
            PushRecipesToUma(uma, n32);

        /*
         * Lobby (sem match): full dirty pra forçar rebuild do boneco.
         * DENTRO da partida NUNCA (guarda de crash do original).
         */
        if (!matchActive && uma != 0)
        {
            int mask = 0;

            for (int cat = 0; cat < CAT_COUNT; ++cat)
            {
                const CategoryPatch& cp = s_Patches[cat];

                if (!cp.Active)
                    continue;

                for (int i = 0; i < 2; ++i)
                {
                    const uint8_t slot = cp.RecipeSlots[i];

                    if (slot == 0xFF)
                        break;

                    mask |= (1 << slot);
                }
            }

            if (mask != 0)
            {
                const int cur = g_FreeFireMemory.Read<int>(
                    uma + Offsets::UmaAvatarSimple::m_ChangedSlots);

                g_FreeFireMemory.Write<int>(
                    uma + Offsets::UmaAvatarSimple::m_ChangedSlots,
                    cur | mask);

                g_FreeFireMemory.Write<bool>(
                    uma + Offsets::UmaAvatarSimple::m_CustomTextureDirty,
                    true);

                /*
                 * umaData (mesh/texture dirty) — via UMAAvatarBase::umaData.
                 */
                const uintptr_t umaData =
                    ReadP(uma + Offsets::UMAAvatarBase::umaData, n32);

                if (umaData != 0 &&
                    Offsets::UMAData::isMeshDirty != 0 &&
                    Offsets::UMAData::isTextureDirty != 0)
                {
                    g_FreeFireMemory.Write<bool>(
                        umaData + Offsets::UMAData::isMeshDirty, true);
                    g_FreeFireMemory.Write<bool>(
                        umaData + Offsets::UMAData::isTextureDirty, true);
                }
            }
        }
    }
}
