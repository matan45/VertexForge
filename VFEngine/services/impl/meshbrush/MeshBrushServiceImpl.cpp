#include "MeshBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../events/render/MaterialEvents.hpp"
#include "../../events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../data/MeshBrushUndoCommands.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../../utilities/math/TransformUtils.hpp"
#include <asset/AssetRef.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace services
{
    MeshBrushServiceImpl::~MeshBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (modeChangedToken.isValid())
            dispatcher.unsubscribe(modeChangedToken);
        if (sceneClearedToken.isValid())
            dispatcher.unsubscribe(sceneClearedToken);
        if (entityDeletedToken.isValid())
            dispatcher.unsubscribe(entityDeletedToken);
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
                rebuildSpatialGrid();
                publishParamsChanged();
            });

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushModeCommand>(
            [this](const events::meshBrush::SetMeshBrushModeCommand& cmd)
            {
                if (currentMode != cmd.mode)
                    finalizeStroke();
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

        dispatcher.registerCommandHandler<events::meshBrush::FinalizeMeshBrushCommand>(
            [this](const events::meshBrush::FinalizeMeshBrushCommand&)
            {
                finalizeStroke();
            });

        dispatcher.registerCommandHandler<events::meshBrush::ApplyMeshBrushInstanceDeltaCommand>(
            [this](const events::meshBrush::ApplyMeshBrushInstanceDeltaCommand& cmd)
            {
                applyInstanceDelta(cmd.removeIds, cmd.respawnSpecs);
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
                if (meshBrushModeActive && !n.isActive)
                    finalizeStroke();
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
                entityInstanceIds.clear();
                instanceSpecs.clear();
                discardStroke();
                aabbYOffsetCache.clear();
            });

        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& n)
            {
                auto it = entityInstanceIds.find(n.entity);
                if (it != entityInstanceIds.end())
                    forgetInstance(it->second);
            });
    }

    void MeshBrushServiceImpl::applyBrush(const glm::vec3& worldPos, const glm::vec3& normal,
                                           float deltaTime, bool isFirst)
    {
        if (palette.empty()) return;

        if (isFirst)
        {
            if (!strokeCreated.empty() || !strokeRemoved.empty())
                finalizeStroke();
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

            if (nextInstanceId == 0 || nextInstanceId == std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("Mesh brush instance ID space exhausted");

            meshbrush::MeshBrushInstanceSpec spec;
            spec.instanceId = nextInstanceId++;
            spec.paletteIndex = paletteIdx;
            spec.worldPosition = candidatePos;
            spec.rotation = rotation;
            spec.scale = glm::vec3(scale);
            spec.meshPath = entry.meshPath;
            spec.materialPath = entry.materialPath;
            spec.useCollider = entry.useCollider;

            auto entity = spawnInstance(spec);
            if (!entity.isValid())
                continue;

            strokeCreated.push_back(spec);
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
            auto specIt = instanceSpecs.find(entry.entityId);
            if (specIt == instanceSpecs.end())
                continue;

            auto spec = specIt->second;
            if (removeInstance(entry.entityId))
                strokeRemoved.push_back(std::move(spec));
        }
    }

    EntityHandle MeshBrushServiceImpl::spawnInstance(const meshbrush::MeshBrushInstanceSpec& spec)
    {
        if (spec.instanceId == 0 || spec.instanceId == std::numeric_limits<uint64_t>::max())
            return EntityHandle::invalid();
        if (instanceEntities.contains(spec.instanceId) || instanceSpecs.contains(spec.instanceId))
            return EntityHandle::invalid();

        auto groupEntity = ensureGroupEntity(spec.paletteIndex, spec.worldPosition);
        if (!groupEntity.isValid())
            return EntityHandle::invalid();

        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::CreateEntityCommand createCmd;
        createCmd.name = "Brush_" + std::to_string(spec.instanceId);
        auto entity = dispatcher.execute(createCmd);
        if (!entity.isValid())
            return EntityHandle::invalid();

        try
        {
            events::scene::ReparentEntityCommand reparentCmd;
            reparentCmd.entity = entity;
            reparentCmd.newParent = groupEntity;
            if (!dispatcher.execute(reparentCmd))
                throw std::runtime_error("Failed to parent mesh brush instance");

            int32_t sx = static_cast<int32_t>(std::floor(spec.worldPosition.x / sectorSize));
            int32_t sz = static_cast<int32_t>(std::floor(spec.worldPosition.z / sectorSize));
            glm::vec3 sectorCenter(
                (static_cast<float>(sx) + 0.5f) * sectorSize,
                0.0f,
                (static_cast<float>(sz) + 0.5f) * sectorSize);

            TransformData transform;
            transform.position = spec.worldPosition - sectorCenter;
            transform.rotation = spec.rotation;
            transform.scale = spec.scale;
            events::scene::SetTransformCommand transformCmd;
            transformCmd.entity = entity;
            transformCmd.transform = transform;
            dispatcher.execute(transformCmd);

            events::scene::AddMeshComponentCommand meshCmd;
            meshCmd.entity = entity;
            dispatcher.execute(meshCmd);

            MeshData meshData;
            meshData.meshRef = asset::AssetRef::fromPath(spec.meshPath);
            events::scene::SetMeshDataCommand meshDataCmd;
            meshDataCmd.entity = entity;
            meshDataCmd.meshData = meshData;
            dispatcher.execute(meshDataCmd);

            if (!spec.materialPath.empty())
            {
                events::material::AddMaterialComponentCommand matCmd;
                matCmd.entity = entity;
                dispatcher.execute(matCmd);

                events::material::SetDefaultMaterialCommand defaultMatCmd;
                defaultMatCmd.entity = entity;
                defaultMatCmd.materialPath = spec.materialPath;
                dispatcher.execute(defaultMatCmd);
            }

            if (spec.useCollider)
            {
                glm::vec3 colliderSize(0.5f);
                glm::vec3 colliderOffset(0.0f);
                events::render::GetMeshBoundingBoxQuery bbQuery;
                bbQuery.meshPath = spec.meshPath;
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

            auto [specIt, specInserted] = instanceSpecs.emplace(spec.instanceId, spec);
            auto [entityIt, entityInserted] = instanceEntities.emplace(spec.instanceId, entity);
            auto [reverseIt, reverseInserted] = entityInstanceIds.emplace(entity, spec.instanceId);
            if (!specInserted || !entityInserted || !reverseInserted)
                throw std::logic_error("Duplicate mesh brush instance tracking state");

            spatialGrid.insert(spec.instanceId, spec.worldPosition);
            nextInstanceId = std::max(nextInstanceId, spec.instanceId + 1);
            return entity;
        }
        catch (...)
        {
            forgetInstance(spec.instanceId);
            try
            {
                events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = entity;
                dispatcher.execute(deleteCmd);
            }
            catch (...)
            {
            }
            return EntityHandle::invalid();
        }
    }

    bool MeshBrushServiceImpl::removeInstance(uint64_t instanceId)
    {
        auto it = instanceEntities.find(instanceId);
        if (it == instanceEntities.end())
        {
            forgetInstance(instanceId);
            return false;
        }

        const auto entity = it->second;
        events::scene::DeleteEntityCommand deleteCmd;
        deleteCmd.entity = entity;
        const bool deleted = events::EventDispatcher::instance().execute(deleteCmd);
        forgetInstance(instanceId);
        return deleted;
    }

    void MeshBrushServiceImpl::forgetInstance(uint64_t instanceId)
    {
        spatialGrid.remove(instanceId);
        auto entityIt = instanceEntities.find(instanceId);
        if (entityIt != instanceEntities.end())
        {
            entityInstanceIds.erase(entityIt->second);
            instanceEntities.erase(entityIt);
        }
        instanceSpecs.erase(instanceId);
    }

    void MeshBrushServiceImpl::applyInstanceDelta(
        const std::vector<uint64_t>& removeIds,
        const std::vector<meshbrush::MeshBrushInstanceSpec>& respawnSpecs)
    {
        std::unordered_set<uint64_t> removeSet(removeIds.begin(), removeIds.end());
        if (removeSet.size() != removeIds.size())
            throw std::invalid_argument("Mesh brush delta contains duplicate removal IDs");

        std::unordered_set<uint64_t> respawnIds;
        respawnIds.reserve(respawnSpecs.size());
        for (const auto& spec : respawnSpecs)
        {
            if (spec.instanceId == 0 || spec.instanceId == std::numeric_limits<uint64_t>::max())
                throw std::invalid_argument("Mesh brush delta contains an invalid instance ID");
            if (!respawnIds.insert(spec.instanceId).second)
                throw std::invalid_argument("Mesh brush delta contains duplicate respawn IDs");
            if ((instanceEntities.contains(spec.instanceId) || instanceSpecs.contains(spec.instanceId)) &&
                !removeSet.contains(spec.instanceId))
                throw std::invalid_argument("Mesh brush delta would respawn a live instance ID");
        }

        std::vector<meshbrush::MeshBrushInstanceSpec> removedSpecs;
        std::vector<uint64_t> spawnedIds;
        removedSpecs.reserve(removeIds.size());
        spawnedIds.reserve(respawnSpecs.size());

        try
        {
            for (uint64_t id : removeIds)
            {
                auto specIt = instanceSpecs.find(id);
                if (specIt == instanceSpecs.end())
                    continue;
                auto snapshot = specIt->second;
                if (removeInstance(id))
                    removedSpecs.push_back(std::move(snapshot));
            }

            for (const auto& spec : respawnSpecs)
            {
                if (!spawnInstance(spec).isValid())
                    throw std::runtime_error("Failed to respawn a mesh brush instance");
                spawnedIds.push_back(spec.instanceId);
            }
        }
        catch (...)
        {
            for (auto it = spawnedIds.rbegin(); it != spawnedIds.rend(); ++it)
            {
                try { removeInstance(*it); } catch (...) {}
            }
            for (const auto& spec : removedSpecs)
            {
                if (!instanceSpecs.contains(spec.instanceId))
                    spawnInstance(spec);
            }
            throw;
        }
    }

    void MeshBrushServiceImpl::rebuildSpatialGrid()
    {
        spatialGrid.clear();
        for (const auto& [id, spec] : instanceSpecs)
            spatialGrid.insert(id, spec.worldPosition);
    }

    void MeshBrushServiceImpl::finalizeStroke()
    {
        if (strokeCreated.empty() && strokeRemoved.empty())
            return;

        auto undoCommand = std::make_shared<MeshBrushStrokeUndoCommand>(strokeCreated, strokeRemoved);
        if (!undoCommand->hasChanges())
            return;

        events::undoredo::PushUndoableCommand pushCommand;
        pushCommand.command = std::move(undoCommand);
        events::EventDispatcher::instance().execute(pushCommand);
        discardStroke();
    }

    void MeshBrushServiceImpl::discardStroke()
    {
        strokeCreated.clear();
        strokeRemoved.clear();
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
        if (!entity.isValid())
            return EntityHandle::invalid();

        try
        {
            // Position the group entity at the center of its sector in world space
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

            groupEntities[key] = entity;
            return entity;
        }
        catch (...)
        {
            try
            {
                events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = entity;
                dispatcher.execute(deleteCmd);
            }
            catch (...)
            {
            }
            return EntityHandle::invalid();
        }
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
