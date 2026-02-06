#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <map>
#include <array>
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
    struct TerrainComponent;
    struct TerrainTileComponent;

    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               VFXComponent, DirectionalLightComponent, PointLightComponent,
                                               SpotLightComponent, TerrainComponent, TerrainTileComponent>;

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
        bool isPrimary = false;
        bool showFrustum = false;
        float fieldOfView = 90.0f;
        float orthoSize = 10.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 1.778f;

        uint32_t cameraId = 0;
        bool enableOcclusionCulling = true;
        bool isRegistered = false;

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
            model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
            model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));

            viewMatrix = glm::inverse(model);
        }
    };

    struct MeshComponent
    {
        std::string meshPath;
        std::string animatorPath;
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
    };

    enum class BillboardSizeMode : uint8_t
    {
        ScreenSpace,
        WorldSpace
    };

    enum class BillboardIconType : uint8_t
    {
        DirectionalLight = 0,
        PointLight,
        SpotLight,
        Camera,
        Audio2D,
        Audio3D,
        Particle,
        Custom
    };

    struct BillboardComponent
    {
        BillboardIconType iconType = BillboardIconType::Custom;
        uint32_t atlasIndex = 0;

        BillboardSizeMode sizeMode = BillboardSizeMode::ScreenSpace;
        glm::vec2 size{64.0f, 64.0f};

        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};

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
            case BillboardIconType::DirectionalLight: return 0;
            case BillboardIconType::PointLight: return 1;
            case BillboardIconType::SpotLight: return 2;
            case BillboardIconType::Camera: return 3;
            case BillboardIconType::Audio2D: return 4;
            case BillboardIconType::Audio3D: return 5;
            case BillboardIconType::Particle: return 6;
            default: return atlasIndex;
            }
        }
    };

    struct AudioSource2DComponent
    {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;

        uint64_t activeHandle = 0;
        bool isPlaying = false;
    };

    struct AudioSource3DComponent
    {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
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

    // Non-owning pointer to AnimatorStateMachine (owned by RuntimeAnimatorSystem)
    // Cast to animation::AnimatorStateMachine* when needed
    struct AnimatorComponent
    {
        void* stateMachine = nullptr;
        std::string animatorPath;
        bool isInitialized = false;
    };

    using RigidBodyType = types::RigidBodyType;
    using ColliderShape = types::ColliderShape;

    struct ColliderComponent
    {
        ColliderShape shape = ColliderShape::Box;

        glm::vec3 size{1.0f};
        float height = 2.0f;
        glm::vec3 offset{0.0f};
        std::string meshPath;

        bool isTrigger = false;
        uint8_t collisionLayer = 1;

        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct RigidBodyComponent
    {
        RigidBodyType type = RigidBodyType::Dynamic;

        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.05f;

        bool freezePositionX = false;
        bool freezePositionY = false;
        bool freezePositionZ = false;
        bool freezeRotationX = false;
        bool freezeRotationY = false;
        bool freezeRotationZ = false;
    };

    struct VFXComponent
    {
        std::string vfxPath;
        bool autoPlay = true;
        bool loop = true;

        uint32_t runtimeInstanceId = 0;
        bool isPlaying = false;
    };

    struct DirectionalLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        bool showGizmo = false;
    };

    struct PointLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float radius{10.0f};
        bool showGizmo = false;
    };

    struct SpotLightComponent
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float innerAngle{30.0f};
        float outerAngle{45.0f};
        float range{20.0f};
        bool showGizmo = false;
    };

    // Terrain component - attached to parent terrain entity
    struct TerrainComponent
    {
        uint8_t resolution = 0;  // 0=Low(33x33), 1=Medium(65x65), 2=High(129x129), 3=Ultra(257x257)
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        // Heightmap source path (for regeneration/serialization)
        std::string heightmapPath;

        bool isActive = true;       // Global terrain enable/disable
        bool isDirty = false;       // Config changed, needs regeneration

        // Runtime statistics (read-only - updated by TerrainService)
        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;
    };

    // Terrain tile component - attached to each tile child entity
    struct TerrainTileComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        uint8_t currentLOD = 0;
        bool isVisible = true;

        bool isDirty = false;           // Needs geometry regeneration
        bool isGPUResident = false;     // Currently uploaded to GPU

        // Cached bounds for inspector display
        float boundingMinY = 0.0f;
        float boundingMaxY = 0.0f;
    };
}
