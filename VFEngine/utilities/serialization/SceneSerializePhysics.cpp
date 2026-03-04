#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"

// Helper to clean null terminators from strings
static void cleanNullTerminators(std::string& str)
{
    if (auto pos = str.find('\0'); pos != std::string::npos)
        str.resize(pos);
}

namespace serialization
{
    json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        return j;
    }

    void SceneSerialization::deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
        if (auto it = j.find("volume"); it != j.end() && it->is_number())
        {
            audioSource.volume = it->get<float>();
        }
        if (auto it = j.find("pitch"); it != j.end() && it->is_number())
        {
            audioSource.pitch = it->get<float>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            audioSource.loop = it->get<bool>();
        }
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeAudioSource3D(const components::AudioSource3DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["minDistance"] = audioSource.minDistance;
        j["maxDistance"] = audioSource.maxDistance;
        j["showDebugSpheres"] = audioSource.showDebugSpheres;
        return j;
    }

    void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
        if (auto it = j.find("volume"); it != j.end() && it->is_number())
        {
            audioSource.volume = it->get<float>();
        }
        if (auto it = j.find("pitch"); it != j.end() && it->is_number())
        {
            audioSource.pitch = it->get<float>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            audioSource.loop = it->get<bool>();
        }
        if (auto it = j.find("minDistance"); it != j.end() && it->is_number())
        {
            audioSource.minDistance = it->get<float>();
        }
        if (auto it = j.find("maxDistance"); it != j.end() && it->is_number())
        {
            audioSource.maxDistance = it->get<float>();
        }
        if (auto it = j.find("showDebugSpheres"); it != j.end() && it->is_boolean())
        {
            audioSource.showDebugSpheres = it->get<bool>();
        }
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeScript(const components::ScriptComponent& script)
    {
        json j;
        json scriptsArray = json::array();
        for (const auto& entry : script.scripts)
        {
            json entryJson;
            std::string cleanPath = entry.scriptPath;
            cleanNullTerminators(cleanPath);
            entryJson["scriptPath"] = cleanPath;
            entryJson["enabled"] = entry.enabled;
            scriptsArray.push_back(entryJson);
        }
        j["scripts"] = scriptsArray;
        return j;
    }

    void SceneSerialization::deserializeScript(const json& j, components::ScriptComponent& script)
    {
        script.scripts.clear();
        if (j.contains("scripts") && j["scripts"].is_array())
        {
            for (const auto& entryJson : j["scripts"])
            {
                components::ScriptEntry entry;
                if (entryJson.contains("scriptPath") && entryJson["scriptPath"].is_string())
                {
                    entry.scriptPath = entryJson["scriptPath"].get<std::string>();
                }
                if (entryJson.contains("enabled") && entryJson["enabled"].is_boolean())
                {
                    entry.enabled = entryJson["enabled"].get<bool>();
                }
                // Reset runtime state
                entry.started = false;
                entry.instanceId = 0;
                script.scripts.push_back(entry);
            }
        }
    }

    std::string SceneSerialization::rigidBodyTypeToString(components::RigidBodyType type)
    {
        switch (type)
        {
        case components::RigidBodyType::Static: return "static";
        case components::RigidBodyType::Kinematic: return "kinematic";
        default: return "dynamic";
        }
    }

    components::RigidBodyType SceneSerialization::stringToRigidBodyType(const std::string& str)
    {
        if (str == "static") return components::RigidBodyType::Static;
        if (str == "kinematic") return components::RigidBodyType::Kinematic;
        return components::RigidBodyType::Dynamic;
    }

    std::string SceneSerialization::colliderShapeToString(components::ColliderShape shape)
    {
        switch (shape)
        {
        case components::ColliderShape::Sphere: return "sphere";
        case components::ColliderShape::Capsule: return "capsule";
        case components::ColliderShape::ConvexMesh: return "convexMesh";
        case components::ColliderShape::TriangleMesh: return "triangleMesh";
        default: return "box";
        }
    }

    components::ColliderShape SceneSerialization::stringToColliderShape(const std::string& str)
    {
        if (str == "sphere") return components::ColliderShape::Sphere;
        if (str == "capsule") return components::ColliderShape::Capsule;
        if (str == "convexMesh") return components::ColliderShape::ConvexMesh;
        if (str == "triangleMesh") return components::ColliderShape::TriangleMesh;
        return components::ColliderShape::Box;
    }

    json SceneSerialization::serializeCollider(const components::ColliderComponent& collider)
    {
        json j;
        j["shape"] = colliderShapeToString(collider.shape);
        j["size"] = json::array({collider.size.x, collider.size.y, collider.size.z});
        j["height"] = collider.height;
        j["offset"] = json::array({collider.offset.x, collider.offset.y, collider.offset.z});
        std::string cleanPath = collider.meshPath;
        cleanNullTerminators(cleanPath);
        j["meshPath"] = cleanPath;
        j["isTrigger"] = collider.isTrigger;
        j["collisionLayer"] = collider.collisionLayer;
        j["friction"] = collider.friction;
        j["restitution"] = collider.restitution;
        return j;
    }

    void SceneSerialization::deserializeCollider(const json& j, components::ColliderComponent& collider)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_string())
        {
            collider.shape = stringToColliderShape(it->get<std::string>());
        }
        if (auto it = j.find("size"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.size = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("height"); it != j.end() && it->is_number())
        {
            collider.height = it->get<float>();
        }
        if (auto it = j.find("offset"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.offset = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("meshPath"); it != j.end() && it->is_string())
        {
            collider.meshPath = it->get<std::string>();
        }
        if (auto it = j.find("isTrigger"); it != j.end() && it->is_boolean())
        {
            collider.isTrigger = it->get<bool>();
        }
        if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned())
        {
            uint8_t layer = it->get<uint8_t>();
            collider.collisionLayer = layer < 16 ? layer : 1;
        }
        if (auto it = j.find("friction"); it != j.end() && it->is_number())
        {
            collider.friction = it->get<float>();
        }
        if (auto it = j.find("restitution"); it != j.end() && it->is_number())
        {
            collider.restitution = it->get<float>();
        }
    }

    json SceneSerialization::serializeRigidBody(const components::RigidBodyComponent& rigidBody)
    {
        json j;
        j["type"] = rigidBodyTypeToString(rigidBody.type);
        j["mass"] = rigidBody.mass;
        j["linearDamping"] = rigidBody.linearDamping;
        j["angularDamping"] = rigidBody.angularDamping;
        j["freezePositionX"] = rigidBody.freezePositionX;
        j["freezePositionY"] = rigidBody.freezePositionY;
        j["freezePositionZ"] = rigidBody.freezePositionZ;
        j["freezeRotationX"] = rigidBody.freezeRotationX;
        j["freezeRotationY"] = rigidBody.freezeRotationY;
        j["freezeRotationZ"] = rigidBody.freezeRotationZ;
        return j;
    }

    void SceneSerialization::deserializeRigidBody(const json& j, components::RigidBodyComponent& rigidBody)
    {
        if (auto it = j.find("type"); it != j.end() && it->is_string())
        {
            rigidBody.type = stringToRigidBodyType(it->get<std::string>());
        }
        if (auto it = j.find("mass"); it != j.end() && it->is_number())
        {
            rigidBody.mass = it->get<float>();
        }
        if (auto it = j.find("linearDamping"); it != j.end() && it->is_number())
        {
            rigidBody.linearDamping = it->get<float>();
        }
        if (auto it = j.find("angularDamping"); it != j.end() && it->is_number())
        {
            rigidBody.angularDamping = it->get<float>();
        }
        if (auto it = j.find("freezePositionX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionX = it->get<bool>();
        }
        if (auto it = j.find("freezePositionY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionY = it->get<bool>();
        }
        if (auto it = j.find("freezePositionZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionZ = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationX = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationY = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationZ = it->get<bool>();
        }
    }

    std::string SceneSerialization::physicsAnimationModeToString(types::PhysicsAnimationMode mode)
    {
        switch (mode)
        {
        case types::PhysicsAnimationMode::Kinematic: return "kinematic";
        case types::PhysicsAnimationMode::Ragdoll: return "ragdoll";
        default: return "animated";
        }
    }

    types::PhysicsAnimationMode SceneSerialization::stringToPhysicsAnimationMode(const std::string& str)
    {
        if (str == "kinematic") return types::PhysicsAnimationMode::Kinematic;
        if (str == "ragdoll") return types::PhysicsAnimationMode::Ragdoll;
        return types::PhysicsAnimationMode::Animated;
    }

    json SceneSerialization::serializePhysicsAnimation(const components::PhysicsAnimationComponent& physAnim)
    {
        json j;
        const auto& config = physAnim.config;

        if (!physAnim.physicsAnimationPath.empty())
            j["physicsAnimationPath"] = physAnim.physicsAnimationPath;

        j["defaultMode"] = physicsAnimationModeToString(config.defaultMode);
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

        return j;
    }

    void SceneSerialization::deserializePhysicsAnimation(const json& j, components::PhysicsAnimationComponent& physAnim)
    {
        auto& config = physAnim.config;

        if (auto it = j.find("physicsAnimationPath"); it != j.end() && it->is_string())
        {
            physAnim.physicsAnimationPath = it->get<std::string>();
        }

        if (auto it = j.find("defaultMode"); it != j.end() && it->is_string())
        {
            config.defaultMode = stringToPhysicsAnimationMode(it->get<std::string>());
        }
        if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned())
        {
            uint8_t layer = it->get<uint8_t>();
            config.collisionLayer = layer < 16 ? layer : 1;
        }
        if (auto it = j.find("kinematicToRagdollBlendTime"); it != j.end() && it->is_number())
        {
            config.kinematicToRagdollBlendTime = it->get<float>();
        }

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

        // Reset runtime state
        physAnim.currentMode = config.defaultMode;
        physAnim.isInitialized = false;
        physAnim.transitionProgress = 0.0f;
        physAnim.ragdollCollisionGroup = 0;
    }

    json SceneSerialization::serializeVFX(const components::VFXComponent& vfx)
    {
        json j;
        std::string cleanPath = vfx.vfxPath;
        cleanNullTerminators(cleanPath);
        j["vfxPath"] = cleanPath;
        j["autoPlay"] = vfx.autoPlay;
        j["loop"] = vfx.loop;
        return j;
    }

    void SceneSerialization::deserializeVFX(const json& j, components::VFXComponent& vfx)
    {
        if (auto it = j.find("vfxPath"); it != j.end() && it->is_string())
        {
            vfx.vfxPath = it->get<std::string>();
        }
        if (auto it = j.find("autoPlay"); it != j.end() && it->is_boolean())
        {
            vfx.autoPlay = it->get<bool>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            vfx.loop = it->get<bool>();
        }
        // Reset runtime state
        vfx.runtimeInstanceId = 0;
        vfx.isPlaying = false;
    }

    json SceneSerialization::serializeNavmeshAgent(const components::NavmeshAgentComponent& agent)
    {
        json j;
        j["radius"] = agent.radius;
        j["height"] = agent.height;
        j["maxSpeed"] = agent.maxSpeed;
        j["maxAcceleration"] = agent.maxAcceleration;
        j["stoppingDistance"] = agent.stoppingDistance;
        j["avoidanceQuality"] = agent.avoidanceQuality;
        j["separationWeight"] = agent.separationWeight;
        return j;
    }

    void SceneSerialization::deserializeNavmeshAgent(const json& j, components::NavmeshAgentComponent& agent)
    {
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
        {
            agent.radius = it->get<float>();
        }
        if (auto it = j.find("height"); it != j.end() && it->is_number())
        {
            agent.height = it->get<float>();
        }
        if (auto it = j.find("maxSpeed"); it != j.end() && it->is_number())
        {
            agent.maxSpeed = it->get<float>();
        }
        if (auto it = j.find("maxAcceleration"); it != j.end() && it->is_number())
        {
            agent.maxAcceleration = it->get<float>();
        }
        if (auto it = j.find("stoppingDistance"); it != j.end() && it->is_number())
        {
            agent.stoppingDistance = it->get<float>();
        }
        if (auto it = j.find("avoidanceQuality"); it != j.end() && it->is_number_unsigned())
        {
            agent.avoidanceQuality = std::min(it->get<uint8_t>(), static_cast<uint8_t>(3));
        }
        if (auto it = j.find("separationWeight"); it != j.end() && it->is_number())
        {
            agent.separationWeight = it->get<float>();
        }
        // Reset runtime state
        agent.isActive = false;
        agent.crowdAgentIndex = -1;
    }

    // ============================================
    // Controller Component
    // ============================================

    static std::string paramSourceToString(components::LocomotionParamSource source)
    {
        switch (source)
        {
        case components::LocomotionParamSource::Speed: return "Speed";
        case components::LocomotionParamSource::Grounded: return "Grounded";
        case components::LocomotionParamSource::VerticalVelocity: return "VerticalVelocity";
        case components::LocomotionParamSource::DirectionX: return "DirectionX";
        case components::LocomotionParamSource::DirectionY: return "DirectionY";
        default: return "Speed";
        }
    }

    static components::LocomotionParamSource stringToParamSource(const std::string& str)
    {
        if (str == "Grounded") return components::LocomotionParamSource::Grounded;
        if (str == "VerticalVelocity") return components::LocomotionParamSource::VerticalVelocity;
        if (str == "DirectionX") return components::LocomotionParamSource::DirectionX;
        if (str == "DirectionY") return components::LocomotionParamSource::DirectionY;
        return components::LocomotionParamSource::Speed;
    }

    json SceneSerialization::serializeController(const components::ControllerComponent& controller)
    {
        json j;
        j["moveSpeed"] = controller.moveSpeed;
        j["sprintMultiplier"] = controller.sprintMultiplier;
        j["jumpForce"] = controller.jumpForce;
        j["arrivalDistance"] = controller.arrivalDistance;
        j["acceleration"] = controller.acceleration;
        j["deceleration"] = controller.deceleration;
        j["rotationSpeed"] = controller.rotationSpeed;
        j["airControlFactor"] = controller.airControlFactor;
        j["walkSpeedThreshold"] = controller.walkSpeedThreshold;
        j["stepHeight"] = controller.stepHeight;
        j["maxSlopeAngle"] = controller.maxSlopeAngle;

        const auto& config = controller.locomotionConfig;
        json lc;
        lc["syncToAnimator"] = config.syncToAnimator;
        lc["idleState"] = config.idleState;
        lc["walkState"] = config.walkState;
        lc["runState"] = config.runState;
        lc["jumpState"] = config.jumpState;
        lc["fallState"] = config.fallState;

        json paramsArr = json::array();
        for (const auto& mapping : config.paramMappings)
        {
            json p;
            p["source"] = paramSourceToString(mapping.source);
            p["paramName"] = mapping.paramName;
            paramsArr.push_back(p);
        }
        lc["paramMappings"] = paramsArr;

        j["locomotionConfig"] = lc;

        return j;
    }

    void SceneSerialization::deserializeController(const json& j, components::ControllerComponent& controller)
    {
        if (auto it = j.find("moveSpeed"); it != j.end() && it->is_number())
        {
            controller.moveSpeed = it->get<float>();
        }
        if (auto it = j.find("sprintMultiplier"); it != j.end() && it->is_number())
        {
            controller.sprintMultiplier = it->get<float>();
        }
        if (auto it = j.find("jumpForce"); it != j.end() && it->is_number())
        {
            controller.jumpForce = it->get<float>();
        }
        if (auto it = j.find("arrivalDistance"); it != j.end() && it->is_number())
        {
            controller.arrivalDistance = it->get<float>();
        }
        if (auto it = j.find("acceleration"); it != j.end() && it->is_number())
        {
            controller.acceleration = it->get<float>();
        }
        if (auto it = j.find("deceleration"); it != j.end() && it->is_number())
        {
            controller.deceleration = it->get<float>();
        }
        if (auto it = j.find("rotationSpeed"); it != j.end() && it->is_number())
        {
            controller.rotationSpeed = it->get<float>();
        }
        if (auto it = j.find("airControlFactor"); it != j.end() && it->is_number())
        {
            controller.airControlFactor = it->get<float>();
        }
        if (auto it = j.find("walkSpeedThreshold"); it != j.end() && it->is_number())
        {
            controller.walkSpeedThreshold = it->get<float>();
        }
        if (auto it = j.find("stepHeight"); it != j.end() && it->is_number())
        {
            controller.stepHeight = it->get<float>();
        }
        if (auto it = j.find("maxSlopeAngle"); it != j.end() && it->is_number())
        {
            controller.maxSlopeAngle = it->get<float>();
        }

        if (j.contains("locomotionConfig") && j["locomotionConfig"].is_object())
        {
            const auto& lc = j["locomotionConfig"];
            auto& config = controller.locomotionConfig;
            if (auto it = lc.find("syncToAnimator"); it != lc.end() && it->is_boolean())
                config.syncToAnimator = it->get<bool>();
            if (auto it = lc.find("idleState"); it != lc.end() && it->is_string())
                config.idleState = it->get<std::string>();
            if (auto it = lc.find("walkState"); it != lc.end() && it->is_string())
                config.walkState = it->get<std::string>();
            if (auto it = lc.find("runState"); it != lc.end() && it->is_string())
                config.runState = it->get<std::string>();
            if (auto it = lc.find("jumpState"); it != lc.end() && it->is_string())
                config.jumpState = it->get<std::string>();
            if (auto it = lc.find("fallState"); it != lc.end() && it->is_string())
                config.fallState = it->get<std::string>();

            if (lc.contains("paramMappings") && lc["paramMappings"].is_array())
            {
                config.paramMappings.clear();
                for (const auto& pJson : lc["paramMappings"])
                {
                    components::LocomotionParamMapping mapping;
                    if (pJson.contains("source") && pJson["source"].is_string())
                        mapping.source = stringToParamSource(pJson["source"].get<std::string>());
                    if (pJson.contains("paramName") && pJson["paramName"].is_string())
                        mapping.paramName = pJson["paramName"].get<std::string>();
                    config.paramMappings.push_back(mapping);
                }
            }
        }

        // Reset runtime state
        controller.moveInput = glm::vec3(0.0f);
        controller.wantsJump = false;
        controller.wantsSprint = false;
        controller.isGrounded = false;
        controller.currentVelocity = glm::vec3(0.0f);
        controller.currentSpeed = 0.0f;
        controller.verticalVelocity = 0.0f;
        controller.locomotionState = "Idle";
        controller.characterControllerActive = false;
        controller.hasMoveToTarget = false;
        controller.moveToDestination = glm::vec3(0.0f);
    }

    json SceneSerialization::serializeNavmesh(const components::NavmeshComponent& navmesh)
    {
        json j;
        j["navmeshPath"] = navmesh.navmeshPath;
        return j;
    }

    void SceneSerialization::deserializeNavmesh(const json& j, components::NavmeshComponent& navmesh)
    {
        if (auto it = j.find("navmeshPath"); it != j.end() && it->is_string())
        {
            navmesh.navmeshPath = it->get<std::string>();
        }
    }
}
