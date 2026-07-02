#include "PhysicsVehicleManager.hpp"
#include "PhysicsBodyRegistry.hpp"
#include "PhysicsContext.hpp"
#include "PhysicsLayers.hpp"
#include "JoltConversions.hpp"
#include "print/Log.hpp"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Vehicle/MotorcycleController.h>
#include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace core::physics
{
    namespace
    {
        float averageWheelWidth(const types::VehicleConfig& config);

        JPH::Vec3 normalizedOr(JPH::Vec3Arg value, JPH::Vec3Arg fallback)
        {
            return value.NormalizedOr(fallback);
        }

        glm::mat4 toGlm(const JPH::RMat44& matrix)
        {
            glm::mat4 result(1.0f);
            for (uint32_t col = 0; col < 3; ++col)
            {
                const JPH::Vec4 c = matrix.GetColumn4(col);
                result[col] = glm::vec4(c.GetX(), c.GetY(), c.GetZ(), c.GetW());
            }

            const JPH::RVec3 t = matrix.GetTranslation();
            result[3] = glm::vec4(
                static_cast<float>(t.GetX()),
                static_cast<float>(t.GetY()),
                static_cast<float>(t.GetZ()),
                1.0f);
            return result;
        }

        void applyCommonWheelSettings(JPH::WheelSettings& outWheel, const types::WheelConfig& inWheel,
                                      JPH::Vec3Arg up, JPH::Vec3Arg forward)
        {
            outWheel.mPosition = toJolt(inWheel.position);
            outWheel.mSuspensionDirection = normalizedOr(toJolt(inWheel.suspensionDirection), -up);
            outWheel.mSteeringAxis = up;
            outWheel.mWheelUp = up;
            outWheel.mWheelForward = forward;
            outWheel.mRadius = inWheel.radius;
            outWheel.mWidth = inWheel.width;
            outWheel.mSuspensionMinLength = inWheel.suspensionMinLength;
            outWheel.mSuspensionMaxLength = inWheel.suspensionMaxLength;
            outWheel.mSuspensionSpring.mFrequency = inWheel.suspensionFrequency;
            outWheel.mSuspensionSpring.mDamping = inWheel.suspensionDamping;
        }

        void setWheeledFrictionCurve(JPH::WheelSettingsWV& wheel, float longitudinal, float lateral)
        {
            wheel.mLongitudinalFriction.Clear();
            wheel.mLongitudinalFriction.Reserve(3);
            wheel.mLongitudinalFriction.AddPoint(0.0f, 0.0f);
            wheel.mLongitudinalFriction.AddPoint(0.06f, longitudinal);
            wheel.mLongitudinalFriction.AddPoint(0.2f, std::max(0.0f, longitudinal * 0.85f));

            wheel.mLateralFriction.Clear();
            wheel.mLateralFriction.Reserve(3);
            wheel.mLateralFriction.AddPoint(0.0f, 0.0f);
            wheel.mLateralFriction.AddPoint(3.0f, lateral);
            wheel.mLateralFriction.AddPoint(20.0f, std::max(0.0f, lateral * 0.85f));
        }

        JPH::Ref<JPH::WheelSettings> createWheelSettings(const types::VehicleConfig& config,
                                                         const types::WheelConfig& wheel,
                                                         JPH::Vec3Arg up,
                                                         JPH::Vec3Arg forward)
        {
            if (config.controllerType == types::VehicleControllerType::Tracked)
            {
                JPH::WheelSettingsTV* settings = new JPH::WheelSettingsTV;
                applyCommonWheelSettings(*settings, wheel, up, forward);
                settings->mLongitudinalFriction = wheel.longitudinalFriction;
                settings->mLateralFriction = wheel.lateralFriction;
                return settings;
            }

            JPH::WheelSettingsWV* settings = new JPH::WheelSettingsWV;
            applyCommonWheelSettings(*settings, wheel, up, forward);
            settings->mMaxSteerAngle = wheel.maxSteerAngle;
            settings->mMaxBrakeTorque = wheel.maxBrakeTorque;
            settings->mMaxHandBrakeTorque = wheel.maxHandBrakeTorque;
            setWheeledFrictionCurve(*settings, wheel.longitudinalFriction, wheel.lateralFriction);
            return settings;
        }

        void applyEngineSettings(JPH::VehicleEngineSettings& engine, const types::VehicleConfig& config)
        {
            engine.mMaxTorque = config.engineMaxTorque;
            engine.mMinRPM = config.engineMinRPM;
            engine.mMaxRPM = config.engineMaxRPM;
        }

        JPH::VehicleDifferentialSettings toJoltDifferential(const types::VehicleDifferentialConfig& config)
        {
            JPH::VehicleDifferentialSettings result;
            result.mLeftWheel = config.leftWheel;
            result.mRightWheel = config.rightWheel;
            result.mDifferentialRatio = config.differentialRatio;
            result.mLeftRightSplit = config.leftRightSplit;
            result.mEngineTorqueRatio = config.engineTorqueRatio;
            return result;
        }

        void addFallbackDifferential(JPH::Array<JPH::VehicleDifferentialSettings>& differentials,
                                     const types::VehicleConfig& config)
        {
            if (!differentials.empty() || config.wheels.size() < 2)
                return;

            int leftWheel = -1;
            int rightWheel = -1;
            for (int i = 0; i < static_cast<int>(config.wheels.size()); ++i)
            {
                if (!config.wheels[i].driven)
                    continue;

                if (leftWheel < 0)
                    leftWheel = i;
                else
                {
                    rightWheel = i;
                    break;
                }
            }

            if (leftWheel < 0)
                leftWheel = 0;
            if (rightWheel < 0)
                rightWheel = config.wheels.size() > 1 ? 1 : -1;

            JPH::VehicleDifferentialSettings diff;
            diff.mLeftWheel = leftWheel;
            diff.mRightWheel = rightWheel;
            diff.mEngineTorqueRatio = 1.0f;
            differentials.push_back(diff);
        }

        JPH::Ref<JPH::VehicleControllerSettings> createWheeledControllerSettings(const types::VehicleConfig& config)
        {
            JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings;
            applyEngineSettings(controller->mEngine, config);
            controller->mDifferentials.reserve(config.differentials.size());
            for (const auto& diff : config.differentials)
                controller->mDifferentials.push_back(toJoltDifferential(diff));
            addFallbackDifferential(controller->mDifferentials, config);
            return controller;
        }

        JPH::Ref<JPH::VehicleControllerSettings> createMotorcycleControllerSettings(const types::VehicleConfig& config)
        {
            JPH::MotorcycleControllerSettings* controller = new JPH::MotorcycleControllerSettings;
            applyEngineSettings(controller->mEngine, config);
            controller->mMaxLeanAngle = config.maxLeanAngle;
            controller->mLeanSpringConstant = config.leanSpringConstant;
            controller->mLeanSpringDamping = config.leanSpringDamping;
            controller->mDifferentials.reserve(config.differentials.size());
            for (const auto& diff : config.differentials)
                controller->mDifferentials.push_back(toJoltDifferential(diff));
            addFallbackDifferential(controller->mDifferentials, config);
            return controller;
        }

        int resolveTrackIndex(const types::WheelConfig& wheel)
        {
            return wheel.trackIndex >= 0 ? wheel.trackIndex : (wheel.position.x >= 0.0f ? 0 : 1);
        }

        JPH::Ref<JPH::VehicleControllerSettings> createTrackedControllerSettings(const types::VehicleConfig& config)
        {
            JPH::TrackedVehicleControllerSettings* controller = new JPH::TrackedVehicleControllerSettings;
            applyEngineSettings(controller->mEngine, config);

            std::array<int, 2> lastDrivenWheel{-1, -1};
            for (uint32_t i = 0; i < static_cast<uint32_t>(config.wheels.size()); ++i)
            {
                const int trackIndex = std::clamp(resolveTrackIndex(config.wheels[i]), 0, 1);
                auto& track = controller->mTracks[trackIndex];
                track.mWheels.push_back(i);
                if (config.wheels[i].driven)
                    lastDrivenWheel[trackIndex] = static_cast<int>(i);
            }

            for (int trackIndex = 0; trackIndex < 2; ++trackIndex)
            {
                auto& track = controller->mTracks[trackIndex];
                if (track.mWheels.empty())
                    continue;

                track.mDrivenWheel = lastDrivenWheel[trackIndex] >= 0
                                          ? static_cast<uint32_t>(lastDrivenWheel[trackIndex])
                                          : track.mWheels.back();
            }

            return controller;
        }

        JPH::Ref<JPH::VehicleControllerSettings> createControllerSettings(const types::VehicleConfig& config)
        {
            switch (config.controllerType)
            {
            case types::VehicleControllerType::Tracked:
                return createTrackedControllerSettings(config);
            case types::VehicleControllerType::Motorcycle:
                return createMotorcycleControllerSettings(config);
            case types::VehicleControllerType::Wheeled:
            default:
                return createWheeledControllerSettings(config);
            }
        }

        JPH::RefConst<JPH::VehicleCollisionTester> createCollisionTester(const types::VehicleConfig& config)
        {
            const auto layer = static_cast<JPH::ObjectLayer>(std::min<uint8_t>(config.wheelCollisionLayer, MAX_COLLISION_LAYERS - 1));
            const JPH::Vec3 up = normalizedOr(toJolt(config.up), JPH::Vec3::sAxisY());

            switch (config.collisionTester)
            {
            case types::VehicleCollisionTesterType::CastSphere:
                return new JPH::VehicleCollisionTesterCastSphere(layer, 0.5f * averageWheelWidth(config), up, config.maxSlopeAngle);
            case types::VehicleCollisionTesterType::CastCylinder:
                return new JPH::VehicleCollisionTesterCastCylinder(layer);
            case types::VehicleCollisionTesterType::Ray:
            default:
                return new JPH::VehicleCollisionTesterRay(layer, up, config.maxSlopeAngle);
            }
        }

        float safeInput(float value, float minValue, float maxValue)
        {
            return std::clamp(std::isfinite(value) ? value : 0.0f, minValue, maxValue);
        }

        types::WheelState readWheelState(const JPH::VehicleConstraint& constraint, uint32_t wheelIndex)
        {
            types::WheelState state;
            const JPH::Wheel* wheel = constraint.GetWheel(wheelIndex);
            state.worldTransform = toGlm(constraint.GetWheelWorldTransform(wheelIndex, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX()));
            state.rotationAngle = wheel->GetRotationAngle();
            state.steerAngle = wheel->GetSteerAngle();
            state.suspensionLength = wheel->GetSuspensionLength();
            state.angularVelocity = wheel->GetAngularVelocity();
            state.hasContact = wheel->HasContact();
            return state;
        }

        float averageWheelWidth(const types::VehicleConfig& config)
        {
            if (config.wheels.empty())
                return 0.1f;

            float total = 0.0f;
            for (const auto& wheel : config.wheels)
                total += wheel.width;
            return total / static_cast<float>(config.wheels.size());
        }
    }

    void PhysicsVehicleManager::init(PhysicsContext* context, PhysicsBodyRegistry* registry)
    {
        ctx = context;
        bodyRegistry = registry;
    }

    void PhysicsVehicleManager::cleanUp()
    {
        if (!ctx || !ctx->physicsSystem)
        {
            vehicles.clear();
            return;
        }

        while (!vehicles.empty())
            destroyVehicle(vehicles.begin()->first);
    }

    bool PhysicsVehicleManager::createVehicle(uint64_t entityId, const types::VehicleConfig& config)
    {
        if (!ctx || !ctx->physicsSystem || !bodyRegistry)
            return false;

        auto validation = types::validateVehicleConfig(config);
        if (!validation)
        {
            for (const auto& error : validation.errors)
                vfLogWarning("Entity {}: Invalid VehicleComponent config: {}", entityId, error);
            return false;
        }

        JPH::BodyID bodyId = bodyRegistry->getBodyForEntity(entityId);
        if (bodyId.IsInvalid())
        {
            vfLogWarning("Entity {}: Cannot create vehicle without a rigid body", entityId);
            return false;
        }

        destroyVehicle(entityId);

        JPH::BodyLockWrite lock(ctx->getBodyLockInterface(), bodyId);
        if (!lock.Succeeded())
        {
            vfLogWarning("Entity {}: Failed to lock chassis body for vehicle creation", entityId);
            return false;
        }

        const JPH::Vec3 up = normalizedOr(toJolt(config.up), JPH::Vec3::sAxisY());
        const JPH::Vec3 forward = normalizedOr(toJolt(config.forward), JPH::Vec3::sAxisZ());

        JPH::VehicleConstraintSettings settings;
        settings.mUp = up;
        settings.mForward = forward;
        settings.mMaxPitchRollAngle = config.maxPitchRollAngle;
        settings.mWheels.reserve(config.wheels.size());
        for (const auto& wheel : config.wheels)
            settings.mWheels.push_back(createWheelSettings(config, wheel, up, forward));
        settings.mController = createControllerSettings(config);

        JPH::Ref<JPH::VehicleConstraint> constraint = new JPH::VehicleConstraint(lock.GetBody(), settings);
        JPH::RefConst<JPH::VehicleCollisionTester> tester = createCollisionTester(config);
        constraint->SetVehicleCollisionTester(tester);

        ctx->physicsSystem->AddConstraint(constraint);
        ctx->physicsSystem->AddStepListener(constraint);

        VehicleInstance instance;
        instance.constraint = constraint;
        instance.collisionTester = tester;
        instance.chassisBody = bodyId;
        instance.controllerType = config.controllerType;
        instance.wheelCount = static_cast<uint32_t>(config.wheels.size());
        instance.inSystem = true;
        vehicles[entityId] = std::move(instance);

        return true;
    }

    void PhysicsVehicleManager::destroyVehicle(uint64_t entityId)
    {
        auto it = vehicles.find(entityId);
        if (it == vehicles.end())
            return;

        if (ctx && ctx->physicsSystem && it->second.constraint && it->second.inSystem)
        {
            ctx->physicsSystem->RemoveStepListener(it->second.constraint);
            ctx->physicsSystem->RemoveConstraint(it->second.constraint);
        }

        vehicles.erase(it);
    }

    bool PhysicsVehicleManager::hasVehicle(uint64_t entityId) const
    {
        return vehicles.find(entityId) != vehicles.end();
    }

    void PhysicsVehicleManager::setInput(uint64_t entityId, float throttle, float steer, float brake, float handbrake)
    {
        auto it = vehicles.find(entityId);
        if (it == vehicles.end())
            return;

        it->second.throttle = safeInput(throttle, -1.0f, 1.0f);
        it->second.steer = safeInput(steer, -1.0f, 1.0f);
        it->second.brake = safeInput(brake, 0.0f, 1.0f);
        it->second.handbrake = safeInput(handbrake, 0.0f, 1.0f);
    }

    void PhysicsVehicleManager::applyPendingInputs()
    {
        if (!ctx || !ctx->physicsSystem)
            return;

        for (auto& [entityId, vehicle] : vehicles)
        {
            if (!vehicle.constraint)
                continue;

            const bool hasDriverInput = std::abs(vehicle.throttle) > 0.001f ||
                                        std::abs(vehicle.steer) > 0.001f ||
                                        vehicle.brake > 0.001f ||
                                        vehicle.handbrake > 0.001f;
            if (hasDriverInput && !vehicle.chassisBody.IsInvalid())
                ctx->getBodyInterface().ActivateBody(vehicle.chassisBody);

            JPH::VehicleController* controller = vehicle.constraint->GetController();
            switch (vehicle.controllerType)
            {
            case types::VehicleControllerType::Tracked:
            {
                auto* tracked = static_cast<JPH::TrackedVehicleController*>(controller);
                float leftRatio = 1.0f;
                float rightRatio = 1.0f;
                if (vehicle.steer < 0.0f)
                    leftRatio = std::max(0.05f, 1.0f + vehicle.steer);
                else if (vehicle.steer > 0.0f)
                    rightRatio = std::max(0.05f, 1.0f - vehicle.steer);

                tracked->SetDriverInput(vehicle.throttle, leftRatio, rightRatio, std::max(vehicle.brake, vehicle.handbrake));
                break;
            }
            case types::VehicleControllerType::Motorcycle:
            {
                auto* motorcycle = static_cast<JPH::MotorcycleController*>(controller);
                motorcycle->SetDriverInput(vehicle.throttle, vehicle.steer, vehicle.brake, vehicle.handbrake);
                break;
            }
            case types::VehicleControllerType::Wheeled:
            default:
            {
                auto* wheeled = static_cast<JPH::WheeledVehicleController*>(controller);
                wheeled->SetDriverInput(vehicle.throttle, vehicle.steer, vehicle.brake, vehicle.handbrake);
                break;
            }
            }
        }
    }

    std::vector<types::WheelState> PhysicsVehicleManager::getWheelStates(uint64_t entityId) const
    {
        std::vector<types::WheelState> result;
        auto it = vehicles.find(entityId);
        if (it == vehicles.end() || !it->second.constraint)
            return result;

        result.reserve(it->second.wheelCount);
        for (uint32_t wheelIndex = 0; wheelIndex < it->second.wheelCount; ++wheelIndex)
            result.push_back(readWheelState(*it->second.constraint, wheelIndex));
        return result;
    }

    std::optional<types::WheelState> PhysicsVehicleManager::getWheelState(uint64_t entityId, int wheelIndex) const
    {
        auto it = vehicles.find(entityId);
        if (it == vehicles.end() || !it->second.constraint || wheelIndex < 0 ||
            wheelIndex >= static_cast<int>(it->second.wheelCount))
        {
            return std::nullopt;
        }

        return readWheelState(*it->second.constraint, static_cast<uint32_t>(wheelIndex));
    }
}
