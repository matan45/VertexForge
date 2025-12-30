#include "SceneBVHManager.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../render/occlusion/OcclusionCullingManager.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "material/MaterialTypes.hpp"

namespace controllers::offscreen
{
    SceneBVHManager::SceneBVHManager() = default;

    SceneBVHManager::~SceneBVHManager()
    {
        cleanUp();
    }

    void SceneBVHManager::init(render::RenderPassHandler* renderHandler)
    {
        // Set up BVH mesh bounds callback
        sceneBVH.setMeshBoundsCallback([renderHandler](const std::string& meshPath) -> const math::AABB*
        {
            auto* meshPipeline = renderHandler->getMeshPipeline();
            if (meshPipeline)
            {
                return meshPipeline->getMeshBoundingBox(meshPath);
            }
            return nullptr;
        });

        // Subscribe to mesh data changes (mesh added/changed/removed on entity)
        auto meshChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);
                if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity))
                {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic)
                    {
                        sceneBVH.markStaticDirty();
                    }
                    else
                    {
                        sceneBVH.markDynamicDirty();
                    }
                }
            });
        meshDataChangedSubscription = std::make_unique<events::SubscriptionToken>(meshChangedToken);

        // Subscribe to entity deleted notification
        auto deletedToken = events::EventDispatcher::instance().subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification)
            {
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);
                if (sceneBVH.isStaticEntity(entityId))
                {
                    sceneBVH.markStaticDirty();
                }
                else
                {
                    sceneBVH.markDynamicDirty();
                }
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(deletedToken);

        // Subscribe to entity static changed notification
        auto staticChangedToken = events::EventDispatcher::instance().subscribe<
            events::scene::EntityStaticChangedNotification>(
            [this](const events::scene::EntityStaticChangedNotification&)
            {
                sceneBVH.markDirty();
            });
        entityStaticChangedSubscription = std::make_unique<events::SubscriptionToken>(staticChangedToken);
    }

    void SceneBVHManager::cleanUp()
    {
        if (meshDataChangedSubscription && meshDataChangedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*meshDataChangedSubscription);
        }
        if (entityDeletedSubscription && entityDeletedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entityDeletedSubscription);
        }
        if (entityStaticChangedSubscription && entityStaticChangedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entityStaticChangedSubscription);
        }
    }

    void SceneBVHManager::rebuild()
    {
        sceneBVH.rebuildAll();
    }

    void SceneBVHManager::markDirty()
    {
        sceneBVH.markDirty();
    }

    void SceneBVHManager::updateOcclusionCullingData(render::RenderPassHandler* renderHandler)
    {
        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* activeCamera = cameraManager->getCamera(cameraManager->getActiveCameraId());

        if (!activeCamera || !activeCamera->useOcclusionCulling || !activeCamera->occlusionInitialized)
        {
            return;
        }

        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        std::vector<render::occlusion::GPUObjectData> objectData;
        objectData.reserve(view.size_hint());

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (meshComp.meshPath.empty())
            {
                continue;
            }

            const math::AABB* localAABB = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
            if (!localAABB || !localAABB->isValid())
            {
                continue;
            }

            uint32_t flags = render::occlusion::OcclusionFlags::None;

            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& matComp = registry.get<components::MaterialComponent>(entity);
                std::string materialToCheck = matComp.defaultMaterial;
                if (!materialToCheck.empty())
                {
                    material::BlendMode blendMode = meshPipeline->getMaterialBlendMode(materialToCheck);
                    if (blendMode != material::BlendMode::Opaque)
                    {
                        flags |= render::occlusion::OcclusionFlags::Transparent;
                    }
                }
            }

            render::occlusion::GPUObjectData obj;
            obj.aabbMin = glm::vec4(localAABB->min, static_cast<float>(entity));
            obj.aabbMax = glm::vec4(localAABB->max, glm::uintBitsToFloat(flags));
            obj.modelMatrix = worldTransform.worldMatrix;

            objectData.push_back(obj);
        }

        if (!objectData.empty())
        {
            render::occlusion::CameraId activeCameraId = cameraManager->getActiveCameraId();
            renderHandler->updateOcclusionObjects(activeCameraId, objectData);
            renderHandler->updateOcclusionCamera(activeCameraId, activeCamera->viewProj, activeCamera->nearPlane);
        }
    }
}
