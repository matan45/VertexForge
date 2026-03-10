#include "MeshBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/MeshBrushComponents.hpp"
#include "../../../utilities/math/TransformUtils.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace services
{
    MeshBrushServiceImpl::~MeshBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (modeChangedToken.isValid())
            dispatcher.unsubscribe(modeChangedToken);
        if (sceneClearedToken.isValid())
            dispatcher.unsubscribe(sceneClearedToken);
    }

    void MeshBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Param commands
        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushParamsCommand>(
            [this](const events::meshBrush::SetMeshBrushParamsCommand& cmd)
            {
                currentParams = cmd.params;
                currentParams.validate();
                spatialGrid.setCellSize(currentParams.spacing);
                publishParamsChanged();
            });

        // Mode command
        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushModeCommand>(
            [this](const events::meshBrush::SetMeshBrushModeCommand& cmd)
            {
                currentMode = cmd.mode;
            });

        // Selected entry command
        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushSelectedEntryCommand>(
            [this](const events::meshBrush::SetMeshBrushSelectedEntryCommand& cmd)
            {
                selectedPaletteIndex = cmd.selectedIndex;
            });

        // Palette commands
        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushPaletteCommand>(
            [this](const events::meshBrush::SetMeshBrushPaletteCommand& cmd)
            {
                palette = cmd.palette;
            });

        dispatcher.registerCommandHandler<events::meshBrush::AddMeshPaletteEntryCommand>(
            [this](const events::meshBrush::AddMeshPaletteEntryCommand& cmd)
            {
                palette.push_back(cmd.entry);
            });

        dispatcher.registerCommandHandler<events::meshBrush::RemoveMeshPaletteEntryCommand>(
            [this](const events::meshBrush::RemoveMeshPaletteEntryCommand& cmd)
            {
                if (cmd.index < palette.size())
                {
                    palette.erase(palette.begin() + cmd.index);
                    // Remove all batch entities for this palette index
                    for (auto it = batchEntities.begin(); it != batchEntities.end();)
                    {
                        if (it->first.paletteIdx == cmd.index)
                            it = batchEntities.erase(it);
                        else
                            ++it;
                    }
                }
            });

        // Apply brush
        dispatcher.registerCommandHandler<events::meshBrush::ApplyMeshBrushCommand>(
            [this](const events::meshBrush::ApplyMeshBrushCommand& cmd)
            {
                if (meshBrushModeActive)
                {
                    applyBrush(cmd.worldPosition, cmd.surfaceNormal, cmd.deltaTime, cmd.isFirstApplication);
                }
            });

        // Queries
        dispatcher.registerQueryHandler<events::meshBrush::GetMeshBrushParamsQuery>(
            [this](const events::meshBrush::GetMeshBrushParamsQuery&)
            {
                return currentParams;
            });

        dispatcher.registerQueryHandler<events::meshBrush::GetMeshBrushPaletteQuery>(
            [this](const events::meshBrush::GetMeshBrushPaletteQuery&)
            {
                return palette;
            });

        dispatcher.registerQueryHandler<events::meshBrush::GetMeshBrushModeQuery>(
            [this](const events::meshBrush::GetMeshBrushModeQuery&)
            {
                return currentMode;
            });

        // Subscribe to mode changes
        modeChangedToken = dispatcher.subscribe<events::meshBrush::MeshBrushModeChangedNotification>(
            [this](const events::meshBrush::MeshBrushModeChangedNotification& n)
            {
                meshBrushModeActive = n.isActive;
            });

        // Clear on scene clear
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                spatialGrid.clear();
                nextInstanceId = 1;
                hasLastPlacement = false;
                batchEntities.clear();
                aabbYOffsetCache.clear();
            });
    }

    void MeshBrushServiceImpl::applyBrush(const glm::vec3& worldPos, const glm::vec3& normal,
                                           float deltaTime, bool isFirst)
    {
        if (palette.empty()) return;

        if (isFirst)
        {
            hasLastPlacement = false;
        }

        // Rate limit: only place when brush has moved at least half the spacing distance
        if (hasLastPlacement && currentMode == meshbrush::MeshBrushMode::Paint)
        {
            float dist = glm::length(glm::vec2(worldPos.x - lastPlacementPos.x, worldPos.z - lastPlacementPos.z));
            if (dist < currentParams.spacing * 0.5f)
            {
                return;
            }
        }

        if (currentMode == meshbrush::MeshBrushMode::Paint)
        {
            placeMeshes(worldPos, normal);
            lastPlacementPos = worldPos;
            hasLastPlacement = true;
        }
        else
        {
            eraseInstances(worldPos);
        }
    }

    void MeshBrushServiceImpl::placeMeshes(const glm::vec3& worldPos, const glm::vec3& normal)
    {
        // Ensure normal points upward (terrain raycast may return inverted normals)
        glm::vec3 surfaceNormal = normal;
        if (surfaceNormal.y < 0.0f)
        {
            surfaceNormal = -surfaceNormal;
        }

        // Build weight distribution from enabled entries only
        std::vector<uint32_t> enabledIndices;
        std::vector<float> weights;

        if (selectedPaletteIndex >= 0 && selectedPaletteIndex < static_cast<int>(palette.size()))
        {
            const auto& entry = palette[selectedPaletteIndex];
            if (!entry.enabled || entry.meshPath.empty()) return;
            enabledIndices.push_back(static_cast<uint32_t>(selectedPaletteIndex));
            weights.push_back(1.0f);
        }
        else
        {
            for (uint32_t idx = 0; idx < palette.size(); ++idx)
            {
                if (palette[idx].enabled && !palette[idx].meshPath.empty())
                {
                    enabledIndices.push_back(idx);
                    weights.push_back(palette[idx].weight);
                }
            }
        }
        if (enabledIndices.empty()) return;
        std::discrete_distribution<uint32_t> paletteDist(weights.begin(), weights.end());

        float radius = currentParams.radius;
        float spacing = currentParams.spacing;
        int maxCandidates = static_cast<int>(currentParams.density * radius * radius * glm::pi<float>() / (spacing * spacing));
        maxCandidates = std::clamp(maxCandidates, 1, 100);

        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> jitterDist(-0.5f, 0.5f);

        uint32_t placedCount = 0;
        auto& registry = scene::EntityRegistry::getRegistry();

        for (int i = 0; i < maxCandidates; ++i)
        {
            float angle = angleDist(rng);
            float r = radius * std::sqrt(radiusDist(rng));

            glm::vec3 candidatePos = worldPos;
            candidatePos.x += r * std::cos(angle);
            candidatePos.z += r * std::sin(angle);

            candidatePos.x += jitterDist(rng) * currentParams.positionJitter * spacing;
            candidatePos.z += jitterDist(rng) * currentParams.positionJitter * spacing;

            // Sample terrain height at this candidate's XZ position
            events::terrain::GetTerrainHeightAtQuery heightQuery;
            heightQuery.worldX = candidatePos.x;
            heightQuery.worldZ = candidatePos.z;
            auto heightResult = events::EventDispatcher::instance().query(heightQuery);
            candidatePos.y = heightResult.valid ? heightResult.height : worldPos.y;

            if (spatialGrid.hasNeighborWithin(candidatePos, spacing))
            {
                continue;
            }

            // Select palette entry
            uint32_t enabledIdx = paletteDist(rng);
            uint32_t paletteIdx = enabledIndices[enabledIdx];
            const auto& entry = palette[paletteIdx];

            // Check slope
            float slopeAngle = std::acos(std::clamp(glm::dot(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f));
            if (glm::degrees(slopeAngle) > entry.maxSlope) continue;

            // Apply AABB + manual Y offset
            candidatePos.y += getAABBYOffset(entry.meshPath);
            candidatePos.y += entry.yOffset;

            // Check height range
            if (candidatePos.y < entry.heightRange.x || candidatePos.y > entry.heightRange.y) continue;

            // Compute random transform
            std::uniform_real_distribution<float> scaleDist(entry.scaleRange.x, entry.scaleRange.y);
            float scale = scaleDist(rng);

            std::uniform_real_distribution<float> rotYDist(entry.rotationYRange.x, entry.rotationYRange.y);
            float rotY = rotYDist(rng);

            float rotX = 0.0f, rotZ = 0.0f;
            if (entry.randomRotationX)
            {
                std::uniform_real_distribution<float> d(0.0f, 360.0f);
                rotX = d(rng);
            }
            if (entry.randomRotationZ)
            {
                std::uniform_real_distribution<float> d(0.0f, 360.0f);
                rotZ = d(rng);
            }

            glm::vec3 rotation(rotX, rotY, rotZ);

            if (entry.alignToNormal && glm::length(surfaceNormal) > 0.001f)
            {
                glm::vec3 up(0.0f, 1.0f, 0.0f);
                glm::vec3 n = glm::normalize(surfaceNormal);
                float alignAngle = glm::degrees(std::acos(std::clamp(glm::dot(n, up), -1.0f, 1.0f)));
                glm::vec3 axis = glm::cross(up, n);
                if (glm::length(axis) > 0.001f)
                {
                    axis = glm::normalize(axis);
                    rotation.x = alignAngle * axis.x;
                    rotation.z = alignAngle * axis.z;
                }
            }

            // Build world-space model matrix for this instance
            glm::mat4 modelMatrix = math::composeMatrix(candidatePos, rotation, glm::vec3(scale));

            // Get or create the batch entity for this palette entry + sector
            auto batchEntity = ensureBatchEntity(paletteIdx, candidatePos);
            auto enttBatch = internal::fromHandle(batchEntity);

            if (!registry.valid(enttBatch)) continue;

            // Add instance to the batch component
            auto& batch = registry.get<components::MeshBrushBatchComponent>(enttBatch);

            components::BrushInstance inst;
            inst.id = nextInstanceId++;
            inst.transform = modelMatrix;
            inst.worldPosition = candidatePos;
            inst.active = true;
            batch.instances.push_back(inst);
            batch.dirty = true;

            spatialGrid.insert(inst.id, candidatePos);

            ++placedCount;
        }

        if (placedCount > 0)
        {
            events::meshBrush::MeshBrushAppliedNotification notification;
            notification.position = worldPos;
            notification.count = placedCount;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void MeshBrushServiceImpl::eraseInstances(const glm::vec3& worldPos)
    {
        auto entries = spatialGrid.queryRadius(worldPos, currentParams.radius);
        auto& registry = scene::EntityRegistry::getRegistry();

        for (const auto& entry : entries)
        {
            spatialGrid.remove(entry.entityId);

            // Find and deactivate the instance in batch components
            for (auto& [key, handle] : batchEntities)
            {
                if (!handle.isValid()) continue;
                auto entt = internal::fromHandle(handle);
                if (!registry.valid(entt)) continue;
                if (!registry.all_of<components::MeshBrushBatchComponent>(entt)) continue;

                auto& batch = registry.get<components::MeshBrushBatchComponent>(entt);
                for (auto& inst : batch.instances)
                {
                    if (inst.id == entry.entityId && inst.active)
                    {
                        inst.active = false;
                        batch.dirty = true;
                        goto nextEntry;
                    }
                }
            }
            nextEntry:;
        }
    }

    EntityHandle MeshBrushServiceImpl::ensureBatchEntity(uint32_t paletteIdx, const glm::vec3& worldPos)
    {
        int32_t sx = static_cast<int32_t>(std::floor(worldPos.x / sectorSize));
        int32_t sz = static_cast<int32_t>(std::floor(worldPos.z / sectorSize));
        BatchKey key{paletteIdx, sx, sz};

        auto it = batchEntities.find(key);
        if (it != batchEntities.end() && it->second.isValid())
        {
            auto entt = internal::fromHandle(it->second);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(entt)) return it->second;
        }

        // Build batch entity name from mesh filename + sector
        std::string meshLabel = "Batch_" + std::to_string(paletteIdx);
        if (paletteIdx < palette.size() && !palette[paletteIdx].meshPath.empty())
        {
            const auto& meshPath = palette[paletteIdx].meshPath;
            auto pos = meshPath.find_last_of("\\/");
            meshLabel = (pos != std::string::npos)
                ? meshPath.substr(pos + 1) : meshPath;
        }
        std::string entityName = "MeshBrush_" + meshLabel
            + "_(" + std::to_string(sx) + "," + std::to_string(sz) + ")";

        auto& dispatcher = events::EventDispatcher::instance();

        // Create entity with mesh component (needed for FramePreparationSystem to pick it up)
        events::scene::CreateEntityCommand createCmd;
        createCmd.name = entityName;
        auto entity = dispatcher.execute(createCmd);

        if (entity.isValid())
        {
            // Set position at sector center for correct sector assignment
            TransformData batchTransform;
            batchTransform.position = glm::vec3(
                (static_cast<float>(sx) + 0.5f) * sectorSize,
                0.0f,
                (static_cast<float>(sz) + 0.5f) * sectorSize
            );
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = batchTransform;
            dispatcher.execute(transformCmd);

            // Add mesh component with the mesh path
            events::scene::AddMeshComponentCommand meshCmd;
            meshCmd.entity = entity;
            dispatcher.execute(meshCmd);

            if (paletteIdx < palette.size())
            {
                MeshData meshData;
                meshData.meshPath = palette[paletteIdx].meshPath;
                events::scene::SetMeshDataCommand meshDataCmd;
                meshDataCmd.entity = entity;
                meshDataCmd.meshData = meshData;
                dispatcher.execute(meshDataCmd);
            }

            // Add batch component via direct registry access
            auto enttEntity = internal::fromHandle(entity);
            auto& registry = scene::EntityRegistry::getRegistry();
            registry.emplace<components::MeshBrushBatchComponent>(enttEntity);
        }

        batchEntities[key] = entity;
        return entity;
    }

    float MeshBrushServiceImpl::getAABBYOffset(const std::string& meshPath)
    {
        if (meshPath.empty()) return 0.0f;

        auto it = aabbYOffsetCache.find(meshPath);
        if (it != aabbYOffsetCache.end())
        {
            return it->second;
        }

        events::render::GetMeshBoundingBoxQuery query;
        query.meshPath = meshPath;
        auto result = events::EventDispatcher::instance().query(query);

        float offset = 0.0f;
        if (result.has_value())
        {
            offset = -result->min.y;
        }

        aabbYOffsetCache[meshPath] = offset;
        return offset;
    }

    void MeshBrushServiceImpl::publishParamsChanged()
    {
        events::meshBrush::MeshBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }
}
