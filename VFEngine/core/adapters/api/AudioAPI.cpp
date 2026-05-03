// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "AudioAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/audio/AudioEvents.hpp"
#include "../../../services/events/audio/AudioBusEvents.hpp"
#include "../../../services/events/audio/AudioEffectEvents.hpp"
#include "types/AudioEffectTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    namespace
    {
        template<typename Func>
        void forEachAudioComp(entt::entity entity, Func&& func)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::AudioSource2DComponent>(entity))
            {
                func(registry.get<components::AudioSource2DComponent>(entity));
            }
            if (registry.all_of<components::AudioSource3DComponent>(entity))
            {
                func(registry.get<components::AudioSource3DComponent>(entity));
            }
        }

        template<typename Func>
        value::Value fromFirstAudioComp(entt::entity entity, Func&& func,
                                        const value::Value& defaultVal)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.all_of<components::AudioSource2DComponent>(entity))
            {
                return func(registry.get<components::AudioSource2DComponent>(entity));
            }
            if (registry.all_of<components::AudioSource3DComponent>(entity))
            {
                return func(registry.get<components::AudioSource3DComponent>(entity));
            }
            return defaultVal;
        }

        void registerPlaybackFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_audio_play2d",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(static_cast<int64_t>(0));

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::AudioSource2DComponent>(*entity))
                        return value::Value(static_cast<int64_t>(0));

                    auto& audioComp = registry.get<components::AudioSource2DComponent>(*entity);
                    if (!audioComp.audioRef.isValid())
                        return value::Value(static_cast<int64_t>(0));

                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::StopSoundCommand stopCmd;
                        stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(stopCmd);
                    }

                    events::audio::PlayStreamingSoundCommand cmd;
                    cmd.path = audioComp.audioRef.resolve();
                    cmd.params.volume = audioComp.volume;
                    cmd.params.pitch = audioComp.pitch;
                    cmd.params.loop = audioComp.loop;
                    cmd.params.is3D = false;
                    cmd.params.busName = audioComp.busName;
                    auto handle = dispatcher.execute(cmd);

                    audioComp.activeHandle = handle.id;
                    audioComp.isPlaying = true;
                    return value::Value(static_cast<int64_t>(handle.id));
                }});

            interpreter->registerNativeFunction("_native_audio_play3d",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(static_cast<int64_t>(0));

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::AudioSource3DComponent>(*entity))
                        return value::Value(static_cast<int64_t>(0));

                    auto& audioComp = registry.get<components::AudioSource3DComponent>(*entity);
                    if (!audioComp.audioRef.isValid())
                        return value::Value(static_cast<int64_t>(0));

                    glm::vec3 position(0.0f);
                    if (registry.all_of<components::WorldTransformComponent>(*entity))
                    {
                        auto& wt = registry.get<components::WorldTransformComponent>(*entity);
                        position = glm::vec3(wt.worldMatrix[3]);
                    }

                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::StopSoundCommand stopCmd;
                        stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(stopCmd);
                    }

                    events::audio::PlaySound3DCommand cmd;
                    cmd.path = audioComp.audioRef.resolve();
                    cmd.position = position;
                    cmd.params.volume = audioComp.volume;
                    cmd.params.pitch = audioComp.pitch;
                    cmd.params.loop = audioComp.loop;
                    cmd.params.is3D = true;
                    cmd.params.minDistance = audioComp.minDistance;
                    cmd.params.maxDistance = audioComp.maxDistance;
                    cmd.params.enableDistanceFilter = audioComp.enableDistanceFilter;
                    cmd.params.filterStartDistance = audioComp.filterStartDistance;
                    cmd.params.filterMaxDistance = audioComp.filterMaxDistance;
                    cmd.params.filterIntensity = audioComp.filterIntensity;
                    cmd.params.innerConeAngle = audioComp.innerConeAngle;
                    cmd.params.outerConeAngle = audioComp.outerConeAngle;
                    cmd.params.outerConeGain = audioComp.outerConeGain;

                    if (registry.all_of<components::TransformComponent>(*entity))
                    {
                        auto& transform = registry.get<components::TransformComponent>(*entity);
                        float yawRad = glm::radians(transform.rotation.y);
                        float pitchRad = glm::radians(transform.rotation.x);
                        glm::vec3 forward;
                        forward.x = -std::sin(yawRad) * std::cos(pitchRad);
                        forward.y = std::sin(pitchRad);
                        forward.z = -std::cos(yawRad) * std::cos(pitchRad);
                        cmd.params.direction = glm::normalize(forward);
                    }

                    cmd.params.busName = audioComp.busName;
                    auto handle = dispatcher.execute(cmd);

                    audioComp.activeHandle = handle.id;
                    audioComp.isPlaying = true;
                    return value::Value(static_cast<int64_t>(handle.id));
                }});

            interpreter->registerNativeFunction("_native_audio_stop",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    forEachAudioComp(*entity, [&](auto& audioComp)
                    {
                        if (audioComp.activeHandle != 0)
                        {
                            events::audio::StopSoundCommand cmd;
                            cmd.handle = services::AudioHandle{audioComp.activeHandle};
                            dispatcher.execute(cmd);
                            audioComp.activeHandle = 0;
                            audioComp.isPlaying = false;
                        }
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_pause",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    forEachAudioComp(*entity, [&](auto& audioComp)
                    {
                        if (audioComp.activeHandle != 0)
                        {
                            events::audio::PauseSoundCommand cmd;
                            cmd.handle = services::AudioHandle{audioComp.activeHandle};
                            dispatcher.execute(cmd);
                        }
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_resume",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    forEachAudioComp(*entity, [&](auto& audioComp)
                    {
                        if (audioComp.activeHandle != 0)
                        {
                            events::audio::ResumeSoundCommand cmd;
                            cmd.handle = services::AudioHandle{audioComp.activeHandle};
                            dispatcher.execute(cmd);
                        }
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_isPlaying",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    auto checkPlaying = [&](uint64_t handle) -> bool
                    {
                        if (handle == 0) return false;
                        events::audio::IsSoundPlayingQuery query;
                        query.handle = services::AudioHandle{handle};
                        return dispatcher.query(query);
                    };

                    if (registry.all_of<components::AudioSource2DComponent>(*entity))
                    {
                        if (checkPlaying(registry.get<components::AudioSource2DComponent>(
                            *entity).activeHandle))
                            return value::Value(true);
                    }
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        if (checkPlaying(registry.get<components::AudioSource3DComponent>(
                            *entity).activeHandle))
                            return value::Value(true);
                    }
                    return value::Value(false);
                }});
        }

        void registerPropertyFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_audio_setVolume",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    float volume = extractFloat(args[1]);
                    forEachAudioComp(*entity, [&](auto& audioComp)
                    {
                        audioComp.volume = volume;
                        if (audioComp.activeHandle != 0)
                        {
                            events::audio::SetSoundVolumeCommand cmd;
                            cmd.handle = services::AudioHandle{audioComp.activeHandle};
                            cmd.volume = volume;
                            dispatcher.execute(cmd);
                        }
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_setPitch",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    float pitch = extractFloat(args[1]);
                    forEachAudioComp(*entity, [&](auto& audioComp)
                    {
                        audioComp.pitch = pitch;
                        if (audioComp.activeHandle != 0)
                        {
                            events::audio::SetSoundPitchCommand cmd;
                            cmd.handle = services::AudioHandle{audioComp.activeHandle};
                            cmd.pitch = pitch;
                            dispatcher.execute(cmd);
                        }
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getVolume",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.volume);
                    }, value::Value(1.0f));
                }});

            interpreter->registerNativeFunction("_native_audio_getPitch",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.pitch);
                    }, value::Value(1.0f));
                }});

            interpreter->registerNativeFunction("_native_audio_setLoop",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    bool loop = extractBool(args[1]);
                    forEachAudioComp(*entity, [loop](auto& audioComp)
                    {
                        audioComp.loop = loop;
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getLoop",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.loop);
                    }, value::Value(false));
                }});
        }

        void registerSpatialPropertyFunctions(services::ScriptInterpreter* interpreter,
                                              events::EventDispatcher& dispatcher)
        {
            // === Distance Filter ===
            interpreter->registerNativeFunction("_native_audio_setDistanceFilter",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.size() < 5) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    bool enabled = extractBool(args[1]);
                    float startDist = extractFloat(args[2]);
                    float maxDist = extractFloat(args[3]);
                    float intensity = extractFloat(args[4]);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        auto& comp = registry.get<components::AudioSource3DComponent>(*entity);
                        comp.enableDistanceFilter = enabled;
                        comp.filterStartDistance = startDist;
                        comp.filterMaxDistance = maxDist;
                        comp.filterIntensity = intensity;
                    }
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getDistanceFilterEnabled",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        return value::Value(registry.get<components::AudioSource3DComponent>(*entity).enableDistanceFilter);
                    }
                    return value::Value(false);
                }});

            interpreter->registerNativeFunction("_native_audio_getFilterIntensity",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        return value::Value(registry.get<components::AudioSource3DComponent>(*entity).filterIntensity);
                    }
                    return value::Value(1.0f);
                }});

            // === Cone Attenuation ===
            interpreter->registerNativeFunction("_native_audio_setConeAngles",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.size() < 3) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    float inner = extractFloat(args[1]);
                    float outer = extractFloat(args[2]);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        auto& comp = registry.get<components::AudioSource3DComponent>(*entity);
                        comp.innerConeAngle = inner;
                        comp.outerConeAngle = outer;
                    }
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getConeInnerAngle",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(360.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(360.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        return value::Value(registry.get<components::AudioSource3DComponent>(*entity).innerConeAngle);
                    }
                    return value::Value(360.0f);
                }});

            interpreter->registerNativeFunction("_native_audio_getConeOuterAngle",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(360.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(360.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        return value::Value(registry.get<components::AudioSource3DComponent>(*entity).outerConeAngle);
                    }
                    return value::Value(360.0f);
                }});

            interpreter->registerNativeFunction("_native_audio_setConeOuterGain",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    float gain = extractFloat(args[1]);
                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        registry.get<components::AudioSource3DComponent>(*entity).outerConeGain = gain;
                    }
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getConeOuterGain",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(0.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(0.0f);

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (registry.all_of<components::AudioSource3DComponent>(*entity))
                    {
                        return value::Value(registry.get<components::AudioSource3DComponent>(*entity).outerConeGain);
                    }
                    return value::Value(0.0f);
                }});

            // === Bus Assignment ===
            interpreter->registerNativeFunction("_native_audio_setBus",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    std::string busName = extractString(args[1]);
                    forEachAudioComp(*entity, [&busName](auto& audioComp)
                    {
                        audioComp.busName = busName;
                    });
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getBus",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    if (args.empty()) return value::Value(std::string("Master"));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::string("Master"));

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.busName);
                    }, value::Value(std::string("Master")));
                }});
        }

        void registerEffectFunctions(services::ScriptInterpreter* interpreter,
                                     events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_audio_addBusEffect",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(static_cast<int64_t>(0));
                    std::string busName = extractString(args[0]);
                    std::string effectType = extractString(args[1]);

                    auto type = types::stringToAudioEffectType(effectType);
                    auto config = types::BusEffectConfig::createDefault(type);

                    events::audio::AddBusEffectCommand cmd;
                    cmd.busName = busName;
                    cmd.config = config;
                    bool result = dispatcher.execute(cmd);
                    return value::Value(static_cast<int64_t>(result ? config.id : 0));
                }});

            interpreter->registerNativeFunction("_native_audio_removeBusEffect",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    auto effectId = static_cast<uint32_t>(extractInt64(args[1]));

                    events::audio::RemoveBusEffectCommand cmd;
                    cmd.busName = busName;
                    cmd.effectId = effectId;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_setBusEffectEnabled",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    auto effectId = static_cast<uint32_t>(extractInt64(args[1]));
                    bool enabled = extractBool(args[2]);

                    events::audio::SetBusEffectEnabledCommand cmd;
                    cmd.busName = busName;
                    cmd.effectId = effectId;
                    cmd.enabled = enabled;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_setBusEffectWetDry",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    auto effectId = static_cast<uint32_t>(extractInt64(args[1]));
                    float mix = extractFloat(args[2]);

                    events::audio::SetBusEffectWetDryCommand cmd;
                    cmd.busName = busName;
                    cmd.effectId = effectId;
                    cmd.wetDryMix = mix;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_setReverbPreset",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    auto effectId = static_cast<uint32_t>(extractInt64(args[1]));
                    std::string presetName = extractString(args[2]);

                    types::BusEffectConfig config;
                    config.id = effectId;
                    config.type = types::AudioEffectType::Reverb;
                    types::ReverbParams params;
                    params.presetName = presetName;
                    config.params = params;

                    events::audio::UpdateBusEffectCommand cmd;
                    cmd.busName = busName;
                    cmd.effectId = effectId;
                    cmd.config = config;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});
        }

        void registerBusFunctions(services::ScriptInterpreter* interpreter,
                                  events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_audio_setBusVolume",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    float volume = extractFloat(args[1]);

                    events::audio::SetBusVolumeCommand cmd;
                    cmd.busName = busName;
                    cmd.volume = volume;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_getBusVolume",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(1.0f);
                    std::string busName = extractString(args[0]);

                    events::audio::GetBusVolumeQuery query;
                    query.busName = busName;
                    return value::Value(dispatcher.query(query));
                }});

            interpreter->registerNativeFunction("_native_audio_muteBus",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value(std::monostate{});
                    std::string busName = extractString(args[0]);
                    bool muted = extractBool(args[1]);

                    events::audio::SetBusMutedCommand cmd;
                    cmd.busName = busName;
                    cmd.muted = muted;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_saveSnapshot",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    std::string name = extractString(args[0]);

                    events::audio::SaveMixSnapshotCommand cmd;
                    cmd.name = name;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});

            interpreter->registerNativeFunction("_native_audio_loadSnapshot",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::monostate{});
                    std::string name = extractString(args[0]);

                    events::audio::LoadMixSnapshotCommand cmd;
                    cmd.name = name;
                    dispatcher.execute(cmd);
                    return value::Value(std::monostate{});
                }});
        }
    }

    void AudioAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerPlaybackFunctions(interpreter, dispatcher);
        registerPropertyFunctions(interpreter, dispatcher);
        registerSpatialPropertyFunctions(interpreter, dispatcher);
        registerBusFunctions(interpreter, dispatcher);
        registerEffectFunctions(interpreter, dispatcher);
    }
}
