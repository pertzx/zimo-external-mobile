#include "ClothChanger.hpp"
#include "GameMemory.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    // AvatarWardrobeData: recipe hashes + ext indices + setID (keep iID @ 0x28)
    constexpr uint32_t kVisualOff = 0x08;
    constexpr uint32_t kVisualSize = 0x20; // 0x08 .. 0x27
    constexpr uint32_t kWardrobeTypeOff = 0x2Cu;
    constexpr uint32_t kClothClassRva = 0xA987F88u;

    // Match / local player chain (v7a — same as Lunar External)
    constexpr uint32_t kInitBaseRva = 0xA986E9Cu;
    constexpr uint32_t kInitBaseJsonRva = 0xABFF6E0u;
    constexpr uint32_t kStaticClass = 0x5Cu;
    constexpr uint32_t kCurrentGame = 0x10u;
    constexpr uint32_t kCurrentMatch = 0x50u;
    constexpr uint32_t kLocalPlayer = 0x94u;
    constexpr uint32_t kAvatarManager = 0x4C0u;
    constexpr uint32_t kUmaAvatar = 0xA8u;       // AvatarManager.IUmaAvatar
    constexpr uint32_t kIsFemale = 0x7C1u;

    // UmaAvatarSimple / UMAAvatarBase
    constexpr uint32_t kUmaData = 0x14u;
    constexpr uint32_t kMRecipes = 0x6Cu;
    constexpr uint32_t kMVisibleSlots = 0x70u;
    constexpr uint32_t kMChangedSlots = 0x74u;
    constexpr uint32_t kLastBuildNotFinish = 0x78u;
    constexpr uint32_t kCustomTextureDirty = 0x94u;
    constexpr uint32_t kUmaMeshDirty = 0x36u;
    constexpr uint32_t kUmaTextureDirty = 0x38u;

    // UIAvatar / UIStatedAvatar (lobby preview)
    constexpr uint32_t kUIAvatar_IAvatar = 0x48u;
    constexpr uint32_t kUIAvatar_IsLocal = 0x61u;
    constexpr uint32_t kUIAvatar_PendingBuild = 0x79u;
    constexpr uint32_t kUIAvatar_IsDirty = 0xAAu;
    constexpr uint32_t kUIAvatar_IsFemale = 0xABu;
    constexpr uint32_t kUIStated_Internal = 0x284u;
    constexpr uint32_t kUIStated_CurrentState = 0x288u;
    constexpr uint32_t kInternal_Dirty = 0x51u;
    constexpr uint32_t kInternal_LastSelect = 0x54u;
    constexpr uint32_t kUIState_Clothes = 0x1Cu;
    constexpr uint32_t kUIState_UseLobbyRecipes = 0x19u;
    constexpr uint32_t kUIState_AvatarID = 0x08u;

    // UIMaleAvatar.GetLocalAvatar — code RVA (dump)
    constexpr uint32_t kGetLocalAvatarRva = 0x1A4BAF0u;

    // Alternate local-player chains (lobby / observer)
    constexpr uint32_t kLocalChain = 0xB4u;
    constexpr uint32_t kNestedLP = 0x28u;

    int g_ForceLiveFrames = 0;
    int g_LiveSwapPulses = 0; // extra rebuild kicks after a panel click (live swap)
    bool g_WasInMatch = false;
    bool g_NeedRebindPatches = false; // stale TargetPtr/Patched after match transition

    // Match-load freeze (anti-99% / anti-crash): while partida is loading, touch
    // NOTHING. Lobby object pointers go stale as the match allocates; writing them
    // is what closed the game on match entry.
    enum class MatchGate : uint8_t { Lobby = 0, Loading = 1, Settled = 2 };
    MatchGate g_MatchGate = MatchGate::Lobby;
    int g_MatchLoadFrames = 0;
    int g_MatchStableFrames = 0;
    bool g_MatchSawBuilding = false;
    constexpr int kMatchStableNeed = 90;    // ~1.5s stable after build finishes
    constexpr int kMatchLoadTimeout = 900;  // ~15s fallback
    int g_MatchQuietCooldown = 0; // frames to skip aggressive push after settle

    uint32_t g_MaleAvatarStaticFields = 0; // UIMaleAvatar static fields (cached)
    bool g_MaleAvatarCacheLoaded = false;
    bool g_MaleAvatarScanDone = false;
    int g_MaleAvatarScanDelta = -0x50000; // progress through BSS near wardrobe MethodInfo
    int g_MaleAvatarCachedDelta = 0x7FFFFFFF;
    constexpr int kAvatarScanMin = -0x50000;
    constexpr int kAvatarScanMax = 0xA0000;
    constexpr int kAvatarScanChunk = 0x3000; // ~3KB per tick — no UI freeze
    constexpr const char* kAvatarDeltaFile = "avatar_mi.delta";

    // EWardrobeType
    constexpr uint8_t kWtHead = 1;
    constexpr uint8_t kWtChest = 3;
    constexpr uint8_t kWtLegs = 4;
    constexpr uint8_t kWtFeet = 5;
    constexpr uint8_t kWtFace = 8;
    constexpr uint8_t kWtHair = 11;
    constexpr uint8_t kWtSet = 12;
    constexpr uint8_t kWtHeadAdd = 14;

    struct PatchedObj
    {
        uint32_t Ptr = 0;
        uint32_t ItemID = 0;
        std::array<uint8_t, kVisualSize> Original{};
    };

    struct CategoryPatch
    {
        uint32_t TargetClothID = 0;
        uint32_t TargetPtr = 0;
        std::vector<PatchedObj> Patched;
        std::unordered_set<int32_t> OldRecipeHashes;
        std::unordered_map<int32_t, int32_t> RecipeRemap; // old hash -> new hash
        // AvatarWardrobeData @ +0x08:
        // [0] recipeHashInLobby (M), [1] recipeHashInGame (M),
        // [2] recipeHashInLobby_F,   [3] recipeHashInGame_F
        int32_t NewHashes[4]{};
        char TargetGender = '?'; // 'f' / 'm' / '?' from icon (female_/male_)
        bool IsFullBody = false;  // onesie / jumpsuit — needs Chest+Set, clears Legs/Feet
    };

    std::vector<ClothEntry> g_DB;
    std::vector<TrajeEntry> g_Trajes;
    std::vector<TrajeEntry> g_Passes;
    std::vector<TrajeEntry> g_Famous;
    std::vector<AppliedCloth> g_Applied;
    std::unordered_map<int, CategoryPatch> g_CatPatches; // key = ClothCategory
    std::unordered_map<int, std::vector<size_t>> g_CategoryCache;

    // Ground-truth slot per itemID, scanned from the game's wardrobe DB (wardrobeType
    // byte @ +0x2C). clothes.json has no slot field, so icon heuristics used to mix
    // hair with masks. Once this is populated we recategorize everything from it.
    std::unordered_map<uint32_t, uint8_t> g_LiveWardrobeType;
    bool g_WardrobeScanned = false;
    bool g_Loaded = false;
    std::string g_Status = "Not loaded";
    std::string g_MemStatus = "Idle";
    int g_TickCounter = 0;

    static std::string ExtractField(const std::string& obj, const char* key)
    {
        const std::string needle = std::string("\"") + key + "\"";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos)
            return {};

        pos = obj.find(':', pos + needle.size());
        if (pos == std::string::npos)
            return {};

        pos = obj.find('"', pos + 1);
        if (pos == std::string::npos)
            return {};

        size_t end = obj.find('"', pos + 1);
        if (end == std::string::npos)
            return {};

        return obj.substr(pos + 1, end - pos - 1);
    }

    static bool ContainsIgnoreCase(const std::string& hay, const std::string& needle)
    {
        if (needle.empty())
            return true;

        auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
        return it != hay.end();
    }

    static std::string StripSlotSuffix(const std::string& desc)
    {
        static const char* suffixes[] = {
            " (Top)", " (Bottom)", " (Shoes)", " (Head)", " (Mask)", " (Facepaint)",
            " (Pants)", " (Hair)", " (Jacket)", " (Shorts)", " (Boots)", " (Robe)",
            " (Footwear)", " (Jersey)", " (Female)", " (Male)"
        };
        for (const char* s : suffixes)
        {
            const size_t n = std::strlen(s);
            if (desc.size() > n && desc.compare(desc.size() - n, n, s) == 0)
                return desc.substr(0, desc.size() - n);
        }
        return {};
    }

    static ClothCategory CategoryFromEntry(const ClothEntry& e);

    static bool StartsWithIgnoreCase(const std::string& hay, const char* prefix)
    {
        if (!prefix)
            return false;
        const size_t n = std::strlen(prefix);
        if (hay.size() < n)
            return false;
        for (size_t i = 0; i < n; ++i)
        {
            if (std::tolower(static_cast<unsigned char>(hay[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        }
        return true;
    }

    static bool LooksLikeJumpsuit(const ClothEntry& e)
    {
        if (ContainsIgnoreCase(e.IconName, "jumpsuit") || ContainsIgnoreCase(e.IconName, "moneyheist"))
            return true;
        if (ContainsIgnoreCase(e.Description, "Top Criminal"))
            return true;
        if (ContainsIgnoreCase(e.Description, "Plan Bermuda"))
            return true;
        if (ContainsIgnoreCase(e.Description, "Crimson Criminal") || ContainsIgnoreCase(e.Description, "Red Robster"))
            return true;
        return false;
    }

    static bool IsPassTierRarity(const std::string& rarity)
    {
        return ContainsIgnoreCase(rarity, "PURPLE_PLUS")
            || ContainsIgnoreCase(rarity, "ORANGE")
            || ContainsIgnoreCase(rarity, "RED");
    }

    static std::string ToLowerCopy(const std::string& s)
    {
        std::string o = s;
        for (char& c : o)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return o;
    }

    static char IconGender(const std::string& icon)
    {
        const std::string l = ToLowerCopy(icon);
        if (l.find("female") != std::string::npos)
            return 'f';
        if (l.find("male") != std::string::npos)
            return 'm';
        return '?';
    }

    static std::string ExtractIconTheme(const std::string& icon)
    {
        if (icon.empty())
            return {};
        const std::string l = ToLowerCopy(icon);
        static const char* markers[] = {
            "cos_top_", "top_cos_", "cos_bottom_", "bottom_cos_",
            "cos_shoe_", "shoe_cos_", "cos_shoes_", "shoes_cos_",
            "cos_hair_", "hair_cos_", "headadditive_", "cos_headadditive_",
            "cos_accessory_", "accessory_cos_"
        };
        size_t bestPos = std::string::npos;
        size_t bestStart = 0;
        for (const char* m : markers)
        {
            const size_t p = l.rfind(m);
            if (p == std::string::npos)
                continue;
            if (bestPos == std::string::npos || p > bestPos)
            {
                bestPos = p;
                bestStart = p + std::strlen(m);
            }
        }
        if (bestPos == std::string::npos || bestStart >= l.size())
            return {};
        return l.substr(bestStart);
    }

    // Full-body one-piece costume icons: Icon_avatar_[male_/female_]cos_XXX
    // where XXX is not a slot (top/bottom/shoe/hair/accessory/headadditive).
    // Also classic "suit" onesies (Schoolgirl, etc.): Icon_avatar_female_suit_*.
    static bool IsFullBodyCosIcon(const std::string& icon)
    {
        static const char* prefixes[] = {
            "Icon_avatar_cos_", "Icon_avatar_male_cos_", "Icon_avatar_female_cos_",
            "Icon_avatar_suit_", "Icon_avatar_male_suit_", "Icon_avatar_female_suit_"
        };
        for (const char* p : prefixes)
        {
            if (!StartsWithIgnoreCase(icon, p))
                continue;
            const std::string rest = ToLowerCopy(icon.substr(std::strlen(p)));
            // suit_* is always a full-body garment
            if (ContainsIgnoreCase(p, "suit_"))
                return true;
            static const char* slots[] = {
                "top", "bottom", "shoe", "hair", "accessory", "headadditive", "tshirt"
            };
            for (const char* s : slots)
            {
                if (rest.compare(0, std::strlen(s), s) == 0)
                    return false;
            }
            return true;
        }
        return false;
    }

    // One-piece trajes (Top Criminal, Plan Bermuda street, etc.)
    static bool IsCompleteOutfitSolo(const ClothEntry& e)
    {
        const uint32_t pref = e.ItemID / 1000000u;
        if (pref != 203 && pref != 212)
            return false;
        if (!StripSlotSuffix(e.Description).empty())
            return false;
        if (!LooksLikeJumpsuit(e))
            return false;
        if (ContainsIgnoreCase(e.Description, "Shinobi") || ContainsIgnoreCase(e.Description, "Kunoichi"))
            return false;
        return true;
    }

    static TrajeEntry MakeOutfitFromIndices(const std::string& name, const std::vector<size_t>& indices)
    {
        TrajeEntry t;
        t.Name = name;
        t.Rarity = "WHITE";
        t.IconItemID = 0;
        t.PassNumber = 0;
        for (size_t idx : indices)
        {
            const ClothEntry& e = g_DB[idx];
            t.PieceIDs.push_back(e.ItemID);
            const ClothCategory c = CategoryFromEntry(e);
            if (c == ClothCategory::Top || t.IconItemID == 0)
            {
                t.IconItemID = e.ItemID;
                t.Rarity = e.Rarity;
            }
        }
        if (t.IconItemID == 0 && !t.PieceIDs.empty())
            t.IconItemID = t.PieceIDs.front();
        return t;
    }

    static bool OutfitHasTop(const TrajeEntry& t)
    {
        for (uint32_t id : t.PieceIDs)
        {
            if (id / 1000000u == 203 || id / 1000000u == 212)
                return true;
        }
        return false;
    }

    static uint32_t OutfitMinId(const TrajeEntry& t)
    {
        if (t.PieceIDs.empty())
            return 0;
        return *std::min_element(t.PieceIDs.begin(), t.PieceIDs.end());
    }

    // A set's pieces (top/bottom/shoes/...) share the same icon theme+color, only the
    // slot token differs: Icon_avatar_male_top_cos_japanesestyle_purple,
    // Icon_avatar_male_bottom_cos_japanesestyle_purple, ...  This returns the shared key
    // ("cos_japanesestyle_purple") so we can auto-complete an outfit even when the pieces
    // are named differently (e.g. "Way of the Bushido" top + "Bushido Bottom").
    static std::string IconSetKey(const std::string& iconRaw)
    {
        std::string s;
        s.reserve(iconRaw.size());
        for (char c : iconRaw)
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        size_t start;
        size_t pos = s.find("icon_avatar_");
        if (pos != std::string::npos)
            start = pos + 12; // strlen("icon_avatar_")
        else
        {
            pos = s.find("icon_");
            if (pos == std::string::npos)
                return "";
            start = pos + 5;
        }

        std::vector<std::string> toks;
        std::string cur;
        for (size_t i = start; i < s.size(); ++i)
        {
            if (s[i] == '_')
            {
                if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            }
            else
                cur.push_back(s[i]);
        }
        if (!cur.empty())
            toks.push_back(cur);
        if (toks.empty())
            return "";

        size_t t = 0;
        if (toks[t] == "male" || toks[t] == "female")
            ++t;
        static const char* slotToks[] = {
            "top","tops","bottom","bottoms","shoe","shoes","foot","footwear","hair",
            "accessory","headadditive","headadd","mask","tshirt","shirt","head",
            "glasses","face","facepaint"
        };
        if (t < toks.size())
        {
            for (const char* st : slotToks)
            {
                if (toks[t] == st) { ++t; break; }
            }
        }

        std::string key;
        for (size_t i = t; i < toks.size(); ++i)
        {
            if (!key.empty()) key += '_';
            key += toks[i];
        }
        return key;
    }

    static ClothCategory CategoryFromWardrobeType(uint8_t wt, bool& known)
    {
        known = true;
        switch (wt)
        {
        case 3:  return ClothCategory::Top;      // Chest
        case 4:  return ClothCategory::Bottom;   // Legs
        case 5:  return ClothCategory::Shoes;    // Feet
        case 8:  return ClothCategory::Mask;     // Face slot = glasses/masks in FF
        case 11: return ClothCategory::Head;     // Hair (and hats)
        case 1:  return ClothCategory::Head;     // legacy Head (helmets)
        case 12: return ClothCategory::Traje;    // Set
        case 14: return ClothCategory::Facepaint;// HeadAdditive = facepaint overlays
        default: known = false; return ClothCategory::Other;
        }
    }

    static ClothCategory CategoryFromEntry(const ClothEntry& e)
    {
        const std::string& d = e.Description;

        // Authoritative name tags first (covers 203045xxx "(Mask)" etc.)
        if (d.find("(Facepaint)") != std::string::npos)
            return ClothCategory::Facepaint;
        if (d.find("(Mask)") != std::string::npos || d.find("(Glasses)") != std::string::npos)
            return ClothCategory::Mask;

        // Definitive itemID prefixes. These take precedence over the live wardrobeType:
        // full-body one-piece costumes are prefix 203 but the game reports a "Set/costume"
        // wardrobeType for them. They MUST be applied as a Top (slot 3) — that renders the
        // whole costume. Sending them to the Set slot (12) via wardrobeType left them
        // "activated but invisible" (no traje showed up in lobby or match).
        switch (e.ItemID / 1000000u)
        {
        case 203: return ClothCategory::Top;
        case 204: return ClothCategory::Bottom;
        case 205: return ClothCategory::Shoes;
        case 212: return ClothCategory::Traje;
        case 214: return ClothCategory::Facepaint; // 214 = facepaint (HeadAdditive overlay)
        case 211:
        {
            // Hair/hats vs masks/glasses is the only genuinely ambiguous case, so here we
            // DO trust the live wardrobeType when available (11/1=Hair/Head, 8=Face=Mask).
            auto wtIt = g_LiveWardrobeType.find(e.ItemID);
            if (wtIt != g_LiveWardrobeType.end())
            {
                const uint8_t wt = wtIt->second;
                if (wt == kWtHair || wt == kWtHead) return ClothCategory::Head;
                if (wt == kWtFace)                  return ClothCategory::Mask;
                if (wt == kWtHeadAdd)               return ClothCategory::Facepaint;
            }
            if (ContainsIgnoreCase(e.IconName, "accessory") ||
                ContainsIgnoreCase(e.IconName, "blinder"))
                return ClothCategory::Mask;
            if (ContainsIgnoreCase(e.IconName, "hair"))
                return ClothCategory::Head;
            if (d.size() >= 5 && d.compare(d.size() - 5, 5, " Mask") == 0)
                return ClothCategory::Mask;
            if (d.find(" Mask ") != std::string::npos || d.find(" Mask(") != std::string::npos)
                return ClothCategory::Mask;
            return ClothCategory::Head;
        }
        default:
        {
            auto wtIt = g_LiveWardrobeType.find(e.ItemID);
            if (wtIt != g_LiveWardrobeType.end())
            {
                bool known = false;
                const ClothCategory c = CategoryFromWardrobeType(wtIt->second, known);
                if (known)
                    return c;
            }
            return ClothCategory::Other;
        }
        }
    }

    static bool MatchesCategory(uint32_t itemID, uint8_t wardrobeType, int category)
    {
        const uint32_t prefix = itemID / 1000000u;
        switch (static_cast<ClothCategory>(category))
        {
        case ClothCategory::Top:
            // Chest slot, OR Set slot for 203 one-piece costumes (wardrobeType often=12).
            // Without Set, female onesies never got patched and looked inactive.
            return wardrobeType == kWtChest
                || (wardrobeType == kWtSet && (itemID / 1000000u) == 203);
        case ClothCategory::Bottom:
            return wardrobeType == kWtLegs;
        case ClothCategory::Shoes:
            return wardrobeType == kWtFeet;
        case ClothCategory::Head:
            // Hats live in Hair (most) or Head (some helmets) — never HeadAdd
            return wardrobeType == kWtHair || wardrobeType == kWtHead;
        case ClothCategory::Mask:
            // Glasses/masks live in the Face slot in FF — never Hair/HeadAdd
            return wardrobeType == kWtFace;
        case ClothCategory::Facepaint:
            // Facepaint overlays live in HeadAdditive (icons say "headadditive")
            return wardrobeType == kWtHeadAdd || (wardrobeType == 0 && prefix == 214);
        case ClothCategory::Traje:
            return wardrobeType == kWtSet || (wardrobeType == 0 && prefix == 212);
        default:
            (void)prefix;
            return false;
        }
    }

    static ClothCategory CategoryOfItemID(uint32_t itemID)
    {
        auto it = std::find_if(g_DB.begin(), g_DB.end(),
            [&](const ClothEntry& e) { return e.ItemID == itemID; });
        if (it != g_DB.end())
            return CategoryFromEntry(*it);
        ClothEntry tmp;
        tmp.ItemID = itemID;
        return CategoryFromEntry(tmp);
    }

    static bool NameMatchesCurated(const std::string& desc, const std::vector<std::string>& matchList)
    {
        const std::string base = StripSlotSuffix(desc);
        const std::string& key = base.empty() ? desc : base;
        auto prefixOk = [](const std::string& keyOrDesc, const std::string& m) -> bool {
            if (keyOrDesc.size() < m.size() || keyOrDesc.compare(0, m.size(), m) != 0)
                return false;
            if (keyOrDesc.size() == m.size())
                return true;
            const char next = keyOrDesc[m.size()];
            // "Name (Top)" / "Name - Neon" / "Plan Bermuda Shinobi"
            if (next == '(' || next == '-' || next == ' ')
                return true;
            // match already includes '(': "Top Criminal (Blue)"
            if (!m.empty() && m.back() == '(')
                return true;
            return false;
        };
        for (const std::string& m : matchList)
        {
            if (m.empty())
                continue;
            if (key == m || desc == m)
                return true;
            if (prefixOk(key, m) || prefixOk(desc, m))
                return true;
        }
        return false;
    }

    static std::vector<size_t> CollectMatchingIndices(const std::vector<std::string>& matchList)
    {
        std::vector<size_t> out;
        for (size_t i = 0; i < g_DB.size(); ++i)
        {
            if (NameMatchesCurated(g_DB[i].Description, matchList))
                out.push_back(i);
        }
        return out;
    }

    // Curated Free Fire Elite Pass + Booyah Pass outfits (researched seasons) + real trajes.
    static void RebuildCategoryCache()
    {
        g_CategoryCache.clear();
        g_Trajes.clear();
        g_Passes.clear();
        g_Famous.clear();

        for (size_t i = 0; i < g_DB.size(); ++i)
        {
            const ClothCategory cat = CategoryFromEntry(g_DB[i]);
            if (cat != ClothCategory::Other && cat != ClothCategory::Traje)
                g_CategoryCache[static_cast<int>(cat)].push_back(i);
        }

        struct OutfitDef { const char* name; std::vector<const char*> match; };
        struct SeasonDef { int num; const char* type; const char* theme; std::vector<OutfitDef> outfits; };

        // Elite Pass (EP) seasons 1-55 + Booyah Pass (BP) — only verified names present in clothes.json.
        // Originals first; "(Elite)" = re-release version sold later.
        const SeasonDef seasons[] = {
            {1, "EP", "Kitsune", {
                {"Way of the Bushido", {"Way of the Bushido", "Bushido Bottom", "Bushido Footwear", "Oni Mask"}},
                {"Kitsune", {"Kitsune Robe", "Kitsune Bottom", "Kitsune Footwear", "Kitsune Mask"}},
                {"Sakura Blossom", {"Sakura Top", "Sakura Bottom", "Sakura Shoes", "Sakura Headwear"}},
                {"Way of the Bushido (Elite)", {"Elite Way of the Bushido"}},
                {"Kitsune (Elite)", {"Elite Kitsune"}},
            }},
            {2, "EP", "Hip Hop", {
                {"The Streets", {"The Streets"}},
                {"Pink Love", {"Pink Love"}},
                {"The Streets (Elite)", {"Elite Streets"}},
                {"Pink Love (Elite)", {"Elite Pink Love"}},
            }},
            {3, "EP", "Doomsday Madness", {
                {"Harbinger", {"Harbinger"}},
                {"Miss Doombringer", {"Miss Doombringer"}},
                {"Harbinger (Elite)", {"Elite Harbinger"}},
                {"Miss Doombringer (Elite)", {"Elite Miss Doombringer"}},
            }},
            {4, "EP", "Royal Revelry", {
                {"Royal Ceremony", {"Royal Ceremony"}},
                {"Trigger Happy", {"Trigger Happy"}},
                {"Royal Ceremony (Elite)", {"Elite Royal Ceremony"}},
                {"Trigger Happy (Elite)", {"Elite Trigger Happy"}},
            }},
            {5, "EP", "Pirates Legend", {
                {"Captain's Order", {"Captain's Order"}},
                {"Pirate's Fantasy", {"Pirate's Fantasy"}},
                {"Captain's Order (Elite)", {"Elite Captain's Order"}},
                {"Pirate's Fantasy (Elite)", {"Elite Pirate's Fantasy"}},
            }},
            {6, "EP", "Arcade Mayhem", {
                {"8 Bit", {"8 Bit"}},
                {"Digital Girl", {"Digital Girl"}},
            }},
            {7, "EP", "Steampunk Revolution", {
                {"Steampunk", {"Steampunk"}},
                {"Alchemist", {"Alchemist"}},
            }},
            {9, "EP", "Bomb Squad", {
                {"Bomb Squad", {"Bomb Squad"}},
                {"Explosive Suit", {"Explosive Suit"}},
            }},
            {11, "EP", "Dragon Slayers", {
                {"Dragon Slayer", {"Dragon Slayer"}},
            }},
            {17, "EP", "Blood Demon", {
                {"Ruby Demon", {"Ruby Demon"}},
                {"Red Samurai", {"Red Samurai"}},
            }},
            {19, "EP", "Ghost Pirates", {
                {"Skull Captain", {"Skull Captain"}},
            }},
            {20, "EP", "Shadow Combat", {
                {"Clandestine Vanquisher", {"Clandestine Vanquisher"}},
                {"Clandestine Vindicator", {"Clandestine Vindicator"}},
            }},
            {22, "EP", "Wasteland Survivors", {
                {"Doomsday Raider", {"Doomsday Raider"}},
                {"Doomsday Ravager", {"Doomsday Ravager"}},
            }},
            {23, "EP", "Agent Paws", {
                {"Agent Tail", {"Agent Tail"}},
                {"Agent Kitty", {"Agent Kitty"}},
            }},
            {24, "EP", "Forsaken Creed", {
                {"Unseen Custodian", {"Unseen Custodian"}},
                {"Shadow Custodian", {"Shadow Custodian"}},
            }},
            {25, "EP", "Fabled Fox", {
                {"Kitsune's Riposte", {"Kitsune's Riposte"}},
                {"Kitsune's Revenge", {"Kitsune's Revenge"}},
            }},
            {26, "EP", "Rampage II", {
                {"Turmoil of Ruins", {"Turmoil of Ruins"}},
                {"Kiss of Ruins", {"Kiss of Ruins"}},
            }},
            {27, "EP", "Sushi Menace", {
                {"Sashimi Slasher", {"Sashimi Slasher"}},
                {"Ramen Slayer", {"Ramen Slayer"}},
            }},
            {29, "EP", "Anubis Legends II", {
                {"Relic Guardian", {"Relic Guardian"}},
                {"Relic Monarch", {"Relic Monarch"}},
            }},
            {30, "EP", "Ultrasonic Rave", {
                {"Stage Master", {"Stage Master ("}},
                {"Stage Starlet", {"Stage Starlet"}},
            }},
            {31, "EP", "Endless Oblivion", {
                {"Endless Black", {"Endless Black"}},
                {"Endless White", {"Endless White"}},
            }},
            {32, "EP", "Specter Squad", {
                {"Specter Raider", {"Specter Raider"}},
                {"Specter Basher", {"Specter Basher"}},
            }},
            {34, "EP", "Willful Wonders", {
                {"Willful Dame", {"Willful Dame"}},
            }},
            {35, "EP", "Bloodwing City", {
                {"Bloodwing Lad", {"Bloodwing Lad"}},
                {"Bloodwing Lass", {"Bloodwing Lass"}},
            }},
            {36, "EP", "Manic Circus", {
                {"Night Clown", {"Night Clown"}},
                {"Wicked Jester", {"Wicked Jester"}},
            }},
            {37, "EP", "Evil Enchanted", {
                {"Prince Afterdark", {"Prince Afterdark"}},
                {"Princess Afterdark", {"Princess Afterdark"}},
            }},
            {38, "EP", "Guns for Hire", {
                {"Regis Gunslinger", {"Regis Gunslinger"}},
                {"Royal Gunslinger", {"Royal Gunslinger"}},
            }},
            {39, "EP", "Wildland Walkers", {
                {"Blazing Scarecrow", {"Blazing Scarecrow"}},
                {"Fiery Scarecrow", {"Fiery Scarecrow"}},
            }},
            {41, "EP", "Mesmerizing Knights", {
                {"Persia Prowess", {"Persia Prowess"}},
                {"Persia Valor", {"Persia Valor"}},
            }},
            {45, "EP", "Papyrus Rebel", {
                {"Platinum Odette", {"Platinum Odette"}},
            }},
            {48, "EP", "Checkered Nobility", {
                {"Checkmate Knight", {"Checkmate Knight"}},
                {"Checkmate Dame", {"Checkmate Dame"}},
            }},
            {49, "EP", "Swordsoul Reality", {
                {"Cybersword Neon", {"Cybersword Neon"}},
                {"Cybersword Ember", {"Cybersword Ember"}},
            }},
            {50, "EP", "Bumble Rumblers", {
                {"Cyborg Piercer", {"Cyborg Piercer"}},
                {"Cyberoid Stinger", {"Cyberoid Stinger"}},
            }},
            {51, "EP", "The Kung-Foodies", {
                {"Crimson Knifemaster", {"Crimson Knifemaster"}},
                {"Cherry Chefmaster", {"Cherry Chefmaster"}},
            }},
            {52, "EP", "Deep Sea Warriors", {
                {"Megajaw Tormentor", {"Megajaw Tormentor"}},
                {"Megafin Taunter", {"Megafin Taunter"}},
            }},
            {53, "EP", "Jutsu Elemental", {
                {"Firestorm Shinobi", {"Firestorm Shinobi"}},
                {"Windfrost Shinobi", {"Windfrost Shinobi"}},
            }},
            {54, "EP", "Voltage Vengeance", {
                {"Gnarl Electrocution", {"Gnarl Electrocution"}},
                {"Monstrous Shock", {"Monstrous Shock"}},
            }},
            {55, "EP", "Avalanche Abyss", {
                {"Wizard of Blizzards", {"Wizard of Blizzards"}},
                {"Witch of Glaciers", {"Witch of Glaciers"}},
            }},
            {99, "EP", "Elite Extra", {
                {"Elite Hunter", {"Elite Hunter"}},
                {"Operation Elite", {"Operation Elite"}},
                {"Contingency Elite", {"Contingency Elite"}},
                {"Power of Booyah", {"Power of Booyah"}},
                {"Spirit of Booyah", {"Spirit of Booyah"}},
                {"Booyah Captain", {"Booyah Captain"}},
                {"Booyah Leader", {"Booyah Leader"}},
                {"Solar Commander", {"Solar Commander"}},
                {"Star General", {"Star General"}},
            }},
            // Booyah Pass (from Jan 2023) — only outfits verified in clothes.json
            {1, "BP", "Fumes on Fire", {
                {"Bang Bang", {"Bang Bang"}},
                {"Hidden Blast", {"Hidden Blast"}},
            }},
            {2, "BP", "Fatal Fauna", {
                {"Crocotamer", {"Crocotamer"}},
                {"Crocodarer", {"Crocodarer"}},
            }},
            {3, "BP", "The Biotroopers", {
                {"Project Azure", {"Project Azure"}},
                {"Project Ether", {"Project Ether"}},
                {"Project Cosmos", {"Project Cosmos"}},
                {"Project Qualia", {"Project Qualia"}},
            }},
            {5, "BP", "Neon Drifterz", {
                {"Cobalt Drifter", {"Cobalt Drifter"}},
                {"Crimson Drifter", {"Crimson Drifter"}},
            }},
            {7, "BP", "T.R.A.P. City", {
                {"Strapped Trap", {"Strapped Trap"}},
                {"Strapped Trance", {"Strapped Trance"}},
            }},
            {9, "BP", "Jelly Assault", {
                {"Jelly Delight", {"Jelly Delight"}},
                {"Jelly Possession", {"Jelly Possession"}},
                {"Jelly Ready", {"Jelly Ready"}},
            }},
            {10, "BP", "Fishing Frenzy", {
                {"Reeling Angler", {"Reeling Angler"}},
            }},
            {11, "BP", "Rise of the Puppets", {
                {"Marionette Fantasy", {"Marionette Fantasy"}},
                {"Marionette Soul", {"Marionette Soul"}},
                {"Marionette Void", {"Marionette Void"}},
                {"Marionette Wonder", {"Marionette Wonder"}},
            }},
            {12, "BP", "Frostfire", {
                {"Frostfire Magma", {"Frostfire Magma"}},
                {"Frostfire Polar", {"Frostfire Polar"}},
            }},
            {18, "BP", "Twilight's End", {
                {"Crystal Twilight", {"Crystal Twilight"}},
                {"Golden Twilight", {"Golden Twilight"}},
                {"Twilight Bolt", {"Twilight Bolt"}},
            }},
            {19, "BP", "Lucky Goosy", {
                {"Goosy Delight", {"Goosy Delight"}},
                {"Goosy Stroll", {"Goosy Stroll"}},
            }},
            {25, "BP", "Mad Stitcher", {
                {"Stitched Tailor", {"Stitched Tailor"}},
                {"Stitched Tailoress", {"Stitched Tailoress"}},
            }},
            {28, "BP", "Moonlit Venture", {
                {"Moonlit Cotton", {"Moonlit Cotton"}},
            }},
        };

        // Index every DB piece by its icon "set key" so we can auto-complete outfits.
        std::unordered_map<std::string, std::vector<size_t>> byIconKey;
        for (size_t i = 0; i < g_DB.size(); ++i)
        {
            std::string k = IconSetKey(g_DB[i].IconName);
            if (k.size() >= 10 && k.find("default") == std::string::npos)
                byIconKey[k].push_back(i);
        }

        int passOrder = 0;
        for (const SeasonDef& season : seasons)
        {
            for (const OutfitDef& od : season.outfits)
            {
                std::vector<std::string> match;
                match.reserve(od.match.size());
                for (const char* m : od.match)
                    match.emplace_back(m);

                std::vector<size_t> idxs = CollectMatchingIndices(match);
                if (idxs.empty())
                    continue;

                // Drop variant heads ("... - Neon", "... - Zeal", "... -Flame" etc.) — keep the core set
                {
                    std::vector<size_t> core;
                    for (size_t idx : idxs)
                    {
                        if (g_DB[idx].Description.find(" -") != std::string::npos)
                            continue;
                        core.push_back(idx);
                    }
                    if (!core.empty())
                        idxs.swap(core);
                }

                // Auto-complete: pull every piece that shares an icon set key with a seed
                // piece, so the pass equips ALL its pieces even when they are named
                // differently ("Way of the Bushido" top → "Bushido Bottom"/"Footwear").
                {
                    std::unordered_set<size_t> have(idxs.begin(), idxs.end());
                    std::unordered_set<std::string> keys;
                    for (size_t idx : idxs)
                    {
                        std::string k = IconSetKey(g_DB[idx].IconName);
                        if (k.size() >= 10 && k.find("default") == std::string::npos)
                            keys.insert(std::move(k));
                    }
                    for (const std::string& k : keys)
                    {
                        auto mit = byIconKey.find(k);
                        if (mit == byIconKey.end())
                            continue;
                        // Skip over-generic buckets (e.g. "blackwhite_01" = 57 recolors)
                        // that aren't a single outfit set.
                        if (mit->second.size() > 14)
                            continue;
                        for (size_t idx : mit->second)
                        {
                            if (g_DB[idx].Description.find(" -") != std::string::npos)
                                continue;
                            if (have.insert(idx).second)
                                idxs.push_back(idx);
                        }
                    }
                }

                TrajeEntry t = MakeOutfitFromIndices(od.name, idxs);
                if (!OutfitHasTop(t))
                    continue;

                ++passOrder;
                t.PassNumber = season.num > 0 ? season.num : passOrder;
                char label[160];
                std::snprintf(label, sizeof(label), "%s%02d %s · %s",
                    season.type, season.num, season.theme, od.name);
                t.Name = label;
                g_Passes.push_back(std::move(t));
            }
        }

        // Guarantee complete outfits: originals whose bundle is incomplete in the DB
        // get ONLY missing categories from the "(Elite)" twin — never replace an
        // already-filled slot (that was mixing peitoral from the Elite re-release).
        for (auto& t : g_Passes)
        {
            static const std::string kEliteSuffix = " (Elite)";
            if (t.Name.size() >= kEliteSuffix.size()
                && t.Name.compare(t.Name.size() - kEliteSuffix.size(), kEliteSuffix.size(), kEliteSuffix) == 0)
                continue;

            const std::string twinName = t.Name + kEliteSuffix;
            const TrajeEntry* twin = nullptr;
            for (const auto& o : g_Passes)
            {
                if (o.Name == twinName)
                {
                    twin = &o;
                    break;
                }
            }
            if (!twin)
                continue;

            std::unordered_set<int> haveCats;
            for (uint32_t id : t.PieceIDs)
            {
                const ClothCategory c = CategoryOfItemID(id);
                if (c != ClothCategory::Other && c != ClothCategory::Traje)
                    haveCats.insert(static_cast<int>(c));
            }

            for (uint32_t id : twin->PieceIDs)
            {
                const ClothCategory c = CategoryOfItemID(id);
                if (c == ClothCategory::Other || c == ClothCategory::Traje)
                    continue;
                // Only fill gaps; keep Elite piece as Alt fallback for that missing cat
                if (haveCats.count(static_cast<int>(c)))
                    continue;
                if (std::find(t.PieceIDs.begin(), t.PieceIDs.end(), id) == t.PieceIDs.end())
                {
                    t.PieceIDs.push_back(id);
                    haveCats.insert(static_cast<int>(c));
                }
            }
        }

        // Trajes: only real jumpsuits / Top Criminal / Plan Bermuda (researched)
        struct TrajeDef { const char* label; std::vector<const char*> match; bool eachVariant; };
        const TrajeDef trajeDefs[] = {
            {"Top Criminal", {"Top Criminal ("}, true}, // exclude "Top Criminal Mask"
            {"Plan Bermuda", {"Plan Bermuda"}, true},
            {"Crimson Criminal", {"Crimson Criminal"}, false},
            {"Red Robster", {"Red Robster"}, false},
        };

        for (const TrajeDef& td : trajeDefs)
        {
            std::vector<std::string> match;
            for (const char* m : td.match)
                match.emplace_back(m);
            std::vector<size_t> idxs = CollectMatchingIndices(match);
            if (idxs.empty())
                continue;

            if (td.eachVariant)
            {
                // One entry per unique full description (Top Criminal Red, Ghost, etc.)
                std::map<std::string, std::vector<size_t>> byDesc;
                for (size_t idx : idxs)
                    byDesc[g_DB[idx].Description].push_back(idx);

                // Also group Plan Bermuda Shinobi pieces by strip base
                std::map<std::string, std::vector<size_t>> byBase;
                for (size_t idx : idxs)
                {
                    const std::string base = StripSlotSuffix(g_DB[idx].Description);
                    if (!base.empty())
                        byBase[base].push_back(idx);
                }

                // Multi-piece variants (Shinobi/Kunoichi)
                for (auto& kv : byBase)
                {
                    if (kv.second.size() < 2)
                        continue;
                    bool hasTop = false, hasBottom = false;
                    for (size_t idx : kv.second)
                    {
                        const ClothCategory c = CategoryFromEntry(g_DB[idx]);
                        if (c == ClothCategory::Top) hasTop = true;
                        if (c == ClothCategory::Bottom) hasBottom = true;
                    }
                    if (!hasTop || !hasBottom)
                        continue;
                    TrajeEntry t = MakeOutfitFromIndices(kv.first, kv.second);
                    g_Trajes.push_back(std::move(t));
                    for (size_t idx : kv.second)
                        byDesc.erase(g_DB[idx].Description);
                }

                // Solo one-piece / remaining
                for (auto& kv : byDesc)
                {
                    TrajeEntry t = MakeOutfitFromIndices(kv.first, kv.second);
                    g_Trajes.push_back(std::move(t));
                }
            }
            else
            {
                TrajeEntry t = MakeOutfitFromIndices(td.label, idxs);
                g_Trajes.push_back(std::move(t));
            }
        }

        // All one-piece full-body costumes (Dino, coelhos, animal suits, macacoes...)
        {
            std::unordered_set<uint32_t> usedIds;
            std::unordered_set<std::string> seenNames;
            for (const auto& t : g_Trajes)
            {
                seenNames.insert(t.Name);
                for (uint32_t id : t.PieceIDs)
                    usedIds.insert(id);
            }
            for (const auto& t : g_Passes)
            {
                for (uint32_t id : t.PieceIDs)
                    usedIds.insert(id);
            }

            for (size_t i = 0; i < g_DB.size(); ++i)
            {
                const ClothEntry& e = g_DB[i];
                if (e.ItemID / 1000000u != 203)
                    continue;
                if (usedIds.count(e.ItemID))
                    continue;
                const std::string& d = e.Description;
                if (d.empty() || d == "Default")
                    continue;
                if (d.find('_') != std::string::npos) // drafts / internal names
                    continue;
                if (!StripSlotSuffix(d).empty()) // "(Top)" etc. => piece of a set
                    continue;
                const bool whitelisted = (d == "Dino" || d == "Crazy Panda");
                if (!whitelisted && !IsFullBodyCosIcon(e.IconName))
                    continue;
                // Plain garments that slipped through the icon rule
                const size_t hoodiePos = d.rfind("Hoodie");
                if (hoodiePos != std::string::npos && hoodiePos + 6 == d.size())
                    continue;
                // Keep male AND female variants (same display name, different icon gender).
                // Deduping by name alone dropped every female onesie that shared a title
                // with a male evolution / recolor that appeared first in the DB.
                {
                    const char g = IconGender(e.IconName);
                    const std::string key = d + "#" + g;
                    if (!seenNames.insert(key).second)
                        continue;
                }

                TrajeEntry t;
                t.Name = d;
                // Disambiguate when both genders exist under the same description
                if (IconGender(e.IconName) == 'f' && d.find("Female") == std::string::npos
                    && d.find("Kunoichi") == std::string::npos)
                {
                    // Only append if a male sibling is also a full-body candidate
                    bool hasMaleTwin = false;
                    for (const auto& o : g_DB)
                    {
                        if (o.Description != d || o.ItemID == e.ItemID)
                            continue;
                        if (IconGender(o.IconName) == 'm' && (IsFullBodyCosIcon(o.IconName) || whitelisted))
                        {
                            hasMaleTwin = true;
                            break;
                        }
                    }
                    if (hasMaleTwin)
                        t.Name = d + " (Female)";
                }
                t.Rarity = e.Rarity;
                t.IconItemID = e.ItemID;
                t.PieceIDs.push_back(e.ItemID);
                g_Trajes.push_back(std::move(t));
            }
        }

        std::sort(g_Trajes.begin(), g_Trajes.end(),
            [](const TrajeEntry& a, const TrajeEntry& b) {
                return a.Name < b.Name;
            });

        // "Famosos" tab removed: player-specific looks are fan-made combos with
        // Portuguese names and dozens of item variants, so they can't be mapped to
        // clothes.json reliably. g_Famous is intentionally left empty.
    }

    static bool TryGetInstance(uint32_t& instance, std::string& err)
    {
        instance = 0;
        if (!GameMemory::IsAttached())
        {
            err = "Not attached";
            return false;
        }
        if (!GameMemory::IsPtr32())
        {
            err = "Need armeabi-v7a (32-bit)";
            return false;
        }

        const uint64_t il2cpp = GameMemory::Il2CppBase();
        uint32_t baseMethod = 0;
        if (!GameMemory::ReadU32(il2cpp + kClothClassRva, baseMethod) || baseMethod == 0)
        {
            err = "RVA/class ptr fail";
            return false;
        }

        uint32_t methodInfo = 0;
        if (!GameMemory::ReadU32(baseMethod, methodInfo) || methodInfo == 0)
        {
            err = "methodInfo fail";
            return false;
        }

        uint32_t klass = 0;
        if (!GameMemory::ReadU32(methodInfo + 0x10u, klass) || klass == 0)
        {
            err = "klass fail";
            return false;
        }

        uint32_t staticFields = 0;
        if (!GameMemory::ReadU32(klass + 0x5Cu, staticFields) || staticFields == 0)
        {
            err = "staticFields fail";
            return false;
        }

        if (!GameMemory::ReadU32(staticFields, instance) || instance == 0)
        {
            err = "instance null (open lobby)";
            return false;
        }
        return true;
    }

    static bool TryGetMData(uint32_t& itemsArray, int32_t& count, std::string& err)
    {
        itemsArray = 0;
        count = 0;

        uint32_t instance = 0;
        if (!TryGetInstance(instance, err))
            return false;

        uint32_t treeDict = 0;
        if (!GameMemory::ReadU32(instance + 0x0Cu, treeDict) || treeDict == 0)
        {
            err = "treeDict fail";
            return false;
        }

        uint32_t mDataList = 0;
        if (!GameMemory::ReadU32(treeDict + 0x0Cu, mDataList) || mDataList == 0)
        {
            err = "mDataList fail";
            return false;
        }

        uint32_t items = 0;
        if (!GameMemory::ReadU32(mDataList + 0x08u, items) || items == 0)
        {
            err = "items array fail";
            return false;
        }

        if (!GameMemory::ReadI32(mDataList + 0x0Cu, count) || count <= 0)
        {
            err = "empty wardrobe list";
            return false;
        }

        itemsArray = items + 0x10u;
        return true;
    }

    static bool TryFindById(uint32_t itemsArray, int32_t count, uint32_t clothID, uint32_t& outPtr)
    {
        outPtr = 0;
        for (int32_t i = 0; i < count; ++i)
        {
            const uint32_t slot = itemsArray + static_cast<uint32_t>(i * 4);
            uint32_t dataPtr = 0;
            if (!GameMemory::ReadU32(slot, dataPtr) || dataPtr == 0)
                continue;

            uint32_t iID = 0;
            if (!GameMemory::ReadU32(dataPtr + 0x28u, iID) || iID != clothID)
                continue;

            outPtr = dataPtr;
            return true;
        }
        return false;
    }

    static void RestoreCategory(int category)
    {
        auto it = g_CatPatches.find(category);
        if (it == g_CatPatches.end())
            return;

        for (const auto& p : it->second.Patched)
        {
            if (p.Ptr == 0)
                continue;
            GameMemory::WriteBytes(p.Ptr + kVisualOff, p.Original.data(), kVisualSize);
            GameMemory::WriteU32(p.Ptr + 0x28u, p.ItemID);
        }
        g_CatPatches.erase(it);
    }

    enum class PatchResult { Ok, TargetMissing, WriteFail, NoItems };

    // Patch ALL wardrobe entries of this category with target recipes.
    // Whatever the player is wearing in that slot will look like the selected skin.
    static PatchResult PatchCategory(uint32_t itemsArray, int32_t count, int category, uint32_t targetClothID)
    {
        uint32_t targetPtr = 0;
        if (!TryFindById(itemsArray, count, targetClothID, targetPtr))
            return PatchResult::TargetMissing;

        std::array<uint8_t, kVisualSize> visual{};
        if (!GameMemory::ReadBytes(targetPtr + kVisualOff, visual.data(), kVisualSize))
            return PatchResult::WriteFail;

        RestoreCategory(category);

        CategoryPatch patch;
        patch.TargetClothID = targetClothID;
        patch.TargetPtr = targetPtr;
        patch.Patched.reserve(256);

        for (int32_t i = 0; i < count; ++i)
        {
            const uint32_t slot = itemsArray + static_cast<uint32_t>(i * 4);
            uint32_t dataPtr = 0;
            if (!GameMemory::ReadU32(slot, dataPtr) || dataPtr == 0)
                continue;

            uint32_t iID = 0;
            if (!GameMemory::ReadU32(dataPtr + 0x28u, iID) || iID == 0)
                continue;

            // Never overwrite the target object itself (keep a clean source)
            if (iID == targetClothID)
                continue;

            uint8_t wt = 0;
            GameMemory::ReadU8(dataPtr + kWardrobeTypeOff, wt);

            // Wardrobe type is ground truth (Mask=HeadAdd vs Head=Hair).
            // Preferring DB alone used to skip HeadAdd items mislabeled as Head,
            // so masks never patched the slot the player was actually wearing.
            if (MatchesCategory(iID, wt, category))
            {
                // ok
            }
            else
            {
                auto dbIt = std::find_if(g_DB.begin(), g_DB.end(),
                    [&](const ClothEntry& e) { return e.ItemID == iID; });
                if (dbIt == g_DB.end() || static_cast<int>(CategoryFromEntry(*dbIt)) != category)
                    continue;
                // DB agrees but wt didn't — only accept if wt is missing/unknown
                if (wt != 0)
                    continue;
            }

            PatchedObj obj;
            obj.Ptr = dataPtr;
            obj.ItemID = iID;
            if (!GameMemory::ReadBytes(dataPtr + kVisualOff, obj.Original.data(), kVisualSize))
                continue;

            if (!GameMemory::WriteBytes(dataPtr + kVisualOff, visual.data(), kVisualSize))
                return PatchResult::WriteFail;
            GameMemory::WriteU32(dataPtr + 0x28u, iID);

            int32_t oldH[4]{};
            std::memcpy(oldH, obj.Original.data(), sizeof(oldH));
            for (int h = 0; h < 4; ++h)
            {
                if (oldH[h] != 0)
                    patch.OldRecipeHashes.insert(oldH[h]);
            }

            patch.Patched.push_back(std::move(obj));
        }

        if (patch.Patched.empty())
            return PatchResult::NoItems;

        std::memcpy(patch.NewHashes, visual.data(), sizeof(patch.NewHashes));

        {
            auto dbIt = std::find_if(g_DB.begin(), g_DB.end(),
                [&](const ClothEntry& e) { return e.ItemID == targetClothID; });
            if (dbIt != g_DB.end())
            {
                patch.TargetGender = IconGender(dbIt->IconName);
                patch.IsFullBody = IsFullBodyCosIcon(dbIt->IconName) || IsCompleteOutfitSolo(*dbIt);
            }
        }

        // Pairwise remap: each old recipe hash -> corresponding new hash
        for (const auto& p : patch.Patched)
        {
            int32_t oldH[4]{};
            std::memcpy(oldH, p.Original.data(), sizeof(oldH));
            for (int h = 0; h < 4; ++h)
            {
                if (oldH[h] != 0 && patch.NewHashes[h] != 0)
                    patch.RecipeRemap[oldH[h]] = patch.NewHashes[h];
            }
        }

        g_CatPatches[category] = std::move(patch);
        return PatchResult::Ok;
    }

    static bool IsGuestPtr(uint32_t p)
    {
        // 32-bit Android guest heap. The old check (>= 0x10000) accepted almost any
        // garbage and we happily wrote into it → HD-Player closed on match entry.
        return p >= 0x01000000u && p < 0xB0000000u;
    }

    // Confirm an address still looks like the AvatarWardrobeData we patched.
    static bool WardrobePtrAlive(uint32_t ptr, uint32_t expectItemID)
    {
        if (!IsGuestPtr(ptr))
            return false;
        uint32_t iID = 0;
        if (!GameMemory::ReadU32(ptr + 0x28u, iID))
            return false;
        if (expectItemID != 0 && iID != expectItemID)
            return false;
        if (iID < 100000u || iID > 300000000u)
            return false;
        uint8_t wt = 0xFF;
        if (!GameMemory::ReadU8(ptr + kWardrobeTypeOff, wt))
            return false;
        return wt <= 20;
    }

    static bool UmaLooksValid(uint32_t uma)
    {
        if (!IsGuestPtr(uma))
            return false;
        uint32_t recipes = 0;
        if (!GameMemory::ReadU32(uma + kMRecipes, recipes) || !IsGuestPtr(recipes))
            return false;
        int32_t len = 0;
        if (!GameMemory::ReadI32(recipes + 0x0Cu, len) || len < 8 || len > 32)
            return false;
        int32_t vis = 0, chg = 0;
        if (!GameMemory::ReadI32(uma + kMVisibleSlots, vis))
            return false;
        if (!GameMemory::ReadI32(uma + kMChangedSlots, chg))
            return false;
        return true;
    }

    static void InvalidatePatchPointers()
    {
        for (auto& kv : g_CatPatches)
        {
            kv.second.TargetPtr = 0;
            for (auto& p : kv.second.Patched)
                p.Ptr = 0;
        }
        g_NeedRebindPatches = true;
    }

    static void RefreshCategoryIfNeeded(uint32_t itemsArray, int32_t count, int category)
    {
        auto it = g_CatPatches.find(category);
        if (it == g_CatPatches.end())
            return;

        // Stale after match transition — don't write until rebound
        if (it->second.TargetPtr == 0 || !WardrobePtrAlive(it->second.TargetPtr, it->second.TargetClothID))
        {
            uint32_t newPtr = 0;
            if (!TryFindById(itemsArray, count, it->second.TargetClothID, newPtr) ||
                !WardrobePtrAlive(newPtr, it->second.TargetClothID))
                return;
            it->second.TargetPtr = newPtr;
        }

        std::array<uint8_t, kVisualSize> visual{};
        if (!GameMemory::ReadBytes(it->second.TargetPtr + kVisualOff, visual.data(), kVisualSize))
            return;

        for (auto& p : it->second.Patched)
        {
            if (p.Ptr == 0 || !WardrobePtrAlive(p.Ptr, p.ItemID))
            {
                // Re-resolve this one item by ID; skip if gone
                uint32_t fresh = 0;
                if (!TryFindById(itemsArray, count, p.ItemID, fresh) || !WardrobePtrAlive(fresh, p.ItemID))
                {
                    p.Ptr = 0;
                    continue;
                }
                p.Ptr = fresh;
            }

            int32_t cur = 0, want = 0;
            if (!GameMemory::ReadI32(p.Ptr + kVisualOff, cur))
                continue;
            if (!GameMemory::ReadI32(it->second.TargetPtr + kVisualOff, want))
                continue;
            if (cur == want)
                continue;
            GameMemory::WriteBytes(p.Ptr + kVisualOff, visual.data(), kVisualSize);
            GameMemory::WriteU32(p.Ptr + 0x28u, p.ItemID);
        }
    }

    static int WardrobeSlotForCategory(int category)
    {
        switch (static_cast<ClothCategory>(category))
        {
        case ClothCategory::Top:       return kWtChest;
        case ClothCategory::Bottom:    return kWtLegs;
        case ClothCategory::Shoes:     return kWtFeet;
        case ClothCategory::Head:      return kWtHair;
        case ClothCategory::Mask:      return kWtFace;
        case ClothCategory::Facepaint: return kWtHeadAdd;
        case ClothCategory::Traje:     return kWtSet;
        default:                       return -1;
        }
    }

    static bool MethodPtrMatches(uint32_t stored, uint32_t want)
    {
        return (stored & ~1u) == (want & ~1u);
    }

    static bool TryMethodInfoToStatics(uint32_t methodInfo, uint32_t& outStaticFields)
    {
        outStaticFields = 0;
        if (!IsGuestPtr(methodInfo))
            return false;
        for (uint32_t koff : { 0x10u, 0x0Cu })
        {
            uint32_t klass = 0;
            if (!GameMemory::ReadU32(methodInfo + koff, klass) || !IsGuestPtr(klass))
                continue;
            uint32_t staticFields = 0;
            if (!GameMemory::ReadU32(klass + kStaticClass, staticFields) || !IsGuestPtr(staticFields))
                continue;
            // LocalAvatar @ +0, WaitedFrames @ +4
            int32_t waited = -1;
            GameMemory::ReadI32(staticFields + 0x4u, waited);
            if (waited < 0 || waited > 5000000)
                continue;
            outStaticFields = staticFields;
            return true;
        }
        return false;
    }

    static void SaveAvatarDelta(int delta)
    {
        std::ofstream f(kAvatarDeltaFile, std::ios::trunc);
        if (f)
            f << delta;
    }

    static void LoadAvatarDeltaCache()
    {
        if (g_MaleAvatarCacheLoaded)
            return;
        g_MaleAvatarCacheLoaded = true;
        std::ifstream f(kAvatarDeltaFile);
        if (!f)
            return;
        int delta = 0;
        if (f >> delta)
            g_MaleAvatarCachedDelta = delta;
    }

    static bool ConsiderAvatarMethodSlot(uint32_t slot, uint32_t want)
    {
        if (!IsGuestPtr(slot))
            return false;

        uint32_t mid = 0;
        if (GameMemory::ReadU32(slot, mid) && IsGuestPtr(mid))
        {
            uint32_t mp = 0;
            if (GameMemory::ReadU32(mid, mp) && MethodPtrMatches(mp, want))
            {
                uint32_t sf = 0;
                if (TryMethodInfoToStatics(mid, sf))
                {
                    g_MaleAvatarStaticFields = sf;
                    return true;
                }
            }
        }

        uint32_t mp2 = 0;
        if (GameMemory::ReadU32(slot, mp2) && MethodPtrMatches(mp2, want))
        {
            uint32_t sf = 0;
            if (TryMethodInfoToStatics(slot, sf))
            {
                g_MaleAvatarStaticFields = sf;
                return true;
            }
        }
        return false;
    }

    static bool EnsureMaleAvatarStatics()
    {
        if (g_MaleAvatarStaticFields != 0)
            return true;
        if (!GameMemory::IsAttached() || !GameMemory::IsPtr32())
            return false;

        LoadAvatarDeltaCache();

        const uint64_t il2 = GameMemory::Il2CppBase();
        const uint32_t want = static_cast<uint32_t>(il2 + kGetLocalAvatarRva);

        // Instant path: previously discovered MethodInfo slot delta
        if (g_MaleAvatarCachedDelta != 0x7FFFFFFF)
        {
            uint32_t slot = 0;
            if (GameMemory::ReadU32(il2 + kClothClassRva + static_cast<int64_t>(g_MaleAvatarCachedDelta), slot)
                && ConsiderAvatarMethodSlot(slot, want))
                return true;
            g_MaleAvatarCachedDelta = 0x7FFFFFFF; // stale
        }

        if (g_MaleAvatarScanDone)
            return false;

        // Incremental BSS scan near wardrobe MethodInfo (never blocks the UI)
        const int chunkEnd = (std::min)(g_MaleAvatarScanDelta + kAvatarScanChunk, kAvatarScanMax);
        for (int delta = g_MaleAvatarScanDelta; delta < chunkEnd; delta += 4)
        {
            uint32_t slot = 0;
            if (!GameMemory::ReadU32(il2 + kClothClassRva + static_cast<int64_t>(delta), slot))
                continue;
            if (!ConsiderAvatarMethodSlot(slot, want))
                continue;

            g_MaleAvatarCachedDelta = delta;
            SaveAvatarDelta(delta);
            g_MaleAvatarScanDone = true;
            return true;
        }

        g_MaleAvatarScanDelta = chunkEnd;
        if (g_MaleAvatarScanDelta >= kAvatarScanMax)
        {
            // wrap once more from min if never found — then stop
            if (g_MaleAvatarScanDelta == kAvatarScanMax && g_MaleAvatarScanDelta > kAvatarScanMin)
            {
                // finished full range
            }
            g_MaleAvatarScanDone = true;
        }
        return false;
    }

    static ClothCategory CategoryFromItemIDFast(uint32_t itemID)
    {
        auto it = std::find_if(g_DB.begin(), g_DB.end(), [&](const ClothEntry& e) { return e.ItemID == itemID; });
        if (it != g_DB.end())
            return CategoryFromEntry(*it);
        ClothEntry tmp;
        tmp.ItemID = itemID;
        return CategoryFromEntry(tmp);
    }

    static bool TryGetLobbyLocalAvatar(uint32_t& outAvatar, uint32_t& outUma, bool& outFemale)
    {
        outAvatar = 0;
        outUma = 0;
        outFemale = false;

        if (!EnsureMaleAvatarStatics())
            return false;

        uint32_t localAvatar = 0;
        if (!GameMemory::ReadU32(g_MaleAvatarStaticFields, localAvatar) || !IsGuestPtr(localAvatar))
            return false;

        outAvatar = localAvatar;

        uint8_t female = 0;
        GameMemory::ReadU8(localAvatar + kUIAvatar_IsFemale, female);
        outFemale = (female != 0);

        uint32_t uma = 0;
        if (GameMemory::ReadU32(localAvatar + kUIAvatar_IAvatar, uma) && IsGuestPtr(uma))
            outUma = uma;

        return outUma != 0 || outAvatar != 0;
    }

    static void ForceLobbyAvatarRefresh(uint32_t uiAvatar, int category, uint32_t targetClothID, uint32_t targetPtr)
    {
        if (!IsGuestPtr(uiAvatar))
            return;

        GameMemory::WriteU8(uiAvatar + kUIAvatar_PendingBuild, 1);
        GameMemory::WriteU8(uiAvatar + kUIAvatar_IsDirty, 1);

        uint32_t internalState = 0;
        if (GameMemory::ReadU32(uiAvatar + kUIStated_Internal, internalState) && IsGuestPtr(internalState))
        {
            GameMemory::WriteU8(internalState + kInternal_Dirty, 1);
            GameMemory::WriteU32(internalState + kInternal_LastSelect, targetClothID);
        }

        uint32_t state = 0;
        if (!GameMemory::ReadU32(uiAvatar + kUIStated_CurrentState, state) || !IsGuestPtr(state))
            return;

        GameMemory::WriteU8(state + kUIState_UseLobbyRecipes, 1);

        uint32_t clothesList = 0;
        if (!GameMemory::ReadU32(state + kUIState_Clothes, clothesList) || !IsGuestPtr(clothesList))
            return;

        uint32_t items = 0;
        int32_t count = 0;
        if (!GameMemory::ReadU32(clothesList + 0x08u, items) || !IsGuestPtr(items))
            return;
        if (!GameMemory::ReadI32(clothesList + 0x0Cu, count) || count <= 0 || count > 64)
            return;

        const uint32_t data = items + 0x10u;
        for (int32_t i = 0; i < count; ++i)
        {
            uint32_t val = 0;
            if (!GameMemory::ReadU32(data + static_cast<uint32_t>(i * 4), val) || val == 0)
                continue;

            if (IsGuestPtr(val))
            {
                uint32_t iID = 0;
                if (GameMemory::ReadU32(val + 0x28u, iID) && iID > 100000u)
                {
                    if (static_cast<int>(CategoryFromItemIDFast(iID)) == category && IsGuestPtr(targetPtr))
                        GameMemory::WriteU32(data + static_cast<uint32_t>(i * 4), targetPtr);
                }
                continue;
            }

            if (val > 100000u && val < 300000000u)
            {
                if (static_cast<int>(CategoryFromItemIDFast(val)) == category)
                    GameMemory::WriteU32(data + static_cast<uint32_t>(i * 4), targetClothID);
            }
        }
    }

    // GameEngine -> Match/Lobby player -> AvatarManager -> UmaAvatar(s)
    static void CollectLocalUmas(std::vector<uint32_t>& outUmas, bool& outFemale, bool& outInMatch)
    {
        outUmas.clear();
        outFemale = false;
        outInMatch = false;

        auto addUma = [&](uint32_t uma) {
            if (!IsGuestPtr(uma))
                return;
            if (std::find(outUmas.begin(), outUmas.end(), uma) == outUmas.end())
                outUmas.push_back(uma);
        };

        auto tryPlayer = [&](uint32_t lp, bool markMatch) {
            if (!IsGuestPtr(lp))
                return;
            // Player.IsFemale offsets vary by build (External tables: 0x7C1 / 0x7B1).
            // Dump field list around there is floats — treat any non-zero as a soft hint
            // and always trust UIAvatar.m_IsCurrentModelFemale (lobby) when available.
            uint8_t femaleA = 0, femaleB = 0;
            GameMemory::ReadU8(lp + kIsFemale, femaleA);
            GameMemory::ReadU8(lp + 0x7B1u, femaleB);
            if (femaleA || femaleB)
                outFemale = true;

            // Only the real AvatarManager / IUmaAvatar offsets. Speculative extras
            // (0x4B4/0x9C/...) matched random objects and writing recipes into them
            // crashed HD-Player on match entry.
            const uint32_t mgrOffs[] = { kAvatarManager };
            const uint32_t umaOffs[] = { kUmaAvatar };
            for (uint32_t mo : mgrOffs)
            {
                uint32_t avatarMgr = 0;
                if (!GameMemory::ReadU32(lp + mo, avatarMgr) || !IsGuestPtr(avatarMgr))
                    continue;
                for (uint32_t uo : umaOffs)
                {
                    uint32_t uma = 0;
                    if (!GameMemory::ReadU32(avatarMgr + uo, uma) || !UmaLooksValid(uma))
                        continue;
                    addUma(uma);
                    if (markMatch)
                        outInMatch = true;
                }
            }
        };

        const uint64_t il2cpp = GameMemory::Il2CppBase();
        const uint32_t rvas[] = { kInitBaseRva, kInitBaseJsonRva };

        for (uint32_t rva : rvas)
        {
            uint32_t step1 = 0;
            if (!GameMemory::ReadU32(il2cpp + rva, step1) || !IsGuestPtr(step1))
                continue;

            uint32_t staticFields = 0;
            if (!GameMemory::ReadU32(step1 + kStaticClass, staticFields) || !IsGuestPtr(staticFields))
                continue;

            uint32_t ge = 0;
            if (!GameMemory::ReadU32(staticFields, ge) || !IsGuestPtr(ge))
                continue;

            uint32_t partida = 0;
            uint32_t matchGame = 0;
            if (GameMemory::ReadU32(ge + kCurrentGame, matchGame) && IsGuestPtr(matchGame))
                GameMemory::ReadU32(matchGame + kCurrentMatch, partida);
            if (!IsGuestPtr(partida))
                GameMemory::ReadU32(ge + kCurrentMatch, partida);
            if (!IsGuestPtr(partida))
                continue;

            uint32_t lp = 0;
            GameMemory::ReadU32(partida + kLocalPlayer, lp);
            tryPlayer(lp, true);

            uint32_t chain = 0;
            if (GameMemory::ReadU32(partida + kLocalChain, chain) && IsGuestPtr(chain))
            {
                uint32_t nested = 0;
                if (GameMemory::ReadU32(chain + kNestedLP, nested))
                    tryPlayer(nested, true);
            }
        }
    }

    // True when a Match/Partida object exists (loading screen OR in-game).
    static bool TryGetCurrentPartida(uint32_t& outPartida)
    {
        outPartida = 0;
        const uint64_t il2cpp = GameMemory::Il2CppBase();
        if (!il2cpp)
            return false;
        const uint32_t rvas[] = { kInitBaseRva, kInitBaseJsonRva };
        for (uint32_t rva : rvas)
        {
            uint32_t step1 = 0;
            if (!GameMemory::ReadU32(il2cpp + rva, step1) || !IsGuestPtr(step1))
                continue;
            uint32_t staticFields = 0;
            if (!GameMemory::ReadU32(step1 + kStaticClass, staticFields) || !IsGuestPtr(staticFields))
                continue;
            uint32_t ge = 0;
            if (!GameMemory::ReadU32(staticFields, ge) || !IsGuestPtr(ge))
                continue;

            uint32_t partida = 0;
            uint32_t matchGame = 0;
            if (GameMemory::ReadU32(ge + kCurrentGame, matchGame) && IsGuestPtr(matchGame))
                GameMemory::ReadU32(matchGame + kCurrentMatch, partida);
            if (!IsGuestPtr(partida))
                GameMemory::ReadU32(ge + kCurrentMatch, partida);
            if (!IsGuestPtr(partida))
                continue;
            outPartida = partida;
            return true;
        }
        return false;
    }

    static bool AnyUmaBuilding(const std::vector<uint32_t>& umas)
    {
        for (uint32_t uma : umas)
        {
            uint8_t building = 0;
            if (GameMemory::ReadU8(uma + kLastBuildNotFinish, building) && building != 0)
                return true;
        }
        return false;
    }

    // Advance the match-load gate. Returns true when it is SAFE to write/dirty.
    static bool UpdateMatchGate(bool inMatch, const std::vector<uint32_t>& umas)
    {
        // Also treat "partida exists" as match context — covers the loading screen
        // before LocalPlayer/UMA pointers are ready (inMatch may still be false).
        uint32_t partida = 0;
        const bool partidaAlive = TryGetCurrentPartida(partida);
        const bool matchCtx = inMatch || partidaAlive;

        if (!matchCtx)
        {
            g_MatchGate = MatchGate::Lobby;
            g_MatchLoadFrames = 0;
            g_MatchStableFrames = 0;
            g_MatchSawBuilding = false;
            g_WasInMatch = false;
            return true; // lobby: writes allowed
        }

        // Rising edge: just entered match / loading screen
        if (g_MatchGate == MatchGate::Lobby || (!g_WasInMatch && inMatch))
        {
            g_MatchGate = MatchGate::Loading;
            g_MatchLoadFrames = 0;
            g_MatchStableFrames = 0;
            g_MatchSawBuilding = false;
            g_MatchQuietCooldown = 0;
            // Cancel any lobby live-swap pulses — dirtying during load = 99%/crash
            g_LiveSwapPulses = 0;
            g_ForceLiveFrames = 0;
            // Lobby wardrobe object pointers are about to be invalid. Writing them
            // during/after transition is the main "game closes on match entry" cause.
            InvalidatePatchPointers();
        }
        g_WasInMatch = true;

        if (g_MatchGate == MatchGate::Settled)
            return true;

        // Loading...
        ++g_MatchLoadFrames;
        const bool building = AnyUmaBuilding(umas);
        if (building)
        {
            g_MatchSawBuilding = true;
            g_MatchStableFrames = 0;
            return false;
        }

        if (g_MatchSawBuilding)
        {
            ++g_MatchStableFrames;
            if (g_MatchStableFrames >= kMatchStableNeed)
            {
                g_MatchGate = MatchGate::Settled;
                g_ForceLiveFrames = 0;      // no spam
                g_LiveSwapPulses = 0;
                g_MatchQuietCooldown = 120;    // ~2s recipes-only after rebind
                g_NeedRebindPatches = true;
                return true;
            }
            return false;
        }

        if (!umas.empty() && g_MatchLoadFrames >= 240)
        {
            g_MatchGate = MatchGate::Settled;
            g_ForceLiveFrames = 0;
            g_LiveSwapPulses = 0;
            g_MatchQuietCooldown = 120;
            g_NeedRebindPatches = true;
            return true;
        }
        if (g_MatchLoadFrames >= kMatchLoadTimeout)
        {
            g_MatchGate = MatchGate::Settled;
            g_ForceLiveFrames = 0;
            g_LiveSwapPulses = 0;
            g_MatchQuietCooldown = 120;
            g_NeedRebindPatches = true;
            return true;
        }
        return false;
    }

    static void EnsureSlotVisible(uint32_t uma, int slot)
    {
        // Lobby only — flipping visibility bits in match dirtied/crashed the avatar.
        if (g_MatchGate != MatchGate::Lobby)
            return;
        if (!IsGuestPtr(uma) || slot < 0 || slot >= 31)
            return;
        int32_t visible = 0;
        GameMemory::ReadI32(uma + kMVisibleSlots, visible);
        const int32_t bit = (1 << slot);
        if ((visible & bit) == 0)
            GameMemory::WriteI32(uma + kMVisibleSlots, visible | bit);
    }

    static void DirtyUma(uint32_t uma, int32_t slotMask = 0x7FFFFFFF)
    {
        if (!IsGuestPtr(uma))
            return;
        // Absolute lock during match load — DirtyUma is the #1 cause of 99% stalls.
        if (g_MatchGate == MatchGate::Loading)
            return;

        // Trigger a rebuild via changed-slots + mesh/texture dirty. Do NOT touch
        // lastBuildNotFinish (0x78): the match loader waits on that flag to reach
        // 100%, so forcing it to 1 froze loading at 99%.
        //
        // Only mark the slots we actually changed. Setting 0x7FFFFFFF forces a FULL
        // 31-slot rebuild; doing that during match load makes the game try to rebuild
        // slots whose assets aren't ready yet, so the build never finishes → 99%.
        if (slotMask == 0)
            return;
        int32_t curMask = 0;
        GameMemory::ReadI32(uma + kMChangedSlots, curMask);
        GameMemory::WriteI32(uma + kMChangedSlots, curMask | slotMask);
        GameMemory::WriteU8(uma + kCustomTextureDirty, 1);

        uint32_t umaData = 0;
        if (GameMemory::ReadU32(uma + kUmaData, umaData) && IsGuestPtr(umaData))
        {
            GameMemory::WriteU8(umaData + kUmaMeshDirty, 1);
            GameMemory::WriteU8(umaData + kUmaTextureDirty, 1);
            GameMemory::WriteU8(umaData + 0xA7u, 1);
        }
    }

    // Pick the wardrobe recipe hash for this avatar/context.
    // Female skins (Icon_avatar_female_*) store the real mesh in *_F fields; the male
    // slots are often stubs that do not attach to a female UMA race — that was why
    // applying a female skin on a female character looked like "nothing happened".
    static int32_t PickRecipeHash(const CategoryPatch& patch, bool female, bool inGame)
    {
        const bool hasM = patch.NewHashes[0] != 0 || patch.NewHashes[1] != 0;
        const bool hasF = patch.NewHashes[2] != 0 || patch.NewHashes[3] != 0;

        bool useF = female;
        if (patch.TargetGender == 'f' && hasF)
            useF = true;
        else if (patch.TargetGender == 'm' && hasM && !female)
            useF = false;
        else if (!hasM && hasF)
            useF = true;
        else if (!hasF && hasM)
            useF = false;

        const int primary = useF ? (inGame ? 3 : 2) : (inGame ? 1 : 0);
        const int secondary = useF ? (inGame ? 2 : 3) : (inGame ? 0 : 1);
        if (patch.NewHashes[primary] != 0)
            return patch.NewHashes[primary];
        if (patch.NewHashes[secondary] != 0)
            return patch.NewHashes[secondary];

        // Cross-gender last resort (e.g. unisex item missing one side)
        for (int i = 0; i < 4; ++i)
        {
            if (patch.NewHashes[i] != 0)
                return patch.NewHashes[i];
        }
        return 0;
    }

    static bool PushRecipesToUma(uint32_t uma, int category, const CategoryPatch& patch, bool female, bool inGame)
    {
        if (!UmaLooksValid(uma) || patch.TargetPtr == 0)
            return false;
        // Never touch UMA recipes while the match is still loading.
        if (g_MatchGate == MatchGate::Loading)
            return false;

        const int32_t writeHash = PickRecipeHash(patch, female, inGame);
        if (writeHash == 0)
            return false;

        // Same-gender companion hash (lobby <-> ingame). Remap may put either; both OK.
        const bool useF = (writeHash == patch.NewHashes[2] || writeHash == patch.NewHashes[3])
            || (patch.TargetGender == 'f' && (patch.NewHashes[2] != 0 || patch.NewHashes[3] != 0));
        const int32_t altSameGender = useF
            ? ((writeHash == patch.NewHashes[3]) ? patch.NewHashes[2] : patch.NewHashes[3])
            : ((writeHash == patch.NewHashes[1]) ? patch.NewHashes[0] : patch.NewHashes[1]);

        uint32_t recipesObj = 0;
        if (!GameMemory::ReadU32(uma + kMRecipes, recipesObj) || !IsGuestPtr(recipesObj))
            return false;

        int32_t len = 0;
        if (!GameMemory::ReadI32(recipesObj + 0x0Cu, len) || len < 8 || len > 32)
            return false;

        const uint32_t data = recipesObj + 0x10u;
        bool anyWrite = false;
        int32_t changedMask = 0;
        const bool inMatchSafe = (g_MatchGate == MatchGate::Settled);

        for (int32_t i = 0; i < len; ++i)
        {
            int32_t cur = 0;
            if (!GameMemory::ReadI32(data + static_cast<uint32_t>(i * 4), cur) || cur == 0)
                continue;

            // Already holding the gender-correct skin (lobby or ingame variant) — leave it.
            if (cur == writeHash || (altSameGender != 0 && cur == altSameGender))
                continue;

            int32_t mapped = writeHash;
            auto it = patch.RecipeRemap.find(cur);
            if (it != patch.RecipeRemap.end())
            {
                // Pairwise remap can map male→male even on a female avatar. Force the
                // gender we picked when the remapped value is the wrong gender side.
                mapped = it->second;
                const bool mappedIsF = (mapped == patch.NewHashes[2] || mapped == patch.NewHashes[3]);
                if (useF != mappedIsF && writeHash != 0)
                    mapped = writeHash;
            }
            else if (patch.OldRecipeHashes.find(cur) == patch.OldRecipeHashes.end())
            {
                continue;
            }

            if (cur == mapped)
                continue;
            if (GameMemory::WriteI32(data + static_cast<uint32_t>(i * 4), mapped))
            {
                anyWrite = true;
                changedMask |= (1 << i);
            }
        }

        auto writeSlot = [&](int s) {
            if (s < 0 || s >= len)
                return;
            int32_t cur = 0;
            GameMemory::ReadI32(data + static_cast<uint32_t>(s * 4), cur);
            // Keep either lobby or ingame hash of the CORRECT gender — never leave a
            // male recipe on a female avatar (that made female skins "not activate").
            const bool ok = (cur == writeHash) ||
                (altSameGender != 0 && cur == altSameGender);
            if (!ok)
            {
                if (GameMemory::WriteI32(data + static_cast<uint32_t>(s * 4), writeHash))
                {
                    anyWrite = true;
                    changedMask |= (1 << s);
                }
            }
            // Empty HeadAdd/Hair slots stay invisible unless the visible-bit is set —
            // this is why masks/hats vanished in match when the player wasn't already
            // wearing that slot.
            EnsureSlotVisible(uma, s);
        };

        auto clearSlot = [&](int s) {
            if (s < 0 || s >= len)
                return;
            int32_t cur = 0;
            GameMemory::ReadI32(data + static_cast<uint32_t>(s * 4), cur);
            if (cur == 0)
                return;
            if (GameMemory::WriteI32(data + static_cast<uint32_t>(s * 4), 0))
            {
                anyWrite = true;
                changedMask |= (1 << s);
            }
        };

        const ClothCategory cat = static_cast<ClothCategory>(category);
        if (cat == ClothCategory::Head)
        {
            writeSlot(kWtHair);
            writeSlot(kWtHead);
        }
        else if (cat == ClothCategory::Mask)
        {
            writeSlot(kWtFace);      // glasses/masks live in the Face slot
        }
        else if (cat == ClothCategory::Facepaint)
        {
            writeSlot(kWtHeadAdd);   // facepaint overlays live in HeadAdditive
        }
        else if (cat == ClothCategory::Top)
        {
            writeSlot(kWtChest);
            // One-piece / female cos: game often stores the recipe in Set (12) as well.
            if (patch.IsFullBody || patch.TargetGender == 'f')
            {
                writeSlot(kWtSet);
                // Clearing legs/feet mid-match corrupted the avatar and closed the game.
                // Only do it in lobby.
                if (!inMatchSafe)
                {
                    clearSlot(kWtLegs);
                    clearSlot(kWtFeet);
                }
            }
        }
        else if (cat == ClothCategory::Traje)
        {
            writeSlot(kWtSet);
            writeSlot(kWtChest);
        }
        else
        {
            writeSlot(WardrobeSlotForCategory(category));
        }

        // In a settled match: write recipes only — DirtyUma right after load is what
        // closed HD-Player / stalled at 99%. Lobby still dirties so the preview updates.
        if (anyWrite && !inMatchSafe)
            DirtyUma(uma, changedMask);

        return anyWrite;
    }

    static int32_t SlotMaskForCategory(int category, const CategoryPatch& patch)
    {
        int32_t mask = 0;
        auto bit = [&](int s) {
            if (s >= 0 && s < 31)
                mask |= (1 << s);
        };
        const ClothCategory cat = static_cast<ClothCategory>(category);
        switch (cat)
        {
        case ClothCategory::Top:
            bit(kWtChest);
            if (patch.IsFullBody || patch.TargetGender == 'f')
            {
                bit(kWtSet);
                bit(kWtLegs);
                bit(kWtFeet);
            }
            break;
        case ClothCategory::Bottom:    bit(kWtLegs); break;
        case ClothCategory::Shoes:     bit(kWtFeet); break;
        case ClothCategory::Head:      bit(kWtHair); bit(kWtHead); break;
        case ClothCategory::Mask:      bit(kWtFace); break;
        case ClothCategory::Facepaint: bit(kWtHeadAdd); break;
        case ClothCategory::Traje:     bit(kWtSet); bit(kWtChest); break;
        default: break;
        }
        return mask;
    }

    // userApply=true: clique no painel — força rebuild NA HORA no lobby.
    // Em partida settled: só recipe quieta (sem DirtyUma) pra não fechar o jogo.
    static int LivePushAll(bool userApply)
    {
        if (g_CatPatches.empty())
            return 0;

        EnsureMaleAvatarStatics();

        std::vector<uint32_t> umas;
        bool female = false;
        bool inMatch = false;
        CollectLocalUmas(umas, female, inMatch);

        if (!UpdateMatchGate(inMatch, umas))
            return 0;

        // Don't push with invalidated TargetPtr — Tick rebinds first.
        if (g_NeedRebindPatches)
            return 0;

        uint32_t lobbyAvatar = 0, lobbyUma = 0;
        bool lobbyFemale = false;
        if (TryGetLobbyLocalAvatar(lobbyAvatar, lobbyUma, lobbyFemale))
        {
            if (UmaLooksValid(lobbyUma) && std::find(umas.begin(), umas.end(), lobbyUma) == umas.end())
                umas.push_back(lobbyUma);
            if (lobbyFemale)
                female = true;
        }

        if (!female)
        {
            for (const auto& kv : g_CatPatches)
            {
                if (kv.second.TargetGender == 'f')
                {
                    female = true;
                    break;
                }
            }
        }

        // Force-dirty ONLY in lobby. In match it closed the game / 99%.
        const bool allowForceDirty = userApply && g_MatchGate == MatchGate::Lobby;

        int32_t applyMask = 0;
        if (allowForceDirty)
        {
            for (const auto& kv : g_CatPatches)
                applyMask |= SlotMaskForCategory(kv.first, kv.second);
            if (applyMask == 0)
                applyMask = (1 << kWtChest) | (1 << kWtLegs) | (1 << kWtFeet);
        }

        int pushed = 0;
        for (uint32_t uma : umas)
        {
            if (!UmaLooksValid(uma))
                continue;

            if (g_MatchGate != MatchGate::Lobby)
            {
                uint8_t building = 0;
                if (GameMemory::ReadU8(uma + kLastBuildNotFinish, building) && building != 0)
                    continue;
            }

            for (const auto& kv : g_CatPatches)
            {
                if (kv.second.TargetPtr == 0 || !WardrobePtrAlive(kv.second.TargetPtr, kv.second.TargetClothID))
                    continue;
                if (PushRecipesToUma(uma, kv.first, kv.second, female, inMatch || g_MatchGate == MatchGate::Settled))
                    ++pushed;
            }

            if (allowForceDirty)
            {
                DirtyUma(uma, applyMask);
                ++pushed;
            }
        }

        if (IsGuestPtr(lobbyAvatar) && g_MatchGate == MatchGate::Lobby)
        {
            for (const auto& kv : g_CatPatches)
            {
                if (kv.second.TargetPtr == 0)
                    continue;
                ForceLobbyAvatarRefresh(lobbyAvatar, kv.first, kv.second.TargetClothID, kv.second.TargetPtr);
            }
            ++pushed;
        }

        return pushed;
    }

    // After match load: rebuild CategoryPatch from item IDs (fresh pointers).
    static bool RebindAppliedPatches()
    {
        if (!g_NeedRebindPatches || g_Applied.empty())
            return false;

        std::string err;
        uint32_t itemsArray = 0;
        int32_t count = 0;
        if (!TryGetMData(itemsArray, count, err))
            return false;

        // Drop stale patches; PatchCategory will recreate with live pointers.
        g_CatPatches.clear();

        int ok = 0;
        for (auto& applied : g_Applied)
        {
            PatchResult r = PatchCategory(itemsArray, count, applied.Category, applied.ClothID);
            if (r == PatchResult::TargetMissing)
            {
                for (uint32_t alt : applied.AltIDs)
                {
                    r = PatchCategory(itemsArray, count, applied.Category, alt);
                    if (r == PatchResult::Ok)
                    {
                        applied.ClothID = alt;
                        break;
                    }
                }
            }
            if (r == PatchResult::Ok)
                ++ok;
        }

        g_NeedRebindPatches = false;
        return ok > 0;
    }
}

namespace ClothChanger
{
    bool LoadFromFile(const std::string& path)
    {
        g_Loaded = false;
        g_DB.clear();
        g_Trajes.clear();
        g_Passes.clear();
        g_Famous.clear();
        g_CategoryCache.clear();
        g_Status = "Loading...";

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            g_Status = "File not found";
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string json = buffer.str();

        size_t pos = 0;
        while (true)
        {
            size_t start = json.find('{', pos);
            if (start == std::string::npos)
                break;

            size_t end = json.find('}', start);
            if (end == std::string::npos)
                break;

            const std::string obj = json.substr(start, end - start + 1);
            pos = end + 1;

            const std::string itemType = ExtractField(obj, "itemType");
            if (!itemType.empty() && itemType != "CLOTHES")
                continue;

            const std::string idStr = ExtractField(obj, "itemID");
            if (idStr.empty())
                continue;

            uint32_t id = 0;
            try { id = static_cast<uint32_t>(std::stoul(idStr)); }
            catch (...) { continue; }

            ClothEntry entry;
            entry.ItemID = id;
            entry.Description = ExtractField(obj, "description");
            entry.Rarity = ExtractField(obj, "Rare");
            if (entry.Rarity.empty())
                entry.Rarity = "WHITE";
            entry.IconName = ExtractField(obj, "icon");
            g_DB.push_back(std::move(entry));
        }

        RebuildCategoryCache();
        g_Loaded = !g_DB.empty();
        g_Status = g_Loaded
            ? ("Loaded " + std::to_string(g_DB.size()) + " | " + std::to_string(g_Trajes.size())
                + " trajes | " + std::to_string(g_Passes.size()) + " passes | "
                + std::to_string(g_Famous.size()) + " famosos")
            : "Parse failed";
        return g_Loaded;
    }

    bool IsLoaded() { return g_Loaded; }
    const std::string& LoadStatus() { return g_Status; }
    const std::string& MemStatus() { return g_MemStatus; }
    const std::vector<ClothEntry>& DB() { return g_DB; }
    const std::vector<TrajeEntry>& Trajes() { return g_Trajes; }
    const std::vector<TrajeEntry>& Passes() { return g_Passes; }
    const std::vector<TrajeEntry>& Famous() { return g_Famous; }

    ClothCategory GetCategory(const ClothEntry& e)
    {
        return CategoryFromEntry(e);
    }

    ClothCategory GetCategoryByItemID(uint32_t itemID)
    {
        auto it = std::find_if(g_DB.begin(), g_DB.end(), [&](const ClothEntry& e) { return e.ItemID == itemID; });
        if (it != g_DB.end())
            return CategoryFromEntry(*it);
        ClothEntry tmp;
        tmp.ItemID = itemID;
        return CategoryFromEntry(tmp);
    }

    const char* GetCategoryName(ClothCategory cat)
    {
        switch (cat)
        {
        case ClothCategory::Top:       return "Top";
        case ClothCategory::Bottom:    return "Bottom";
        case ClothCategory::Shoes:     return "Shoes";
        case ClothCategory::Head:      return "Head";
        case ClothCategory::Mask:      return "Mask";
        case ClothCategory::Facepaint: return "Facepaint";
        case ClothCategory::Traje:     return "Trajes";
        default:                       return "Other";
        }
    }

    const char* GetBrowseTabName(ClothBrowseTab tab)
    {
        switch (tab)
        {
        case ClothBrowseTab::Trajes:     return "TRAJES";
        case ClothBrowseTab::Passes:     return "PASSES";
        case ClothBrowseTab::Famous:     return "FAMOSOS";
        case ClothBrowseTab::Top:        return "TOP";
        case ClothBrowseTab::Bottom:     return "BOTTOM";
        case ClothBrowseTab::Shoes:      return "SHOES";
        case ClothBrowseTab::Head:       return "HEAD";
        case ClothBrowseTab::Mask:       return "MASK";
        case ClothBrowseTab::Facepaint:  return "FACEPAINT";
        case ClothBrowseTab::Applied:    return "APPLIED SKINS";
        default:                         return "CLOTHES";
        }
    }

    std::vector<size_t> FilterIndices(ClothCategory cat, const char* search)
    {
        std::vector<size_t> result;
        const auto& source = g_CategoryCache[static_cast<int>(cat)];
        result.reserve(source.size());

        const std::string query = search ? search : "";
        for (size_t idx : source)
        {
            const ClothEntry& e = g_DB[idx];
            if (query.empty() ||
                ContainsIgnoreCase(e.Description, query) ||
                ContainsIgnoreCase(std::to_string(e.ItemID), query))
            {
                result.push_back(idx);
            }
        }
        return result;
    }

    std::vector<size_t> FilterTrajes(const char* search)
    {
        std::vector<size_t> result;
        result.reserve(g_Trajes.size());
        const std::string query = search ? search : "";
        for (size_t i = 0; i < g_Trajes.size(); ++i)
        {
            if (query.empty() || ContainsIgnoreCase(g_Trajes[i].Name, query))
                result.push_back(i);
        }
        return result;
    }

    std::vector<size_t> FilterPasses(const char* search)
    {
        std::vector<size_t> result;
        result.reserve(g_Passes.size());
        const std::string query = search ? search : "";
        for (size_t i = 0; i < g_Passes.size(); ++i)
        {
            if (query.empty() || ContainsIgnoreCase(g_Passes[i].Name, query))
                result.push_back(i);
        }
        return result;
    }

    std::vector<size_t> FilterFamous(const char* search)
    {
        std::vector<size_t> result;
        result.reserve(g_Famous.size());
        const std::string query = search ? search : "";
        for (size_t i = 0; i < g_Famous.size(); ++i)
        {
            if (query.empty() || ContainsIgnoreCase(g_Famous[i].Name, query))
                result.push_back(i);
        }
        return result;
    }

    void RemoveByCategory(int category)
    {
        RestoreCategory(category);
        g_Applied.erase(std::remove_if(g_Applied.begin(), g_Applied.end(),
            [&](const AppliedCloth& a) { return a.Category == category; }), g_Applied.end());
        g_MemStatus = "Removed";
    }

    void RemoveByClothID(uint32_t clothID)
    {
        for (auto it = g_Applied.begin(); it != g_Applied.end(); )
        {
            if (it->ClothID == clothID)
            {
                RestoreCategory(it->Category);
                it = g_Applied.erase(it);
            }
            else
                ++it;
        }
        g_MemStatus = "Removed";
    }

    void ClearAllCloths()
    {
        std::vector<int> cats;
        cats.reserve(g_CatPatches.size());
        for (const auto& kv : g_CatPatches)
            cats.push_back(kv.first);
        for (int c : cats)
            RestoreCategory(c);
        g_CatPatches.clear();
        g_Applied.clear();
        g_MemStatus = "Cleared";
    }

    bool ApplyOrToggle(uint32_t clothID)
    {
        if (IsApplied(clothID))
        {
            RemoveByClothID(clothID);
            return false;
        }

        const ClothCategory cat = GetCategoryByItemID(clothID);
        if (cat == ClothCategory::Other || cat == ClothCategory::Traje)
        {
            g_MemStatus = "Invalid category";
            return false;
        }

        std::string desc, rare = "WHITE";
        auto it = std::find_if(g_DB.begin(), g_DB.end(), [&](const ClothEntry& e) { return e.ItemID == clothID; });
        if (it != g_DB.end())
        {
            desc = it->Description;
            rare = it->Rarity;
        }

        RemoveByCategory(static_cast<int>(cat));

        AppliedCloth entry;
        entry.ClothID = clothID;
        entry.Category = static_cast<int>(cat);
        entry.ClothDesc = desc;
        entry.Rarity = rare;
        g_Applied.push_back(std::move(entry));
        CommitCloths();
        return true;
    }

    // Equip all pieces of an outfit. Clears previous outfit first so passes never mix
    // (was leaving the other pass's peitoral when the new Top failed / was missing).
    static void ApplyOutfitPieces(const TrajeEntry& t)
    {
        ClearAllCloths();

        // Prefer pieces matching the lobby avatar gender so female chars get
        // Icon_avatar_female_* items (male stubs don't render on female UMA).
        bool preferFemale = false;
        {
            uint32_t av = 0, uma = 0;
            bool fem = false;
            if (TryGetLobbyLocalAvatar(av, uma, fem) && fem)
                preferFemale = true;
        }

        auto pickPrimary = [&](const std::vector<uint32_t>& ids) -> uint32_t {
            if (ids.empty())
                return 0;
            if (ids.size() == 1)
                return ids.front();

            uint32_t fallback = ids.front();
            for (uint32_t id : ids)
            {
                auto it = std::find_if(g_DB.begin(), g_DB.end(),
                    [&](const ClothEntry& e) { return e.ItemID == id; });
                if (it == g_DB.end())
                    continue;
                const char g = IconGender(it->IconName);
                if (preferFemale && g == 'f')
                    return id;
                if (!preferFemale && g == 'm')
                    return id;
                if (g == '?')
                    fallback = id; // unisex icon — decent fallback
            }
            // No exact gender match: if female, prefer any female-looking id in alts
            // by checking description tags, else first.
            return fallback;
        };

        std::unordered_map<int, std::vector<uint32_t>> byCat;
        std::vector<int> order;
        for (uint32_t pid : t.PieceIDs)
        {
            // Full-body one-piece costumes (Set/212) are category Traje — they MUST be
            // applied to the Set slot, not skipped, or the "Equipar" button does nothing.
            const ClothCategory cat = GetCategoryByItemID(pid);
            if (cat == ClothCategory::Other)
                continue;
            auto& v = byCat[static_cast<int>(cat)];
            if (v.empty())
                order.push_back(static_cast<int>(cat));
            v.push_back(pid);
        }

        for (int c : order)
        {
            auto& v = byCat[c];
            const uint32_t primary = pickPrimary(v);
            // Keep other gender / variants as AltIDs for CommitCloths fallbacks
            std::vector<uint32_t> alts;
            alts.reserve(v.size());
            for (uint32_t id : v)
            {
                if (id != primary)
                    alts.push_back(id);
            }

            std::string desc, rare = t.Rarity;
            auto it = std::find_if(g_DB.begin(), g_DB.end(),
                [&](const ClothEntry& e) { return e.ItemID == primary; });
            if (it != g_DB.end())
            {
                desc = it->Description;
                rare = it->Rarity;
            }

            AppliedCloth entry;
            entry.ClothID = primary;
            entry.Category = c;
            entry.ClothDesc = desc;
            entry.Rarity = rare;
            entry.AltIDs = std::move(alts);
            g_Applied.push_back(std::move(entry));
        }

        CommitCloths();
    }

    bool ApplyOrTogglePass(size_t passIndex)
    {
        if (passIndex >= g_Passes.size())
            return false;

        if (IsPassApplied(passIndex))
        {
            ClearAllCloths();
            g_MemStatus = "Removed";
            return false;
        }

        ApplyOutfitPieces(g_Passes[passIndex]);
        return true;
    }

    bool ApplyOrToggleFamous(size_t famousIndex)
    {
        if (famousIndex >= g_Famous.size())
            return false;

        if (IsFamousApplied(famousIndex))
        {
            ClearAllCloths();
            g_MemStatus = "Removed";
            return false;
        }

        ApplyOutfitPieces(g_Famous[famousIndex]);
        return true;
    }

    bool ApplyOrToggleTraje(size_t trajeIndex)
    {
        if (trajeIndex >= g_Trajes.size())
            return false;

        if (IsTrajeApplied(trajeIndex))
        {
            ClearAllCloths();
            g_MemStatus = "Removed";
            return false;
        }

        ApplyOutfitPieces(g_Trajes[trajeIndex]);
        return true;
    }

    bool IsCategoryApplied(int category, uint32_t* outClothID)
    {
        for (const auto& a : g_Applied)
        {
            if (a.Category == category)
            {
                if (outClothID)
                    *outClothID = a.ClothID;
                return true;
            }
        }
        return false;
    }

    const std::vector<AppliedCloth>& Applied() { return g_Applied; }

    bool IsApplied(uint32_t clothID)
    {
        return std::any_of(g_Applied.begin(), g_Applied.end(),
            [&](const AppliedCloth& a) { return a.ClothID == clothID; });
    }

    bool IsOutfitFullyApplied(const TrajeEntry& outfit)
    {
        if (outfit.PieceIDs.empty() || g_Applied.empty())
            return false;

        std::unordered_map<int, std::unordered_set<uint32_t>> byCat;
        for (uint32_t pid : outfit.PieceIDs)
        {
            const ClothCategory cat = GetCategoryByItemID(pid);
            if (cat == ClothCategory::Other)
                continue;
            byCat[static_cast<int>(cat)].insert(pid);
        }
        if (byCat.empty())
            return false;

        // Every outfit category must be equipped with one of that category's piece IDs
        for (const auto& kv : byCat)
        {
            bool hit = false;
            for (const auto& a : g_Applied)
            {
                if (a.Category != kv.first)
                    continue;
                if (kv.second.count(a.ClothID))
                {
                    hit = true;
                    break;
                }
                for (uint32_t alt : a.AltIDs)
                {
                    if (kv.second.count(alt))
                    {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }
            if (!hit)
                return false;
        }
        return true;
    }

    bool IsTrajeApplied(size_t trajeIndex)
    {
        if (trajeIndex >= g_Trajes.size())
            return false;
        return IsOutfitFullyApplied(g_Trajes[trajeIndex]);
    }

    bool IsPassApplied(size_t passIndex)
    {
        if (passIndex >= g_Passes.size())
            return false;
        return IsOutfitFullyApplied(g_Passes[passIndex]);
    }

    bool IsFamousApplied(size_t famousIndex)
    {
        if (famousIndex >= g_Famous.size())
            return false;
        return IsOutfitFullyApplied(g_Famous[famousIndex]);
    }

    void CommitCloths()
    {
        if (g_Applied.empty())
        {
            g_MemStatus = "No skins";
            return;
        }

        if (!GameMemory::IsAttached())
        {
            g_MemStatus = "Not attached";
            return;
        }

        std::string err;
        uint32_t itemsArray = 0;
        int32_t count = 0;
        if (!TryGetMData(itemsArray, count, err))
        {
            g_MemStatus = err;
            return;
        }

        int okCount = 0;
        std::vector<uint32_t> failedIds;
        for (auto& applied : g_Applied)
        {
            PatchResult r = PatchCategory(itemsArray, count, applied.Category, applied.ClothID);

            // Primary piece missing from the game's wardrobe DB? Try the fallbacks
            // (e.g. Elite re-release of the same outfit) so no slot is left out.
            if (r == PatchResult::TargetMissing)
            {
                for (uint32_t alt : applied.AltIDs)
                {
                    r = PatchCategory(itemsArray, count, applied.Category, alt);
                    if (r == PatchResult::Ok)
                    {
                        applied.ClothID = alt;
                        auto it = std::find_if(g_DB.begin(), g_DB.end(),
                            [&](const ClothEntry& e) { return e.ItemID == alt; });
                        if (it != g_DB.end())
                        {
                            applied.ClothDesc = it->Description;
                            applied.Rarity = it->Rarity;
                        }
                        break;
                    }
                    if (r == PatchResult::WriteFail)
                        break;
                }
            }

            if (r == PatchResult::Ok)
            {
                ++okCount;
            }
            else if (r == PatchResult::WriteFail)
            {
                g_MemStatus = "Write failed";
                return;
            }
            else
            {
                // No candidate of this category exists in the game DB — skip it.
                failedIds.push_back(applied.ClothID);
            }
        }

        // Drop pieces that can't be applied so the UI reflects reality
        if (!failedIds.empty())
        {
            g_Applied.erase(std::remove_if(g_Applied.begin(), g_Applied.end(),
                [&](const AppliedCloth& a) {
                    return std::find(failedIds.begin(), failedIds.end(), a.ClothID) != failedIds.end();
                }), g_Applied.end());
        }

        if (okCount == 0 && g_Applied.empty())
        {
            g_MemStatus = "Skin fora do DB do jogo";
            return;
        }

        g_ForceLiveFrames = 180; // ~3s sticky recipe rewrite every frame
        g_LiveSwapPulses = 12;   // a dozen rebuild kicks so mesh swaps without relog
        g_MemStatus = "Applying...";
        const std::string skipped = failedIds.empty()
            ? std::string()
            : (" · " + std::to_string(failedIds.size()) + " peca(s) fora do jogo");
        const int live = LivePushAll(true); // immediate live swap (lobby + partida)
        if (live > 0)
            g_MemStatus = "OK live " + std::to_string(okCount) + "/" + std::to_string(g_Applied.size()) + skipped;
        else if (g_MaleAvatarStaticFields != 0)
            g_MemStatus = "OK " + std::to_string(okCount) + "/" + std::to_string(g_Applied.size()) + " (avatar null)" + skipped;
        else if (!g_MaleAvatarScanDone)
            g_MemStatus = "OK " + std::to_string(okCount) + "/" + std::to_string(g_Applied.size()) + " (buscando avatar...)" + skipped;
        else
            g_MemStatus = "OK " + std::to_string(okCount) + "/" + std::to_string(g_Applied.size())
                + " (aguarda load da partida)" + skipped;
    }

    // Scan the game's wardrobe DB once to learn each item's real slot (wardrobeType).
    // Returns true when the scan completed and categories were rebuilt.
    static bool ScanLiveWardrobeTypesOnce()
    {
        if (g_WardrobeScanned || !GameMemory::IsAttached())
            return false;

        std::string err;
        uint32_t itemsArray = 0;
        int32_t count = 0;
        if (!TryGetMData(itemsArray, count, err))
            return false;
        if (count <= 0 || count > 200000)
            return false;

        g_LiveWardrobeType.clear();
        g_LiveWardrobeType.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i)
        {
            uint32_t dataPtr = 0;
            if (!GameMemory::ReadU32(itemsArray + static_cast<uint32_t>(i * 4), dataPtr) || dataPtr == 0)
                continue;
            uint32_t iID = 0;
            if (!GameMemory::ReadU32(dataPtr + 0x28u, iID) || iID == 0)
                continue;
            uint8_t wt = 0;
            GameMemory::ReadU8(dataPtr + kWardrobeTypeOff, wt);
            g_LiveWardrobeType[iID] = wt;
        }

        g_WardrobeScanned = true;
        if (!g_LiveWardrobeType.empty())
            RebuildCategoryCache(); // re-sort tabs using ground-truth slots
        return true;
    }

    void Tick()
    {
        // Learn real slots as soon as we're attached (before anything is applied),
        // so the Hair/Mask tabs are correctly separated.
        if (!g_WardrobeScanned && GameMemory::IsAttached())
            ScanLiveWardrobeTypesOnce();

        if (g_Applied.empty() || !GameMemory::IsAttached())
            return;

        // During match load we may have cleared CatPatches pointers — still need Tick
        // to advance the gate even if g_CatPatches looks empty-of-ptrs.
        if (g_CatPatches.empty() && !g_NeedRebindPatches)
            return;

        const bool forcing = g_ForceLiveFrames > 0;
        if (!forcing)
        {
            if (++g_TickCounter < 2)
                return;
            g_TickCounter = 0;
        }
        else
        {
            --g_ForceLiveFrames;
            g_TickCounter = 0;
        }

        std::vector<uint32_t> umas;
        bool fem = false, inM = false;
        CollectLocalUmas(umas, fem, inM);
        const bool safe = UpdateMatchGate(inM, umas);

        // Match loading: absolute silence (no wardrobe refresh, no UMA writes).
        if (!safe)
            return;

        // First safe frames after load: rebind wardrobe patches to FRESH pointers.
        if (g_NeedRebindPatches)
        {
            if (!RebindAppliedPatches())
                return; // try again next tick
        }

        if (g_MatchQuietCooldown > 0)
        {
            --g_MatchQuietCooldown;
            // Wardrobe-only refresh with validated pointers — no LivePush yet.
            std::string err;
            uint32_t itemsArray = 0;
            int32_t count = 0;
            if (TryGetMData(itemsArray, count, err))
            {
                for (const auto& a : g_Applied)
                    RefreshCategoryIfNeeded(itemsArray, count, a.Category);
            }
            return;
        }

        {
            std::string err;
            uint32_t itemsArray = 0;
            int32_t count = 0;
            if (TryGetMData(itemsArray, count, err))
            {
                for (const auto& a : g_Applied)
                    RefreshCategoryIfNeeded(itemsArray, count, a.Category);
            }
        }

        const bool pulse = g_LiveSwapPulses > 0 && g_MatchGate == MatchGate::Lobby;
        if (pulse)
            --g_LiveSwapPulses;
        LivePushAll(pulse);
    }
}
