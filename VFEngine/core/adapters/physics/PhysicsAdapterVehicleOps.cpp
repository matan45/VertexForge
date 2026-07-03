#include "PhysicsAdapter.hpp"
#include "print/Log.hpp"

namespace core
{
    namespace
    {
        bool isVehicleMutationBlocked(bool asyncStepInFlight, const char* operation)
        {
            if (!asyncStepInFlight)
                return false;

            vfLogWarning("PhysicsAdapter: {} ignored while async physics step is in flight", operation);
            return true;
        }
    }

    bool PhysicsAdapter::createVehicle(services::EntityHandle entity, const types::VehicleConfig& config)
    {
        if (!physicsWorld || isVehicleMutationBlocked(asyncStepInFlight, "createVehicle"))
            return false;

        return physicsWorld->createVehicle(entity.id, config);
    }

    void PhysicsAdapter::destroyVehicle(services::EntityHandle entity)
    {
        if (!physicsWorld || isVehicleMutationBlocked(asyncStepInFlight, "destroyVehicle"))
            return;

        physicsWorld->destroyVehicle(entity.id);
    }

    bool PhysicsAdapter::hasVehicle(services::EntityHandle entity) const
    {
        return physicsWorld && physicsWorld->hasVehicle(entity.id);
    }

    void PhysicsAdapter::setVehicleInput(services::EntityHandle entity, float throttle, float steer, float brake, float handbrake)
    {
        if (!physicsWorld)
            return;

        physicsWorld->setVehicleInput(entity.id, throttle, steer, brake, handbrake);
    }

    std::vector<types::WheelState> PhysicsAdapter::getVehicleWheelStates(services::EntityHandle entity) const
    {
        if (!physicsWorld || isVehicleMutationBlocked(asyncStepInFlight, "getVehicleWheelStates"))
            return {};

        return physicsWorld->getVehicleWheelStates(entity.id);
    }

    std::optional<types::WheelState> PhysicsAdapter::getVehicleWheelState(services::EntityHandle entity, int wheelIndex) const
    {
        if (!physicsWorld || isVehicleMutationBlocked(asyncStepInFlight, "getVehicleWheelState"))
            return std::nullopt;

        return physicsWorld->getVehicleWheelState(entity.id, wheelIndex);
    }
}
