// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "AudioAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/audio/AudioEvents.hpp"
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
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(static_cast<int64_t>(0));

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::AudioSource2DComponent>(*entity))
                        return value::Value(static_cast<int64_t>(0));

                    auto& audioComp = registry.get<components::AudioSource2DComponent>(*entity);
                    if (audioComp.audioFilePath.empty())
                        return value::Value(static_cast<int64_t>(0));

                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::StopSoundCommand stopCmd;
                        stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(stopCmd);
                    }

                    events::audio::PlayStreamingSoundCommand cmd;
                    cmd.path = audioComp.audioFilePath;
                    cmd.params.volume = audioComp.volume;
                    cmd.params.pitch = audioComp.pitch;
                    cmd.params.loop = audioComp.loop;
                    cmd.params.is3D = false;
                    auto handle = dispatcher.execute(cmd);

                    audioComp.activeHandle = handle.id;
                    audioComp.isPlaying = true;
                    return value::Value(static_cast<int64_t>(handle.id));
                });

            interpreter->registerNativeFunction("_native_audio_play3d",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(static_cast<int64_t>(0));

                    auto& registry = scene::EntityRegistry::getRegistry();
                    if (!registry.all_of<components::AudioSource3DComponent>(*entity))
                        return value::Value(static_cast<int64_t>(0));

                    auto& audioComp = registry.get<components::AudioSource3DComponent>(*entity);
                    if (audioComp.audioFilePath.empty())
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
                    cmd.path = audioComp.audioFilePath;
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

                    auto handle = dispatcher.execute(cmd);

                    audioComp.activeHandle = handle.id;
                    audioComp.isPlaying = true;
                    return value::Value(static_cast<int64_t>(handle.id));
                });

            interpreter->registerNativeFunction("_native_audio_stop",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });

            interpreter->registerNativeFunction("_native_audio_pause",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });

            interpreter->registerNativeFunction("_native_audio_resume",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });

            interpreter->registerNativeFunction("_native_audio_isPlaying",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });
        }

        void registerPropertyFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_audio_setVolume",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });

            interpreter->registerNativeFunction("_native_audio_setPitch",
                [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                {
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
                });

            interpreter->registerNativeFunction("_native_audio_getVolume",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.volume);
                    }, value::Value(1.0f));
                });

            interpreter->registerNativeFunction("_native_audio_getPitch",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(1.0f);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(1.0f);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.pitch);
                    }, value::Value(1.0f));
                });

            interpreter->registerNativeFunction("_native_audio_setLoop",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.size() < 2) return value::Value(std::monostate{});
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(std::monostate{});

                    bool loop = extractBool(args[1]);
                    forEachAudioComp(*entity, [loop](auto& audioComp)
                    {
                        audioComp.loop = loop;
                    });
                    return value::Value(std::monostate{});
                });

            interpreter->registerNativeFunction("_native_audio_getLoop",
                [](const std::vector<value::Value>& args) -> value::Value
                {
                    if (args.empty()) return value::Value(false);
                    auto entity = resolveEntity(args[0]);
                    if (!entity) return value::Value(false);

                    return fromFirstAudioComp(*entity, [](auto& audioComp) -> value::Value
                    {
                        return value::Value(audioComp.loop);
                    }, value::Value(false));
                });
        }
    }

    void AudioAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerPlaybackFunctions(interpreter, dispatcher);
        registerPropertyFunctions(interpreter, dispatcher);
    }
}
