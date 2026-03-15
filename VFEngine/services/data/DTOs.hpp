#pragma once
#include "EntityHandle.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
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

    struct NavmeshRootData
    {
        std::string navmeshPath;
    };

    struct MeshData
    {
        std::string meshPath;
        std::string animatorPath; // Path to .vfAnimator file (optional)
        bool showBoundingBox = false;
        bool applyRootMotion = false;
        float maxDrawDistance = 0.0f; // 0 = use category default from render config
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
        std::string busName = "Music";
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

        bool enableDistanceFilter = true;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        bool showDebugCone = false;
        std::string busName = "SFX";
    };

    struct ReverbZoneData
    {
        uint8_t shape = 0; // 0=Sphere, 1=Box
        float radius = 10.0f;
        glm::vec3 halfExtents{5.0f};
        std::string presetName = "Generic";
        types::ReverbParams customParams;
        int priority = 0;
        float falloffDistance = 2.0f;
        float wetLevel = 1.0f;
        bool showDebugVolume = false;
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

    struct PhysicsAnimationComponentData
    {
        std::string physicsAnimationPath;
        types::PhysicsAnimationMode defaultMode = types::PhysicsAnimationMode::Animated;
        uint8_t collisionLayer = 1;
        std::vector<types::BoneBodyMapping> boneBodyMappings;
        std::vector<types::JointConstraintLimits> jointLimits;
    };

    struct VFXData
    {
        std::string vfxPath; // Path to .vfVFX asset file
        bool autoPlay = true; // Auto-start when play mode begins
        bool loop = true; // Loop the VFX effect
    };

    struct RenderTextureData
    {
        uint32_t width = 512;
        uint32_t height = 512;
        uint8_t updateMode = 0; // 0=EveryFrame, 1=OnDemand, 2=FixedInterval
        float fixedIntervalSeconds = 1.0f / 30.0f;
        glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        uint32_t priority = 0;
        bool enabled = true;
    };

    struct BillboardData
    {
        std::string texturePath; // Path to .vfImage file
        glm::vec2 size{1.0f, 1.0f};
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        EntityHandle renderTextureSource;
        std::string renderTextureSourceName;
    };

    struct DecalData
    {
        glm::vec3 halfExtents{0.5f, 0.5f, 0.1f};
        std::string albedoTexture;
        std::string normalTexture;
        std::string ormTexture;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float angleFadeStart = 0.7f;
        float angleFadeEnd = 0.3f;
        float edgeFalloff = 0.1f;
        int32_t sortPriority = 0;
        bool modifyNormals = true;
        float normalStrength = 1.0f;
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
        EntityHandle renderTextureSource;
        std::string renderTextureSourceName;

        uint8_t imageType = 0; // 0=Simple, 1=Sliced, 2=Tiled
        glm::vec4 border{0.0f, 0.0f, 0.0f, 0.0f}; // left, right, top, bottom (source pixels)
        uint32_t sourceWidth = 0;
        uint32_t sourceHeight = 0;
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

    struct UICheckboxData
    {
        // Checked state
        bool isChecked = false;

        // Radio group
        std::string groupName;
        bool allowUncheck = true;

        // Per-state colors
        glm::vec4 uncheckedColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 checkedColor{0.3f, 0.7f, 1.0f, 1.0f};
        glm::vec4 hoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.5f};

        // Per-state textures
        std::string uncheckedTexture;
        std::string checkedTexture;
        std::string hoveredTexture;
        std::string disabledTexture;

        // Config
        float colorTransitionDuration = 0.1f;
        bool interactable = true;
        bool labelToggle = false;

        // Runtime
        uint8_t currentState = 0; // 0=Normal, 1=Hovered, 2=Disabled
    };

    struct DropdownOptionData
    {
        std::string text;
        std::string iconPath; // empty = no icon
    };

    struct UIDropdownData
    {
        // Config
        std::vector<DropdownOptionData> options;
        int selectedIndex = -1;
        std::string placeholderText = "Select...";
        int maxVisibleItems = 5;
        bool interactable = true;

        // Header state colors
        glm::vec4 normalColor{0.25f, 0.25f, 0.25f, 1.0f};
        glm::vec4 hoveredColor{0.3f, 0.3f, 0.3f, 1.0f};
        glm::vec4 openColor{0.2f, 0.2f, 0.35f, 1.0f};
        glm::vec4 disabledColor{0.15f, 0.15f, 0.15f, 0.5f};

        // List colors
        glm::vec4 listBackgroundColor{0.18f, 0.18f, 0.18f, 1.0f};
        glm::vec4 itemNormalColor{0.18f, 0.18f, 0.18f, 0.0f};
        glm::vec4 itemHoveredColor{0.3f, 0.5f, 0.8f, 0.5f};

        // Font
        std::string fontPath;
        float fontSize = 16.0f;

        float colorTransitionDuration = 0.1f;

        // Runtime
        uint8_t currentState = 0; // 0=Normal, 1=Hovered, 2=Open, 3=Disabled
        bool isOpen = false;
        int hoveredOptionIndex = -1;
    };

    struct UITabsData
    {
        uint8_t tabBarPosition = 0; // 0=Top, 1=Bottom, 2=Left, 3=Right
        int activeTabIndex = 0;
        int previousTabIndex = -1;
    };

    struct UISliderData
    {
        // Value config
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.5f;
        float stepSize = 0.0f;
        uint8_t orientation = 0; // 0=Horizontal, 1=Vertical
        bool clickTrackToSet = true;

        // Handle appearance
        float handleSizeRatio = 0.08f;
        glm::vec4 handleNormalColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 handleHoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 handlePressedColor{0.7f, 0.7f, 0.7f, 1.0f};
        glm::vec4 handleDisabledColor{0.5f, 0.5f, 0.5f, 0.5f};
        std::string handleNormalTexture;
        std::string handleHoveredTexture;
        std::string handlePressedTexture;
        std::string handleDisabledTexture;

        // Fill appearance
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        std::string fillTexture;

        // Config
        float colorTransitionDuration = 0.1f;
        bool interactable = true;

        // Runtime
        uint8_t currentState = 0; // 0=Normal, 1=Hovered, 2=Pressed, 3=Disabled
        bool isDragging = false;
    };

    struct UIProgressBarData
    {
        // Value config
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float value = 0.0f;
        uint8_t orientation = 0; // 0=Horizontal, 1=Vertical
        bool invertDirection = false;

        // Smooth interpolation
        bool smoothInterpolation = false;
        float interpolationSpeed = 5.0f;

        // Track appearance
        glm::vec4 trackColor{0.2f, 0.2f, 0.2f, 1.0f};
        std::string trackTexture;

        // Fill appearance
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        std::string fillTexture;

        // Runtime
        float displayValue = 0.0f;
    };

    // ========== UI Mask ==========

    struct UIMaskData
    {
        std::string maskTexturePath;
        float alphaThreshold = 0.5f;
        bool showMaskGraphic = false;
    };

    // ========== UI Drag & Drop ==========

    struct UIDraggableData
    {
        float ghostOpacity = 0.5f;
        glm::vec2 ghostOffset{0.0f, 0.0f};
        bool constrainToParent = true;
        std::string dragTag;
    };

    struct UIDropTargetData
    {
        std::string acceptTag;
        glm::vec4 highlightColor{0.3f, 0.7f, 1.0f, 0.3f};
        glm::vec4 rejectColor{1.0f, 0.2f, 0.2f, 0.3f};
        bool interactable = true;
    };

    // ========== UI Animation ==========

    struct UIAnimationClipData
    {
        uint8_t property = 0;   // UITweenProperty
        float startValue = 0.0f;
        float endValue = 1.0f;
        float duration = 1.0f;
        float delay = 0.0f;
        uint8_t easing = 0;     // UIEasingFunction
        uint8_t loopMode = 0;   // UIAnimationLoopMode
    };

    struct UIAnimationNodeData
    {
        uint8_t type = 0;       // UIAnimationNodeType
        UIAnimationClipData clip;
        std::vector<UIAnimationNodeData> children;
        uint8_t loopMode = 0;   // UIAnimationLoopMode
    };

    struct UIAnimationData
    {
        UIAnimationNodeData rootNode;
        bool autoPlay = false;
    };
}
