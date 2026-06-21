#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <entt/entt.hpp>
#include "../../services/data/ScriptTypes.hpp"
#include "../asset/AssetRef.hpp"
#include "../animator/SocketTypes.hpp"
#include "../types/AudioEffectTypes.hpp"

namespace components
{
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
        ReverbZone,
        FogVolume,
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

        asset::AssetRef textureRef; // .vfImage asset (invalid = use atlas icon)

        entt::entity renderTextureSource = entt::null;
        std::string renderTextureSourceName;

        // --- Animation (Phase 1). Defaults below render identically to a static billboard. ---
        uint32_t flipbookColumns = 1;   // sprite-sheet columns (1 = no flipbook)
        uint32_t flipbookRows = 1;      // sprite-sheet rows (1 = no flipbook)
        float flipbookFrameRate = 0.0f; // frames/sec (<=0 = no flipbook advance)
        float scrollU = 0.0f;           // UV scroll speed along U (units/sec)
        float scrollV = 0.0f;           // UV scroll speed along V (units/sec)
        float pulseAmplitude = 0.0f;    // scale-throb amplitude (0 = no pulse)
        float pulseFrequency = 0.0f;    // scale-throb frequency (rad/sec)
        float spinSpeed = 0.0f;         // spin speed about view normal (rad/sec)
        float animStartTime = 0.0f;     // animation time origin (engine seconds)
        bool loopAnimation = true;      // flipbook: true=loop (mod), false=play once then hold last frame
        bool worldMarker = false;       // Phase 2: route through GPU billboard path

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
            case BillboardIconType::ReverbZone: return 9;
            case BillboardIconType::FogVolume: return 10;
            default: return atlasIndex;
            }
        }
    };

    struct AudioSource2DComponent
    {
        asset::AssetRef audioRef;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;

        std::string busName = "Music";

        uint64_t activeHandle = 0;
        bool isPlaying = false;
    };

    struct AudioSource3DComponent
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

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        bool showDebugCone = false;

        std::string busName = "SFX";

        uint64_t activeHandle = 0;
        bool isPlaying = false;
    };

    struct ScriptEntry
    {
        asset::AssetRef scriptRef;
        std::string scriptPath;
        bool enabled = true;
        int inputPriority = 0;

        bool started = false;
        uint64_t instanceId = 0;

        services::ScriptPlaybackState playbackState = services::ScriptPlaybackState::Stopped;
    };

    struct ScriptComponent
    {
        std::vector<ScriptEntry> scripts;

        ScriptEntry* findByRef(const asset::AssetRef& ref)
        {
            for (auto& entry : scripts)
            {
                if (entry.scriptRef == ref) return &entry;
            }
            return nullptr;
        }

        const ScriptEntry* findByRef(const asset::AssetRef& ref) const
        {
            for (const auto& entry : scripts)
            {
                if (entry.scriptRef == ref) return &entry;
            }
            return nullptr;
        }

        bool hasScript(const asset::AssetRef& ref) const
        {
            return findByRef(ref) != nullptr;
        }

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

        bool hasScriptPath(const std::string& path) const
        {
            return findByPath(path) != nullptr;
        }

        bool removeByRef(const asset::AssetRef& ref)
        {
            auto it = std::remove_if(scripts.begin(), scripts.end(),
                                     [&ref](const ScriptEntry& e) { return e.scriptRef == ref; });
            if (it != scripts.end())
            {
                scripts.erase(it, scripts.end());
                return true;
            }
            return false;
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
        asset::AssetRef animatorRef;
        bool isInitialized = false;
        bool applyRootMotion = false;
    };

    struct VFXComponent
    {
        asset::AssetRef vfxRef;
        bool autoPlay = true;
        bool loop = true;
        uint8_t priority = 2; // 0=Critical, 1=High, 2=Normal, 3=Low
        bool cameraRelative = false;

        uint32_t runtimeInstanceId = 0;
        bool isPlaying = false;
    };

    struct VFXSequenceTrigger
    {
        std::string eventName;       // animation notify event name
        asset::AssetRef sequenceRef; // .vfVFXSequence to play when that event fires
        std::string socketName;      // optional attach socket
    };

    struct VFXSequenceComponent
    {
        asset::AssetRef sequenceRef; // standalone autoplay sequence
        bool autoPlay = false;
        bool loop = false;
        std::string socketName; // optional attach for the standalone combo
        std::vector<VFXSequenceTrigger> triggers;
        uint32_t runtimeComboId = 0; // transient — NOT serialized
    };

    struct SocketAttachmentComponent
    {
        entt::entity parentEntity = entt::null;
        std::string parentEntityName; // For persistence across scene/prefab loads
        std::string socketName;
        int32_t cachedSocketIndex = -1;
        bool isActive = true;
        bool needsParentResolution = false; // Set true on deserialization/mode change
    };

    struct SocketOverrideComponent
    {
        std::vector<animator::SocketDefinition> additionalSockets;
        std::vector<animator::SocketDefinition> overriddenSockets;
    };

    struct BehaviorTreeComponent
    {
        asset::AssetRef behaviorTreeRef;
        bool isInitialized = false;
        bool enabled = true;
    };

    enum class ReverbZoneShape : uint8_t
    {
        Sphere = 0,
        Box = 1
    };

    struct ReverbZoneComponent
    {
        ReverbZoneShape shape = ReverbZoneShape::Sphere;
        float radius = 10.0f;
        glm::vec3 halfExtents{5.0f};

        std::string presetName = "Generic";
        types::ReverbParams customParams;

        int priority = 0;
        float falloffDistance = 2.0f;
        float wetLevel = 1.0f;
        bool showDebugVolume = false;

        // Runtime (not serialized)
        bool isListenerInside = false;
        float currentBlendWeight = 0.0f;
    };
}
