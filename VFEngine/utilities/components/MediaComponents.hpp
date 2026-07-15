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
#include "../types/AudioVariationTypes.hpp"

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

        // VK-1513: arbitration weight when the scene's real-voice budget is full.
        // LOWER = MORE IMPORTANT (0 = critical, 255 = least, 128 = neutral), matching
        // VFXComponent::priority and Unity. Note ReverbZoneComponent::priority below uses
        // the opposite convention — that one is zone override, not budget eviction.
        uint8_t priority = 128;

        // VK-1521: ramp up from silence over this many ms when the sound starts. 0 = no fade,
        // so existing scenes are unchanged. This component plays through the STREAMING path
        // (_native_audio_play2d dispatches PlayStreamingSoundCommand), so it is the streaming
        // ramp that makes this field do anything.
        float fadeInMs = 0.0f;

        // VK-1520: variation container. The playable pool is [audioRef] ++ the
        // valid entries of clipVariants, so audioRef IS variant 0 and stays in
        // the rotation. Defaults (Single + zero variation) make playback
        // byte-identical to pre-VK-1520 for every existing scene.
        std::vector<asset::AssetRef> clipVariants;
        types::AudioPlayOrder playOrder = types::AudioPlayOrder::Single;
        float pitchVariation = 0.0f;  // +/- fraction of the authored pitch; 0 = inert
        float volumeVariation = 0.0f; // +/- fraction of the authored volume; 0 = inert

        uint64_t activeHandle = 0;
        bool isPlaying = false;
        // VK-1520 runtime state, like activeHandle/isPlaying above: never
        // serialized, never in the DTO. lastVariant drives no-immediate-repeat;
        // playCount is the per-play seed entropy (seeding from lastVariant alone
        // would collapse the sequence into a fixed cycle).
        uint8_t lastVariant = types::AUDIO_VARIANT_NONE;
        uint32_t playCount = 0;
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

        // VK-1518: geometry occlusion — a listener->emitter raycast muffles this source when
        // level geometry blocks it. Opt-in, so no existing scene changes behaviour.
        // Both floats are CUT AMOUNTS at full occlusion (0 = inert, 1 = full cut), matching
        // filterIntensity above. occlusionLpf drives AL_LOWPASS_GAINHF and occlusionVolume
        // drives AL_LOWPASS_GAIN on the direct path — deliberately NOT AL_GAIN, which
        // already carries volume * bus * fade.
        bool enableOcclusion = false;
        float occlusionLpf = 0.7f;     // cut 70% of the highs when fully occluded
        float occlusionVolume = 0.3f;  // cut 30% of the volume when fully occluded
        // The "trace channel": which collision layers count as blocking. Static|Kinematic by
        // default, so walls and moving doors occlude but trigger volumes and the player's own
        // Dynamic capsule do not (the raycast has no sensor filter — it matches on layer
        // only). Bit N = layer N; indices per types::PhysicsSettings::createDefault():
        // Static 0, Dynamic 1, Kinematic 2, Sensor 3. Kept as a literal rather than an
        // include — this header must not depend on PhysicsTypes.hpp.
        uint16_t occlusionLayerMask = 0x0005;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        bool showDebugCone = false;

        std::string busName = "SFX";

        // VK-1513: see AudioSource2DComponent::priority. Lower = more important.
        uint8_t priority = 128;

        // VK-1521: see AudioSource2DComponent::fadeInMs. 0 = no fade. This component plays
        // through the pooled path (_native_audio_play3d dispatches PlaySound3DCommand).
        float fadeInMs = 0.0f;

        // VK-1520: see AudioSource2DComponent's variation block. Pool is
        // [audioRef] ++ valid(clipVariants); audioRef is variant 0.
        std::vector<asset::AssetRef> clipVariants;
        types::AudioPlayOrder playOrder = types::AudioPlayOrder::Single;
        float pitchVariation = 0.0f;
        float volumeVariation = 0.0f;

        uint64_t activeHandle = 0;
        bool isPlaying = false;
        // VK-1520 runtime state — not serialized, not in the DTO.
        uint8_t lastVariant = types::AUDIO_VARIANT_NONE;
        uint32_t playCount = 0;
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
        // Classifies the socket parent so the per-frame update can skip cache lookups
        // for static-mesh parents (whose model-space socket offset never changes). VK-1432.
        enum class ParentKind : uint8_t { Unknown = 0, Skinned, Static };

        entt::entity parentEntity = entt::null;
        std::string parentEntityName; // For persistence across scene/prefab loads
        std::string socketName;
        int32_t cachedSocketIndex = -1;
        bool isActive = true;
        bool needsParentResolution = false; // Set true on deserialization/mode change
        // Transient runtime cache (NOT serialized) — resolved lazily on the cold path,
        // reset alongside cachedSocketIndex whenever the parent/socket must re-resolve.
        ParentKind parentKind = ParentKind::Unknown;
        glm::mat4 cachedStaticSocketOffset = glm::mat4(1.0f); // constant model-space offset (Static only)
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
