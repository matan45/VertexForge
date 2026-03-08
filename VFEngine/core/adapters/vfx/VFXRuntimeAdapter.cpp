#include "VFXRuntimeAdapter.hpp"
#include "VFXSceneRenderer.hpp"
#include "../../graphics/core/VulkanContext.hpp"
#include "../../graphics/render/vfx/GPUVFXTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/CoreComponents.hpp"
#include "components/PhysicsComponents.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/project/SceneEvents.hpp"
#include "../../services/events/terrain/TerrainEvents.hpp"
#include "../../services/events/terrain/BrushEvents.hpp"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include "print/Log.hpp"

namespace core
{
    VFXRuntimeAdapter::VFXRuntimeAdapter()
    {
        subscribeTerrainNotifications();
    }

    VFXRuntimeAdapter::~VFXRuntimeAdapter() noexcept
    {
        unsubscribeTerrainNotifications();
        if (renderer)
        {
            renderer->cleanUp();
            renderer.reset();
        }
    }

    void VFXRuntimeAdapter::init(vk::RenderPass sceneRenderPass)
    {
        if (!renderer)
        {
            auto& device = *VulkanContext::getDevice();
            auto& swapChain = *VulkanContext::getSwapChain();
            renderer = std::make_unique<controllers::VFXSceneRenderer>(device, swapChain);
        }
        renderer->init(sceneRenderPass);
    }

    void VFXRuntimeAdapter::cleanUp()
    {
        if (renderer)
        {
            renderer->cleanUp();
        }
    }

    void VFXRuntimeAdapter::recreate(vk::RenderPass sceneRenderPass)
    {
        if (renderer)
        {
            renderer->recreate(sceneRenderPass);
        }
    }

    bool VFXRuntimeAdapter::isInitialized() const
    {
        return renderer && renderer->isInitialized();
    }

    services::VFXInstanceId VFXRuntimeAdapter::createInstance(const services::VFXRuntimeParams& params)
    {
        if (!renderer)
        {
            vfLogWarning("VFXRuntimeAdapter::createInstance called before init");
            return 0;
        }

        controllers::VFXRuntimeParams controllerParams;
        controllerParams.vfxAssetPath = params.vfxAssetPath;
        controllerParams.worldTransform = params.worldTransform;
        controllerParams.loop = params.loop;
        controllerParams.entityId = params.entityId;
        controllerParams.priority = params.priority;
        controllerParams.cameraRelative = params.cameraRelative;

        return renderer->createInstance(controllerParams);
    }

    void VFXRuntimeAdapter::destroyInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->destroyInstance(id);
        }
    }

    void VFXRuntimeAdapter::setInstanceTransform(services::VFXInstanceId id, const glm::mat4& worldTransform)
    {
        if (renderer)
        {
            renderer->setInstanceTransform(id, worldTransform);
        }
    }

    void VFXRuntimeAdapter::playInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->playInstance(id);
        }
    }

    void VFXRuntimeAdapter::stopInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->stopInstance(id);
        }
    }

    void VFXRuntimeAdapter::resetInstance(services::VFXInstanceId id)
    {
        if (renderer)
        {
            renderer->resetInstance(id);
        }
    }

    bool VFXRuntimeAdapter::isInstancePlaying(services::VFXInstanceId id) const
    {
        return renderer ? renderer->isInstancePlaying(id) : false;
    }

    void VFXRuntimeAdapter::update(float deltaTime)
    {
        if (renderer)
        {
            updateSceneColliders();
            updateTerrainHeightfield();
            renderer->update(deltaTime);
        }
    }

    void VFXRuntimeAdapter::setCamera(const services::VFXCameraParams& camera)
    {
        if (renderer)
        {
            renderer->setCamera(camera);
        }
    }

    void VFXRuntimeAdapter::setSceneDepthImageView(vk::ImageView depthView)
    {
        if (renderer)
        {
            renderer->setSceneDepthImageView(depthView);
        }
    }

    void VFXRuntimeAdapter::recordComputeCommands(const vk::CommandBuffer& cmd)
    {
        if (renderer)
        {
            renderer->recordComputeCommands(cmd);
        }
    }

    void VFXRuntimeAdapter::recordDrawCommands(const vk::CommandBuffer& cmd)
    {
        if (renderer)
        {
            renderer->recordDrawCommands(cmd);
        }
    }

    size_t VFXRuntimeAdapter::getInstanceCount() const
    {
        return renderer ? renderer->getInstanceCount() : 0;
    }

    void VFXRuntimeAdapter::setDistanceCullingEnabled(bool enabled)
    {
        if (renderer) renderer->setDistanceCullingEnabled(enabled);
    }

    void VFXRuntimeAdapter::setMaxDrawDistance(float distance)
    {
        if (renderer) renderer->setMaxDrawDistance(distance);
    }

    services::IVFXRuntimeProvider::BudgetStats VFXRuntimeAdapter::getBudgetStats() const
    {
        BudgetStats stats{};
        if (renderer)
        {
            auto rs = renderer->getBudgetStats();
            stats.activeEmitters = rs.activeEmitters;
            stats.maxEmitters = rs.maxEmitters;
            stats.allocatedParticles = rs.allocatedParticles;
            stats.maxParticles = rs.maxParticles;
            std::copy(std::begin(rs.lodCounts), std::end(rs.lodCounts), std::begin(stats.lodCounts));
            stats.fragmentationPercent = rs.fragmentationPercent;
            stats.poolWarmSlots = rs.poolWarmSlots;
            stats.poolUsedSlots = rs.poolUsedSlots;
            stats.poolTotalSlots = rs.poolTotalSlots;
        }
        return stats;
    }

    void VFXRuntimeAdapter::updateSceneColliders()
    {
        // Skip if no VFX instances are active
        if (renderer->getInstanceCount() == 0)
            return;

        // Refresh max colliders from physics settings periodically (~every 2 seconds at 60fps)
        if (++colliderSettingsRefreshCounter >= 120)
        {
            colliderSettingsRefreshCounter = 0;
            try
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::scene::GetPhysicsSettingsQuery query;
                auto physSettings = dispatcher.query(query);
                cachedMaxColliders = std::min(physSettings.maxVFXSceneColliders,
                                              render::vfx::GPUVFXConstants::MAX_SCENE_COLLIDERS);
            }
            catch (...) {}
        }

        uint32_t maxColliders = cachedMaxColliders;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ColliderComponent, components::TransformComponent>();

        std::vector<render::vfx::GPUCollider> gpuColliders;
        gpuColliders.reserve(maxColliders);

        for (auto entity : view)
        {
            if (gpuColliders.size() >= maxColliders)
                break;

            const auto& collider = view.get<components::ColliderComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            render::vfx::GPUCollider gpu{};
            glm::vec3 worldPos = transform.position + collider.offset;

            // Convert Euler rotation (degrees) to quaternion
            glm::vec3 radians = glm::radians(transform.rotation);
            glm::quat quat = glm::quat(radians);

            gpu.rotation = glm::vec4(quat.x, quat.y, quat.z, quat.w);

            switch (collider.shape)
            {
                case components::ColliderShape::Box:
                    gpu.positionAndType = glm::vec4(worldPos, 1.0f);  // type 1 = Box
                    gpu.dimensions = glm::vec4(collider.size * 0.5f, 0.0f);
                    break;

                case components::ColliderShape::Sphere:
                    gpu.positionAndType = glm::vec4(worldPos, 0.0f);  // type 0 = Sphere
                    gpu.dimensions = glm::vec4(collider.size.x, 0.0f, 0.0f, 0.0f);
                    break;

                case components::ColliderShape::Capsule:
                    gpu.positionAndType = glm::vec4(worldPos, 2.0f);  // type 2 = Capsule
                    gpu.dimensions = glm::vec4(collider.size.x, collider.height * 0.5f, 0.0f, 0.0f);
                    break;

                default:
                    // Skip unsupported shapes (ConvexMesh, TriangleMesh, HeightField)
                    continue;
            }

            gpuColliders.push_back(gpu);
        }

        renderer->setSceneColliders(gpuColliders);
    }

    void VFXRuntimeAdapter::updateTerrainHeightfield()
    {
        if (terrainHeightfieldCached)
            return;

        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::terrain::GetTerrainHeightfieldQuery query;
            auto result = dispatcher.query(query);

            if (result.valid && !result.heights.empty())
            {
                render::vfx::GPUTerrainHeightfield header{};
                header.worldOriginX = result.worldOriginX;
                header.worldOriginZ = result.worldOriginZ;
                header.tileWorldSize = result.tileWorldSize;
                header.vertexSpacing = result.vertexSpacing;
                header.gridCountX = result.gridCountX;
                header.gridCountZ = result.gridCountZ;
                header.verticesPerTile = result.verticesPerTile;
                header.enabled = 1;

                renderer->setTerrainHeightfield(header, std::move(result.heights));
                terrainHeightfieldCached = true;
            }
            else
            {
                // No terrain yet — will retry next frame
                render::vfx::GPUTerrainHeightfield header{};
                header.enabled = 0;
                renderer->setTerrainHeightfield(header, {});
            }
        }
        catch (...)
        {
            // No terrain service registered — disable terrain collision
        }
    }

    void VFXRuntimeAdapter::subscribeTerrainNotifications()
    {
        if (!terrainSubscriptions.empty())
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        terrainSubscriptions.push_back(
            dispatcher.subscribe<events::terrain::TerrainCreatedNotification>(
                [this](const events::terrain::TerrainCreatedNotification&) {
                    terrainHeightfieldCached = false;
                }));

        terrainSubscriptions.push_back(
            dispatcher.subscribe<events::terrain::TerrainDeletedNotification>(
                [this](const events::terrain::TerrainDeletedNotification&) {
                    terrainHeightfieldCached = false;
                }));

        terrainSubscriptions.push_back(
            dispatcher.subscribe<events::terrain::TerrainLoadedNotification>(
                [this](const events::terrain::TerrainLoadedNotification&) {
                    terrainHeightfieldCached = false;
                }));

        terrainSubscriptions.push_back(
            dispatcher.subscribe<events::brush::BrushAppliedNotification>(
                [this](const events::brush::BrushAppliedNotification&) {
                    terrainHeightfieldCached = false;
                }));
    }

    void VFXRuntimeAdapter::unsubscribeTerrainNotifications()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        for (auto& token : terrainSubscriptions)
        {
            if (token.isValid())
                dispatcher.unsubscribe(token);
        }
        terrainSubscriptions.clear();
    }
}
