#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <map>
#include <optional>
#include <cstdint>
#include "../uuid/UUID.hpp"
#include "../types/PhysicsTypes.hpp"
#include "../../services/data/ScriptTypes.hpp"

namespace components
{
    struct IBLComponent;
    struct CameraComponent;
    struct MeshComponent;
    struct MaterialComponent;
    struct BillboardComponent;
    struct AudioSource2DComponent;
    struct AudioSource3DComponent;
    struct ScriptComponent;
    struct ColliderComponent;
    struct RigidBodyComponent;
    struct AnimatorComponent;
    struct VFXComponent;
    struct DirectionalLightComponent;
    struct PointLightComponent;
    struct SpotLightComponent;

    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               VFXComponent, DirectionalLightComponent, PointLightComponent, SpotLightComponent>;

    struct WorldTransformComponent
    {
        glm::mat4 worldMatrix;
    };

    struct ParentComponent
    {
        entt::entity parent = entt::null;
    };

    struct ChildrenComponent
    {
        std::vector<entt::entity> children;
    };

    struct NameComponent
    {
        std::string name;
        bool isActive = true;
    };

    struct UUIDComponent
    {
        uuid::UUID id;

        UUIDComponent() : id()
        {
        }

        explicit UUIDComponent(uuid::UUID existingId) : id(existingId)
        {
        }

        explicit UUIDComponent(uint64_t existingId) : id(existingId)
        {
        }
    };

    struct IBLComponent
    {
        std::string fileName;
    };

    struct TransformComponent
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        bool isDirty = true;
        bool isStatic = true;


        void setPosition(const glm::vec3& newPos)
        {
            position = newPos;
            isDirty = true;
        }

        void setRotation(const glm::vec3& newRot)
        {
            rotation = newRot;
            isDirty = true;
        }

        void setScale(const glm::vec3& newScale)
        {
            scale = newScale;
            isDirty = true;
        }

        glm::mat4 getMatrix() const
        {
            auto transform = glm::mat4(1.0f);
            transform = glm::translate(transform, position);
            transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
            transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
            transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
            transform = glm::scale(transform, scale);
            return transform;
        }
    };

    struct CameraComponent
    {
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewMatrix{1.0f};
        bool isPerspective = true;
        bool isPrimary = false; // True if this is the primary camera for runtime playback
        bool showFrustum = false;
        float fieldOfView = 90.0f; // For perspective cameras, in degrees
        float orthoSize = 10.0f; // For orthographic cameras, half the height of the view
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 1.778f; // Typically screen width / height

        // Occlusion culling settings
        uint32_t cameraId = 0; // Unique ID for occlusion culling system
        bool enableOcclusionCulling = true; // Whether to use Hi-Z occlusion culling for this camera
        bool isRegistered = false; // Whether this camera has been registered with the occlusion system

        // Static counter for generating unique camera IDs
        static inline uint32_t nextCameraId = 0;

        static uint32_t generateCameraId()
        {
            return nextCameraId++;
        }

        CameraComponent()
        {
            cameraId = generateCameraId();
            updateProjectionMatrix();
            viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        }

        void updateProjectionMatrix()
        {
            if (isPerspective)
            {
                projectionMatrix = glm::perspective(
                    glm::radians(fieldOfView),
                    aspectRatio,
                    nearPlane,
                    farPlane
                );
            }
            else
            {
                float orthoHalfWidth = orthoSize * aspectRatio;
                projectionMatrix = glm::ortho(
                    -orthoHalfWidth,
                    orthoHalfWidth,
                    -orthoSize,
                    orthoSize,
                    nearPlane,
                    farPlane
                );
            }

            projectionMatrix[1][1] *= -1;
        }


        void updateViewMatrix(const glm::vec3& position, const glm::vec3& rotation)
        {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, position);
            model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0)); // Yaw
            model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0)); // Pitch
            model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1)); // Roll

            viewMatrix = glm::inverse(model);
        }
    };

    struct MeshComponent
    {
        std::string meshPath;
        std::string animatorPath;  // Path to .vfAnimator file (optional)
        bool showBoundingBox = false;
    };

    struct MaterialComponent
    {
        std::string defaultMaterial;
        std::map<std::string, std::string> subMeshMaterials;
        std::map<std::string, float> parameterOverrides;

        void setSubMeshMaterial(const std::string& submeshName, const std::string& matPath)
        {
            subMeshMaterials[submeshName] = matPath;
        }

        void setDefaultMaterial(const std::string& matPath)
        {
            defaultMaterial = matPath;
        }

        std::string getMaterialForSubmesh(const std::string& submeshName) const
        {
            auto it = subMeshMaterials.find(submeshName);
            if (it != subMeshMaterials.end())
            {
                return it->second;
            }
            return defaultMaterial;
        }

        bool hasSubmeshMaterial(const std::string& submeshName) const
        {
            return subMeshMaterials.find(submeshName) != subMeshMaterials.end();
        }

        void clearSubMeshMaterials()
        {
            subMeshMaterials.clear();
        }

        void setParameterOverride(const std::string& paramName, float value)
        {
            parameterOverrides[paramName] = value;
        }

        std::optional<float> getParameterOverride(const std::string& paramName) const
        {
            auto it = parameterOverrides.find(paramName);
            if (it != parameterOverrides.end())
            {
                return it->second;
            }
            return std::nullopt;
        }
    };


    enum class BillboardSizeMode : uint8_t
    {
        ScreenSpace,
        WorldSpace
    };


    enum class BillboardIconType : uint8_t
    {
        Light = 0,
        Camera,
        AudioSource,
        Particle,
        Custom,
    };

    struct BillboardComponent
    {
        BillboardIconType iconType = BillboardIconType::Custom;
        uint32_t atlasIndex = 0;

        BillboardSizeMode sizeMode = BillboardSizeMode::ScreenSpace;
        glm::vec2 size{64.0f, 64.0f}; // Pixels (screen-space) or world units

        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA


        bool editorOnly = true;
        bool selectable = true;

        uint32_t getEffectiveAtlasIndex() const
        {
            if (iconType == BillboardIconType::Custom)
            {
                return atlasIndex;
            }

            switch (iconType)
            {
            case BillboardIconType::Light: return 0;
            case BillboardIconType::Camera: return 1;
            case BillboardIconType::AudioSource: return 2;
            case BillboardIconType::Particle: return 3;
            default: return atlasIndex;
            }
        }
    };


    struct AudioSource2DComponent
    {
        std::string audioFilePath;
        float volume = 1.0f; // 0.0 to 1.0
        float pitch = 1.0f; // 0.5 to 2.0
        bool loop = false;

        uint64_t activeHandle = 0;
        bool isPlaying = false;
    };


    struct AudioSource3DComponent
    {
        std::string audioFilePath;
        float volume = 1.0f; // 0.0 to 1.0
        float pitch = 1.0f; // 0.5 to 2.0
        bool loop = false;
        float minDistance = 1.0f; // Distance where volume starts to attenuate
        float maxDistance = 100.0f; // Distance where volume reaches minimum
        bool showDebugSpheres = false;

        uint64_t activeHandle = 0;
        bool isPlaying = false;
    };


    struct ScriptEntry
    {
        std::string scriptPath;
        bool enabled = true;

        bool started = false;
        uint64_t instanceId = 0;

        services::ScriptPlaybackState playbackState = services::ScriptPlaybackState::Stopped;
    };

    struct ScriptComponent
    {
        std::vector<ScriptEntry> scripts;

        ScriptEntry* findByPath(const std::string& path)
        {
            for (auto& entry : scripts)
            {
                if (entry.scriptPath == path) return &entry;
            }
            return nullptr;
        }

        const ScriptEntry* findByPath(const std::string& path) const
        {
            for (const auto& entry : scripts)
            {
                if (entry.scriptPath == path) return &entry;
            }
            return nullptr;
        }

        ScriptEntry* findByInstanceId(uint64_t instanceId)
        {
            for (auto& entry : scripts)
            {
                if (entry.instanceId == instanceId) return &entry;
            }
            return nullptr;
        }

        bool hasScript(const std::string& path) const
        {
            return findByPath(path) != nullptr;
        }

        bool removeByPath(const std::string& path)
        {
            auto it = std::remove_if(scripts.begin(), scripts.end(),
                                     [&path](const ScriptEntry& e) { return e.scriptPath == path; });
            if (it != scripts.end())
            {
                scripts.erase(it, scripts.end());
                return true;
            }
            return false;
        }
    };

    /**
     * AnimatorComponent provides ECS access to an entity's animation state machine.
     *
     * OWNERSHIP MODEL:
     * - The stateMachine pointer is NON-OWNING (observer pattern)
     * - Actual ownership: RuntimeAnimatorSystem singleton owns all AnimatorStateMachine instances
     * - Lifetime guarantee: RuntimeAnimatorSystem clears this pointer before destroying the state machine
     *
     * USAGE:
     * - Do NOT delete or store this pointer long-term
     * - Do NOT access after RuntimeAnimatorSystem::shutdown()
     * - Prefer using AnimatorComponentService or EventDispatcher commands for safe access
     * - Direct pointer access is only safe during the frame it was retrieved
     *
     * WHY void*:
     * - Avoids circular header dependencies (Components.hpp cannot include AnimatorStateMachine.hpp)
     * - Cast to animation::AnimatorStateMachine* when needed in code that includes the header
     */
    struct AnimatorComponent
    {
        // Non-owning pointer to AnimatorStateMachine (owned by RuntimeAnimatorSystem)
        // Valid only while RuntimeAnimatorSystem is active and entity has animator initialized
        void* stateMachine = nullptr;

        // Cached animator asset path (mirrors MeshComponent::animatorPath)
        // Used by RuntimeAnimatorSystem to detect when animator needs reinitialization
        std::string animatorPath;

        // True when stateMachine is valid and ready for use
        // Set to false when RuntimeAnimatorSystem destroys or reinitializes the animator
        bool isInitialized = false;
    };

    using RigidBodyType = types::RigidBodyType;
    using ColliderShape = types::ColliderShape;

    struct ColliderComponent
    {
        ColliderShape shape = ColliderShape::Box;

        // Shape dimensions
        glm::vec3 size{1.0f};       // Box half-extents, or x=radius for sphere/capsule
        float height = 2.0f;         // Capsule total height

        // Transform offset from entity center
        glm::vec3 offset{0.0f};

        // Mesh collider path (for ConvexMesh/TriangleMesh)
        std::string meshPath;

        // Behavior
        bool isTrigger = false;      // Trigger/Sensor mode (no physical response)

        // Collision layer (0-15, default 1 = Dynamic layer)
        uint8_t collisionLayer = 1;

        // Physics material properties
        float friction = 0.5f;
        float restitution = 0.0f;    // Bounciness
    };

    struct RigidBodyComponent
    {
        RigidBodyType type = RigidBodyType::Dynamic;

        // Mass properties (ignored for Static bodies)
        float mass = 1.0f;

        // Damping
        float linearDamping = 0.0f;
        float angularDamping = 0.05f;

        // Axis constraints (lock movement/rotation on specific axes)
        bool freezePositionX = false;
        bool freezePositionY = false;
        bool freezePositionZ = false;
        bool freezeRotationX = false;
        bool freezeRotationY = false;
        bool freezeRotationZ = false;
    };

    struct VFXComponent
    {
        std::string vfxPath;              // Path to .vfVFX asset file
        bool autoPlay = true;             // Auto-start when entity becomes active
        bool loop = true;                 // Loop the VFX effect

        // Runtime state (managed by VFXSceneRenderer, not serialized)
        uint32_t runtimeInstanceId = 0;   // Internal ID for VFXSceneRenderer
        bool isPlaying = false;           // Current playback state
    };

    struct DirectionalLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};  // RGB color
        float intensity{1.0f};              // Light strength multiplier
        // Direction is computed from TransformComponent rotation, not stored here
    };

    struct PointLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};  // RGB color
        float intensity{1.0f};              // Light strength multiplier
        float radius{10.0f};                // Attenuation radius
        // Position is derived from WorldTransformComponent, not stored here
    };

    struct SpotLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};  // RGB color
        float intensity{1.0f};              // Light strength multiplier
        float innerAngle{30.0f};            // Inner cone angle in degrees
        float outerAngle{45.0f};            // Outer cone angle in degrees
        float range{20.0f};                 // Maximum distance
        // Direction computed from TransformComponent rotation
        // Position derived from WorldTransformComponent
    };
}
