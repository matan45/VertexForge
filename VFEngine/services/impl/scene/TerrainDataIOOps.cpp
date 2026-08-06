#include "TerrainService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainWeightMapAsset.hpp"
#include "vegetation/VegetationSerializer.hpp"
#include "vegetation/ScatterProfileSerialization.hpp" // VK-1585 foliage scatter sidecar
#include "foliage/FoliageSerializer.hpp"
#include "resource/VirtualFileSystem.hpp"
#include <asset/AssetRef.hpp>
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include <filesystem>
#include <format>

namespace services
{
    bool TerrainService::saveWeightMaps(uint64_t terrainEntityId, const std::string& path)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

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
            materialPath = registry.get<components::TerrainComponent>(ent).terrainMaterialRef.resolve();
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
                registry.get<components::TerrainComponent>(ent).terrainMaterialRef = asset::AssetRef::fromPath(materialPath);
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
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        namespace fs = std::filesystem;
        std::string vegDir = getVegetationDirectory(terrainPath);

        auto allTiles = gridIt->second->getAllTiles();
        bool anyData = false;

        for (const auto* tile : allTiles)
        {
            if (!tile || tile->billboardInstances.empty()) continue;

            anyData = true;
            std::string instancesPath = std::format("{}/tile_{}_{}.vfVegInstances",
                vegDir, tile->coord.x, tile->coord.z);
            if (!vegetation::VegetationSerializer::saveBillboardInstances(instancesPath, tile->billboardInstances))
            {
                vfLogError("TerrainService: Failed to save billboard instances for tile ({}, {})",
                           tile->coord.x, tile->coord.z);
            }
        }

        // Save billboard palette alongside instance data
        try {
            auto palette = events::EventDispatcher::instance().query(
                events::vegetation::GetBillboardPaletteQuery{});
            if (!palette.empty())
            {
                vegetation::VegetationSerializer::saveBillboardPalette(
                    vegDir + "/billboard_palette.vfBillboard", palette);
                anyData = true;
            }
        } catch (...) {}

        if (anyData)
            vfLogInfo("TerrainService: Saved vegetation data to {}", vegDir);

        return true;
    }

    bool TerrainService::loadVegetation(uint64_t terrainEntityId, const std::string& terrainPath)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        std::string vegDir = getVegetationDirectory(terrainPath);

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedCount = 0;

        for (auto* tile : allTiles)
        {
            if (!tile) continue;

            std::string instancesPath = std::format("{}/tile_{}_{}.vfVegInstances",
                vegDir, tile->coord.x, tile->coord.z);
            if (resource::VirtualFileSystem::instance().exists(instancesPath))
            {
                if (vegetation::VegetationSerializer::loadBillboardInstances(instancesPath, tile->billboardInstances))
                {
                    tile->billboardInstancesDirty = true;
                    tile->billboardInstancesGPUDirty = true;
                    loadedCount++;
                }
            }
        }

        // Load billboard palette
        std::string palettePath = vegDir + "/billboard_palette.vfBillboard";
        if (resource::VirtualFileSystem::instance().exists(palettePath))
        {
            std::vector<vegetation::BillboardPaletteEntry> palette;
            if (vegetation::VegetationSerializer::loadBillboardPalette(palettePath, palette))
            {
                // Store on a GrassComponent so the renderer can find it
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity terrainEntity = internal::fromHandle(EntityHandle{terrainEntityId});
                if (registry.valid(terrainEntity))
                {
                    if (!registry.all_of<components::GrassComponent>(terrainEntity))
                        registry.emplace<components::GrassComponent>(terrainEntity);
                    registry.get<components::GrassComponent>(terrainEntity).billboardPalette = palette;
                    vfLogInfo("TerrainService: Loaded billboard palette ({} entries)", palette.size());
                }
            }
        }

        if (loadedCount > 0)
        {
            vfLogInfo("TerrainService: Loaded billboard instances ({} tiles) from {}",
                      loadedCount, vegDir);
        }

        return true;
    }

    std::string TerrainService::getFoliageDirectory(const std::string& terrainPath)
    {
        namespace fs = std::filesystem;
        fs::path p(terrainPath);
        return (p.parent_path() / (p.stem().string() + "_foliage")).string();
    }

    bool TerrainService::saveFoliage(uint64_t terrainEntityId, const std::string& terrainPath)
    {
        if (resource::VirtualFileSystem::instance().isArchiveMode())
            return false;

        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        namespace fs = std::filesystem;
        std::string foliageDir = getFoliageDirectory(terrainPath);

        auto allTiles = gridIt->second->getAllTiles();
        bool anyData = false;

        for (const auto* tile : allTiles)
        {
            if (!tile) continue;

            std::string instancesPath = std::format("{}/tile_{}_{}.vfFoliage",
                foliageDir, tile->coord.x, tile->coord.z);

            if (tile->foliageInstances.empty())
            {
                // Tile fully erased: drop any stale sidecar so a later load doesn't resurrect it.
                std::error_code ec;
                if (fs::exists(instancesPath)) fs::remove(instancesPath, ec);
                continue;
            }

            anyData = true;
            if (!foliage::FoliageSerializer::saveFoliageInstances(instancesPath, tile->foliageInstances))
            {
                vfLogError("TerrainService: Failed to save foliage instances for tile ({}, {})",
                           tile->coord.x, tile->coord.z);
            }
        }

        // Save the foliage palette (lives on TerrainService, not on a component).
        if (!foliagePalette.empty())
        {
            foliage::FoliageSerializer::saveFoliagePalette(
                foliageDir + "/foliage_palette.json", foliagePalette);
            anyData = true;
        }

        // Save the procedural foliage scatter profile (VK-1585) as a sidecar alongside the palette.
        if (!foliageScatterProfile.rules.empty())
        {
            std::error_code ec;
            fs::create_directories(foliageDir, ec);
            vegetation::saveScatterProfileFile(foliageDir + "/foliage_scatter.json", foliageScatterProfile);
            anyData = true;
        }

        if (anyData)
            vfLogInfo("TerrainService: Saved foliage data to {}", foliageDir);

        return true;
    }

    bool TerrainService::loadFoliage(uint64_t terrainEntityId, const std::string& terrainPath)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        std::string foliageDir = getFoliageDirectory(terrainPath);

        auto allTiles = gridIt->second->getAllTiles();
        uint32_t loadedCount = 0;

        for (auto* tile : allTiles)
        {
            if (!tile) continue;

            std::string instancesPath = std::format("{}/tile_{}_{}.vfFoliage",
                foliageDir, tile->coord.x, tile->coord.z);
            if (resource::VirtualFileSystem::instance().exists(instancesPath))
            {
                if (foliage::FoliageSerializer::loadFoliageInstances(instancesPath, tile->foliageInstances))
                {
                    tile->foliageInstancesDirty = true;
                    tile->foliageInstancesGPUDirty = true;
                    loadedCount++;
                }
            }
        }

        // Load the foliage palette onto TerrainService (drives the render collector).
        std::string palettePath = foliageDir + "/foliage_palette.json";
        if (resource::VirtualFileSystem::instance().exists(palettePath))
        {
            std::vector<foliage::FoliageType> palette;
            if (foliage::FoliageSerializer::loadFoliagePalette(palettePath, palette))
            {
                setFoliagePalette(std::move(palette));
                vfLogInfo("TerrainService: Loaded foliage palette ({} entries)", foliagePalette.size());
            }
        }

        // Load the procedural foliage scatter profile (VK-1585).
        std::string scatterPath = foliageDir + "/foliage_scatter.json";
        if (resource::VirtualFileSystem::instance().exists(scatterPath))
        {
            vegetation::ScatterProfile profile;
            if (vegetation::loadScatterProfileFile(scatterPath, profile))
                setFoliageScatterProfile(std::move(profile));
        }

        if (loadedCount > 0)
        {
            vfLogInfo("TerrainService: Loaded foliage instances ({} tiles) from {}",
                      loadedCount, foliageDir);
        }

        return true;
    }
}
