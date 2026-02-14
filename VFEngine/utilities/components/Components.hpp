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
    struct TextComponent;
    struct UICanvasComponent;
    struct UIRectComponent;
    struct UIImageComponent;
    struct UIScrollComponent;
    struct UILayoutGroupComponent;
    struct UILabelComponent;
    struct UIButtonComponent;

    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               VFXComponent, DirectionalLightComponent, PointLightComponent,
                                               SpotLightComponent, TerrainComponent, TerrainTileComponent, TextComponent,
                                               UICanvasComponent, UIRectComponent, UIImageComponent, UIScrollComponent,
                                               UILayoutGroupComponent, UILabelComponent, UIButtonComponent>;

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
        Billboard,
        Text,
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

        std::string texturePath; // Path to .vfImage file (empty = use atlas icon)

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
            case BillboardIconType::Billboard: return 7;
            case BillboardIconType::Text: return 8;
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

    struct TerrainComponent
    {
        uint8_t resolution = 0;
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        std::array<float, 4> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        std::string heightmapPath;
        std::string terrainMaterialPath;
        std::string weightMapPath;

        bool isActive = true;
        bool isDirty = false;

        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;
        bool saveDirty = false;
    };

    struct TerrainTileComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        uint8_t currentLOD = 0;
        bool isVisible = true;

        bool isDirty = false;
        bool isGPUResident = false;

        float boundingMinY = 0.0f;
        float boundingMaxY = 0.0f;
    };

    struct TerrainColliderComponent
    {
        bool hasCollider = false;
        uint8_t collisionLayer = 0;
        float friction = 0.5f;
        float restitution = 0.0f;
    };

    struct TerrainColliderDebugData
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> lineIndices;
        uint32_t version = 0;
    };

    struct TerrainTileColliderDebugComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        TerrainColliderDebugData debugData;
    };

    struct TextComponent
    {
        std::string fontPath;
        std::string text = "Hello World";
        float fontSize = 32.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        float maxWidth = 0.0f;
    };

    enum class UIScaleMode : uint8_t
    {
        ConstantPixelSize,
        ScaleWithScreenSize
    };

    struct UICanvasComponent
    {
        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        UIScaleMode scaleMode = UIScaleMode::ScaleWithScreenSize;
        float pixelsPerUnit = 100.0f;
    };

    struct UIRectComponent
    {
        glm::vec2 anchorMin{0.0f, 0.0f};
        glm::vec2 anchorMax{1.0f, 1.0f};
        glm::vec2 pivot{0.5f, 0.5f};
        glm::vec2 sizeDelta{0.0f, 0.0f};
        glm::vec2 anchoredPosition{0.0f, 0.0f};
    };

    struct UIImageComponent
    {
        std::string texturePath;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    enum class ScrollbarVisibility : uint8_t
    {
        Auto,          // Show only when content overflows
        AlwaysVisible, // Always show
        Hidden         // Never show (still scrollable)
    };

    struct UIScrollComponent
    {
        // Config (serialized)
        bool horizontalScrollEnabled = false;
        bool verticalScrollEnabled = true;
        ScrollbarVisibility horizontalScrollbarVisibility = ScrollbarVisibility::Auto;
        ScrollbarVisibility verticalScrollbarVisibility = ScrollbarVisibility::Auto;
        float scrollSensitivity = 1.0f;

        // Runtime state (NOT serialized)
        glm::vec2 scrollOffset{0.0f, 0.0f};
        glm::vec2 contentSize{0.0f, 0.0f};
        glm::vec2 viewportSize{0.0f, 0.0f};
        glm::vec4 computedScissorRect{0.0f, 0.0f, 0.0f, 0.0f}; // x, y, width, height

        // Drag state
        bool isDragging = false;
        uint8_t dragAxis = 0; // 0=horizontal, 1=vertical
        glm::vec2 dragStartScrollOffset{0.0f, 0.0f};
        glm::vec2 dragStartMousePos{0.0f, 0.0f};
    };

    enum class LayoutDirection : uint8_t
    {
        Vertical,
        Horizontal,
        Grid
    };

    enum class ChildAlignment : uint8_t
    {
        Start,
        Center,
        End
    };

    struct UILayoutGroupComponent
    {
        LayoutDirection direction = LayoutDirection::Vertical;
        float spacing = 0.0f;
        glm::vec4 padding{0.0f, 0.0f, 0.0f, 0.0f}; // left, right, top, bottom
        ChildAlignment childAlignment = ChildAlignment::Start;
        int constraintCount = 2; // columns (Grid mode only)
    };

    enum class HorizontalAlignment : uint8_t
    {
        Left,
        Center,
        Right
    };

    enum class VerticalAlignment : uint8_t
    {
        Top,
        Middle,
        Bottom
    };

    enum class TextOverflow : uint8_t
    {
        Overflow,
        Clip,
        Ellipsis
    };

    enum class FontStyle : uint8_t
    {
        Normal,
        Bold,
        Italic,
        BoldItalic
    };

    struct UILabelComponent
    {
        std::string text = "Label";
        std::string fontPath;
        float fontSize = 16.0f;
        FontStyle fontStyle = FontStyle::Normal;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        HorizontalAlignment horizontalAlignment = HorizontalAlignment::Left;
        VerticalAlignment verticalAlignment = VerticalAlignment::Top;
        TextOverflow overflow = TextOverflow::Overflow;
        bool wordWrap = true;
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
    };

    enum class UIButtonState : uint8_t
    {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    struct UIButtonComponent
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

        // Runtime state (NOT serialized)
        UIButtonState currentState = UIButtonState::Normal;
        glm::vec4 currentDisplayColor{1.0f, 1.0f, 1.0f, 1.0f}; // lerped display color
    };
}
