#include "TerrainService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainWeightMapAsset.hpp"
#include "vegetation/VegetationSerializer.hpp"
#include "../../data/EntityConversion.hpp"
#include <filesystem>
#include <format>

namespace services
{
    bool TerrainService::saveWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto allTiles = gridIt->second->getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainService: No tiles to save weight maps for");
            return true;
        }

        uint32_t resolution = allTiles[0]->config.getVertexCount();

        std::string materialPath;
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
        if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
        {
            materialPath = registry.get<components::TerrainComponent>(ent).terrainMaterialPath;
        }

        std::unordered_map<terrain::TileCoord, terrain::TileWeightMapData, terrain::TileCoordHash> tileWeights;
        for (const auto* tile : allTiles)
        {
            if (tile->hasWeightMap())
            {
                tileWeights.emplace(tile->coord, tile->weightMap);
            }
        }

        return terrain::TerrainWeightMapAsset::save(path, tileWeights, resolution, materialPath);
    }

    bool TerrainService::loadWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        std::string materialPath;
        auto loadedWeights = terrain::TerrainWeightMapAsset::load(path, &materialPath);
        if (loadedWeights.empty())
        {
            return false;
        }

        if (!materialPath.empty())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
            {
                registry.get<components::TerrainComponent>(ent).terrainMaterialPath = materialPath;
                syncWeightMapLayerCount(terrainEntityId, materialPath);
            }
        }

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedCount = 0;

        for (auto* tile : allTiles)
        {
            auto it = loadedWeights.find(tile->coord);
            if (it != loadedWeights.end())
            {
                if (it->second.resolution == tile->config.getVertexCount())
                {
                    tile->weightMap = std::move(it->second);
                    tile->weightMapDirty = true;
                    tile->weightMapGPUDirty = true;
                    loadedCount++;
                }
                else
                {
                    vfLogWarning("TerrainService: Weight map resolution mismatch for tile ({}, {}): "
                                 "expected {}, got {}",
                                 tile->coord.x, tile->coord.z,
                                 tile->config.getVertexCount(), it->second.resolution);
                }
            }
        }

        vfLogInfo("TerrainService: Loaded weight maps for {} of {} tiles", loadedCount, allTiles.size());
        return true;
    }

    std::string TerrainService::getVegetationDirectory(const std::string& terrainPath)
    {
        namespace fs = std::filesystem;
        fs::path p(terrainPath);
        return (p.parent_path() / (p.stem().string() + "_vegetation")).string();
    }

    bool TerrainService::saveVegetation(uint64_t terrainEntityId, const std::string& terrainPath)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        namespace fs = std::filesystem;
        std::string vegDir = getVegetationDirectory(terrainPath);

        auto allTiles = gridIt->second->getAllTiles();
        bool anyData = false;

        for (const auto* tile : allTiles)
        {
            if (!tile) continue;

            if (tile->vegetationDensity.isInitialized())
            {
                anyData = true;
                std::string densityPath = std::format("{}/tile_{}_{}.vfVegDensity",
                    vegDir, tile->coord.x, tile->coord.z);
                if (!vegetation::VegetationSerializer::saveDensityMap(densityPath, tile->vegetationDensity))
                {
                    vfLogError("TerrainService: Failed to save vegetation density for tile ({}, {})",
                               tile->coord.x, tile->coord.z);
                }
            }

        }

        if (anyData)
            vfLogInfo("TerrainService: Saved vegetation data to {}", vegDir);

        return true;
    }

    bool TerrainService::loadVegetation(uint64_t terrainEntityId, const std::string& terrainPath)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        namespace fs = std::filesystem;
        std::string vegDir = getVegetationDirectory(terrainPath);

        if (!fs::exists(vegDir) || !fs::is_directory(vegDir))
            return true; // No vegetation data — not an error

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedDensity = 0;

        for (auto* tile : allTiles)
        {
            if (!tile) continue;

            std::string densityPath = std::format("{}/tile_{}_{}.vfVegDensity",
                vegDir, tile->coord.x, tile->coord.z);
            if (fs::exists(densityPath))
            {
                if (vegetation::VegetationSerializer::loadDensityMap(densityPath, tile->vegetationDensity))
                {
                    tile->vegetationDensityDirty = true;
                    tile->vegetationDensityGPUDirty = true;
                    loadedDensity++;
                }
            }

        }

        if (loadedDensity > 0)
        {
            vfLogInfo("TerrainService: Loaded vegetation density data ({} tiles) from {}",
                      loadedDensity, vegDir);
        }

        return true;
    }
}
