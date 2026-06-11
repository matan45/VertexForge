#include "PhysicsAnimationAsset.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace physics
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    std::string PhysicsAnimationAsset::colliderShapeToString(types::ColliderShape shape)
    {
        switch (shape)
        {
        case types::ColliderShape::Sphere:  return "sphere";
        case types::ColliderShape::Capsule: return "capsule";
        default:                            return "box";
        }
    }

    types::ColliderShape PhysicsAnimationAsset::stringToColliderShape(const std::string& str)
    {
        if (str == "sphere")  return types::ColliderShape::Sphere;
        if (str == "capsule") return types::ColliderShape::Capsule;
        return types::ColliderShape::Box;
    }

    std::string PhysicsAnimationAsset::modeToString(types::PhysicsAnimationMode mode)
    {
        switch (mode)
        {
        case types::PhysicsAnimationMode::Kinematic:      return "kinematic";
        case types::PhysicsAnimationMode::Ragdoll:        return "ragdoll";
        case types::PhysicsAnimationMode::PoweredRagdoll: return "poweredRagdoll";
        default:                                          return "animated";
        }
    }

    types::PhysicsAnimationMode PhysicsAnimationAsset::stringToMode(const std::string& str)
    {
        if (str == "kinematic")      return types::PhysicsAnimationMode::Kinematic;
        if (str == "ragdoll")        return types::PhysicsAnimationMode::Ragdoll;
        if (str == "poweredRagdoll") return types::PhysicsAnimationMode::PoweredRagdoll;
        return types::PhysicsAnimationMode::Animated;
    }

    json PhysicsAnimationAsset::serializeConfig(const types::PhysicsAnimationConfig& config)
    {
        json j;
        j["defaultMode"] = modeToString(config.defaultMode);
        j["collisionLayer"] = config.collisionLayer;
        j["kinematicToRagdollBlendTime"] = config.kinematicToRagdollBlendTime;

        json mappingsArray = json::array();
        for (const auto& mapping : config.boneBodyMappings)
        {
            json m;
            m["boneName"] = mapping.boneName;
            m["shape"] = colliderShapeToString(mapping.shape);
            m["size"] = json::array({mapping.size.x, mapping.size.y, mapping.size.z});
            m["offset"] = json::array({mapping.offset.x, mapping.offset.y, mapping.offset.z});
            m["rotationOffset"] = json::array({mapping.rotationOffset.w, mapping.rotationOffset.x,
                                                mapping.rotationOffset.y, mapping.rotationOffset.z});
            m["mass"] = mapping.mass;
            m["friction"] = mapping.friction;
            m["restitution"] = mapping.restitution;
            if (mapping.collisionLayer != 255)
                m["collisionLayer"] = mapping.collisionLayer;
            mappingsArray.push_back(m);
        }
        j["boneBodyMappings"] = mappingsArray;

        json limitsArray = json::array();
        for (const auto& limit : config.jointLimits)
        {
            json l;
            l["boneName"] = limit.boneName;
            l["swingNormalHalfAngle"] = limit.swingNormalHalfAngle;
            l["swingPlaneHalfAngle"] = limit.swingPlaneHalfAngle;
            l["twistMinAngle"] = limit.twistMinAngle;
            l["twistMaxAngle"] = limit.twistMaxAngle;
            l["maxFrictionTorque"] = limit.maxFrictionTorque;
            limitsArray.push_back(l);
        }
        j["jointLimits"] = limitsArray;

        json boneMotorsArray = json::array();
        for (const auto& motor : config.boneMotors)
        {
            json m;
            m["boneName"] = motor.boneName;
            m["strength"] = motor.strength;
            m["frequency"] = motor.frequency;
            m["damping"] = motor.damping;
            m["maxTorque"] = motor.maxTorque;
            boneMotorsArray.push_back(m);
        }
        j["boneMotors"] = boneMotorsArray;

        j["defaultMotorStrength"] = config.defaultMotorStrength;
        j["defaultMotorFrequency"] = config.defaultMotorFrequency;
        j["defaultMotorDamping"] = config.defaultMotorDamping;
        j["defaultMotorMaxTorque"] = config.defaultMotorMaxTorque;
        j["rootMotorStrength"] = config.rootMotorStrength;
        j["poweredBlendInTime"] = config.poweredBlendInTime;
        j["ragdollToAnimatedBlendTime"] = config.ragdollToAnimatedBlendTime;

        json hr;
        hr["defaultRecoverTime"] = config.hitReaction.defaultRecoverTime;
        hr["strengthDip"] = config.hitReaction.strengthDip;
        hr["chainDepth"] = config.hitReaction.chainDepth;
        hr["chainFalloff"] = config.hitReaction.chainFalloff;
        j["hitReaction"] = hr;

        j["settleLinearVelocityThreshold"] = config.settleLinearVelocityThreshold;
        j["settleAngularVelocityThreshold"] = config.settleAngularVelocityThreshold;
        j["settleFrameCount"] = config.settleFrameCount;

        return j;
    }

    types::PhysicsAnimationConfig PhysicsAnimationAsset::deserializeConfig(const json& j)
    {
        types::PhysicsAnimationConfig config;

        if (auto it = j.find("defaultMode"); it != j.end() && it->is_string())
            config.defaultMode = stringToMode(it->get<std::string>());

        if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned())
        {
            uint8_t layer = it->get<uint8_t>();
            config.collisionLayer = layer < 16 ? layer : 1;
        }

        if (auto it = j.find("kinematicToRagdollBlendTime"); it != j.end() && it->is_number())
            config.kinematicToRagdollBlendTime = it->get<float>();

        config.boneBodyMappings.clear();
        if (j.contains("boneBodyMappings") && j["boneBodyMappings"].is_array())
        {
            for (const auto& mJson : j["boneBodyMappings"])
            {
                types::BoneBodyMapping mapping;
                if (mJson.contains("boneName") && mJson["boneName"].is_string())
                    mapping.boneName = mJson["boneName"].get<std::string>();
                if (mJson.contains("shape") && mJson["shape"].is_string())
                    mapping.shape = stringToColliderShape(mJson["shape"].get<std::string>());
                if (mJson.contains("size") && mJson["size"].is_array() && mJson["size"].size() >= 3)
                    mapping.size = glm::vec3(mJson["size"][0].get<float>(), mJson["size"][1].get<float>(), mJson["size"][2].get<float>());
                if (mJson.contains("offset") && mJson["offset"].is_array() && mJson["offset"].size() >= 3)
                    mapping.offset = glm::vec3(mJson["offset"][0].get<float>(), mJson["offset"][1].get<float>(), mJson["offset"][2].get<float>());
                if (mJson.contains("rotationOffset") && mJson["rotationOffset"].is_array() && mJson["rotationOffset"].size() >= 4)
                    mapping.rotationOffset = glm::quat(mJson["rotationOffset"][0].get<float>(), mJson["rotationOffset"][1].get<float>(),
                                                        mJson["rotationOffset"][2].get<float>(), mJson["rotationOffset"][3].get<float>());
                if (mJson.contains("mass") && mJson["mass"].is_number())
                    mapping.mass = mJson["mass"].get<float>();
                if (mJson.contains("friction") && mJson["friction"].is_number())
                    mapping.friction = mJson["friction"].get<float>();
                if (mJson.contains("restitution") && mJson["restitution"].is_number())
                    mapping.restitution = mJson["restitution"].get<float>();
                if (mJson.contains("collisionLayer") && mJson["collisionLayer"].is_number_unsigned())
                {
                    uint8_t layer = mJson["collisionLayer"].get<uint8_t>();
                    mapping.collisionLayer = layer < 16 ? layer : 255;
                }
                config.boneBodyMappings.push_back(mapping);
            }
        }

        config.jointLimits.clear();
        if (j.contains("jointLimits") && j["jointLimits"].is_array())
        {
            for (const auto& lJson : j["jointLimits"])
            {
                types::JointConstraintLimits limits;
                if (lJson.contains("boneName") && lJson["boneName"].is_string())
                    limits.boneName = lJson["boneName"].get<std::string>();
                if (lJson.contains("swingNormalHalfAngle") && lJson["swingNormalHalfAngle"].is_number())
                    limits.swingNormalHalfAngle = lJson["swingNormalHalfAngle"].get<float>();
                if (lJson.contains("swingPlaneHalfAngle") && lJson["swingPlaneHalfAngle"].is_number())
                    limits.swingPlaneHalfAngle = lJson["swingPlaneHalfAngle"].get<float>();
                if (lJson.contains("twistMinAngle") && lJson["twistMinAngle"].is_number())
                    limits.twistMinAngle = lJson["twistMinAngle"].get<float>();
                if (lJson.contains("twistMaxAngle") && lJson["twistMaxAngle"].is_number())
                    limits.twistMaxAngle = lJson["twistMaxAngle"].get<float>();
                if (lJson.contains("maxFrictionTorque") && lJson["maxFrictionTorque"].is_number())
                    limits.maxFrictionTorque = lJson["maxFrictionTorque"].get<float>();
                config.jointLimits.push_back(limits);
            }
        }

        config.boneMotors.clear();
        if (j.contains("boneMotors") && j["boneMotors"].is_array())
        {
            for (const auto& mJson : j["boneMotors"])
            {
                types::BoneMotorSettings motor;
                if (mJson.contains("boneName") && mJson["boneName"].is_string())
                    motor.boneName = mJson["boneName"].get<std::string>();
                if (mJson.contains("strength") && mJson["strength"].is_number())
                    motor.strength = mJson["strength"].get<float>();
                if (mJson.contains("frequency") && mJson["frequency"].is_number())
                    motor.frequency = mJson["frequency"].get<float>();
                if (mJson.contains("damping") && mJson["damping"].is_number())
                    motor.damping = mJson["damping"].get<float>();
                if (mJson.contains("maxTorque") && mJson["maxTorque"].is_number())
                    motor.maxTorque = mJson["maxTorque"].get<float>();
                config.boneMotors.push_back(motor);
            }
        }

        if (auto it = j.find("defaultMotorStrength"); it != j.end() && it->is_number())
            config.defaultMotorStrength = it->get<float>();
        if (auto it = j.find("defaultMotorFrequency"); it != j.end() && it->is_number())
            config.defaultMotorFrequency = it->get<float>();
        if (auto it = j.find("defaultMotorDamping"); it != j.end() && it->is_number())
            config.defaultMotorDamping = it->get<float>();
        if (auto it = j.find("defaultMotorMaxTorque"); it != j.end() && it->is_number())
            config.defaultMotorMaxTorque = it->get<float>();
        if (auto it = j.find("rootMotorStrength"); it != j.end() && it->is_number())
            config.rootMotorStrength = it->get<float>();
        if (auto it = j.find("poweredBlendInTime"); it != j.end() && it->is_number())
            config.poweredBlendInTime = it->get<float>();
        if (auto it = j.find("ragdollToAnimatedBlendTime"); it != j.end() && it->is_number())
            config.ragdollToAnimatedBlendTime = it->get<float>();

        if (j.contains("hitReaction") && j["hitReaction"].is_object())
        {
            const auto& hr = j["hitReaction"];
            if (auto it = hr.find("defaultRecoverTime"); it != hr.end() && it->is_number())
                config.hitReaction.defaultRecoverTime = it->get<float>();
            if (auto it = hr.find("strengthDip"); it != hr.end() && it->is_number())
                config.hitReaction.strengthDip = it->get<float>();
            if (auto it = hr.find("chainDepth"); it != hr.end() && it->is_number_integer())
                config.hitReaction.chainDepth = it->get<int>();
            if (auto it = hr.find("chainFalloff"); it != hr.end() && it->is_number())
                config.hitReaction.chainFalloff = it->get<float>();
        }

        if (auto it = j.find("settleLinearVelocityThreshold"); it != j.end() && it->is_number())
            config.settleLinearVelocityThreshold = it->get<float>();
        if (auto it = j.find("settleAngularVelocityThreshold"); it != j.end() && it->is_number())
            config.settleAngularVelocityThreshold = it->get<float>();
        if (auto it = j.find("settleFrameCount"); it != j.end() && it->is_number_integer())
            config.settleFrameCount = it->get<int>();

        return config;
    }

    std::optional<types::PhysicsAnimationConfig> PhysicsAnimationAsset::load(std::string_view path)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("Physics animation file not found: {}", path);
            return std::nullopt;
        }

        json j;
        try { j = resource::readJsonFile(filePath.string()); }
        catch (const json::parse_error& e)
        {
            vfLogError("Physics animation file '{}' contains invalid JSON: {}", path, e.what());
            return std::nullopt;
        }
        if (j.is_null())
        {
            vfLogError("Failed to open physics animation file: {}", path);
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Physics animation file '{}' must contain a JSON object", path);
            return std::nullopt;
        }

        try
        {
            if (!j.contains("config") || !j["config"].is_object())
            {
                vfLogError("Physics animation file '{}' missing 'config' object", path);
                return std::nullopt;
            }

            auto config = deserializeConfig(j["config"]);

            vfLogInfo("Loaded physics animation config from '{}': {} bone mappings, {} joint limits",
                      path, config.boneBodyMappings.size(), config.jointLimits.size());

            return config;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse physics animation file '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool PhysicsAnimationAsset::save(std::string_view path, const types::PhysicsAnimationConfig& config)
    {
        json j;
        j["format"] = "VertexForge.PhysicsAnimationConfig";
        j["version"] = PHYSICS_ANIM_FORMAT_VERSION;
        j["config"] = serializeConfig(config);

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create physics animation file: {}", path);
                return false;
            }

            file << j.dump(4);

            vfLogInfo("Saved physics animation config to '{}': {} bone mappings, {} joint limits",
                      path, config.boneBodyMappings.size(), config.jointLimits.size());
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save physics animation file '{}': {}", path, e.what());
            return false;
        }
    }
}
