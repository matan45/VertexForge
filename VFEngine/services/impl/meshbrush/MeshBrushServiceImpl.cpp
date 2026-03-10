#include "MeshBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/components/MeshBrushComponents.hpp"
#include <glm/gtc/constants.hpp>
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
                    // Remove all group entities for this palette index
                    for (auto it = groupEntities.begin(); it != groupEntities.end();)
                    {
                        if (it->first.paletteIdx == cmd.index)
                            it = groupEntities.erase(it);
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

        // Clear spatial grid on scene clear
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                spatialGrid.clear();
                entityCounter = 0;
                hasLastPlacement = false;
                groupEntities.clear();
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
        auto& dispatcher = events::EventDispatcher::instance();

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
            // Single entry selected
            const auto& entry = palette[selectedPaletteIndex];
            if (!entry.enabled || entry.meshPath.empty()) return;
            enabledIndices.push_back(static_cast<uint32_t>(selectedPaletteIndex));
            weights.push_back(1.0f);
        }
        else
        {
            // All enabled entries (weighted random)
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

        // Generate candidate positions using simple random sampling within brush radius
        float radius = currentParams.radius;
        float spacing = currentParams.spacing;
        int maxCandidates = static_cast<int>(currentParams.density * radius * radius * glm::pi<float>() / (spacing * spacing));
        maxCandidates = std::clamp(maxCandidates, 1, 100);

        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> jitterDist(-0.5f, 0.5f);

        uint32_t placedCount = 0;

        for (int i = 0; i < maxCandidates; ++i)
        {
            // Random point in circle
            float angle = angleDist(rng);
            float r = radius * std::sqrt(radiusDist(rng));

            glm::vec3 candidatePos = worldPos;
            candidatePos.x += r * std::cos(angle);
            candidatePos.z += r * std::sin(angle);

            // Add jitter
            candidatePos.x += jitterDist(rng) * currentParams.positionJitter * spacing;
            candidatePos.z += jitterDist(rng) * currentParams.positionJitter * spacing;

            // Use the worldPos Y (terrain height) for the candidate
            candidatePos.y = worldPos.y;

            // Check spacing
            if (spatialGrid.hasNeighborWithin(candidatePos, spacing))
            {
                continue;
            }

            // Select palette entry from enabled entries
            uint32_t enabledIdx = paletteDist(rng);
            uint32_t paletteIdx = enabledIndices[enabledIdx];
            const auto& entry = palette[paletteIdx];

            // Check slope
            float slopeAngle = std::acos(std::clamp(glm::dot(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f));
            float slopeDeg = glm::degrees(slopeAngle);
            if (slopeDeg > entry.maxSlope)
            {
                continue;
            }

            // Apply AABB-based Y offset so mesh bottom sits on terrain surface
            float aabbOffset = getAABBYOffset(entry.meshPath);
            candidatePos.y += aabbOffset;

            // Apply manual Y offset from palette entry
            candidatePos.y += entry.yOffset;

            // Check height range
            if (candidatePos.y < entry.heightRange.x || candidatePos.y > entry.heightRange.y)
            {
                continue;
            }

            // Compute random transform
            std::uniform_real_distribution<float> scaleDist(entry.scaleRange.x, entry.scaleRange.y);
            float scale = scaleDist(rng);

            std::uniform_real_distribution<float> rotYDist(entry.rotationYRange.x, entry.rotationYRange.y);
            float rotY = rotYDist(rng);

            float rotX = 0.0f;
            float rotZ = 0.0f;
            if (entry.randomRotationX)
            {
                std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
                rotX = rotDist(rng);
            }
            if (entry.randomRotationZ)
            {
                std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);
                rotZ = rotDist(rng);
            }

            TransformData transform;
            transform.position = candidatePos;
            transform.rotation = glm::vec3(rotX, rotY, rotZ);
            transform.scale = glm::vec3(scale);

            if (entry.alignToNormal && glm::length(surfaceNormal) > 0.001f)
            {
                glm::vec3 up(0.0f, 1.0f, 0.0f);
                glm::vec3 n = glm::normalize(surfaceNormal);
                float alignAngle = glm::degrees(std::acos(std::clamp(glm::dot(n, up), -1.0f, 1.0f)));
                glm::vec3 axis = glm::cross(up, n);
                if (glm::length(axis) > 0.001f)
                {
                    axis = glm::normalize(axis);
                    transform.rotation.x = alignAngle * axis.x;
                    transform.rotation.z = alignAngle * axis.z;
                }
            }

            // Create entity under per-entry per-sector group
            auto groupEntity = ensureGroupEntity(paletteIdx, candidatePos);
            events::scene::CreateEntityCommand createCmd;
            createCmd.name = "MeshBrush_" + std::to_string(entityCounter++);
            createCmd.parent = groupEntity;
            auto entity = dispatcher.execute(createCmd);
            if (!entity.isValid()) continue;

            // Convert world position to local space relative to parent group
            int32_t sx = static_cast<int32_t>(std::floor(candidatePos.x / sectorSize));
            int32_t sz = static_cast<int32_t>(std::floor(candidatePos.z / sectorSize));
            glm::vec3 parentPos(
                (static_cast<float>(sx) + 0.5f) * sectorSize,
                0.0f,
                (static_cast<float>(sz) + 0.5f) * sectorSize
            );
            transform.position = candidatePos - parentPos;

            events::scene::AddMeshComponentCommand meshCmd;
            meshCmd.entity = entity;
            dispatcher.execute(meshCmd);

            MeshData meshData;
            meshData.meshPath = entry.meshPath;
            events::scene::SetMeshDataCommand meshDataCmd;
            meshDataCmd.entity = entity;
            meshDataCmd.meshData = meshData;
            dispatcher.execute(meshDataCmd);

            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = transform;
            dispatcher.execute(transformCmd);

            // Add brush instance tag via direct registry access
            auto enttEntity = internal::fromHandle(entity);
            auto& registry = scene::EntityRegistry::getRegistry();
            registry.emplace<components::MeshBrushInstanceComponent>(enttEntity,
                components::MeshBrushInstanceComponent{0, paletteIdx, surfaceNormal});

            spatialGrid.insert(entity.id, candidatePos);

            ++placedCount;
        }

        if (placedCount > 0)
        {
            events::meshBrush::MeshBrushAppliedNotification notification;
            notification.position = worldPos;
            notification.count = placedCount;
            dispatcher.publish(notification);
        }
    }

    void MeshBrushServiceImpl::eraseInstances(const glm::vec3& worldPos)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto entries = spatialGrid.queryRadius(worldPos, currentParams.radius);

        for (const auto& entry : entries)
        {
            EntityHandle entityHandle;
            entityHandle.id = entry.entityId;

            auto enttEntity = internal::fromHandle(entityHandle);
            auto& registry = scene::EntityRegistry::getRegistry();

            if (!registry.valid(enttEntity)) continue;
            if (!registry.all_of<components::MeshBrushInstanceComponent>(enttEntity)) continue;

            spatialGrid.remove(entry.entityId);

            events::scene::DeleteEntityCommand deleteCmd;
            deleteCmd.entity = entityHandle;
            dispatcher.execute(deleteCmd);
        }
    }

    EntityHandle MeshBrushServiceImpl::ensureGroupEntity(uint32_t paletteIdx, const glm::vec3& worldPos)
    {
        // Compute sector coordinates from world position
        int32_t sx = static_cast<int32_t>(std::floor(worldPos.x / sectorSize));
        int32_t sz = static_cast<int32_t>(std::floor(worldPos.z / sectorSize));
        GroupKey key{paletteIdx, sx, sz};

        auto it = groupEntities.find(key);
        if (it != groupEntities.end() && it->second.isValid())
        {
            // Verify it still exists in registry
            auto entt = internal::fromHandle(it->second);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(entt)) return it->second;
        }

        // Build group name: "MeshBrush_<filename>_(sX,sZ)"
        std::string meshLabel = "Group_" + std::to_string(paletteIdx);
        if (paletteIdx < palette.size() && !palette[paletteIdx].meshPath.empty())
        {
            const auto& meshPath = palette[paletteIdx].meshPath;
            auto pos = meshPath.find_last_of("\\/");
            meshLabel = (pos != std::string::npos)
                ? meshPath.substr(pos + 1) : meshPath;
        }
        std::string groupName = "MeshBrush_" + meshLabel
            + "_(" + std::to_string(sx) + "," + std::to_string(sz) + ")";

        // Position the group entity at the center of the sector
        // so it gets assigned to the correct sector by WorldSectorManager
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::CreateEntityCommand createCmd;
        createCmd.name = groupName;
        auto entity = dispatcher.execute(createCmd);

        if (entity.isValid())
        {
            TransformData groupTransform;
            groupTransform.position = glm::vec3(
                (static_cast<float>(sx) + 0.5f) * sectorSize,
                0.0f,
                (static_cast<float>(sz) + 0.5f) * sectorSize
            );
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = groupTransform;
            dispatcher.execute(transformCmd);
        }

        groupEntities[key] = entity;
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

        // Query the mesh AABB from the render service
        events::render::GetMeshBoundingBoxQuery query;
        query.meshPath = meshPath;
        auto result = events::EventDispatcher::instance().query(query);

        float offset = 0.0f;
        if (result.has_value())
        {
            // Offset by -min.y so the bottom of the mesh sits on the terrain surface
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
