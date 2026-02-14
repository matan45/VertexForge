#pragma once
#include "EntityHandle.hpp"
#include "types/PhysicsTypes.hpp"
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
        std::string animatorPath; // Path to .vfAnimator file (optional)
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
    };

    struct ViewportTextureHandle
    {
        void* imguiDescriptorSet = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;

        bool isValid() const { return imguiDescriptorSet != nullptr; }
    };

    struct ImportResult
    {
        std::string sourcePath;
        bool success = false;
        std::string errorMessage;
    };

    struct ImportFileRequest
    {
        std::string path;
        bool flipVertically = false;
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
    struct AudioSource2DData
    {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
    };

    struct AudioSource3DData
    {
        std::string audioFilePath;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        bool showDebugSpheres = false;
    };

    struct ColliderComponentData
    {
        types::ColliderShape shape = types::ColliderShape::Box;
        glm::vec3 size{1.0f};
        float height = 2.0f;
        glm::vec3 offset{0.0f};
        std::string meshPath;
        bool isTrigger = false;
        uint8_t collisionLayer = 1;
        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct RigidBodyComponentData
    {
        types::RigidBodyType type = types::RigidBodyType::Dynamic;
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

    struct VFXData
    {
        std::string vfxPath; // Path to .vfVFX asset file
        bool autoPlay = true; // Auto-start when play mode begins
        bool loop = true; // Loop the VFX effect
    };

    struct BillboardData
    {
        std::string texturePath; // Path to .vfImage file
        glm::vec2 size{1.0f, 1.0f};
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct TextData
    {
        std::string fontPath;
        std::string text = "Hello World";
        float fontSize = 32.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        float maxWidth = 0.0f;
    };

    struct DirectionalLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        bool showGizmo = false;
    };

    struct PointLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float radius = 10.0f;
        bool showGizmo = false;
    };

    struct SpotLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float innerAngle = 30.0f;
        float outerAngle = 45.0f;
        float range = 20.0f;
        bool showGizmo = false;
    };

    struct UICanvasData
    {
        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        uint8_t scaleMode = 1; // 0=ConstantPixelSize, 1=ScaleWithScreenSize
        float pixelsPerUnit = 100.0f;
    };

    struct UIRectData
    {
        glm::vec2 anchorMin{0.0f, 0.0f};
        glm::vec2 anchorMax{1.0f, 1.0f};
        glm::vec2 pivot{0.5f, 0.5f};
        glm::vec2 sizeDelta{0.0f, 0.0f};
        glm::vec2 anchoredPosition{0.0f, 0.0f};
    };

    struct UIImageData
    {
        std::string texturePath;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct UIScrollData
    {
        bool horizontalScrollEnabled = false;
        bool verticalScrollEnabled = true;
        uint8_t horizontalScrollbarVisibility = 0; // 0=Auto, 1=AlwaysVisible, 2=Hidden
        uint8_t verticalScrollbarVisibility = 0;   // 0=Auto, 1=AlwaysVisible, 2=Hidden
        float scrollSensitivity = 1.0f;
    };

    struct UILayoutGroupData
    {
        uint8_t direction = 0;      // 0=Vertical, 1=Horizontal, 2=Grid
        float spacing = 0.0f;
        glm::vec4 padding{0.0f, 0.0f, 0.0f, 0.0f}; // left, right, top, bottom
        uint8_t childAlignment = 0; // 0=Start, 1=Center, 2=End
        int constraintCount = 2;    // columns (Grid mode only)
    };

    struct UILabelData
    {
        std::string text = "Label";
        std::string fontPath;
        float fontSize = 16.0f;
        uint8_t fontStyle = 0;              // 0=Normal, 1=Bold, 2=Italic, 3=BoldItalic
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        uint8_t horizontalAlignment = 0;    // 0=Left, 1=Center, 2=Right
        uint8_t verticalAlignment = 0;      // 0=Top, 1=Middle, 2=Bottom
        uint8_t overflow = 0;               // 0=Overflow, 1=Clip, 2=Ellipsis
        bool wordWrap = true;
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
    };

    struct UIButtonData
    {
        // Per-state colors
        glm::vec4 normalColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 hoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 pressedColor{0.7f, 0.7f, 0.7f, 1.0f};
        glm::vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.5f};

        // Per-state texture paths (empty = use color only)
        std::string normalTexture;
        std::string hoverTexture;
        std::string pressedTexture;
        std::string disabledTexture;

        // Config
        float colorTransitionDuration = 0.1f;
        bool interactable = true;

        // Runtime
        uint8_t currentState = 0; // 0=Normal, 1=Hovered, 2=Pressed, 3=Disabled
    };

    struct UITextInputData
    {
        // Config
        std::string text;
        std::string placeholderText = "Enter text...";
        std::string fontPath;
        float fontSize = 16.0f;
        glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 placeholderColor{0.5f, 0.5f, 0.5f, 0.7f};

        // Per-state background colors
        glm::vec4 normalColor{0.2f, 0.2f, 0.2f, 1.0f};
        glm::vec4 hoveredColor{0.25f, 0.25f, 0.25f, 1.0f};
        glm::vec4 focusedColor{0.15f, 0.15f, 0.3f, 1.0f};
        glm::vec4 disabledColor{0.15f, 0.15f, 0.15f, 0.5f};

        float colorTransitionDuration = 0.1f;
        bool interactable = true;
        int maxLength = 0; // 0 = unlimited

        // Caret and selection
        glm::vec4 selectionColor{0.3f, 0.5f, 0.8f, 0.5f};
        glm::vec4 caretColor{1.0f, 1.0f, 1.0f, 1.0f};
        float caretWidth = 2.0f;
        float caretBlinkRate = 0.53f;

        // Runtime
        uint8_t currentState = 0; // 0=Normal, 1=Hovered, 2=Focused, 3=Disabled
    };
}
