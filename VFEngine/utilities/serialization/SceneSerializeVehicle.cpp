#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

#include <algorithm>

namespace serialization
{
    namespace
    {
        json writeVec3Value(const glm::vec3& value)
        {
            return json::array({value.x, value.y, value.z});
        }

        void readVec3Value(const json& j, const char* key, glm::vec3& out)
        {
            if (auto it = j.find(key); it != j.end() && it->is_array() && it->size() >= 3)
            {
                out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
            }
        }

        json serializeWheel(const types::WheelConfig& wheel)
        {
            json j;
            j["position"] = writeVec3Value(wheel.position);
            j["suspensionDirection"] = writeVec3Value(wheel.suspensionDirection);
            j["radius"] = wheel.radius;
            j["width"] = wheel.width;
            j["suspensionMinLength"] = wheel.suspensionMinLength;
            j["suspensionMaxLength"] = wheel.suspensionMaxLength;
            j["suspensionFrequency"] = wheel.suspensionFrequency;
            j["suspensionDamping"] = wheel.suspensionDamping;
            j["maxSteerAngle"] = wheel.maxSteerAngle;
            j["maxBrakeTorque"] = wheel.maxBrakeTorque;
            j["maxHandBrakeTorque"] = wheel.maxHandBrakeTorque;
            j["driven"] = wheel.driven;
            j["trackIndex"] = wheel.trackIndex;
            j["longitudinalFriction"] = wheel.longitudinalFriction;
            j["lateralFriction"] = wheel.lateralFriction;
            return j;
        }

        void deserializeWheel(const json& j, types::WheelConfig& wheel)
        {
            readVec3Value(j, "position", wheel.position);
            readVec3Value(j, "suspensionDirection", wheel.suspensionDirection);
            if (auto it = j.find("radius"); it != j.end() && it->is_number()) wheel.radius = it->get<float>();
            if (auto it = j.find("width"); it != j.end() && it->is_number()) wheel.width = it->get<float>();
            if (auto it = j.find("suspensionMinLength"); it != j.end() && it->is_number()) wheel.suspensionMinLength = it->get<float>();
            if (auto it = j.find("suspensionMaxLength"); it != j.end() && it->is_number()) wheel.suspensionMaxLength = it->get<float>();
            if (auto it = j.find("suspensionFrequency"); it != j.end() && it->is_number()) wheel.suspensionFrequency = it->get<float>();
            if (auto it = j.find("suspensionDamping"); it != j.end() && it->is_number()) wheel.suspensionDamping = it->get<float>();
            if (auto it = j.find("maxSteerAngle"); it != j.end() && it->is_number()) wheel.maxSteerAngle = it->get<float>();
            if (auto it = j.find("maxBrakeTorque"); it != j.end() && it->is_number()) wheel.maxBrakeTorque = it->get<float>();
            if (auto it = j.find("maxHandBrakeTorque"); it != j.end() && it->is_number()) wheel.maxHandBrakeTorque = it->get<float>();
            if (auto it = j.find("driven"); it != j.end() && it->is_boolean()) wheel.driven = it->get<bool>();
            if (auto it = j.find("trackIndex"); it != j.end() && it->is_number_integer()) wheel.trackIndex = it->get<int>();
            if (auto it = j.find("longitudinalFriction"); it != j.end() && it->is_number()) wheel.longitudinalFriction = it->get<float>();
            if (auto it = j.find("lateralFriction"); it != j.end() && it->is_number()) wheel.lateralFriction = it->get<float>();
        }

        json serializeDifferential(const types::VehicleDifferentialConfig& diff)
        {
            json j;
            j["leftWheel"] = diff.leftWheel;
            j["rightWheel"] = diff.rightWheel;
            j["differentialRatio"] = diff.differentialRatio;
            j["leftRightSplit"] = diff.leftRightSplit;
            j["engineTorqueRatio"] = diff.engineTorqueRatio;
            return j;
        }

        void deserializeDifferential(const json& j, types::VehicleDifferentialConfig& diff)
        {
            if (auto it = j.find("leftWheel"); it != j.end() && it->is_number_integer()) diff.leftWheel = it->get<int>();
            if (auto it = j.find("rightWheel"); it != j.end() && it->is_number_integer()) diff.rightWheel = it->get<int>();
            if (auto it = j.find("differentialRatio"); it != j.end() && it->is_number()) diff.differentialRatio = it->get<float>();
            if (auto it = j.find("leftRightSplit"); it != j.end() && it->is_number()) diff.leftRightSplit = it->get<float>();
            if (auto it = j.find("engineTorqueRatio"); it != j.end() && it->is_number()) diff.engineTorqueRatio = it->get<float>();
        }
    }

    std::string SceneSerialization::vehicleControllerTypeToString(types::VehicleControllerType type)
    {
        switch (type)
        {
        case types::VehicleControllerType::Tracked: return "tracked";
        case types::VehicleControllerType::Motorcycle: return "motorcycle";
        default: return "wheeled";
        }
    }

    types::VehicleControllerType SceneSerialization::stringToVehicleControllerType(const std::string& str)
    {
        if (str == "tracked") return types::VehicleControllerType::Tracked;
        if (str == "motorcycle") return types::VehicleControllerType::Motorcycle;
        return types::VehicleControllerType::Wheeled;
    }

    std::string SceneSerialization::vehicleCollisionTesterTypeToString(types::VehicleCollisionTesterType type)
    {
        switch (type)
        {
        case types::VehicleCollisionTesterType::CastSphere: return "castSphere";
        case types::VehicleCollisionTesterType::CastCylinder: return "castCylinder";
        default: return "ray";
        }
    }

    types::VehicleCollisionTesterType SceneSerialization::stringToVehicleCollisionTesterType(const std::string& str)
    {
        if (str == "castSphere") return types::VehicleCollisionTesterType::CastSphere;
        if (str == "castCylinder") return types::VehicleCollisionTesterType::CastCylinder;
        return types::VehicleCollisionTesterType::Ray;
    }

    json SceneSerialization::serializeVehicle(const components::VehicleComponent& vehicle)
    {
        const auto& cfg = vehicle.config;
        json j;
        j["controllerType"] = vehicleControllerTypeToString(cfg.controllerType);
        j["up"] = writeVec3Value(cfg.up);
        j["forward"] = writeVec3Value(cfg.forward);
        j["maxPitchRollAngle"] = cfg.maxPitchRollAngle;
        j["engineMaxTorque"] = cfg.engineMaxTorque;
        j["engineMinRPM"] = cfg.engineMinRPM;
        j["engineMaxRPM"] = cfg.engineMaxRPM;
        j["maxLeanAngle"] = cfg.maxLeanAngle;
        j["leanSpringConstant"] = cfg.leanSpringConstant;
        j["leanSpringDamping"] = cfg.leanSpringDamping;
        j["collisionTester"] = vehicleCollisionTesterTypeToString(cfg.collisionTester);
        j["wheelCollisionLayer"] = cfg.wheelCollisionLayer;
        j["maxSlopeAngle"] = cfg.maxSlopeAngle;

        json wheels = json::array();
        for (const auto& wheel : cfg.wheels)
            wheels.push_back(serializeWheel(wheel));
        j["wheels"] = std::move(wheels);

        json differentials = json::array();
        for (const auto& diff : cfg.differentials)
            differentials.push_back(serializeDifferential(diff));
        j["differentials"] = std::move(differentials);

        return j;
    }

    void SceneSerialization::deserializeVehicle(const json& j, components::VehicleComponent& vehicle)
    {
        auto& cfg = vehicle.config;
        if (auto it = j.find("controllerType"); it != j.end() && it->is_string())
            cfg.controllerType = stringToVehicleControllerType(it->get<std::string>());

        readVec3Value(j, "up", cfg.up);
        readVec3Value(j, "forward", cfg.forward);

        if (auto it = j.find("maxPitchRollAngle"); it != j.end() && it->is_number()) cfg.maxPitchRollAngle = it->get<float>();
        if (auto it = j.find("engineMaxTorque"); it != j.end() && it->is_number()) cfg.engineMaxTorque = it->get<float>();
        if (auto it = j.find("engineMinRPM"); it != j.end() && it->is_number()) cfg.engineMinRPM = it->get<float>();
        if (auto it = j.find("engineMaxRPM"); it != j.end() && it->is_number()) cfg.engineMaxRPM = it->get<float>();
        if (auto it = j.find("maxLeanAngle"); it != j.end() && it->is_number()) cfg.maxLeanAngle = it->get<float>();
        if (auto it = j.find("leanSpringConstant"); it != j.end() && it->is_number()) cfg.leanSpringConstant = it->get<float>();
        if (auto it = j.find("leanSpringDamping"); it != j.end() && it->is_number()) cfg.leanSpringDamping = it->get<float>();
        if (auto it = j.find("collisionTester"); it != j.end() && it->is_string())
            cfg.collisionTester = stringToVehicleCollisionTesterType(it->get<std::string>());
        if (auto it = j.find("wheelCollisionLayer"); it != j.end() && it->is_number_unsigned())
            cfg.wheelCollisionLayer = static_cast<uint8_t>(std::min<unsigned int>(it->get<unsigned int>(), 15u));
        if (auto it = j.find("maxSlopeAngle"); it != j.end() && it->is_number()) cfg.maxSlopeAngle = it->get<float>();

        if (auto it = j.find("wheels"); it != j.end() && it->is_array())
        {
            cfg.wheels.clear();
            cfg.wheels.reserve(it->size());
            for (const auto& wheelJson : *it)
            {
                if (!wheelJson.is_object()) continue;
                types::WheelConfig wheel;
                deserializeWheel(wheelJson, wheel);
                cfg.wheels.push_back(wheel);
            }
        }

        if (auto it = j.find("differentials"); it != j.end() && it->is_array())
        {
            cfg.differentials.clear();
            cfg.differentials.reserve(it->size());
            for (const auto& diffJson : *it)
            {
                if (!diffJson.is_object()) continue;
                types::VehicleDifferentialConfig diff;
                deserializeDifferential(diffJson, diff);
                cfg.differentials.push_back(diff);
            }
        }
    }
}
