// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "VFXAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"

namespace core::api
{
    void VFXAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_vfx_play(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_play",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                if (vfxComp.runtimeInstanceId == 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::PlayVFXInstanceCommand cmd;
                cmd.instanceId = vfxComp.runtimeInstanceId;
                dispatcher.execute(cmd);
                vfxComp.isPlaying = true;

                return value::Value(std::monostate{});
            });

        // _native_vfx_stop(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_stop",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                if (vfxComp.runtimeInstanceId == 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::StopVFXInstanceCommand cmd;
                cmd.instanceId = vfxComp.runtimeInstanceId;
                dispatcher.execute(cmd);
                vfxComp.isPlaying = false;

                return value::Value(std::monostate{});
            });

        // _native_vfx_reset(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_reset",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                if (vfxComp.runtimeInstanceId == 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::ResetVFXInstanceCommand cmd;
                cmd.instanceId = vfxComp.runtimeInstanceId;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_vfx_isPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_isPlaying",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(false);
                }

                const auto& vfxComp = registry.get<components::VFXComponent>(entity);
                if (vfxComp.runtimeInstanceId == 0)
                {
                    return value::Value(false);
                }

                services::events::vfxruntime::IsVFXInstancePlayingQuery query;
                query.instanceId = vfxComp.runtimeInstanceId;
                return value::Value(dispatcher.query(query));
            });

        // _native_vfx_getLoop(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_getLoop",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(false);
                }

                const auto& vfxComp = registry.get<components::VFXComponent>(entity);
                return value::Value(vfxComp.loop);
            });

        // _native_vfx_setLoop(entityId, loop) -> void
        interpreter->registerNativeFunction("_native_vfx_setLoop",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                vfxComp.loop = loop;

                return value::Value(std::monostate{});
            });

        // _native_vfx_getPath(entityId) -> string
        interpreter->registerNativeFunction("_native_vfx_getPath",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::string(""));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::string(""));
                }

                const auto& vfxComp = registry.get<components::VFXComponent>(entity);
                return value::Value(vfxComp.vfxRef.resolve());
            });

        // _native_vfx_setPath(entityId, path) -> void
        interpreter->registerNativeFunction("_native_vfx_setPath",
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

                std::string path = extractString(args[1], "VFX.setPath");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                vfxComp.vfxRef = asset::AssetRef::fromPath(path);

                return value::Value(std::monostate{});
            });

        // _native_vfx_getAutoPlay(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_getAutoPlay",
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
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(false);
                }

                const auto& vfxComp = registry.get<components::VFXComponent>(entity);
                return value::Value(vfxComp.autoPlay);
            });

        // _native_vfx_setAutoPlay(entityId, autoPlay) -> void
        interpreter->registerNativeFunction("_native_vfx_setAutoPlay",
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

                bool autoPlay = false;
                if (std::holds_alternative<bool>(args[1]))
                {
                    autoPlay = std::get<bool>(args[1]);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::VFXComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& vfxComp = registry.get<components::VFXComponent>(entity);
                vfxComp.autoPlay = autoPlay;

                return value::Value(std::monostate{});
            });
    }
}
