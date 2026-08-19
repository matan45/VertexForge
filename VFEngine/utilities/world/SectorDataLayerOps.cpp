#include "SectorDataLayerOps.hpp"
#include "WorldSectorManager.hpp"
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace world
{
    namespace
    {
        // SectorCoord defines only ==/!=/+/- (WorldTypes.hpp), so every sort here needs an
        // explicit comparator. Row-major, north-up - the order the editor's sector grid walks in.
        [[nodiscard]] bool coordLess(const SectorCoord& a, const SectorCoord& b) noexcept
        {
            return (a.z != b.z) ? (a.z < b.z) : (a.x < b.x);
        }
    } // namespace

    void summarizeDataLayers(const WorldSectorManager& manager, DataLayerInventory& out)
    {
        out.loadedSectors.clear();
        out.layers.clear();

        // Index by name so a layer present on N sectors accumulates into one row. Holding an
        // INDEX rather than a pointer or iterator is deliberate: the push_back below reallocates.
        std::unordered_map<std::string, size_t> indexByName;

        manager.forEachSector([&](const WorldSector& sector)
        {
            if (sector.state != SectorState::Loaded)
                return;

            out.loadedSectors.push_back(sector.coord);

            for (const auto& [name, bytes] : sector.dataLayers)
            {
                const auto [it, inserted] = indexByName.try_emplace(name, out.layers.size());
                if (inserted)
                {
                    DataLayerSummary fresh;
                    fresh.name = name;
                    out.layers.push_back(std::move(fresh));
                }

                DataLayerSummary& entry = out.layers[it->second];
                ++entry.sectorCount;
                entry.totalBytes += bytes.size();
                entry.sectors.push_back(sector.coord);
            }
        });

        std::sort(out.loadedSectors.begin(), out.loadedSectors.end(), coordLess);
        for (auto& entry : out.layers)
            std::sort(entry.sectors.begin(), entry.sectors.end(), coordLess);

        // Invalidates indexByName - nothing reads it past this point.
        std::sort(out.layers.begin(), out.layers.end(),
                  [](const DataLayerSummary& a, const DataLayerSummary& b)
                  {
                      return a.name < b.name;
                  });
    }

    bool mergeSectorDataLayers(SectorDataLayers& resident, SectorDataLayers& incoming)
    {
        // Fast path: nothing was resident, so nothing can be unsaved. This is the overwhelmingly
        // common case - a sector loading for the first time - and it keeps the byte comparison
        // below off the hot path entirely.
        if (resident.empty())
        {
            resident = std::move(incoming);
            return false;
        }

        // A resident layer the file does not hold, or holds with different bytes, is unsaved work.
        // Comparing the bytes matters: a runtime write to a layer that already exists on disk is
        // exactly the case that must survive the dirty reset in finalizeSectorLoad.
        bool unsaved = false;
        for (const auto& [name, bytes] : resident)
        {
            const auto it = incoming.find(name);
            if (it == incoming.end() || it->second != bytes)
            {
                unsaved = true;
                break;
            }
        }

        for (auto& [name, bytes] : incoming)
            resident.try_emplace(name, std::move(bytes));

        return unsaved;
    }

} // namespace world
