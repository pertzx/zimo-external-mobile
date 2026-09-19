using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace CHADEXTERNAL
{
    internal class ClothEntry
    {
        public uint ItemID { get; set; }
        public string Description { get; set; } = "";
        public string Rarity { get; set; } = "WHITE";
        public string IconName { get; set; } = "";
        public string WardrobeType { get; set; } = "";
    }

    internal class AppliedCloth
    {
        public uint OwnedClothID { get; set; }
        public uint ClothID { get; set; }
        public string ClothDesc { get; set; } = "";
        public string Rarity { get; set; } = "WHITE";
    }

    internal sealed class ClothSwapState
    {
        public uint OwnedClothID { get; set; }
        public uint TargetClothID { get; set; }
        public uint ValueAddr { get; set; }   
        public uint OriginalPtr { get; set; }
        public uint TargetPtr { get; set; }   
    }

    internal static class ClothChanger
    {
        private const uint niggaclass = 0xA987F88;

        private static readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(60) };
        private static List<ClothEntry> _db = new();
        private static List<AppliedCloth> _applied = new();
        private static Dictionary<uint, ClothSwapState> _swapStates = new();

        public static bool IsLoaded { get; private set; }
        public static string LoadStatus { get; private set; } = "Not loaded";

        public static IReadOnlyList<ClothEntry> DB => _db;
        public static IReadOnlyList<AppliedCloth> Applied => _applied;

        private static int _tickCounter = 0;


    public static async Task LoadFromGitHubAsync(string url)
    {
        try
        {
            LoadStatus = "Downloading...";
            IsLoaded = false;

            string json = await _http.GetStringAsync(url);
            bool ok = ParseJSON(json);

            IsLoaded = ok;
            LoadStatus = ok ? $"Loaded {_db.Count} clothes" : "Parse failed";
        }
        catch (Exception ex)
        {
            LoadStatus = $"Error: {ex.Message}";
            IsLoaded = false;
        }
    }

        private static bool ParseJSON(string jsonStr)
        {
            _db.Clear();
            try
            {
                using var doc = JsonDocument.Parse(jsonStr);
                JsonElement arr;

                if (doc.RootElement.ValueKind == JsonValueKind.Array)
                {
                    arr = doc.RootElement;
                }
                else
                {
                    JsonElement found = default;
                    foreach (var k in new[] { "items", "data", "clothes" })
                    {
                        if (doc.RootElement.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.Array)
                        { found = v; break; }
                    }
                    if (found.ValueKind != JsonValueKind.Array) return false;
                    arr = found;
                }

                foreach (var item in arr.EnumerateArray())
                {
                    string itemType = item.TryGetProperty("itemType", out var it) ? it.GetString() ?? "" : "";
                    if (!string.IsNullOrEmpty(itemType) &&
                        !string.Equals(itemType, "CLOTHES", StringComparison.OrdinalIgnoreCase))
                        continue;

                    string idStr = item.TryGetProperty("itemID", out var idProp) ? idProp.GetString() ?? "" : "";
                    if (!uint.TryParse(idStr, out var id)) continue;

                    _db.Add(new ClothEntry
                    {
                        ItemID = id,
                        Description = item.TryGetProperty("description", out var d) ? d.GetString() ?? "" : "",
                        Rarity = item.TryGetProperty("Rare", out var r) ? r.GetString() ?? "WHITE" : "WHITE",
                        IconName = item.TryGetProperty("icon", out var ic) ? ic.GetString() ?? "" : "",
                        WardrobeType = item.TryGetProperty("wardrobeType", out var wt) ? wt.GetString() ?? "" : "",
                    });
                }
            }
            catch { return false; }
            return _db.Count > 0;
        }

        public static List<ClothEntry> GetCloths() => _db.ToList();


        public static void ApplyCloth(uint ownedClothID, uint clothID, string clothDesc = "", string rarity = "")
        {
            if (IsLoaded)
            {
                var dbEntry = _db.FirstOrDefault(e => e.ItemID == clothID);
                if (dbEntry != null)
                {
                    if (string.IsNullOrEmpty(clothDesc)) clothDesc = dbEntry.Description;
                    if (string.IsNullOrEmpty(rarity)) rarity = dbEntry.Rarity;
                }
            }
            if (string.IsNullOrEmpty(rarity)) rarity = "WHITE";

            var existing = _applied.FirstOrDefault(a => a.OwnedClothID == ownedClothID);
            if (existing != null)
            {
                existing.ClothID = clothID;
                existing.ClothDesc = clothDesc;
                existing.Rarity = rarity;
                _swapStates.Remove(ownedClothID);
            }
            else
            {
                _applied.Add(new AppliedCloth
                {
                    OwnedClothID = ownedClothID,
                    ClothID = clothID,
                    ClothDesc = clothDesc,
                    Rarity = rarity,
                });
            }

        }

        public static void RemoveCloth(uint ownedClothID)
        {
            RestoreOriginalCloth(ownedClothID);
            _applied.RemoveAll(a => a.OwnedClothID == ownedClothID);
        }

        public static void ClearAllCloths()
        {
            foreach (var a in _applied.ToList())
                RestoreOriginalCloth(a.OwnedClothID);
            _applied.Clear();
            _swapStates.Clear();
        }


        // Called every frame from Data.Work
        public static void Tick()
        {
            if (!Config.ClothChangerEnabled) return;
            if (_applied.Count == 0) return;

            if (++_tickCounter >= 50)
            {
                _tickCounter = 0;
                CommitCloths();
            }
        }

        public static void CommitCloths()
        {
            if (_applied.Count == 0) return;

            if (!TryGetMData(out var mData, out var count))
                return;

            foreach (var applied in _applied)
                EnsureSwap(mData, count, applied.OwnedClothID, applied.ClothID);
        }



        private static bool TryGetInstance(out uint instance)
        {
            instance = 0;

            uint readAddr = O.Il2Cpp + niggaclass;
            if (!ChadExt.Read<uint>(readAddr, out var baseMethod) || baseMethod == 0)
            { return false; }

            if (!ChadExt.Read<uint>(baseMethod, out var methodInfo) || methodInfo == 0)
            { return false; }

            if (!ChadExt.Read<uint>(methodInfo + 0x10, out var klass) || klass == 0)
            { return false; }

            if (!ChadExt.Read<uint>(klass + 0x5C, out var staticFields) || staticFields == 0)
            { return false; }

            if (!ChadExt.Read<uint>(staticFields, out instance) || instance == 0)
            { return false; }

            return true;
        }

        private static bool TryGetMData(out uint itemsArray, out int count)
        {
            itemsArray = 0;
            count = 0;

            if (!TryGetInstance(out var instance)) return false;


            if (!ChadExt.Read<uint>(instance + 0x0C, out var treeDict) || treeDict == 0)
            { return false; }


            if (!ChadExt.Read<uint>(treeDict + 0x0C, out var mDataList) || mDataList == 0)
            {  return false; }


            if (!ChadExt.Read<uint>(mDataList + 0x08, out var items) || items == 0)
            { return false; }

            if (!ChadExt.Read<int>(mDataList + 0x0C, out count) || count <= 0)
            { return false; }

            itemsArray = items + 0x10;
            return true;
        }

        private static void EnsureSwap(uint itemsArray, int count, uint ownedClothID, uint targetClothID)
        {

            if (!_swapStates.TryGetValue(ownedClothID, out var state))
            {
                if (!TryFindListEntry(itemsArray, count, ownedClothID, out var ownedSlotAddr, out var ownedPtr))
                {
                    return;
                }

                if (!TryFindListEntry(itemsArray, count, targetClothID, out _, out var targetPtr))
                {
                    return;
                }

                state = new ClothSwapState
                {
                    OwnedClothID  = ownedClothID,
                    TargetClothID = targetClothID,
                    ValueAddr     = ownedSlotAddr,
                    OriginalPtr   = ownedPtr,
                    TargetPtr     = targetPtr,
                };
                _swapStates[ownedClothID] = state;
            }
            else if (state.TargetClothID != targetClothID)
            {
                if (!TryFindListEntry(itemsArray, count, targetClothID, out _, out var newTargetPtr))
                {
                    return;
                }
                state.TargetClothID = targetClothID;
                state.TargetPtr     = newTargetPtr;
            }

            if (!ChadExt.Read<uint>(state.ValueAddr, out var current)) return;
            if (current == state.TargetPtr) return;

            bool ok = ChadExt.Write<uint>(state.ValueAddr, state.TargetPtr);
        }

        private static bool TryFindListEntry(uint itemsArray, int count, uint clothID,
                                             out uint slotAddr, out uint wardrobeDataPtr)
        {
            slotAddr = 0;
            wardrobeDataPtr = 0;

            for (int i = 0; i < count; i++)
            {
                uint slot = itemsArray + (uint)(i * 4);
                if (!ChadExt.Read<uint>(slot, out var dataPtr) || dataPtr == 0)
                    continue;

                if (!ChadExt.Read<uint>(dataPtr + 0x28, out var iID))
                    continue;

                if (iID != clothID)
                    continue;

                slotAddr = slot;
                wardrobeDataPtr = dataPtr;
                return true;
            }

            return false;
        }

        public static void RestoreOriginalCloth(uint ownedClothID)
        {
            if (!_swapStates.TryGetValue(ownedClothID, out var state)) return;

            if (state.ValueAddr == 0 || state.OriginalPtr == 0)
            {
                _swapStates.Remove(ownedClothID);
                return;
            }

            bool ok = ChadExt.Write<uint>(state.ValueAddr, state.OriginalPtr);
            _swapStates.Remove(ownedClothID);
        }

        public static void RestoreAllCloths()
        {
            foreach (var id in _swapStates.Keys.ToList())
                RestoreOriginalCloth(id);
        }

        public static string GetCategory(ClothEntry e)
        {
            if (e.Description.Contains("(Mask)")) return "Mask";
            if (e.Description.Contains("(Facepaint)")) return "Facepaint";
            if (e.IconName.Contains("hair", StringComparison.OrdinalIgnoreCase)) return "Head";
            return (e.ItemID / 1_000_000) switch
            {
                203 => "Top",
                204 => "Bottom",
                205 => "Shoes",
                211 => "Head",
                214 => "Facepaint",
                _   => "Other"
            };
        }

        public static List<ClothEntry> GetClothsByCategory(string category)
            => _db.Where(e => GetCategory(e) == category).ToList();

        public static List<string> BuildClothLabels(List<ClothEntry> clothes)
        {
            var descCount = clothes.GroupBy(e => e.Description).ToDictionary(g => g.Key, g => g.Count());
            var descIndex = new Dictionary<string, int>();
            var labels = new List<string>(clothes.Count);
            foreach (var e in clothes)
            {
                if (descCount[e.Description] > 1)
                {
                    int idx = descIndex.GetValueOrDefault(e.Description, 0) + 1;
                    descIndex[e.Description] = idx;
                    labels.Add($"{e.Description} (#{idx})");
                }
                else
                {
                    labels.Add(e.Description);
                }
            }
            return labels;
        }
    }
}
