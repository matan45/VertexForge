#include "GPUObjectStreamManager.hpp"
#include "MergedMeshBuffer.hpp"
#include "../../mesh/MeshTypes.hpp"
#include "components/CoreComponents.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstring>

namespace render::gpudriven
{
    GPUObjectStreamManager::GPUObjectStreamManager(MergedMeshBuffer& buffer)
        : buffer(buffer)
    {
    }

    void GPUObjectStreamManager::init(const ObjectStreamConfig& cfg)
    {
        config = cfg;
        entries.clear();
        sectorObjects.clear();
        currentFrame = 0;
        stats = {};

        vfLogInfo("GPUObjectStreamManager: Initialized (uploads/frame={}, evictions/frame={}, "
                  "eviction threshold={:.0f}%, target={:.0f}%)",
                  config.maxUploadsPerFrame, config.maxEvictionsPerFrame,
                  config.evictionThreshold * 100.0f, config.evictionTarget * 100.0f);
    }

    void GPUObjectStreamManager::cleanup()
    {
        // Free all active GPU slots
        for (auto& [uuid, entry] : entries)
        {
            if (entry.gpuSlot != FreeListAllocator::ALLOCATION_FAILED)
            {
                buffer.freeObjectSlot(entry.gpuSlot);
            }
        }
        entries.clear();
        sectorObjects.clear();
        stats = {};
    }

    void GPUObjectStreamManager::registerSectorObjects(
        uint32_t sectorId,
        const std::vector<std::pair<uint64_t, entt::entity>>& entities,
        entt::registry& registry)
    {
        auto& sectorSet = sectorObjects[sectorId];

        for (const auto& [uuid, entity] : entities)
        {
            if (entries.count(uuid)) continue;

            ObjectStreamEntry entry;
            entry.entityUUID = uuid;
            entry.entity = entity;
            entry.sectorId = sectorId;
            entry.state = ObjectStreamState::Queued;

            if (registry.valid(entity))
            {
                auto* transform = registry.try_get<components::WorldTransformComponent>(entity);
                if (transform)
                {
                    entry.position = glm::vec3(transform->worldMatrix[3]);
                }

                auto* transformComp = registry.try_get<components::TransformComponent>(entity);
                if (transformComp)
                {
                    entry.isStatic = transformComp->isStatic;
                }
            }

            entries[uuid] = entry;
            sectorSet.insert(uuid);
        }

        vfLogInfo("GPUObjectStreamManager: Registered {} objects for sector {}",
                  entities.size(), sectorId);
    }

    void GPUObjectStreamManager::unregisterSectorObjects(uint32_t sectorId)
    {
        auto it = sectorObjects.find(sectorId);
        if (it == sectorObjects.end()) return;

        uint32_t freedCount = 0;
        for (uint64_t uuid : it->second)
        {
            auto entryIt = entries.find(uuid);
            if (entryIt == entries.end()) continue;

            if (entryIt->second.gpuSlot != FreeListAllocator::ALLOCATION_FAILED)
            {
                buffer.freeObjectSlot(entryIt->second.gpuSlot);
                buffer.unmapEntitySlot(uuid);
                freedCount++;
            }

            entries.erase(entryIt);
        }

        sectorObjects.erase(it);

        if (freedCount > 0)
        {
            buffer.rebuildActiveIndexList();
        }

        vfLogInfo("GPUObjectStreamManager: Unregistered sector {} ({} GPU slots freed)",
                  sectorId, freedCount);
    }

    void GPUObjectStreamManager::update(
        const glm::vec3& cameraPosition,
        const ObjectResolvers& resolvers,
        entt::registry& registry)
    {
        currentFrame++;

        updatePriorities(cameraPosition);
        processEvictions();
        processUploads(resolvers, registry);

        buffer.rebuildActiveIndexList();
        updateStats();
    }

    void GPUObjectStreamManager::updatePriorities(const glm::vec3& cameraPosition)
    {
        for (auto& [uuid, entry] : entries)
        {
            entry.distanceToCamera = glm::length(entry.position - cameraPosition);
            entry.priority = calculatePriority(entry, cameraPosition);
        }
    }

    float GPUObjectStreamManager::calculatePriority(
        const ObjectStreamEntry& entry,
        const glm::vec3& cameraPos) const
    {
        float distancePriority = 1.0f / (1.0f + entry.distanceToCamera * 0.01f);
        float staticBonus = entry.isStatic ? 0.1f : 0.0f;
        float activeBonus = (entry.state == ObjectStreamState::Active) ? config.hysteresisMargin : 0.0f;

        return distancePriority + staticBonus + activeBonus;
    }

    void GPUObjectStreamManager::processEvictions()
    {
        uint32_t maxSlots = buffer.getMaxObjectCount();
        uint32_t activeCount = buffer.getEntitySlotCount();
        float utilization = static_cast<float>(activeCount) / static_cast<float>(maxSlots);

        if (utilization < config.evictionThreshold) return;

        // Gather active entries, sorted by priority (lowest first for eviction)
        std::vector<uint64_t> candidates;
        candidates.reserve(activeCount);
        for (const auto& [uuid, entry] : entries)
        {
            if (entry.state == ObjectStreamState::Active)
            {
                candidates.push_back(uuid);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
            [this](uint64_t a, uint64_t b) {
                return entries.at(a).priority < entries.at(b).priority;
            });

        uint32_t evicted = 0;
        float targetUtilization = config.evictionTarget;
        uint32_t targetActive = static_cast<uint32_t>(targetUtilization * maxSlots);

        for (uint64_t uuid : candidates)
        {
            if (activeCount <= targetActive || evicted >= config.maxEvictionsPerFrame)
                break;

            auto& entry = entries[uuid];
            buffer.freeObjectSlot(entry.gpuSlot);
            buffer.unmapEntitySlot(uuid);
            entry.gpuSlot = FreeListAllocator::ALLOCATION_FAILED;
            entry.state = ObjectStreamState::Queued;
            activeCount--;
            evicted++;
        }

        stats.evictionsThisFrame = evicted;
    }

    void GPUObjectStreamManager::processUploads(
        const ObjectResolvers& resolvers,
        entt::registry& registry)
    {
        // Gather queued entries, sorted by priority (highest first)
        std::vector<uint64_t> candidates;
        for (const auto& [uuid, entry] : entries)
        {
            if (entry.state == ObjectStreamState::Queued)
            {
                candidates.push_back(uuid);
            }
        }

        std::sort(candidates.begin(), candidates.end(),
            [this](uint64_t a, uint64_t b) {
                return entries.at(a).priority > entries.at(b).priority;
            });

        uint32_t uploaded = 0;

        for (uint64_t uuid : candidates)
        {
            if (uploaded >= config.maxUploadsPerFrame) break;

            auto& entry = entries[uuid];

            // Validate entity is still alive
            if (!registry.valid(entry.entity)) continue;

            // Check if entity has required components
            auto* meshComp = registry.try_get<components::MeshComponent>(entry.entity);
            auto* worldTransform = registry.try_get<components::WorldTransformComponent>(entry.entity);
            if (!meshComp || !worldTransform) continue;

            std::string meshPath = meshComp->meshRef.resolve();
            if (meshPath.empty()) continue;

            // Get submesh location
            const auto* submeshLoc = buffer.getSubmeshLocation(meshPath, "", 0);
            if (!submeshLoc || !submeshLoc->hasRenderableLOD()) continue;

            // Allocate GPU slot
            uint32_t slot = buffer.allocateObjectSlot();
            if (slot == FreeListAllocator::ALLOCATION_FAILED)
            {
                break; // Buffer full, try next frame after evictions
            }

            // Build MeshRenderData for this entity
            mesh::MeshRenderData renderData;
            renderData.entity = entry.entity;
            renderData.meshPath = meshPath;
            renderData.modelMatrix = worldTransform->worldMatrix;
            renderData.maxDrawDistance = meshComp->maxDrawDistance;
            renderData.showBoundingBox = false;

            auto* materialComp = registry.try_get<components::MaterialComponent>(entry.entity);
            if (materialComp)
            {
                renderData.defaultMaterialPath = materialComp->defaultMaterialRef.resolve();
                for (const auto& subMatPair : materialComp->subMeshMaterials)
                {
                    mesh::SubMeshMaterialInfo info;
                    info.materialPath = subMatPair.second.resolve();
                    renderData.submeshMaterials[subMatPair.first] = info;
                }
            }

            // Build GPUObjectData using MergedMeshBuffer's populate methods
            const auto& registeredMeshes = buffer.getRegisteredMeshes();
            bool foundMesh = false;
            for (const auto& meshInfo : registeredMeshes)
            {
                if (meshInfo.meshPath != meshPath) continue;
                foundMesh = true;

                for (uint32_t subIdx = 0; subIdx < meshInfo.submeshCount; ++subIdx)
                {
                    const auto* subLoc = buffer.getSubmeshLocation(
                        meshPath, meshInfo.submeshes[subIdx].submeshName, subIdx);
                    if (!subLoc || !subLoc->hasRenderableLOD()) continue;

                    if (subIdx > 0)
                    {
                        // Need additional slots for extra submeshes
                        uint32_t extraSlot = buffer.allocateObjectSlot();
                        if (extraSlot == FreeListAllocator::ALLOCATION_FAILED) break;
                        slot = extraSlot;
                    }

                    GPUObjectData obj{};
                    obj.modelMatrix = renderData.modelMatrix;
                    float drawDistSq = renderData.maxDrawDistance > 0.0f
                        ? renderData.maxDrawDistance * renderData.maxDrawDistance : 0.0f;
                    obj.aabbMin = glm::vec4(subLoc->aabbMin, drawDistSq);
                    obj.aabbMax = glm::vec4(subLoc->aabbMax, 0.0f);

                    // LOD data
                    for (uint32_t i = 0; i < LOD_LEVEL_COUNT; ++i)
                    {
                        glm::uvec4& lodData = (i == 0) ? obj.lod0Data
                            : (i == 1) ? obj.lod1Data
                            : (i == 2) ? obj.lod2Data
                            : obj.lod3Data;
                        lodData = glm::uvec4(
                            subLoc->lods[i].vertexOffset,
                            subLoc->lods[i].indexOffset,
                            subLoc->lods[i].indexCount,
                            subLoc->lods[i].vertexCount);
                    }

                    // Meshlet data
                    obj.meshletLod0 = glm::uvec4(subLoc->meshletLods[0].meshletOffset,
                        subLoc->meshletLods[0].meshletCount, subLoc->meshletLods[0].baseVertexOffset, 0);
                    obj.meshletLod1 = glm::uvec4(subLoc->meshletLods[1].meshletOffset,
                        subLoc->meshletLods[1].meshletCount, subLoc->meshletLods[1].baseVertexOffset, 0);
                    obj.meshletLod2 = glm::uvec4(subLoc->meshletLods[2].meshletOffset,
                        subLoc->meshletLods[2].meshletCount, subLoc->meshletLods[2].baseVertexOffset, 0);
                    obj.meshletLod3 = glm::uvec4(subLoc->meshletLods[3].meshletOffset,
                        subLoc->meshletLods[3].meshletCount, subLoc->meshletLods[3].baseVertexOffset, 0);

                    obj.lodThresholds = glm::vec4(LOD_THRESHOLD_0, LOD_THRESHOLD_1, LOD_THRESHOLD_2, 0.0f);

                    // Material - resolve from submesh materials or default
                    const auto* subMat = renderData.getMaterialForSubmesh(subLoc->submeshName);
                    std::string materialPath;
                    if (subMat)
                    {
                        obj.albedo = subMat->albedo;
                        obj.iblParams = glm::vec4(subMat->iblDiffuse, subMat->iblSpecular, subMat->alphaCutoff, 0.0f);
                        obj.materialParams = glm::vec4(subMat->metallic, subMat->roughness, subMat->ao, subMat->emission);
                        materialPath = subMat->materialPath;
                    }
                    else
                    {
                        obj.albedo = glm::vec4(1.0f);
                        obj.materialParams = glm::vec4(0.0f, 0.5f, 1.0f, 0.0f);
                        obj.iblParams = glm::vec4(1.0f, 0.5f, 0.5f, 0.0f);
                        materialPath = renderData.defaultMaterialPath;
                    }

                    // Texture indices
                    if (resolvers.textureResolver && !materialPath.empty())
                    {
                        obj.textureIndices0 = glm::uvec4(
                            resolvers.textureResolver(materialPath, TextureSlotType::Albedo),
                            resolvers.textureResolver(materialPath, TextureSlotType::Normal),
                            resolvers.textureResolver(materialPath, TextureSlotType::ORM),
                            resolvers.textureResolver(materialPath, TextureSlotType::Metallic));
                        obj.textureIndices1 = glm::uvec4(
                            resolvers.textureResolver(materialPath, TextureSlotType::Roughness),
                            resolvers.textureResolver(materialPath, TextureSlotType::AO),
                            resolvers.textureResolver(materialPath, TextureSlotType::Emission),
                            resolvers.textureResolver(materialPath, TextureSlotType::Height));
                    }
                    else
                    {
                        obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
                        obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
                    }

                    // Shader group
                    obj.shaderGroupIndex = (resolvers.shaderGroupResolver && !materialPath.empty())
                        ? resolvers.shaderGroupResolver(materialPath) : 0;

                    obj.availableLODMask = subLoc->getAvailableLODMask();
                    obj.entityId = slot;

                    uint32_t one = 1;
                    std::memcpy(&obj.aabbMax.w, &one, sizeof(uint32_t));
                    obj.instanceData = glm::uvec4(INVALID_TEXTURE_INDEX, 0, 0, 0);

                    // Uniform scale flag
                    float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
                    float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
                    float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
                    if (std::abs(scaleX - scaleY) < 0.001f && std::abs(scaleY - scaleZ) < 0.001f)
                    {
                        obj.flags |= ObjectFlags::UniformScale;
                    }

                    buffer.updateObjectAtSlot(slot, obj);
                    buffer.mapEntityToSlot(uuid, slot);
                }
                break;
            }

            if (!foundMesh) {
                buffer.freeObjectSlot(slot);
                continue;
            }

            entry.gpuSlot = slot;
            entry.state = ObjectStreamState::Active;
            entry.lastAccessFrame = currentFrame;
            uploaded++;
        }

        stats.uploadsThisFrame = uploaded;
    }

    void GPUObjectStreamManager::updateStats()
    {
        uint32_t active = 0;
        uint32_t queued = 0;
        for (const auto& [uuid, entry] : entries)
        {
            if (entry.state == ObjectStreamState::Active) active++;
            else if (entry.state == ObjectStreamState::Queued) queued++;
        }

        stats.totalRegistered = static_cast<uint32_t>(entries.size());
        stats.activeOnGPU = active;
        stats.queuedForUpload = queued;
        stats.slotUtilization = buffer.getMaxObjectCount() > 0
            ? static_cast<float>(active) / static_cast<float>(buffer.getMaxObjectCount()) : 0.0f;
    }
}
