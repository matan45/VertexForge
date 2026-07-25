#include "ControllerServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/animation/AnimatorEvents.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/ControllerComponents.hpp"
#include "components/CoreComponents.hpp"
#include "components/NavmeshComponents.hpp"
#include "components/PhysicsComponents.hpp"
#include "components/MediaComponents.hpp"
#include "water/SwimMath.hpp"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace services
{
    ControllerServiceImpl::ControllerServiceImpl(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider)
    {
    }

    void ControllerServiceImpl::applyControllerMovement(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto view = registry.view<components::ControllerComponent, components::TransformComponent>();

        // VK-1606: resolve "is there water" once per tick rather than once per character.
        // GetOceanHeightAtQuery cannot answer it - it returns 0 both for "no water" and for "the
        // water really is at y = 0". Skipped entirely in the common case where nothing swims.
        //
        // VK-1607: this used to ask GetOceanEntityQuery, i.e. "is there an OCEAN entity", so a
        // character could not swim in a lake unless the scene also happened to contain an ocean.
        waterPresentThisFrame = false;
        {
            bool anySwimmer = false;
            for (auto entity : view)
            {
                if (view.get<components::ControllerComponent>(entity).swimEnabled)
                {
                    anySwimmer = true;
                    break;
                }
            }
            if (anySwimmer)
            {
                ::events::ocean::HasAnyWaterQuery waterQuery;
                waterPresentThisFrame = dispatcher.query(waterQuery);
            }
        }

        for (auto entity : view)
        {
            auto& controller = view.get<components::ControllerComponent>(entity);
            auto& transform = view.get<components::TransformComponent>(entity);
            auto handle = internal::toHandle(entity);

            if (controller.hasMoveToTarget)
            {
                float dist = glm::length(controller.moveToDestination - transform.position);
                if (dist < controller.arrivalDistance)
                {
                    controller.hasMoveToTarget = false;
                    controller.moveToDestinationDirty = false;
                    controller.moveInput = glm::vec3(0.0f);

                    if (registry.all_of<components::NavmeshAgentComponent>(entity))
                    {
                        ::events::navmesh::StopAgentCommand stopCmd;
                        stopCmd.entity = handle;
                        dispatcher.execute(stopCmd);
                    }
                    continue;
                }

                if (registry.all_of<components::NavmeshAgentComponent>(entity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(entity);
                    float targetSpeed = controller.moveSpeed;
                    if (controller.wantsSprint)
                    {
                        targetSpeed *= controller.sprintMultiplier;
                    }
                    agent.maxSpeed = targetSpeed;

                    if (controller.moveToDestinationDirty)
                    {
                        ::events::navmesh::SetAgentDestinationCommand destCmd;
                        destCmd.entity = handle;
                        destCmd.target = controller.moveToDestination;
                        dispatcher.execute(destCmd);
                        controller.moveToDestinationDirty = false;
                    }
                    continue;
                }

                glm::vec3 dir = glm::normalize(controller.moveToDestination - transform.position);
                controller.moveInput = dir;
            }

            if (controller.characterControllerActive && physicsProvider)
            {
                // Character controllers manage their own wantsJump latch so a
                // frame that runs zero fixed sub-steps does not swallow the input.
                updateCharacterControllerEntity(entity);
            }
            else
            {
                updateRigidBodyEntity(entity, deltaTime);
                controller.wantsJump = false;
            }

            deriveLocomotionState(controller);
            syncLocomotionToAnimator(entity, controller);

            if (!controller.hasMoveToTarget)
            {
                controller.moveInput = glm::vec3(0.0f);
            }
            controller.wantsSprint = false;
        }

        // VK-1530: bound ccInterp growth. The view above enumerates only live controllers,
        // so any stored key that is no longer valid belongs to a destroyed entity; drop it.
        // (Reused entt slots get a new version, so a stale key never collides with a live
        // entity.)
        for (auto it = ccInterp.begin(); it != ccInterp.end();)
        {
            if (registry.valid(it->first))
                ++it;
            else
                it = ccInterp.erase(it);
        }
    }

    void ControllerServiceImpl::updateCharacterControllerEntity(entt::entity entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& controller = registry.get<components::ControllerComponent>(entity);
        auto& transform = registry.get<components::TransformComponent>(entity);
        auto handle = internal::toHandle(entity);

        // VK-1530: advance the character controller at the same fixed cadence as
        // the rigid-body world (shared step count + interpolation alpha from the
        // physics provider) so movement is frame-rate independent, and render the
        // transform interpolated between fixed steps to stay smooth above tick rate.
        const int steps = physicsProvider->getPhysicsStepsTaken();
        const float fixedDt = physicsProvider->getFixedTimestep();
        const float alpha = physicsProvider->getInterpolationAlpha();

        // VK-1606: decide whether this character is swimming, ONCE for the whole frame. Sampling the
        // surface per fixed sub-step would multiply an already expensive query by the step count for
        // a height that moves a few millimetres in between.
        //
        // Note this samples at the RENDERED position, which lags the authoritative one by the
        // interpolation alpha. Over one frame that is centimetres, far below the hysteresis band, so
        // it cannot make the swim state flicker.
        bool swimming = false;
        float waterSurfaceY = 0.0f;
        float swimHalfHeight = 0.0f;
        // VK-1607: GetWaterSurfaceAtQuery, not GetOceanHeightAtQuery. A bounded water body covers
        // part of the world and dry land covers the rest, so "no water here" has to be expressible -
        // otherwise a character standing beside a lake reads the fallback 0 as a surface and can
        // register submersion on dry ground. With an infinite ocean present the optional is always
        // engaged, so ocean-only scenes behave exactly as before.
        bool overWater = false;
        if (controller.swimEnabled && waterPresentThisFrame)
        {
            ::events::ocean::GetWaterSurfaceAtQuery surfaceQuery;
            surfaceQuery.worldXZ = glm::vec2(transform.position.x, transform.position.z);
            if (auto surface = ::events::EventDispatcher::instance().query(surfaceQuery))
            {
                overWater = true;
                waterSurfaceY = *surface;
            }
        }

        if (overWater)
        {
            // The capsule the character controller actually uses comes from its collider; fall back
            // to a 1.8 m humanoid so a controller authored without one still floats sensibly.
            swimHalfHeight = 0.9f;
            if (registry.all_of<components::ColliderComponent>(entity))
            {
                const auto& collider = registry.get<components::ColliderComponent>(entity);
                swimHalfHeight = water::colliderHalfHeight(collider.shape, collider.size, collider.height);
            }

            controller.submersion =
                water::capsuleSubmersion(transform.position.y, swimHalfHeight, waterSurfaceY);
            swimming = water::swimStateFor(controller.isSwimming, controller.submersion,
                                           controller.swimEnterSubmersion);
        }
        else
        {
            controller.submersion = 0.0f;
        }
        controller.isSwimming = swimming;

        // Swimming replaces the ground speed outright; sprinting is a land affordance.
        const float targetSpeed = swimming
            ? controller.swimSpeed
            : (controller.wantsSprint ? controller.moveSpeed * controller.sprintMultiplier
                                       : controller.moveSpeed);

        glm::vec3 desiredHorizontal{0.0f};
        if (glm::length2(controller.moveInput) > 0.001f)
        {
            desiredHorizontal = glm::normalize(controller.moveInput) * targetSpeed;
        }

        const glm::vec3 gravity = physicsProvider->getGravity();
        const float swimTargetY = swimming
            ? water::swimTargetCenterY(waterSurfaceY, swimHalfHeight, controller.floatDepth)
            : 0.0f;

        // Two persistent sim snapshots for this entity (prev = after sub-step N-1, curr =
        // after sub-step N). On first sight, seed both onto the current transform (freshly
        // placed); a fresh entity is never teleport-snap-tested.
        auto& interp = ccInterp[entity];
        const bool freshlySeeded = !interp.seeded;
        if (freshlySeeded)
            physics::seedCharacterInterp(interp, transform.position);
        const glm::vec3 beforeFrame = interp.curr;

        for (int i = 0; i < steps; ++i)
        {
            glm::vec3 currentHorizontal{controller.currentVelocity.x, 0.0f, controller.currentVelocity.z};
            glm::vec3 velocityDiff = desiredHorizontal - currentHorizontal;
            float diffLen = glm::length(velocityDiff);

            if (diffLen > 0.001f)
            {
                float rate = glm::length2(desiredHorizontal) >= glm::length2(currentHorizontal)
                    ? controller.acceleration
                    : controller.deceleration;

                if (!controller.isGrounded)
                {
                    rate *= controller.airControlFactor;
                }

                float maxDelta = rate * fixedDt;
                if (diffLen <= maxDelta)
                {
                    currentHorizontal = desiredHorizontal;
                }
                else
                {
                    currentHorizontal += (velocityDiff / diffLen) * maxDelta;
                }
            }

            float verticalVel = controller.currentVelocity.y;

            if (swimming)
            {
                // VK-1606: buoyancy REPLACES gravity while swimming. A CharacterVirtual has no
                // solver body for OceanService::updateBuoyancy to push, so the spring is integrated
                // here instead. Backward Euler, so a frame hitch cannot launch the character.
                verticalVel = water::buoyancyStep(interp.curr.y, swimTargetY, verticalVel,
                                                  controller.swimBuoyancyStiffness, fixedDt);
                verticalVel = water::applyWaterDrag(verticalVel, controller.waterDrag, fixedDt);

                // Jump means "swim up" in water — and unlike the ground jump it does NOT require
                // being grounded, which is the whole point.
                if (i == 0 && controller.wantsJump)
                {
                    verticalVel = controller.swimUpSpeed;
                }
            }
            else
            {
                verticalVel += gravity.y * fixedDt;

                // Latch the jump to the first sub-step: a multi-step frame must not
                // apply the impulse more than once.
                if (i == 0 && controller.wantsJump && controller.jumpForce > 0.0f && controller.isGrounded)
                {
                    verticalVel = controller.jumpForce;
                }
            }

            glm::vec3 fullVelocity{currentHorizontal.x, verticalVel, currentHorizontal.z};
            auto result = physicsProvider->updateCharacterController(handle, fullVelocity, fixedDt);

            controller.isGrounded = result.isGrounded;
            controller.currentVelocity = result.linearVelocity;

            // A swimmer brushing the seabed is not standing on it. Left alone, the ground contact
            // would pick Idle/Walk out of deriveLocomotionState and the character would appear to
            // walk along the bottom while floating.
            if (swimming)
                controller.isGrounded = false;

            physics::pushSimStep(interp, result.position);   // shift prev<-curr, curr<-new
        }

        // Consume the jump only if a sub-step actually ran this frame.
        if (steps > 0)
        {
            controller.wantsJump = false;
        }

        controller.verticalVelocity = controller.currentVelocity.y;
        controller.currentSpeed = glm::length(glm::vec2(controller.currentVelocity.x, controller.currentVelocity.z));

        // Snap (skip interpolation) across teleports / respawns — a character never
        // legitimately moves this far in one frame; interpolating would streak. A freshly
        // seeded entity has no meaningful "before" to compare against, so it is exempt.
        constexpr float CC_TELEPORT_SNAP_DISTANCE2 = 50.0f * 50.0f;
        if (!freshlySeeded)
            physics::snapOnTeleport(interp, beforeFrame, CC_TELEPORT_SNAP_DISTANCE2);

        // Render between the two persistent sim snapshots; the character's authoritative
        // position (Jolt-side) is always interp.curr — only the rendered transform lags by
        // the interpolation factor (alpha is clamped inside interpRenderPos).
        transform.position = physics::interpRenderPos(interp, alpha);
        transform.isDirty = true;
    }

    void ControllerServiceImpl::updateRigidBodyEntity(entt::entity entity, float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& controller = registry.get<components::ControllerComponent>(entity);
        auto& transform = registry.get<components::TransformComponent>(entity);
        auto handle = internal::toHandle(entity);
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (glm::length(controller.moveInput) > 0.001f)
        {
            float speed = controller.moveSpeed;
            if (controller.wantsSprint)
            {
                speed *= controller.sprintMultiplier;
            }

            glm::vec3 velocity = controller.moveInput * speed;

            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                ::events::physics::SetLinearVelocityCommand velCmd;
                velCmd.entity = handle;
                velCmd.velocity = glm::vec3(velocity.x, 0.0f, velocity.z);
                dispatcher.execute(velCmd);
            }
            else
            {
                transform.position += velocity * deltaTime;
                transform.isDirty = true;
            }

            controller.currentVelocity = velocity;
            controller.currentSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
        }
        else
        {
            controller.currentVelocity = glm::vec3(0.0f);
            controller.currentSpeed = 0.0f;
        }

        if (controller.wantsJump && controller.jumpForce > 0.0f && controller.isGrounded)
        {
            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                ::events::physics::ApplyImpulseCommand impulseCmd;
                impulseCmd.entity = handle;
                impulseCmd.impulse = glm::vec3(0.0f, controller.jumpForce, 0.0f);
                dispatcher.execute(impulseCmd);
                controller.isGrounded = false;
            }
        }
    }

    void ControllerServiceImpl::deriveLocomotionState(components::ControllerComponent& controller)
    {
        const auto& config = controller.locomotionConfig;

        const std::string* newState = nullptr;

        // VK-1606: swimming wins over everything else. It is checked first because a swimmer is
        // deliberately never grounded, so the fall branch below would otherwise claim it.
        if (controller.isSwimming)
        {
            newState = &config.swimState;
        }
        else if (!controller.isGrounded)
        {
            newState = controller.verticalVelocity > 0.1f
                ? &config.jumpState
                : &config.fallState;
        }
        else if (controller.currentSpeed < 0.1f)
        {
            newState = &config.idleState;
        }
        else if (controller.currentSpeed < controller.walkSpeedThreshold)
        {
            newState = &config.walkState;
        }
        else
        {
            newState = &config.runState;
        }

        if (newState && !newState->empty())
        {
            controller.locomotionState = *newState;
        }
    }

    void ControllerServiceImpl::syncLocomotionToAnimator(entt::entity entity,
                                                          const components::ControllerComponent& controller)
    {
        if (!controller.locomotionConfig.syncToAnimator) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::AnimatorComponent>(entity)) return;

        const auto& animComp = registry.get<components::AnimatorComponent>(entity);
        if (!animComp.isInitialized) return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto handle = internal::toHandle(entity);

        bool hasDirection = glm::length2(controller.moveInput) > 0.001f;

        for (const auto& mapping : controller.locomotionConfig.paramMappings)
        {
            if (mapping.paramName.empty()) continue;

            switch (mapping.source)
            {
            case components::LocomotionParamSource::Speed:
            {
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.currentSpeed;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::Grounded:
            {
                ::services::events::animator::SetEntityAnimatorBoolCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.isGrounded;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::VerticalVelocity:
            {
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.verticalVelocity;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::DirectionX:
            {
                if (!hasDirection) break;
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.moveInput.x;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::DirectionY:
            {
                if (!hasDirection) break;
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.moveInput.z;
                dispatcher.execute(cmd);
                break;
            }
            }
        }
    }
}
