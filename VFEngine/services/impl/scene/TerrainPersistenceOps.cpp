#include "TerrainService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include <asset/AssetRef.hpp>
#include "threading/JobSystem.hpp"
#include <asset/AssetDatabase.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include "../../../graphics/render/svt/SVTFileFormat.hpp"
#include "../../../graphics/render/svt/SVTTypes.hpp"
#include "resource/BC7Encoder.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/TerrainWeightMap.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>

namespace services
{
    bool TerrainService::prepareSave(uint64_t terrainEntityId)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
            return true;

        auto& grid = *gridIt->second;
        auto& generator = grid.getGenerator();
        auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
            return grid.getTile(coord);
        };

        // Stream in all previously-saved tiles from file cache that aren't in the grid
        // so they are included in the save (streaming may have unloaded them).
        // Uses getSavedCoords() to skip entries with no file data (newly added, never saved).
        // This MUST run on the main thread to avoid racing with the render thread.
        auto availableCoords = cacheIt->second->getSavedCoords();
        for (const auto& coord : availableCoords)
        {
            if (!grid.getTile(coord))
            {
                grid.addTileFromFile(coord);
            }
        }

        for (auto* tile : grid.getAllTiles())
        {
            if (tile && !tile->hasHeightData())
                cacheIt->second->ensureHeightsLoaded(*tile);
            if (tile && !tile->hasAnyLODData())
                cacheIt->second->ensureLODsLoaded(*tile, generator, getTile);
        }

        return true;
    }

    bool TerrainService::prepareSaveIncremental(uint64_t terrainEntityId)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
            return false;

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
            return prepareSave(terrainEntityId);

        auto& grid = *gridIt->second;
        auto& cache = *cacheIt->second;

        // Detect conditions that force a full save BEFORE the background thread starts,
        // because prepareSave() adds tiles to the grid (not thread-safe).
        bool needsFullSave = cache.hasNewOrRemovedTiles();

        // Check if header size would change (physics/streaming flags toggled).
        // NOTE: TerrainSerializer::saveIncremental() has a matching guard as a safety net.
        // Both must agree — if updating one, update the other.
        if (!needsFullSave)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});
            const auto& savedHeader = cache.getHeader();

            bool hadPhysics = terrain::hasFlag(savedHeader.flags, terrain::TerrainFormatFlags::HAS_PHYSICS_DATA);
            bool hasPhysicsNow = false;
            if (registry.valid(ent) && registry.all_of<components::TerrainColliderComponent>(ent))
                hasPhysicsNow = registry.get<components::TerrainColliderComponent>(ent).hasCollider;

            bool hadStreaming = terrain::hasFlag(savedHeader.flags, terrain::TerrainFormatFlags::HAS_STREAMING_CONFIG);
            bool hasStreamingNow = false;
            auto streamerIt = worldStreamers.find(terrainEntityId);
            if (streamerIt != worldStreamers.end() && streamerIt->second)
                hasStreamingNow = streamerIt->second->isEnabled();

            if (hadPhysics != hasPhysicsNow || hadStreaming != hasStreamingNow)
                needsFullSave = true;
        }

        if (needsFullSave)
        {
            vfLogInfo("TerrainService: Incremental save not possible, preparing full save");
            return prepareSave(terrainEntityId);
        }

        auto& generator = grid.getGenerator();
        auto getTile = [&grid](const terrain::TileCoord& coord) -> const terrain::TerrainTile* {
            return grid.getTile(coord);
        };

        // Only ensure dirty tiles have heights + LODs loaded
        for (const auto& coord : cache.getDirtyCoords())
        {
            auto* tile = grid.getTile(coord);
            if (!tile) continue;

            if (!tile->hasHeightData())
                cache.ensureHeightsLoaded(*tile);
            if (!tile->hasAnyLODData())
                cache.ensureLODsLoaded(*tile, generator, getTile);
        }

        return true;
    }

    bool TerrainService::saveTerrainIncremental(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto cacheIt = fileCaches.find(terrainEntityId);
        if (cacheIt == fileCaches.end() || !cacheIt->second)
        {
            vfLogInfo("TerrainService: No file cache, falling back to full save");
            return saveTerrain(terrainEntityId, path);
        }

        auto& cache = *cacheIt->second;

        // If tiles were added/removed, tile count changed → fall back to full save
        // Note: prepareSaveIncremental() on the main thread should have already detected this
        // and called prepareSave(). We only call saveTerrain() here (no prepareSave — unsafe on bg thread).
        if (cache.hasNewOrRemovedTiles())
        {
            vfLogInfo("TerrainService: Tiles added/removed, falling back to full save");
            return saveTerrain(terrainEntityId, path);
        }

        if (cache.getDirtyCount() == 0)
        {
            // No dirty tiles — but prepareSaveIncremental() may have detected a header change
            // (e.g. streaming/physics toggled) and called prepareSave() for a full save.
            // Fall back to full save to persist those config changes.
            vfLogInfo("TerrainService: No dirty tiles, falling back to full save for config changes");
            return saveTerrain(terrainEntityId, path);
        }

        // Gather physics and streaming config from components
        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("TerrainService: Entity {} has no TerrainComponent", terrainEntityId);
            return false;
        }

        terrain::TerrainPhysicsConfig physicsConfig;
        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            physicsConfig.hasCollider = cc.hasCollider;
            physicsConfig.collisionLayer = cc.collisionLayer;
            physicsConfig.friction = cc.friction;
            physicsConfig.restitution = cc.restitution;
        }

        terrain::TerrainStreamingConfig streamingConfig;
        auto streamerIt = worldStreamers.find(terrainEntityId);
        if (streamerIt != worldStreamers.end() && streamerIt->second)
        {
            streamingConfig.enabled = streamerIt->second->isEnabled();
            const auto& cfg = streamerIt->second->getConfig();
            streamingConfig.loadRadius = cfg.loadRadius;
            streamingConfig.unloadRadius = cfg.unloadRadius;
            streamingConfig.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
            streamingConfig.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
        }

        terrain::TerrainIncrementalSaveParams incParams;
        incParams.path = path;
        incParams.grid = gridIt->second.get();
        incParams.dirtyCoords = &cache.getDirtyCoords();
        incParams.currentHeader = cache.getHeader();
        incParams.indexTableOffset = cache.getIndexTableOffset();
        incParams.currentIndexMap = &cache.getIndexMap();
        incParams.physicsConfig = physicsConfig;
        incParams.streamingConfig = streamingConfig;

        bool result = terrain::TerrainSerializer::saveIncremental(incParams);

        if (!result)
        {
            // Fall back to full save (no prepareSave — unsafe on background thread)
            // saveTerrain() will save whatever tiles are currently in the grid
            vfLogWarning("TerrainService: Incremental save failed, falling back to full save");
            return saveTerrain(terrainEntityId, path);
        }

        // Post-save bookkeeping
        size_t savedCount = cache.getDirtyCount();

        saveVegetation(terrainEntityId, path);

        auto& comp = registry.get<components::TerrainComponent>(ent);
        comp.saveDirty = false;

        cache.refreshIndex(path);

        events::terrain::TerrainSavedNotification savedNotification;
        savedNotification.terrainEntity = EntityHandle{terrainEntityId};
        savedNotification.path = path;
        events::EventDispatcher::instance().publish(savedNotification);

        vfLogInfo("TerrainService: Incremental save completed ({} tiles updated)", savedCount);
        return true;
    }

    bool TerrainService::saveTerrain(uint64_t terrainEntityId, const std::string& path)
    {
        auto gridIt = terrainGrids.find(terrainEntityId);
        if (gridIt == terrainGrids.end())
        {
            vfLogError("TerrainService: No terrain grid for entity {}", terrainEntityId);
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(EntityHandle{terrainEntityId});

        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("TerrainService: Entity {} has no TerrainComponent", terrainEntityId);
            return false;
        }

        const auto& comp = registry.get<components::TerrainComponent>(ent);

        terrain::TerrainTileConfig tileConfig;
        switch (comp.resolution)
        {
        case 0: tileConfig.resolution = terrain::TileResolution::Low; break;
        case 1: tileConfig.resolution = terrain::TileResolution::Medium; break;
        case 2: tileConfig.resolution = terrain::TileResolution::High; break;
        default: tileConfig.resolution = terrain::TileResolution::Low; break;
        }
        tileConfig.worldTileSize = comp.worldTileSize;
        tileConfig.maxHeight = comp.maxHeight;
        tileConfig.minHeight = comp.minHeight;

        terrain::TerrainPhysicsConfig physicsConfig;
        if (registry.all_of<components::TerrainColliderComponent>(ent))
        {
            const auto& cc = registry.get<components::TerrainColliderComponent>(ent);
            physicsConfig.hasCollider = cc.hasCollider;
            physicsConfig.collisionLayer = cc.collisionLayer;
            physicsConfig.friction = cc.friction;
            physicsConfig.restitution = cc.restitution;
        }

        terrain::TerrainStreamingConfig streamingConfig;
        auto streamerIt = worldStreamers.find(terrainEntityId);
        if (streamerIt != worldStreamers.end() && streamerIt->second)
        {
            streamingConfig.enabled = streamerIt->second->isEnabled();
            const auto& cfg = streamerIt->second->getConfig();
            streamingConfig.loadRadius = cfg.loadRadius;
            streamingConfig.unloadRadius = cfg.unloadRadius;
            streamingConfig.maxLoadsPerFrame = cfg.maxLoadsPerFrame;
            streamingConfig.maxUnloadsPerFrame = cfg.maxUnloadsPerFrame;
        }

        // Compute actual bounds from grid tiles (supports sparse/dynamic grids)
        int32_t boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ;
        gridIt->second->computeBounds(boundsMinX, boundsMinZ, boundsMaxX, boundsMaxZ);

        terrain::TerrainSaveParams saveParams;
        saveParams.path = path;
        saveParams.grid = gridIt->second.get();
        saveParams.config = tileConfig;
        saveParams.gridMinX = boundsMinX;
        saveParams.gridMinZ = boundsMinZ;
        saveParams.gridMaxX = boundsMaxX;
        saveParams.gridMaxZ = boundsMaxZ;
        saveParams.materialPath = comp.terrainMaterialRef.resolve();
        saveParams.physicsConfig = physicsConfig;
        saveParams.streamingConfig = streamingConfig;

        bool result = terrain::TerrainSerializer::save(saveParams);

        if (result)
        {
            saveVegetation(terrainEntityId, path);

            auto& mutableComp = registry.get<components::TerrainComponent>(ent);
            mutableComp.savePath = path;
            mutableComp.saveDirty = false;
            mutableComp.gridMinX = boundsMinX;
            mutableComp.gridMinZ = boundsMinZ;
            mutableComp.gridMaxX = boundsMaxX;
            mutableComp.gridMaxZ = boundsMaxZ;
            mutableComp.activeTileCount = static_cast<uint32_t>(gridIt->second->getTileCount());

            auto cacheIt = fileCaches.find(terrainEntityId);
            if (cacheIt != fileCaches.end() && cacheIt->second)
            {
                cacheIt->second->refreshIndex(path);
            }
            else
            {
                terrain::TerrainFileHeader newHeader;
                std::vector<terrain::TileIndexEntry> newIndex;
                uint64_t newIndexOffset = 0;
                if (terrain::TerrainSerializer::readHeader(path, newHeader, newIndex, &newIndexOffset))
                {
                    auto cache = std::make_shared<terrain::TerrainFileCache>(path, newHeader, newIndex, newIndexOffset);
                    fileCaches[terrainEntityId] = cache;
                    gridIt->second->setFileCache(cache);
                }
            }

            // Create or update .vfmeta sidecar
            {
                auto& db = asset::AssetDatabase::instance();
                auto metaPath = asset::AssetMetadataSerializer::getMetaPath(path);
                auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

                asset::AssetMetadata metadata;
                if (existingMeta.has_value())
                {
                    metadata.guid = existingMeta->guid;
                }
                else
                {
                    metadata.guid = asset::AssetGUID::generate();
                }
                metadata.type = resource::AssetType::Terrain;
                metadata.importSourcePath = path;
                {
                    auto now = std::chrono::system_clock::now();
                    auto time = std::chrono::system_clock::to_time_t(now);
                    std::tm tm{};
                    localtime_s(&tm, &time);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
                    metadata.importTimestamp = oss.str();
                }
                asset::AssetMetadataSerializer::save(metadata, metaPath);

                if (!db.getGUID(path).has_value())
                {
                    db.registerAssetWithGUID(metadata.guid, path, resource::AssetType::Terrain);
                }
            }

            events::terrain::TerrainSavedNotification savedNotification;
            savedNotification.terrainEntity = EntityHandle{terrainEntityId};
            savedNotification.path = path;
            events::EventDispatcher::instance().publish(savedNotification);

            vfLogInfo("TerrainService: Saved terrain to {}", path);
        }

        return result;
    }

    EntityHandle TerrainService::loadTerrain(const std::string& path)
    {
        terrain::TerrainFileHeader header;
        std::vector<terrain::TileIndexEntry> index;
        uint64_t indexTableOffset = 0;

        if (!terrain::TerrainSerializer::readHeader(path, header, index, &indexTableOffset))
        {
            vfLogError("TerrainService: Failed to read terrain header from {}", path);
            return {};
        }

        return finishLoadTerrain(header, index, path, indexTableOffset);
    }

    void TerrainService::loadInitialTiles(
        terrain::TerrainGrid& grid,
        const terrain::TerrainFileHeader& header,
        const std::vector<terrain::TileIndexEntry>& index)
    {
        if (header.streamingConfig.enabled)
        {
            float loadRadiusSq = header.streamingConfig.loadRadius * header.streamingConfig.loadRadius;
            float tileSize = header.worldTileSize;

            for (const auto& entry : index)
            {
                float cx = (static_cast<float>(entry.coordX) + 0.5f) * tileSize;
                float cz = (static_cast<float>(entry.coordZ) + 0.5f) * tileSize;
                if (cx * cx + cz * cz <= loadRadiusSq)
                    grid.addTileFromFile(terrain::TileCoord{entry.coordX, entry.coordZ});
            }
        }
        else
        {
            grid.loadMetadataOnly(header, index);
        }
    }

    void TerrainService::initTerrainComponent(
        components::TerrainComponent& comp,
        const terrain::TerrainFileHeader& header,
        const std::string& path,
        uint32_t activeTileCount)
    {
        comp.resolution = header.resolution;
        comp.worldTileSize = header.worldTileSize;
        comp.maxHeight = header.maxHeight;
        comp.minHeight = header.minHeight;
        comp.gridMinX = header.gridMinX;
        comp.gridMinZ = header.gridMinZ;
        comp.gridMaxX = header.gridMaxX;
        comp.gridMaxZ = header.gridMaxZ;
        comp.terrainMaterialRef = asset::AssetRef::fromPath(header.materialPath);
        comp.isActive = true;
        comp.isDirty = false;
        comp.activeTileCount = activeTileCount;
        comp.visibleTileCount = 0;
        comp.savePath = path;
        comp.saveDirty = false;
    }

    void TerrainService::publishTerrainCreated(EntityHandle handle, const terrain::TerrainFileHeader& header)
    {
        events::terrain::TerrainCreatedNotification notification;
        notification.terrainEntity = handle;
        notification.config.resolution = header.resolution;
        notification.config.worldTileSize = header.worldTileSize;
        notification.config.maxHeight = header.maxHeight;
        notification.config.minHeight = header.minHeight;
        notification.config.terrainMaterialPath = header.materialPath;
        notification.config.tilesX = header.gridMaxX - header.gridMinX + 1;
        notification.config.tilesZ = header.gridMaxZ - header.gridMinZ + 1;
        events::EventDispatcher::instance().publish(notification);
    }

    EntityHandle TerrainService::finishLoadTerrain(
        terrain::TerrainFileHeader& header,
        std::vector<terrain::TileIndexEntry>& index,
        const std::string& path,
        uint64_t indexTableOffset)
    {
        terrain::TerrainTileConfig tileConfig;
        tileConfig.resolution = static_cast<terrain::TileResolution>(header.resolution);
        tileConfig.worldTileSize = header.worldTileSize;
        tileConfig.maxHeight = header.maxHeight;
        tileConfig.minHeight = header.minHeight;
        tileConfig.skirtDepth = header.skirtDepth;

        auto grid = std::make_unique<terrain::TerrainGrid>(tileConfig);
        auto cache = std::make_shared<terrain::TerrainFileCache>(path, header, index, indexTableOffset);
        grid->setFileCache(cache);
        loadInitialTiles(*grid, header, index);

        scene::Entity parentEntity("Terrain");
        sceneGraph->addChild(sceneGraph->GetRoot(), parentEntity);

        auto& terrainComp = parentEntity.addComponent<components::TerrainComponent>();
        initTerrainComponent(terrainComp, header, path, static_cast<uint32_t>(grid->getTileCount()));

        EntityHandle parentHandle = internal::toHandle(parentEntity.getHandle());
        createTileEntities(parentHandle, *grid);

        terrainGrids[parentHandle.id] = std::move(grid);
        fileCaches[parentHandle.id] = cache;

        terrain::StreamingConfig stCfg{
            header.streamingConfig.loadRadius, header.streamingConfig.unloadRadius,
            header.streamingConfig.maxLoadsPerFrame, header.streamingConfig.maxUnloadsPerFrame};
        auto streamer = std::make_unique<terrain::TerrainWorldStreamer>(stCfg);
        streamer->setEnabled(header.streamingConfig.enabled);
        worldStreamers[parentHandle.id] = std::move(streamer);

        if (!header.materialPath.empty())
            syncWeightMapLayerCount(parentHandle.id, header.materialPath);

        publishTerrainCreated(parentHandle, header);

        if (header.physicsConfig.hasCollider && physicsProvider)
        {
            auto& cc = parentEntity.addComponent<components::TerrainColliderComponent>();
            cc.collisionLayer = header.physicsConfig.collisionLayer;
            cc.friction = header.physicsConfig.friction;
            cc.restitution = header.physicsConfig.restitution;
            addTerrainCollider(parentHandle);
        }

        if (header.streamingConfig.enabled)
            vfLogInfo("TerrainService: Loaded terrain with {}/{} initial tiles (streaming) from {}",
                      terrainComp.activeTileCount, header.tileCount, path);
        else
            vfLogInfo("TerrainService: Loaded terrain with {} tiles from {}", header.tileCount, path);

        loadVegetation(parentHandle.id, path);
        return parentHandle;
    }

    struct TerrainService::BakeLayerCPU
    {
        std::shared_ptr<resource::TextureData> albedo;
        std::shared_ptr<resource::TextureData> normal;
        std::shared_ptr<resource::TextureData> orm;
        float tilingScale = 1.0f;
        float roughness = 0.5f, metallic = 0.0f, ao = 1.0f;
    };

    struct TerrainService::BakeParams
    {
        uint32_t tileSizeLog2 = 7;
        uint32_t virtualSizeLog2 = 0;
        uint32_t mipLevels = 0;
        uint32_t border = 4;
        uint32_t tileSize = 128;
        uint32_t physTileSize = 136;
        float wMinX = 0, wMinZ = 0;
        float terrainWidth = 0, terrainDepth = 0;
    };

    namespace
    {
        glm::vec4 sampleTexCPU(const resource::TextureData* tex, float worldX, float worldZ, float tilingScale)
        {
            if (!tex || tex->mipData.empty() || tex->width == 0)
                return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
            if (tex->compressionFormat != resource::TextureCompressionFormat::Uncompressed)
                return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);

            float u = worldX * tilingScale;
            float v = worldZ * tilingScale;
            u -= std::floor(u);
            v -= std::floor(v);

            uint32_t px = static_cast<uint32_t>(u * (tex->width - 1));
            uint32_t py = static_cast<uint32_t>(v * (tex->height - 1));
            uint32_t ch = tex->numbersOfChannels;
            if (ch == 0) ch = 4;

            const auto& mip0 = tex->mipData[0];
            size_t idx = (static_cast<size_t>(py) * tex->width + px) * ch;
            if (idx + 2 >= mip0.data.size()) return glm::vec4(0.5f);

            return glm::vec4(mip0.data[idx] / 255.0f, mip0.data[idx + 1] / 255.0f,
                             mip0.data[idx + 2] / 255.0f,
                             (ch >= 4 && idx + 3 < mip0.data.size()) ? mip0.data[idx + 3] / 255.0f : 1.0f);
        }
    }

    bool TerrainService::loadBakeMaterial(const std::string& materialPath, std::vector<BakeLayerCPU>& layers)
    {
        auto materialData = resource::ResourceManager::loadTerrainMaterial(
            asset::AssetRef::fromPath(materialPath));
        if (!materialData || materialData->activeLayerCount == 0)
            return false;

        layers.resize(materialData->activeLayerCount);
        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const auto& ml = materialData->layers[i];
            auto loadTex = [](const std::string& path) -> std::shared_ptr<resource::TextureData>
            {
                if (path.empty()) return nullptr;
                try { return resource::ResourceManager::loadTextureAsync(asset::AssetRef::fromPath(path)).get(); }
                catch (...) { return nullptr; }
            };

            layers[i].albedo = loadTex(ml.albedoTextureRef.resolve());
            layers[i].normal = loadTex(ml.normalTextureRef.resolve());
            layers[i].orm = loadTex(ml.ormTextureRef.resolve());
            layers[i].tilingScale = ml.tilingScale;
            layers[i].roughness = ml.roughness;
            layers[i].metallic = ml.metallic;
            layers[i].ao = ml.ao;
        }
        return true;
    }

    bool TerrainService::computeBakeParams(const components::TerrainComponent& comp, BakeParams& params)
    {
        float worldTileSize = comp.worldTileSize;
        params.wMinX = static_cast<float>(comp.gridMinX) * worldTileSize;
        params.wMinZ = static_cast<float>(comp.gridMinZ) * worldTileSize;
        float wMaxX = static_cast<float>(comp.gridMaxX + 1) * worldTileSize;
        float wMaxZ = static_cast<float>(comp.gridMaxZ + 1) * worldTileSize;

        params.terrainWidth = wMaxX - params.wMinX;
        params.terrainDepth = wMaxZ - params.wMinZ;
        float maxDim = std::max(params.terrainWidth, params.terrainDepth);

        if (maxDim <= 0.0f) return false;

        params.tileSizeLog2 = 7;
        params.tileSize = 1u << params.tileSizeLog2;
        uint32_t virtualSize = params.tileSize;
        params.virtualSizeLog2 = params.tileSizeLog2;
        uint32_t targetTexels = static_cast<uint32_t>(maxDim * 16.0f);
        while (virtualSize < targetTexels && params.virtualSizeLog2 < 17)
        {
            virtualSize <<= 1;
            ++params.virtualSizeLog2;
        }

        params.mipLevels = render::svt::computeMipLevelCount(params.virtualSizeLog2, params.tileSizeLog2);
        params.border = 4;
        params.physTileSize = params.tileSize + 2 * params.border;
        return true;
    }

    void TerrainService::compositeAndWriteTile(const BakeParams& params,
                                                const std::vector<BakeLayerCPU>& layers,
                                                const std::vector<const terrain::TerrainTile*>& allTiles,
                                                uint32_t mip, uint32_t tx, uint32_t ty,
                                                render::svt::SVTFileWriter& albedoWriter,
                                                render::svt::SVTFileWriter& normalWriter,
                                                render::svt::SVTFileWriter& ormWriter)
    {
        uint32_t pts = params.physTileSize;
        float mipVirtualSize = static_cast<float>(1u << (params.virtualSizeLog2 - mip));
        float texelToWorld = params.terrainWidth / mipVirtualSize;

        std::vector<uint8_t> tileAlbedo(static_cast<size_t>(pts) * pts * 4);
        std::vector<uint8_t> tileNormal(static_cast<size_t>(pts) * pts * 4);
        std::vector<uint8_t> tileORM(static_cast<size_t>(pts) * pts * 4);

        auto clampByte = [](float v) {
            return static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
        };

        for (uint32_t py = 0; py < pts; ++py)
        {
            for (uint32_t px = 0; px < pts; ++px)
            {
                float vx = static_cast<float>(tx * params.tileSize + px) - static_cast<float>(params.border);
                float vy = static_cast<float>(ty * params.tileSize + py) - static_cast<float>(params.border);
                float worldX = params.wMinX + vx * texelToWorld;
                float worldZ = params.wMinZ + vy * texelToWorld;

                float weights[terrain::WEIGHT_CHANNELS] = {};
                weights[0] = 1.0f;
                std::array<uint8_t, terrain::WEIGHT_CHANNELS> tileLayerIndices = {0,1,2,3,4,5,6,7};

                for (const auto* tile : allTiles)
                {
                    if (worldX >= tile->worldBounds.min.x && worldX <= tile->worldBounds.max.x &&
                        worldZ >= tile->worldBounds.min.z && worldZ <= tile->worldBounds.max.z &&
                        tile->hasWeightMap())
                    {
                        const auto& wm = tile->weightMap;
                        float u = (worldX - tile->worldBounds.min.x) /
                                  (tile->worldBounds.max.x - tile->worldBounds.min.x);
                        float v = (worldZ - tile->worldBounds.min.z) /
                                  (tile->worldBounds.max.z - tile->worldBounds.min.z);

                        uint32_t x0 = std::min(static_cast<uint32_t>(u * (wm.resolution - 1)), wm.resolution - 1);
                        uint32_t z0 = std::min(static_cast<uint32_t>(v * (wm.resolution - 1)), wm.resolution - 1);

                        for (int ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
                            weights[ch] = wm.getWeight(ch, x0, z0);

                        tileLayerIndices = wm.layerIndices;
                        break;
                    }
                }

                glm::vec3 albedo(0.5f), normal(0.0f, 0.0f, 1.0f);
                float rough = 0.5f, metal = 0.0f, ao_val = 1.0f, totalW = 0.0f;

                for (int ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
                {
                    float w = weights[ch];
                    if (w < 0.001f) continue;
                    uint32_t li = tileLayerIndices[ch];
                    if (li >= layers.size()) continue;

                    const auto& layer = layers[li];
                    glm::vec4 a = sampleTexCPU(layer.albedo.get(), worldX, worldZ, layer.tilingScale);
                    glm::vec4 n = sampleTexCPU(layer.normal.get(), worldX, worldZ, layer.tilingScale);

                    albedo += glm::vec3(a) * w;
                    normal += glm::vec3(n.r * 2.0f - 1.0f, n.g * 2.0f - 1.0f, n.b * 2.0f - 1.0f) * w;

                    if (layer.orm)
                    {
                        glm::vec4 o = sampleTexCPU(layer.orm.get(), worldX, worldZ, layer.tilingScale);
                        ao_val += o.r * w; rough += o.g * w; metal += o.b * w;
                    }
                    else
                    {
                        ao_val += layer.ao * w; rough += layer.roughness * w; metal += layer.metallic * w;
                    }
                    totalW += w;
                }

                if (totalW > 0.001f)
                {
                    float inv = 1.0f / totalW;
                    albedo *= inv;
                    rough *= inv; metal *= inv; ao_val *= inv;
                    float nLen = glm::length(normal);
                    normal = (nLen > 0.001f) ? normal / nLen : glm::vec3(0.0f, 0.0f, 1.0f);
                }
                else
                {
                    normal = glm::vec3(0.0f, 0.0f, 1.0f);
                }

                size_t idx = (static_cast<size_t>(py) * pts + px) * 4;

                tileAlbedo[idx+0] = clampByte(albedo.r); tileAlbedo[idx+1] = clampByte(albedo.g);
                tileAlbedo[idx+2] = clampByte(albedo.b); tileAlbedo[idx+3] = 255;

                tileNormal[idx+0] = clampByte(normal.x*0.5f+0.5f); tileNormal[idx+1] = clampByte(normal.y*0.5f+0.5f);
                tileNormal[idx+2] = clampByte(normal.z*0.5f+0.5f); tileNormal[idx+3] = 255;

                tileORM[idx+0] = clampByte(ao_val); tileORM[idx+1] = clampByte(rough);
                tileORM[idx+2] = clampByte(metal); tileORM[idx+3] = 255;
            }
        }

        auto compAlbedo = resource::BC7Encoder::compress(tileAlbedo.data(), pts, pts);
        auto compNormal = resource::BC7Encoder::compress(tileNormal.data(), pts, pts);
        auto compORM = resource::BC7Encoder::compress(tileORM.data(), pts, pts);

        render::svt::VirtualTileCoord coord{tx, ty, mip};
        albedoWriter.writeTile(coord, compAlbedo.data(), static_cast<uint32_t>(compAlbedo.size()));
        normalWriter.writeTile(coord, compNormal.data(), static_cast<uint32_t>(compNormal.size()));
        ormWriter.writeTile(coord, compORM.data(), static_cast<uint32_t>(compORM.size()));
    }

    bool TerrainService::bakeTerrainSVT(EntityHandle terrainEntity)
    {
        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end() || !gridIt->second)
        {
            vfLogError("BakeTerrainSVT: No terrain grid found");
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
        {
            vfLogError("BakeTerrainSVT: Invalid terrain entity");
            return false;
        }

        const auto& terrainComp = registry.get<components::TerrainComponent>(ent);
        if (terrainComp.savePath.empty())
        {
            vfLogError("BakeTerrainSVT: Terrain must be saved first");
            return false;
        }

        BakeParams params;
        if (!computeBakeParams(terrainComp, params))
        {
            vfLogError("BakeTerrainSVT: Invalid terrain bounds");
            return false;
        }

        std::string materialPath = terrainComp.terrainMaterialRef.resolve();
        std::vector<BakeLayerCPU> layers;
        if (!loadBakeMaterial(materialPath, layers))
        {
            vfLogError("BakeTerrainSVT: Failed to load terrain material: {}", materialPath);
            return false;
        }

        std::string basePath = terrainComp.savePath;
        auto dotPos = basePath.rfind('.');
        if (dotPos != std::string::npos) basePath = basePath.substr(0, dotPos);

        render::svt::SVTFileWriter albedoWriter, normalWriter, ormWriter;
        if (!albedoWriter.create(basePath + "_svt_albedo.vfSVT", params.virtualSizeLog2, params.tileSizeLog2, params.border))
            return false;
        if (!normalWriter.create(basePath + "_svt_normal.vfSVT", params.virtualSizeLog2, params.tileSizeLog2, params.border))
            return false;
        if (!ormWriter.create(basePath + "_svt_orm.vfSVT", params.virtualSizeLog2, params.tileSizeLog2, params.border))
            return false;

        auto& grid = *gridIt->second;
        auto allTilesRaw = grid.getAllTiles();
        std::vector<const terrain::TerrainTile*> allTiles(allTilesRaw.begin(), allTilesRaw.end());

        uint32_t totalTiles = 0;
        for (uint32_t m = 0; m < params.mipLevels; ++m)
        {
            uint32_t s = render::svt::computeTilesPerMipSide(m, params.virtualSizeLog2, params.tileSizeLog2);
            if (s == 0) s = 1;
            totalTiles += s * s;
        }

        uint32_t totalWritten = 0;
        bool cancelled = false;
        for (uint32_t mip = 0; mip < params.mipLevels && !cancelled; ++mip)
        {
            uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(mip, params.virtualSizeLog2, params.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;

            for (uint32_t ty = 0; ty < tilesPerSide && !cancelled; ++ty)
                for (uint32_t tx = 0; tx < tilesPerSide && !cancelled; ++tx)
                {
                    if (pendingSVTBake && pendingSVTBake->cancelled.load(std::memory_order_relaxed))
                    {
                        cancelled = true;
                        break;
                    }

                    compositeAndWriteTile(params, layers, allTiles, mip, tx, ty,
                                          albedoWriter, normalWriter, ormWriter);
                    ++totalWritten;

                    if (pendingSVTBake && totalTiles > 0)
                        pendingSVTBake->progress.store(
                            static_cast<float>(totalWritten) / static_cast<float>(totalTiles),
                            std::memory_order_relaxed);
                }
        }

        if (cancelled)
        {
            vfLogWarning("BakeTerrainSVT: Cancelled after {} tiles", totalWritten);
            return false;
        }

        albedoWriter.finalize();
        normalWriter.finalize();
        ormWriter.finalize();

        vfLogInfo("BakeTerrainSVT: Wrote {} tiles to SVT cache at {}", totalWritten, basePath);
        return true;
    }

    bool TerrainService::beginBakeTerrainSVTAsync(EntityHandle terrainEntity)
    {
        if (svtBakeInProgress.load(std::memory_order_acquire))
            return false;

        auto gridIt = terrainGrids.find(terrainEntity.id);
        if (gridIt == terrainGrids.end() || !gridIt->second)
            return false;

        auto& registry = scene::EntityRegistry::getRegistry();
        entt::entity ent = internal::fromHandle(terrainEntity);
        if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
            return false;

        const auto& terrainComp = registry.get<components::TerrainComponent>(ent);
        if (terrainComp.savePath.empty())
            return false;

        svtBakeInProgress.store(true, std::memory_order_release);
        auto bake = std::make_shared<PendingSVTBake>();
        pendingSVTBake = bake;

        EntityHandle entityCopy = terrainEntity;
        bake->future = threading::JobSystem::instance().submit(
            [this, entityCopy, bake]() -> bool
            {
                {
                    std::lock_guard lock(bake->stageMutex);
                    bake->stage = "Starting SVT bake...";
                }
                bool result = bakeTerrainSVT(entityCopy);
                bake->done.store(true, std::memory_order_release);
                return result;
            },
            threading::JobPriority::LOW
        );

        return true;
    }

    events::terrain::SVTBakePollResult TerrainService::pollBakeTerrainSVT()
    {
        events::terrain::SVTBakePollResult result;

        if (!pendingSVTBake)
            return result;

        result.active = true;
        result.progress = pendingSVTBake->progress.load(std::memory_order_acquire);
        {
            std::lock_guard lock(pendingSVTBake->stageMutex);
            result.stage = pendingSVTBake->stage;
        }

        if (pendingSVTBake->done.load(std::memory_order_acquire))
        {
            result.completed = true;
            result.success = pendingSVTBake->future.get();
            svtBakeInProgress.store(false, std::memory_order_release);
            pendingSVTBake.reset();

            events::terrain::TerrainSVTBakeProgressNotification notification;
            notification.progress = 1.0f;
            notification.completed = true;
            notification.success = result.success;
            events::EventDispatcher::instance().publish(notification);

            if (result.success)
            {
                events::resource::AssetSavedNotification saved;
                events::EventDispatcher::instance().publish(saved);
            }
        }

        return result;
    }

    void TerrainService::cancelBakeTerrainSVT()
    {
        if (pendingSVTBake)
            pendingSVTBake->cancelled.store(true, std::memory_order_release);
    }
}
