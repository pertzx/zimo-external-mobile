#pragma once



#include <cstdint>

#include <string>

#include <vector>

#include <unordered_map>



struct ClothEntry

{

    uint32_t ItemID = 0;

    std::string Description;

    std::string Rarity = "WHITE";

    std::string IconName;

};



struct AppliedCloth

{

    uint32_t ClothID = 0;

    int Category = 0;

    std::string ClothDesc;

    std::string Rarity = "WHITE";

    // Fallback item IDs (same look, e.g. Elite re-release) used when
    // ClothID isn't present in the game's wardrobe DB.

    std::vector<uint32_t> AltIDs;

};



struct TrajeEntry

{

    std::string Name;

    std::string Rarity = "WHITE";

    uint32_t IconItemID = 0;

    std::vector<uint32_t> PieceIDs;

    int PassNumber = 0; // 1..N for Passes (oldest first), 0 for Trajes

};



enum class ClothCategory : int

{

    Top = 0,

    Bottom,

    Shoes,

    Head,

    Mask,

    Facepaint,

    Traje,

    Other,

    Count

};



// Sidebar browse tabs (not the same as ClothCategory)

enum class ClothBrowseTab : int

{

    Trajes = 0,

    Passes,

    Famous,

    Top,

    Bottom,

    Shoes,

    Head,

    Mask,

    Facepaint,

    Applied,

    Count

};



namespace ClothChanger

{

    bool LoadFromFile(const std::string& path);

    bool IsLoaded();

    const std::string& LoadStatus();

    const std::vector<ClothEntry>& DB();

    const std::vector<TrajeEntry>& Trajes();

    const std::vector<TrajeEntry>& Passes();

    const std::vector<TrajeEntry>& Famous();



    ClothCategory GetCategory(const ClothEntry& e);

    ClothCategory GetCategoryByItemID(uint32_t itemID);

    const char* GetCategoryName(ClothCategory cat);

    const char* GetBrowseTabName(ClothBrowseTab tab);



    std::vector<size_t> FilterIndices(ClothCategory cat, const char* search);

    std::vector<size_t> FilterTrajes(const char* search);

    std::vector<size_t> FilterPasses(const char* search);

    std::vector<size_t> FilterFamous(const char* search);



    // Click skin: apply to character (no Owned ID). Same skin again = remove.

    bool ApplyOrToggle(uint32_t clothID);

    bool ApplyOrToggleTraje(size_t trajeIndex);

    bool ApplyOrTogglePass(size_t passIndex);

    bool ApplyOrToggleFamous(size_t famousIndex);



    void RemoveByCategory(int category);

    void RemoveByClothID(uint32_t clothID);

    void ClearAllCloths();



    const std::vector<AppliedCloth>& Applied();

    bool IsApplied(uint32_t clothID);

    bool IsTrajeApplied(size_t trajeIndex);

    bool IsPassApplied(size_t passIndex);

    bool IsFamousApplied(size_t famousIndex);

    bool IsCategoryApplied(int category, uint32_t* outClothID = nullptr);

    // Exact outfit match: every primary category slot equals one of the outfit piece IDs.
    bool IsOutfitFullyApplied(const TrajeEntry& outfit);



    void Tick();

    void CommitCloths();

    const std::string& MemStatus();

}


