#include "MeshBrushServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
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
            });
    }

    void MeshBrushServiceImpl::applyBrush(const glm::vec3& worldPos, const glm::vec3& normal,
                                           float deltaTime, bool isFirst)
    {
        if (palette.empty()) return;

        if (currentMode == meshbrush::MeshBrushMode::Paint)
        {
            placeMeshes(worldPos, normal);
        }
        else
        {
            eraseInstances(worldPos);
        }
    }

    void MeshBrushServiceImpl::placeMeshes(const glm::vec3& worldPos, const glm::vec3& normal)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Build weight distribution
        std::vector<float> weights;
        for (const auto& entry : palette)
        {
            weights.push_back(entry.weight);
        }
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

            // Select palette entry
            uint32_t paletteIdx = paletteDist(rng);
            const auto& entry = palette[paletteIdx];

            // Check slope
            float slopeAngle = std::acos(std::clamp(glm::dot(normal, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f));
            float slopeDeg = glm::degrees(slopeAngle);
            if (slopeDeg > entry.maxSlope)
            {
                continue;
            }

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

            if (entry.alignToNormal && glm::length(normal) > 0.001f)
            {
                glm::vec3 up(0.0f, 1.0f, 0.0f);
                glm::vec3 n = glm::normalize(normal);
                float alignAngle = glm::degrees(std::acos(std::clamp(glm::dot(n, up), -1.0f, 1.0f)));
                glm::vec3 axis = glm::cross(up, n);
                if (glm::length(axis) > 0.001f)
                {
                    axis = glm::normalize(axis);
                    transform.rotation.x = alignAngle * axis.x;
                    transform.rotation.z = alignAngle * axis.z;
                }
            }

            // Create entity
            events::scene::CreateEntityCommand createCmd;
            createCmd.name = "MeshBrush_" + std::to_string(placedCount);
            auto entity = dispatcher.execute(createCmd);
            if (!entity.isValid()) continue;

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
                components::MeshBrushInstanceComponent{0, paletteIdx, normal});

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

    void MeshBrushServiceImpl::publishParamsChanged()
    {
        events::meshBrush::MeshBrushParamsChangedNotification notification;
        notification.params = currentParams;
        events::EventDispatcher::instance().publish(notification);
    }
}
