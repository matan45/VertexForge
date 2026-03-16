#include "MeshBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/math/TransformUtils.hpp"
#include <asset/AssetRef.hpp>
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

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushParamsCommand>(
            [this](const events::meshBrush::SetMeshBrushParamsCommand& cmd)
            {
                currentParams = cmd.params;
                currentParams.validate();
                spatialGrid.setCellSize(currentParams.spacing);
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushModeCommand>(
            [this](const events::meshBrush::SetMeshBrushModeCommand& cmd)
            {
                currentMode = cmd.mode;
            });

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushSelectedEntryCommand>(
            [this](const events::meshBrush::SetMeshBrushSelectedEntryCommand& cmd)
            {
                selectedPaletteIndex = cmd.selectedIndex;
            });

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushPaletteCommand>(
            [this](const events::meshBrush::SetMeshBrushPaletteCommand& cmd)
            {
                // Prune group entities for palette indices that no longer exist
                uint32_t newSize = static_cast<uint32_t>(cmd.palette.size());
                for (auto it = groupEntities.begin(); it != groupEntities.end();)
                {
                    if (it->first.paletteIdx >= newSize)
                        it = groupEntities.erase(it);
                    else
                        ++it;
                }
                palette = cmd.palette;
            });

        dispatcher.registerCommandHandler<events::meshBrush::ApplyMeshBrushCommand>(
            [this](const events::meshBrush::ApplyMeshBrushCommand& cmd)
            {
                if (meshBrushModeActive)
                {
                    applyBrush(cmd.worldPosition, cmd.surfaceNormal, cmd.deltaTime, cmd.isFirstApplication);
                }
            });

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

        modeChangedToken = dispatcher.subscribe<events::meshBrush::MeshBrushModeChangedNotification>(
            [this](const events::meshBrush::MeshBrushModeChangedNotification& n)
            {
                meshBrushModeActive = n.isActive;
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                spatialGrid.clear();
                nextInstanceId = 1;
                hasLastPlacement = false;
                groupEntities.clear();
                instanceEntities.clear();
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
            if (entry.meshPath.empty()) return;
            enabledIndices.push_back(static_cast<uint32_t>(selectedPaletteIndex));
            weights.push_back(1.0f);
        }
        else
        {
            for (uint32_t idx = 0; idx < palette.size(); ++idx)
            {
                if (!palette[idx].meshPath.empty())
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
        auto& dispatcher = events::EventDispatcher::instance();

        for (int i = 0; i < maxCandidates; ++i)
        {
            float angle = angleDist(rng);
            float r = radius * std::sqrt(radiusDist(rng));

            glm::vec3 candidatePos = worldPos;
            candidatePos.x += r * std::cos(angle);
            candidatePos.z += r * std::sin(angle);

            candidatePos.x += jitterDist(rng) * currentParams.positionJitter * spacing;
            candidatePos.z += jitterDist(rng) * currentParams.positionJitter * spacing;

            events::terrain::GetTerrainHeightAtQuery heightQuery;
            heightQuery.worldX = candidatePos.x;
            heightQuery.worldZ = candidatePos.z;
            auto heightResult = dispatcher.query(heightQuery);
            candidatePos.y = heightResult.valid ? heightResult.height : worldPos.y;

            if (spatialGrid.hasNeighborWithin(candidatePos, spacing))
            {
                continue;
            }

            uint32_t enabledIdx = paletteDist(rng);
            uint32_t paletteIdx = enabledIndices[enabledIdx];
            const auto& entry = palette[paletteIdx];

            float slopeAngle = std::acos(std::clamp(glm::dot(surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f));
            if (glm::degrees(slopeAngle) > entry.maxSlope) continue;

            candidatePos.y += getAABBYOffset(entry.meshPath);
            candidatePos.y += entry.yOffset;

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

            auto groupEntity = ensureGroupEntity(paletteIdx, candidatePos);

            uint64_t instanceId = nextInstanceId++;

            events::scene::CreateEntityCommand createCmd;
            createCmd.name = "Brush_" + std::to_string(instanceId);
            auto entity = dispatcher.execute(createCmd);
            if (!entity.isValid()) continue;

            events::scene::ReparentEntityCommand reparentCmd;
            reparentCmd.entity = entity;
            reparentCmd.newParent = groupEntity;
            dispatcher.execute(reparentCmd);

            TransformData transform;
            transform.position = candidatePos;
            transform.rotation = rotation;
            transform.scale = glm::vec3(scale);
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = transform;
            dispatcher.execute(transformCmd);

            events::scene::AddMeshComponentCommand meshCmd;
            meshCmd.entity = entity;
            dispatcher.execute(meshCmd);

            MeshData meshData;
            meshData.meshRef = asset::AssetRef::fromPath(entry.meshPath);
            events::scene::SetMeshDataCommand meshDataCmd;
            meshDataCmd.entity = entity;
            meshDataCmd.meshData = meshData;
            dispatcher.execute(meshDataCmd);

            if (!entry.materialPath.empty())
            {
                events::material::AddMaterialComponentCommand matCmd;
                matCmd.entity = entity;
                dispatcher.execute(matCmd);

                events::material::SetDefaultMaterialCommand defaultMatCmd;
                defaultMatCmd.entity = entity;
                defaultMatCmd.materialPath = entry.materialPath;
                dispatcher.execute(defaultMatCmd);
            }

            if (entry.useCollider)
            {
                glm::vec3 colliderSize(0.5f);
                glm::vec3 colliderOffset(0.0f);
                events::render::GetMeshBoundingBoxQuery bbQuery;
                bbQuery.meshPath = entry.meshPath;
                auto bbResult = dispatcher.query(bbQuery);
                if (bbResult.has_value())
                {
                    colliderSize = (bbResult->max - bbResult->min) * 0.5f;
                    colliderOffset = (bbResult->max + bbResult->min) * 0.5f;
                }

                events::scene::AddColliderComponentCommand colliderCmd;
                colliderCmd.entity = entity;
                dispatcher.execute(colliderCmd);

                ColliderComponentData colliderData;
                colliderData.shape = types::ColliderShape::Box;
                colliderData.size = colliderSize;
                colliderData.offset = colliderOffset;
                events::scene::SetColliderDataCommand setColliderCmd;
                setColliderCmd.entity = entity;
                setColliderCmd.colliderData = colliderData;
                dispatcher.execute(setColliderCmd);

                events::scene::AddRigidBodyComponentCommand rbCmd;
                rbCmd.entity = entity;
                dispatcher.execute(rbCmd);

                RigidBodyComponentData rbData;
                rbData.type = types::RigidBodyType::Static;
                events::scene::SetRigidBodyDataCommand setRbCmd;
                setRbCmd.entity = entity;
                setRbCmd.rigidBodyData = rbData;
                dispatcher.execute(setRbCmd);
            }

            spatialGrid.insert(instanceId, candidatePos);
            instanceEntities[instanceId] = entity;

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
        auto entries = spatialGrid.queryRadius(worldPos, currentParams.radius);

        for (const auto& entry : entries)
        {
            spatialGrid.remove(entry.entityId);

            auto it = instanceEntities.find(entry.entityId);
            if (it != instanceEntities.end())
            {
                events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = it->second;
                events::EventDispatcher::instance().execute(deleteCmd);
                instanceEntities.erase(it);
            }
        }
    }

    EntityHandle MeshBrushServiceImpl::ensureGroupEntity(uint32_t paletteIdx, const glm::vec3& worldPos)
    {
        int32_t sx = static_cast<int32_t>(std::floor(worldPos.x / sectorSize));
        int32_t sz = static_cast<int32_t>(std::floor(worldPos.z / sectorSize));
        GroupKey key{paletteIdx, sx, sz};

        auto it = groupEntities.find(key);
        if (it != groupEntities.end() && it->second.isValid())
        {
            auto entt = internal::fromHandle(it->second);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(entt)) return it->second;
        }

        std::string meshLabel = "Group_" + std::to_string(paletteIdx);
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

        // Create empty group entity (no mesh component - just for hierarchy organization)
        events::scene::CreateEntityCommand createCmd;
        createCmd.name = entityName;
        auto entity = dispatcher.execute(createCmd);

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

        events::render::GetMeshBoundingBoxQuery query;
        query.meshPath = meshPath;
        auto result = events::EventDispatcher::instance().query(query);

        if (result.has_value())
        {
            float offset = -result->min.y;
            aabbYOffsetCache[meshPath] = offset;
            return offset;
        }

        // Mesh not loaded yet - don't cache, will retry next time
        return 0.0f;
    }

    void MeshBrushServiceImpl::publishParamsChanged()
    {
        events::meshBrush::MeshBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }
}
