#pragma once

#include "types/VehicleTypes.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <optional>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace core::physics
{
    struct PhysicsContext;
    class PhysicsBodyRegistry;

    class PhysicsVehicleManager
    {
    public:
        void init(PhysicsContext* context, PhysicsBodyRegistry* registry);
        void cleanUp();

        bool createVehicle(uint64_t entityId, const types::VehicleConfig& config);
        void destroyVehicle(uint64_t entityId);
        bool hasVehicle(uint64_t entityId) const;

        void setInput(uint64_t entityId, float throttle, float steer, float brake, float handbrake);
        void applyPendingInputs();

        std::vector<types::WheelState> getWheelStates(uint64_t entityId) const;
        std::optional<types::WheelState> getWheelState(uint64_t entityId, int wheelIndex) const;

    private:
        struct VehicleInstance
        {
            JPH::Ref<JPH::VehicleConstraint> constraint;
            JPH::RefConst<JPH::VehicleCollisionTester> collisionTester;
            JPH::BodyID chassisBody;
            types::VehicleControllerType controllerType = types::VehicleControllerType::Wheeled;
            uint32_t wheelCount = 0;
            float throttle = 0.0f;
            float steer = 0.0f;
            float brake = 0.0f;
            float handbrake = 0.0f;
            bool inSystem = false;
        };

        PhysicsContext* ctx = nullptr;
        PhysicsBodyRegistry* bodyRegistry = nullptr;
        std::unordered_map<uint64_t, VehicleInstance> vehicles;
    };
}
