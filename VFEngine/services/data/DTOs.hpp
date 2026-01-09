#pragma once
#include "EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace services
{
    struct TransformData
    {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees
        glm::vec3 scale{1.0f, 1.0f, 1.0f};

        bool operator==(const TransformData& other) const
        {
            return position == other.position &&
                rotation == other.rotation &&
                scale == other.scale;
        }
    };

    struct CameraData
    {
        float fieldOfView = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 16.0f / 9.0f;
        bool isPerspective = true;
        bool isPrimary = false;
        bool showFrustum = false;
        float orthoSize = 10.0f;
    };

    struct IBLData
    {
        std::string fileName;
    };

    struct MeshData
    {
        std::string meshPath;
        bool showBoundingBox = false;
    };

    struct MeshBoundingBox
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };


    struct MaterialData
    {
        std::string defaultMaterial; // .vfMat path for unmapped submeshes
        std::map<std::string, std::string> subMeshMaterials; // submesh NAME -> .vfMat path
        std::map<std::string, float> parameterOverrides; // Runtime parameter tweaks
    };

    struct SubMeshInfo
    {
        std::string name;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct LODInfo
    {
        uint32_t lodLevel = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        float reductionPercent = 100.0f; // 100% for LOD0, 50% for LOD1, etc.
    };

    struct EntityData
    {
        EntityHandle handle;
        std::string name;
        bool isActive = true;
        std::optional<EntityHandle> parent;
        std::vector<EntityHandle> children;
        TransformData localTransform;
        TransformData worldTransform;
        std::vector<ComponentTypeId> components;

        bool hasComponent(ComponentTypeId type) const
        {
            for (auto c : components)
            {
                if (c == type) return true;
            }
            return false;
        }
    };

    struct SceneHierarchyData
    {
        EntityHandle root;
        std::vector<EntityData> entities;

        const EntityData* findEntity(EntityHandle handle) const
        {
            for (const auto& entity : entities)
            {
                if (entity.handle == handle)
                {
                    return &entity;
                }
            }
            return nullptr;
        }
    };

    struct ViewportTextureHandle
    {
        void* imguiDescriptorSet = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;

        bool isValid() const { return imguiDescriptorSet != nullptr; }
    };

    struct ImportFileRequest
    {
        std::string path;
        bool flipVertically = false;
    };

    struct ImportResult
    {
        std::string sourcePath;
        bool success = false;
        std::string errorMessage;
    };
    
    struct EditorTextureHandle
    {
        void* imguiDescriptorSet = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 1;
        std::vector<void*> mipDescriptorSets; // One descriptor per mip level for preview

        bool isValid() const { return imguiDescriptorSet != nullptr; }

        void* getMipDescriptor(uint32_t level) const
        {
            if (level < mipDescriptorSets.size())
            {
                return mipDescriptorSets[level];
            }
            return imguiDescriptorSet; // Fallback to main descriptor
        }
    };

    struct CameraMovement
    {
        glm::vec3 deltaPosition{0.0f};
        glm::vec2 deltaRotation{0.0f};
    };

    // Audio component data structs
    struct AudioSource2DData {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
    };

    struct AudioSource3DData {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        bool showDebugSpheres = false;
    };

    // Physics component enums
    enum class ColliderShapeType : uint8_t {
        Box,
        Sphere,
        Capsule,
        ConvexMesh,
        TriangleMesh
    };

    enum class RigidBodyTypeData : uint8_t {
        Static,
        Dynamic,
        Kinematic
    };

    // Physics component data structs
    struct ColliderComponentData {
        ColliderShapeType shape = ColliderShapeType::Box;
        glm::vec3 size{1.0f};
        float height = 2.0f;
        glm::vec3 offset{0.0f};
        std::string meshPath;
        bool isTrigger = false;
        uint8_t collisionLayer = 1;  // 0-15, default 1 = Dynamic layer
        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct RigidBodyComponentData {
        RigidBodyTypeData type = RigidBodyTypeData::Dynamic;
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
}
