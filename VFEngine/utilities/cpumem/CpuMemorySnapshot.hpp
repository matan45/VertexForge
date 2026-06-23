#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Pure-data header shared between the CpuMemory DLL and the editor diagnostics
// window. No export macro needed: the PODs below are passed by value across the
// DLL boundary (same convention as asset::AssetDatabase::getAllAssets()), and
// the producing member functions carry the export.

namespace memory
{
    using CategoryId = uint32_t;
    inline constexpr CategoryId kInvalidCategory = 0xFFFFFFFFu;

    // Coarse classification of a named CPU RAM category, for diagnostics grouping.
    enum class CategoryKind : uint8_t
    {
        AssetDecoded, // decoded asset bytes (note: the authoritative decoded total
                      //   lives in resource::AssetLifecycleManager, not here)
        Transient,    // short-lived import/decode scratch (mip chains, aiScene, PCM)
        Staging,      // CPU-visible upload staging (ring + overflow)
        Other
    };

    // Asset-load CPU budget gate state, published by the resource scheduler each
    // tick so the diagnostics window shows the real state instead of recomputing.
    enum class GateState : uint8_t
    {
        Open,            // budget disabled, or under budget — loads admitted
        Closed,          // over budget — non-Critical loads are being deferred
        CriticalOverage  // over budget, but a Critical load was admitted anyway
    };

    inline const char* categoryKindName(CategoryKind k)
    {
        switch (k)
        {
        case CategoryKind::AssetDecoded: return "Asset";
        case CategoryKind::Transient:    return "Transient";
        case CategoryKind::Staging:      return "Staging";
        case CategoryKind::Other:        return "Other";
        }
        return "Unknown";
    }

    inline const char* gateStateName(GateState s)
    {
        switch (s)
        {
        case GateState::Open:            return "Open";
        case GateState::Closed:          return "Closed";
        case GateState::CriticalOverage: return "Critical Overage";
        }
        return "Unknown";
    }

    struct CategoryView
    {
        std::string name;
        CategoryKind kind = CategoryKind::Other;
        uint64_t bytes = 0;
        uint64_t peak = 0;
    };

    // Cold-path snapshot for the editor's Memory Diagnostics CPU tab.
    struct CpuMemorySnapshotData
    {
        std::vector<CategoryView> categories;
        uint64_t totalTrackedBytes = 0;   // sum of CpuMemory's own categories (transient+staging)
        uint64_t totalReservedBytes = 0;  // outstanding pre-load reservations
        uint64_t budgetBytes = 0;         // 0 = advisory/disabled
        GateState gateState = GateState::Open;
        uint32_t deferredLoadCount = 0;   // non-Critical loads currently deferred by the gate
    };
}
