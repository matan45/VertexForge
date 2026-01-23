// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "AudioAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/AudioEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/data/EntityConversion.hpp"

namespace core::api
{
    void AudioAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_audio_play2d(entityId) -> int64 (audio handle, 0 if failed)
        interpreter->registerNativeFunction("_native_audio_play2d",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::AudioSource2DComponent>(entity))
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& audioComp = registry.get<components::AudioSource2DComponent>(
                    entity);
                if (audioComp.audioFilePath.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                // Stop existing playback if any
                if (audioComp.activeHandle != 0)
                {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                    dispatcher.execute(stopCmd);
                }

                // Play streaming sound (2D audio uses streaming)
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

        // _native_audio_play3d(entityId) -> int64 (audio handle, 0 if failed)
        interpreter->registerNativeFunction("_native_audio_play3d",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::AudioSource3DComponent>(entity))
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& audioComp = registry.get<components::AudioSource3DComponent>(
                    entity);
                if (audioComp.audioFilePath.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                // Get position from transform
                glm::vec3 position(0.0f);
                if (registry.all_of<components::WorldTransformComponent>(entity))
                {
                    auto& worldTransform = registry.get<
                        components::WorldTransformComponent>(entity);
                    position = glm::vec3(worldTransform.worldMatrix[3]);
                }

                // Stop existing playback if any
                if (audioComp.activeHandle != 0)
                {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                    dispatcher.execute(stopCmd);
                }

                // Play 3D sound
                events::audio::PlaySound3DCommand cmd;
                cmd.path = audioComp.audioFilePath;
                cmd.position = position;
                cmd.params.volume = audioComp.volume;
                cmd.params.pitch = audioComp.pitch;
                cmd.params.loop = audioComp.loop;
                cmd.params.is3D = true;
                cmd.params.minDistance = audioComp.minDistance;
                cmd.params.maxDistance = audioComp.maxDistance;
                auto handle = dispatcher.execute(cmd);

                audioComp.activeHandle = handle.id;
                audioComp.isPlaying = true;

                return value::Value(static_cast<int64_t>(handle.id));
            });

        // _native_audio_stop(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_stop",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::StopSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                        audioComp.activeHandle = 0;
                        audioComp.isPlaying = false;
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::StopSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                        audioComp.activeHandle = 0;
                        audioComp.isPlaying = false;
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_pause(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_pause",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::PauseSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::PauseSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_resume(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_resume",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::ResumeSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::ResumeSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_isPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_audio_isPlaying",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(false);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(false);
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::IsSoundPlayingQuery query;
                        query.handle = services::AudioHandle{audioComp.activeHandle};
                        return value::Value(dispatcher.query(query));
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::IsSoundPlayingQuery query;
                        query.handle = services::AudioHandle{audioComp.activeHandle};
                        return value::Value(dispatcher.query(query));
                    }
                }

                return value::Value(false);
            });

        // _native_audio_setVolume(entityId, volume) -> void
        interpreter->registerNativeFunction("_native_audio_setVolume",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float volume = extractFloat(args[1]);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                // Update component and live audio for 2D
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    audioComp.volume = volume;
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::SetSoundVolumeCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.volume = volume;
                        dispatcher.execute(cmd);
                    }
                }

                // Update component and live audio for 3D
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    audioComp.volume = volume;
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::SetSoundVolumeCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.volume = volume;
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_setPitch(entityId, pitch) -> void
        interpreter->registerNativeFunction("_native_audio_setPitch",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float pitch = extractFloat(args[1]);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                // Update component and live audio for 2D
                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(
                        entity);
                    audioComp.pitch = pitch;
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::SetSoundPitchCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.pitch = pitch;
                        dispatcher.execute(cmd);
                    }
                }

                // Update component and live audio for 3D
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(
                        entity);
                    audioComp.pitch = pitch;
                    if (audioComp.activeHandle != 0)
                    {
                        events::audio::SetSoundPitchCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.pitch = pitch;
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_getVolume(entityId) -> float
        interpreter->registerNativeFunction("_native_audio_getVolume",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(1.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(1.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(1.0f);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource2DComponent>(
                            entity).volume);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource3DComponent>(
                            entity).volume);
                }

                return value::Value(1.0f);
            });

        // _native_audio_getPitch(entityId) -> float
        interpreter->registerNativeFunction("_native_audio_getPitch",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(1.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(1.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(1.0f);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource2DComponent>(entity).pitch);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource3DComponent>(entity).pitch);
                }

                return value::Value(1.0f);
            });

        // _native_audio_setLoop(entityId, loop) -> void
        interpreter->registerNativeFunction("_native_audio_setLoop",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                bool loop = false;
                if (std::holds_alternative<bool>(args[1]))
                {
                    loop = std::get<bool>(args[1]);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(std::monostate{});
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    registry.get<components::AudioSource2DComponent>(entity).loop =
                        loop;
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    registry.get<components::AudioSource3DComponent>(entity).loop =
                        loop;
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_getLoop(entityId) -> bool
        interpreter->registerNativeFunction("_native_audio_getLoop",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(false);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity))
                {
                    return value::Value(false);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource2DComponent>(entity).loop);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity))
                {
                    return value::Value(
                        registry.get<components::AudioSource3DComponent>(entity).loop);
                }

                return value::Value(false);
            });
    }
}
