#pragma once
#include "EntityHandle.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include "types/AudioVariationTypes.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include "types/VehicleTypes.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <asset/AssetRef.hpp>
#include <resource/AssetTypes.hpp>
#include <material/MaterialTypes.hpp>
#include <components/DestructionComponents.hpp>
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
        uint32_t cullingMask = 0xFFFFFFFFu; // VK-1415: per-camera render-layer mask (all layers by default)
    };

    struct IBLData
    {
        asset::AssetRef hdrRef;
        // VK-1574: global IBL knobs (mirror components::IBLComponent).
        float intensity = 1.0f;
        float rotationDeg = 0.0f;
        glm::vec3 tint{1.0f};
    };

    struct NavmeshRootData
    {
        asset::AssetRef navmeshRef;
    };

    struct MeshData
    {
        asset::AssetRef meshRef;
        asset::AssetRef animatorRef; // .vfAnimator asset (optional)
        asset::AssetRef retargetRef; // .vfretarget binding (optional, VK-910)
        bool showBoundingBox = false;
        bool applyRootMotion = false;
        float maxDrawDistance = 0.0f; // 0 = use category default from render config
        int32_t submeshIndex = -1; // -1 = all, >= 0 = only this submesh
        uint32_t renderLayer = 0; // VK-1415: render-layer index 0-31
    };

    struct MeshBoundingBox
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };


    // VK-1418: per-entity binding of a material texture slot to a live RTT. The entity handle is
    // runtime-only; only the name is persisted (mirrors BillboardData renderTextureSource).
    struct RenderTextureSlotBindingData
    {
        EntityHandle source = EntityHandle::invalid();
        std::string sourceName;
    };

    struct MaterialData
    {
        asset::AssetRef defaultMaterialRef; // .vfMat asset for unmapped submeshes
        std::map<std::string, asset::AssetRef> subMeshMaterials; // submesh NAME -> .vfMat asset
        std::map<std::string, material::ParameterValue> parameterOverrides; // Runtime named-parameter tweaks
        // VK-1418: slot name ("albedo" | "emission") -> RTT source entity binding.
        std::map<std::string, RenderTextureSlotBindingData> renderTextureSlotBindings;
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
        bool isEffectivelyActive = true;
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
        std::string outputPath;
        resource::AssetType assetType = resource::AssetType::COUNT;
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
        asset::AssetRef audioRef;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        std::string busName = "Music";
        // VK-1521: fade-in ramp length in ms. 0 = no fade.
        float fadeInMs = 0.0f;

        // VK-1520: mirrors AudioSource2DComponent's variation block. Pool is
        // [audioRef] ++ valid(clipVariants) — audioRef is variant 0. AUTHORED
        // fields only: lastVariant/playCount are runtime state and stay out, the
        // same way activeHandle/isPlaying do. That is load-bearing, not tidiness
        // — the drawer round-trips this whole DTO every frame a slider is
        // dragged, which would reset the round-robin cursor mid-drag.
        std::vector<asset::AssetRef> clipVariants;
        types::AudioPlayOrder playOrder = types::AudioPlayOrder::Single;
        float pitchVariation = 0.0f;
        float volumeVariation = 0.0f;
    };

    struct AudioSource3DData
    {
        asset::AssetRef audioRef;
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

        // VK-1518: mirrors AudioSource3DComponent's occlusion block. Both floats are CUT
        // amounts at full occlusion (0 = inert); occlusionLayerMask is the trace channel
        // (bit N = collision layer N), Static|Kinematic by default.
        bool enableOcclusion = false;
        float occlusionLpf = 0.7f;
        float occlusionVolume = 0.3f;
        uint16_t occlusionLayerMask = 0x0005;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        bool showDebugCone = false;
        std::string busName = "SFX";
        // VK-1513: lower = more important (0 = critical, 255 = least, 128 = neutral).
        uint8_t priority = 128;
        // VK-1521: fade-in ramp length in ms. 0 = no fade.
        float fadeInMs = 0.0f;

        // VK-1520: see AudioSource2DData's variation block. Authored fields only.
        std::vector<asset::AssetRef> clipVariants;
        types::AudioPlayOrder playOrder = types::AudioPlayOrder::Single;
        float pitchVariation = 0.0f;
        float volumeVariation = 0.0f;
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

    struct FogVolumeData
    {
        uint8_t shape = 0; // 0=Box, 1=Sphere, 2=Cylinder
        glm::vec3 halfExtents{5.0f};
        float density = 0.5f;
        glm::vec3 albedo{0.8f, 0.85f, 0.9f};
        glm::vec3 emission{0.0f};
        float edgeFalloff = 0.5f;
        uint8_t blendMode = 0; // 0=Additive, 1=Subtractive
        bool showGizmo = false;
    };

    // VK-1577 — mirrors components::ReflectionProbeComponent. `dirty` is intentionally absent:
    // bake state is owned by the renderer, not authored through the inspector (the editor requests
    // a bake with BakeReflectionProbesCommand instead).
    struct ReflectionProbeData
    {
        uint8_t shape = 0; // 0=Box, 1=Sphere
        glm::vec3 halfExtents{5.0f};
        float blendDistance = 1.0f;
        float intensity = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        int32_t priority = 0;
        bool captureShadows = false;
        bool showGizmo = true;
    };

    struct ColliderComponentData
    {
        types::ColliderShape shape = types::ColliderShape::Box;
        glm::vec3 size{1.0f};
        float height = 2.0f;
        glm::vec3 offset{0.0f};
        asset::AssetRef meshRef;
        int32_t submeshIndex = -1;
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

    struct VehicleComponentData
    {
        types::VehicleConfig config = types::VehicleConfig::createFourWheelCar();
    };

    struct BuoyancyComponentData
    {
        bool customSampleMode = false;
        glm::vec3 customPoints[8]{};
        uint32_t customPointCount = 0;
        float buoyancyScale = 1.0f;
        float angularDrag = 0.5f;
    };

    // VK-1606
    struct WaterWakeEmitterComponentData
    {
        glm::vec3 offset{0.0f};
        float radius = 1.5f;
        float strength = 0.5f;
        float minSpeed = 0.5f;
        float travelInterval = 0.5f;
        bool continuous = false;
        bool enabled = true;
    };

    // VK-1607. Mirrors components::WaterBodyComponent; `type` is the raw enum value so this DTO
    // stays free of the components headers.
    struct WaterBodyComponentData
    {
        uint32_t type = 0;                    // 0 = Lake, 1 = Pool
        float waterHeight = 0.0f;             // offset above the entity transform's Y
        glm::vec2 halfExtents{10.0f, 10.0f};  // world metres
        float depth = 10.0f;                  // world metres below the surface
        uint32_t bandMask = 0u;
        bool physicsEnabled = true;
        bool isActive = true;
    };

    // One row of GetWaterBodiesQuery: the entity plus its settings, so the editor can list and
    // select bodies without a second query per entity.
    struct WaterBodyEntry
    {
        EntityHandle entity;
        WaterBodyComponentData data;
    };

    struct PhysicsAnimationComponentData
    {
        asset::AssetRef physicsAnimationRef;
        types::PhysicsAnimationConfig config;
    };

    struct VFXData
    {
        asset::AssetRef vfxRef; // .vfVFX asset
        bool autoPlay = true; // Auto-start when play mode begins
        bool loop = true; // Loop the VFX effect
    };

    // Mirrors components::VFXSequenceTrigger (MediaComponents.hpp:240-245): one anim-event ->
    // .vfVFXSequence mapping.
    struct VFXSequenceTriggerData
    {
        asset::AssetRef sequenceRef; // .vfVFXSequence to play when the event fires
        std::string eventName;       // animation notify event name
        std::string socketName;      // optional attach socket
    };

    // Mirrors components::VFXSequenceComponent (MediaComponents.hpp:247-255). The transient
    // runtimeComboId is not exposed (never edited through the DTO/command path).
    struct VFXSequenceData
    {
        asset::AssetRef sequenceRef; // standalone autoplay sequence (.vfVFXSequence)
        bool autoPlay = false;       // component default is false (MediaComponents.hpp:250)
        bool loop = false;           // component default is false (:251)
        std::string socketName;      // optional attach for the standalone combo
        std::vector<VFXSequenceTriggerData> triggers;
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
        bool renderShadows = false; // false = flat-lit (e.g. minimap); true = sample shadows
        bool tonemap = true; // true = match main viewport (tonemap/gamma); false = raw HDR (e.g. minimap)
        // VK-1414: optional reference to a SEPARATE camera entity to render from. The handle is
        // in/out for the editor picker; sourceCameraName is the serialized identity that drives
        // resolution. Empty/invalid = use this entity's own camera (legacy).
        EntityHandle sourceCamera;
        std::string sourceCameraName;
        // Output-only: the live render-texture id, populated only in play mode (0 / INVALID
        // otherwise). Read for the editor live preview; never written back to the component.
        uint32_t runtimeTextureId = 0;
    };

    // Read-only snapshot of one active render-texture controller, used by the editor RTT debug
    // overlay (VK-1413). Enumerated from RenderTextureAdapter's controllers in render order.
    struct RenderTextureDebugInfo
    {
        uint32_t textureId = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t updateMode = 0; // 0=EveryFrame, 1=OnDemand, 2=FixedInterval
        uint32_t priority = 0;
        bool enabled = false;
        bool hasRendered = false;       // has produced at least one frame (lastRenderedHandle != null)
        bool submittedLastFrame = false; // rendered this frame (didSubmitLastRender)
    };

    struct BillboardData
    {
        asset::AssetRef textureRef; // .vfImage asset
        glm::vec2 size{1.0f, 1.0f};
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        EntityHandle renderTextureSource;
        std::string renderTextureSourceName;

        // Animation (Phase 1). Defaults render identically to a static billboard.
        uint32_t flipbookColumns = 1;
        uint32_t flipbookRows = 1;
        float flipbookFrameRate = 0.0f;
        float scrollU = 0.0f;
        float scrollV = 0.0f;
        float pulseAmplitude = 0.0f;
        float pulseFrequency = 0.0f;
        float spinSpeed = 0.0f;
        float animStartTime = 0.0f;
        bool loopAnimation = true; // flipbook: true=loop, false=play once then hold last frame
        bool worldMarker = false;
    };

    struct DecalData
    {
        uint8_t shape = 0; // 0=Rectangle, 1=Circle, 2=Triangle
        glm::vec3 halfExtents{0.5f, 0.5f, 0.1f};
        asset::AssetRef albedoTextureRef;
        asset::AssetRef normalTextureRef;
        asset::AssetRef ormTextureRef;
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
        asset::AssetRef fontRef;
        std::string text = "Hello World";
        float fontSize = 32.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        float maxWidth = 0.0f;
        uint8_t fontStyle = 0;  // 0=Normal, 1=Bold, 2=Italic, 3=BoldItalic
    };

    struct DirectionalLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float lightSize = 1.0f;
        bool showGizmo = false;
    };

    struct PointLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float radius = 10.0f;
        float lightSize = 0.1f;
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct SpotLightData
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float innerAngle = 30.0f;
        float outerAngle = 45.0f;
        float range = 20.0f;
        float lightSize = 0.1f;
        bool castsShadow = false;
        bool showGizmo = false;
    };

    struct ShadowOverrideData
    {
        float depthBias = -1.0f;
        float slopeBias = -1.0f;
        float normalBias = -1.0f;
        uint32_t maxPages = 0;
        bool softShadows = false;
        bool hasSoftShadowOverride = false;
    };

    struct UICanvasData
    {
        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        uint8_t scaleMode = 1; // 0=ConstantPixelSize, 1=ScaleWithScreenSize
        float pixelsPerUnit = 100.0f;
        int sortOrder = 0;
    };

    struct UIRectData
    {
        glm::vec2 anchorMin{0.0f, 0.0f};
        glm::vec2 anchorMax{1.0f, 1.0f};
        glm::vec2 pivot{0.5f, 0.5f};
        glm::vec2 sizeDelta{0.0f, 0.0f};
        glm::vec2 anchoredPosition{0.0f, 0.0f};
        bool blocksRaycast = true;
    };

    // Resolved on-screen pixel rect of a UIRect (viewport space, top-left origin,
    // y down) — the same pixel space as SetUIRectPixelsCommand and the runtime UI
    // hit tests, so scripts can map mouse coordinates into a UI element.
    struct UIResolvedRectData
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    // World-space corners of a UI element's quad as rendered in edit mode
    // (TL, TR, BR, BL) — used by the editor viewport selection outline.
    struct UIQuadCorners
    {
        glm::vec3 corners[4]{};
    };

    struct UIImageData
    {
        asset::AssetRef textureRef;
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        EntityHandle renderTextureSource;
        std::string renderTextureSourceName;
        // VK-1488: runtime-only plugin/GPU external texture key ("__plugintex_<id>__").
        std::string externalTextureKey;

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
        asset::AssetRef fontRef;
        float fontSize = 16.0f;
        uint8_t fontStyle = 0;              // 0=Normal, 1=Bold, 2=Italic, 3=BoldItalic
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        uint8_t horizontalAlignment = 0;    // 0=Left, 1=Center, 2=Right
        uint8_t verticalAlignment = 0;      // 0=Top, 1=Middle, 2=Bottom
        uint8_t overflow = 0;               // 0=Overflow, 1=Clip, 2=Ellipsis
        bool wordWrap = true;
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        bool richText = false;
    };

    struct UITooltipData
    {
        uint8_t mode = 0; // 0=Text, 1=ChildPanel
        std::string text;
        float showDelay = 0.5f;
        bool followCursor = true;
        glm::vec2 offset{12.0f, 16.0f};
        float maxWidth = 280.0f;
        glm::vec4 backgroundColor{0.08f, 0.08f, 0.08f, 0.95f};
        glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
        asset::AssetRef fontRef;
        float fontSize = 14.0f;
        float letterSpacing = 0.0f;
        glm::vec4 padding{8.0f, 8.0f, 6.0f, 6.0f}; // left, right, top, bottom
        bool enabled = true;
        std::string panelChildName;
    };

    struct UIListViewData
    {
        asset::AssetRef itemTemplateRef; // .vfPrefab
        int itemCount = 0;
        bool selectable = true;
        glm::vec4 selectedTint{0.3f, 0.5f, 0.8f, 0.35f};
        int selectedIndex = -1; // runtime, read-only through this DTO
    };

    struct UIWindowData
    {
        std::string title = "Window";
        bool showTitleBar = true;
        float titleBarHeight = 28.0f;
        bool draggable = true;
        bool closable = true;
        bool modal = false;
        glm::vec4 backgroundColor{0.12f, 0.12f, 0.12f, 1.0f};
        glm::vec4 titleBarColor{0.18f, 0.18f, 0.22f, 1.0f};
        glm::vec4 titleTextColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 backdropColor{0.0f, 0.0f, 0.0f, 0.55f};
        asset::AssetRef fontRef;
        float titleFontSize = 16.0f;
    };

    struct UIButtonData
    {
        // Per-state colors
        glm::vec4 normalColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 hoveredColor{0.9f, 0.9f, 0.9f, 1.0f};
        glm::vec4 pressedColor{0.7f, 0.7f, 0.7f, 1.0f};
        glm::vec4 disabledColor{0.5f, 0.5f, 0.5f, 0.5f};

        // Per-state textures (empty = use color only)
        asset::AssetRef normalTextureRef;
        asset::AssetRef hoverTextureRef;
        asset::AssetRef pressedTextureRef;
        asset::AssetRef disabledTextureRef;

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
        asset::AssetRef fontRef;
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
        asset::AssetRef uncheckedTextureRef;
        asset::AssetRef checkedTextureRef;
        asset::AssetRef hoveredTextureRef;
        asset::AssetRef disabledTextureRef;

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
        asset::AssetRef iconRef; // empty = no icon
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
        asset::AssetRef fontRef;
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
        asset::AssetRef handleNormalTextureRef;
        asset::AssetRef handleHoveredTextureRef;
        asset::AssetRef handlePressedTextureRef;
        asset::AssetRef handleDisabledTextureRef;

        // Fill appearance
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        asset::AssetRef fillTextureRef;

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
        asset::AssetRef trackTextureRef;

        // Fill appearance
        glm::vec4 fillColor{0.3f, 0.5f, 0.8f, 1.0f};
        asset::AssetRef fillTextureRef;

        // Runtime
        float displayValue = 0.0f;
    };

    // ========== UI Mask ==========

    struct UIMaskData
    {
        asset::AssetRef maskTextureRef;
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

    struct DestructibleComponentData
    {
        float maxHealth = 100.0f;
        float destructionThreshold = 0.0f;
        asset::AssetRef fractureAssetRef;
        uint32_t fragmentCount = 0;
        components::DestructionMode mode = components::DestructionMode::OneShot;
        components::DamageType damageFilter = components::DamageType::Any;
        float fragmentMassTotal = 1.0f;
        float fragmentLifetime = 5.0f;

        float propagationRadius = 0.0f;
        float propagationDamage = 50.0f;

        asset::AssetRef onDamageVFX;
        asset::AssetRef onDestroyVFX;
        asset::AssetRef onDamageAudio;
        asset::AssetRef onDestroyAudio;
        asset::AssetRef fragmentCollisionAudio;
        asset::AssetRef damageDecalAlbedo;
        asset::AssetRef damageDecalNormal;
        float decalHalfExtents = 0.3f;
    };
}
