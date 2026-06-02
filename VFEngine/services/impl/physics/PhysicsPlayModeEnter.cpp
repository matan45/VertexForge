#include "print/Log.hpp"
#include "PhysicsPlayModeHandler.hpp"
#include "PhysicsBodyBuilder.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/PhysicsAnimationComponent.hpp"
#include "components/ControllerComponents.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "threading/JobSystem.hpp"
#include <glm/gtc/quaternion.hpp>

namespace services
{
    void PhysicsPlayModeHandler::enterPlayMode()
    {
        if (!physicsProvider || !physicsProvider->isInitialized())
        {
            vfLogWarning("Physics provider not available, skipping physics initialization");
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::RigidBodyComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& rigidBody = view.get<components::RigidBodyComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            RigidBodyData rbData = buildRigidBodyData(rigidBody);

            ColliderData colData;
            if (registry.all_of<components::ColliderComponent>(entity))
            {
                const auto& collider = registry.get<components::ColliderComponent>(entity);
                std::string entityName = registry.all_of<components::NameComponent>(entity)
                    ? registry.get<components::NameComponent>(entity).name : "Unknown";

                std::string validationError = validateCollider(collider, rigidBody, entityName);
                if (!validationError.empty())
                {
                    vfLogWarning("{} - skipping physics body creation", validationError);
                    continue;
                }
                colData = buildColliderData(collider, entity, registry);
            }
            else
            {
                colData.shape = ColliderData::Shape::Box;
                colData.size = glm::vec3(1.0f);
            }

            applyScaleToCollider(colData, transform.scale);

            EntityHandle handle = internal::toHandle(entity);
            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::quat rotQuat = glm::quat(glm::radians(transform.rotation));
            physicsProvider->setRotation(handle, rotQuat);
            activePhysicsBodies.insert(handle);

        }

        auto colliderOnlyView = registry.view<components::ColliderComponent, components::TransformComponent>(
            entt::exclude<components::RigidBodyComponent>);

        for (auto entity : colliderOnlyView)
        {
            const auto& collider = colliderOnlyView.get<components::ColliderComponent>(entity);
            const auto& transform = colliderOnlyView.get<components::TransformComponent>(entity);

            RigidBodyData rbData;
            rbData.type = RigidBodyData::Type::Static;

            ColliderData colData = buildColliderData(collider, entity, registry);
            applyScaleToCollider(colData, transform.scale);

            EntityHandle handle = internal::toHandle(entity);
            physicsProvider->addRigidBody(handle, rbData, colData);
            physicsProvider->setPosition(handle, transform.position);

            glm::quat rotQuat = glm::quat(glm::radians(transform.rotation));
            physicsProvider->setRotation(handle, rotQuat);
            activePhysicsBodies.insert(handle);
        }

        initializePhysicsAnimations();
        initializeCharacterControllers();

        if (scriptFixedUpdateCallback)
        {
            physicsProvider->setPostStepCallback([this](float fixedDt)
            {
                scriptFixedUpdateCallback(fixedDt);
            });
        }

        physicsActive = true;
        vfLogInfo("Physics play mode started with {} bodies, {} characters",
                  activePhysicsBodies.size(), activeCharacterControllers.size());
    }
}
