#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace types
{
    enum class VehicleControllerType : uint8_t
    {
        Wheeled = 0,
        Tracked = 1,
        Motorcycle = 2
    };

    enum class VehicleCollisionTesterType : uint8_t
    {
        Ray = 0,
        CastSphere = 1,
        CastCylinder = 2
    };

    struct WheelConfig
    {
        glm::vec3 position{0.0f};
        glm::vec3 suspensionDirection{0.0f, -1.0f, 0.0f};
        float radius = 0.35f;
        float width = 0.2f;
        float suspensionMinLength = 0.3f;
        float suspensionMaxLength = 0.5f;
        float suspensionFrequency = 1.5f;
        float suspensionDamping = 1.0f;
        float maxSteerAngle = 0.0f;
        float maxBrakeTorque = 1500.0f;
        float maxHandBrakeTorque = 0.0f;
        bool driven = false;
        int trackIndex = -1;
        float longitudinalFriction = 1.0f;
        float lateralFriction = 1.0f;
    };

    struct VehicleDifferentialConfig
    {
        int leftWheel = 0;
        int rightWheel = 1;
        float differentialRatio = 3.42f;
        float leftRightSplit = 0.5f;
        float engineTorqueRatio = 1.0f;
    };

    struct WheelState
    {
        glm::mat4 worldTransform{1.0f};
        float rotationAngle = 0.0f;
        float steerAngle = 0.0f;
        float suspensionLength = 0.0f;
        float angularVelocity = 0.0f;
        bool hasContact = false;
    };

    struct VehicleValidationResult
    {
        bool valid = true;
        std::vector<std::string> errors;

        explicit operator bool() const { return valid; }
    };

    struct VehicleConfig
    {
        VehicleControllerType controllerType = VehicleControllerType::Wheeled;
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        float maxPitchRollAngle = 1.04719755f; // 60 degrees
        std::vector<WheelConfig> wheels;

        float engineMaxTorque = 500.0f;
        float engineMinRPM = 1000.0f;
        float engineMaxRPM = 6000.0f;
        std::vector<VehicleDifferentialConfig> differentials;

        float maxLeanAngle = 0.78539816f; // 45 degrees
        float leanSpringConstant = 5000.0f;
        float leanSpringDamping = 1000.0f;

        VehicleCollisionTesterType collisionTester = VehicleCollisionTesterType::Ray;
        uint8_t wheelCollisionLayer = 1;
        float maxSlopeAngle = 1.3962634f; // 80 degrees

        static VehicleConfig createFourWheelCar()
        {
            VehicleConfig cfg;
            cfg.controllerType = VehicleControllerType::Wheeled;
            cfg.engineMaxTorque = 500.0f;
            cfg.engineMinRPM = 1000.0f;
            cfg.engineMaxRPM = 6000.0f;
            cfg.collisionTester = VehicleCollisionTesterType::Ray;

            cfg.wheels.resize(4);
            const float halfWidth = 0.9f;
            const float frontZ = 1.4f;
            const float rearZ = -1.4f;
            const float y = -0.18f;
            const float steer = 0.52359878f; // 30 degrees

            cfg.wheels[0].position = { halfWidth, y, frontZ };
            cfg.wheels[1].position = { -halfWidth, y, frontZ };
            cfg.wheels[2].position = { halfWidth, y, rearZ };
            cfg.wheels[3].position = { -halfWidth, y, rearZ };

            for (auto& wheel : cfg.wheels)
            {
                wheel.radius = 0.3f;
                wheel.width = 0.1f;
                wheel.suspensionMinLength = 0.3f;
                wheel.suspensionMaxLength = 0.5f;
                wheel.suspensionFrequency = 1.5f;
                wheel.suspensionDamping = 1.0f;
                wheel.maxBrakeTorque = 1500.0f;
            }

            cfg.wheels[0].maxSteerAngle = steer;
            cfg.wheels[1].maxSteerAngle = steer;
            cfg.wheels[2].driven = true;
            cfg.wheels[3].driven = true;
            cfg.wheels[2].maxHandBrakeTorque = 4000.0f;
            cfg.wheels[3].maxHandBrakeTorque = 4000.0f;

            cfg.differentials.push_back({2, 3, 3.42f, 0.5f, 1.0f});
            return cfg;
        }

        static VehicleConfig createTank()
        {
            VehicleConfig cfg;
            cfg.controllerType = VehicleControllerType::Tracked;
            cfg.engineMaxTorque = 2500.0f;
            cfg.engineMinRPM = 800.0f;
            cfg.engineMaxRPM = 4500.0f;
            cfg.collisionTester = VehicleCollisionTesterType::Ray;

            constexpr int wheelsPerTrack = 9;
            cfg.wheels.reserve(wheelsPerTrack * 2);
            const float halfWidth = 1.7f;
            const float wheelZ[wheelsPerTrack] = {2.95f, 2.1f, 1.4f, 0.7f, 0.0f, -0.7f, -1.4f, -2.1f, -2.75f};

            for (int track = 0; track < 2; ++track)
            {
                for (int i = 0; i < wheelsPerTrack; ++i)
                {
                    WheelConfig wheel;
                    wheel.position = {track == 0 ? halfWidth : -halfWidth, (i == 0 || i == wheelsPerTrack - 1) ? 0.0f : -0.3f, wheelZ[i]};
                    wheel.radius = 0.3f;
                    wheel.width = 0.1f;
                    wheel.suspensionMinLength = 0.3f;
                    wheel.suspensionMaxLength = (i == 0 || i == wheelsPerTrack - 1) ? 0.3f : 0.5f;
                    wheel.suspensionFrequency = 1.0f;
                    wheel.trackIndex = track;
                    wheel.driven = i == wheelsPerTrack - 1;
                    wheel.longitudinalFriction = 1.0f;
                    wheel.lateralFriction = 1.0f;
                    cfg.wheels.push_back(wheel);
                }
            }

            return cfg;
        }

        static VehicleConfig createMotorcycle()
        {
            VehicleConfig cfg;
            cfg.controllerType = VehicleControllerType::Motorcycle;
            cfg.engineMaxTorque = 150.0f;
            cfg.engineMinRPM = 1000.0f;
            cfg.engineMaxRPM = 10000.0f;
            cfg.collisionTester = VehicleCollisionTesterType::CastCylinder;

            cfg.wheels.resize(2);
            cfg.wheels[0].position = {0.0f, -0.27f, 0.75f};
            cfg.wheels[0].radius = 0.31f;
            cfg.wheels[0].width = 0.05f;
            cfg.wheels[0].maxSteerAngle = 0.52359878f;
            cfg.wheels[0].maxBrakeTorque = 500.0f;
            cfg.wheels[0].suspensionMinLength = 0.3f;
            cfg.wheels[0].suspensionMaxLength = 0.5f;
            cfg.wheels[0].suspensionFrequency = 1.5f;

            cfg.wheels[1].position = {0.0f, -0.27f, -0.75f};
            cfg.wheels[1].radius = 0.31f;
            cfg.wheels[1].width = 0.05f;
            cfg.wheels[1].maxBrakeTorque = 250.0f;
            cfg.wheels[1].driven = true;
            cfg.wheels[1].suspensionMinLength = 0.3f;
            cfg.wheels[1].suspensionMaxLength = 0.5f;
            cfg.wheels[1].suspensionFrequency = 2.0f;

            cfg.differentials.push_back({-1, 1, 4.825f, 0.5f, 1.0f});
            return cfg;
        }
    };

    inline VehicleValidationResult validateVehicleConfig(const VehicleConfig& cfg)
    {
        VehicleValidationResult result;

        auto addError = [&result](std::string message)
        {
            result.valid = false;
            result.errors.push_back(std::move(message));
        };

        if (cfg.wheels.empty())
            addError("Vehicle must contain at least one wheel");

        if (cfg.engineMaxTorque <= 0.0f)
            addError("engineMaxTorque must be positive");
        if (cfg.engineMinRPM < 0.0f || cfg.engineMaxRPM <= cfg.engineMinRPM)
            addError("engine RPM range is invalid");
        if (cfg.wheelCollisionLayer >= 16)
            addError("wheelCollisionLayer must be in range 0..15");

        for (size_t i = 0; i < cfg.wheels.size(); ++i)
        {
            const auto& wheel = cfg.wheels[i];
            if (wheel.radius <= 0.0f)
                addError("wheel radius must be positive");
            if (wheel.width <= 0.0f)
                addError("wheel width must be positive");
            if (wheel.suspensionMinLength < 0.0f || wheel.suspensionMaxLength < wheel.suspensionMinLength)
                addError("wheel suspension length range is invalid");
            if (wheel.suspensionFrequency <= 0.0f)
                addError("wheel suspensionFrequency must be positive");
            if (cfg.controllerType == VehicleControllerType::Tracked &&
                (wheel.trackIndex < -1 || wheel.trackIndex > 1))
            {
                addError("tracked wheel trackIndex must be -1, 0, or 1");
            }
        }

        const int wheelCount = static_cast<int>(cfg.wheels.size());
        for (const auto& diff : cfg.differentials)
        {
            if (diff.leftWheel >= wheelCount || diff.rightWheel >= wheelCount)
                addError("differential wheel index is out of range");
            if (diff.leftWheel < -1 || diff.rightWheel < -1)
                addError("differential wheel index must be -1 or a valid wheel");
            if (diff.differentialRatio <= 0.0f)
                addError("differentialRatio must be positive");
            if (diff.leftRightSplit < 0.0f || diff.leftRightSplit > 1.0f)
                addError("leftRightSplit must be in range 0..1");
            if (diff.engineTorqueRatio < 0.0f)
                addError("engineTorqueRatio must be non-negative");
        }

        if (cfg.controllerType == VehicleControllerType::Tracked)
        {
            int leftCount = 0;
            int rightCount = 0;
            for (const auto& wheel : cfg.wheels)
            {
                const int track = wheel.trackIndex >= 0 ? wheel.trackIndex : (wheel.position.x >= 0.0f ? 0 : 1);
                if (track == 0)
                    ++leftCount;
                else if (track == 1)
                    ++rightCount;
                else
                    addError("tracked wheel trackIndex must be 0 or 1 after auto assignment");
            }
            if (leftCount == 0 || rightCount == 0)
                addError("tracked vehicles need wheels on both tracks");
        }

        return result;
    }
}
