// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "VFXAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace core::api
{
    void VFXAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_vfx_play(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_play",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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
            }});

        // _native_vfx_stop(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_stop",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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
            }});

        // _native_vfx_reset(entityId) -> void
        interpreter->registerNativeFunction("_native_vfx_reset",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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
            }});

        // _native_vfx_isPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_isPlaying",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
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
            }});

        // _native_vfx_getLoop(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_getLoop",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
            }});

        // _native_vfx_setLoop(entityId, loop) -> void
        interpreter->registerNativeFunction("_native_vfx_setLoop",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
                if (value::isBool(args[1]))
                {
                    loop = value::asBool(args[1]);
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
            }});

        // _native_vfx_getPath(entityId) -> string
        interpreter->registerNativeFunction("_native_vfx_getPath",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
            }});

        // _native_vfx_setPath(entityId, path) -> void
        interpreter->registerNativeFunction("_native_vfx_setPath",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
            }});

        // _native_vfx_getAutoPlay(entityId) -> bool
        interpreter->registerNativeFunction("_native_vfx_getAutoPlay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
            }});

        // _native_vfx_setAutoPlay(entityId, autoPlay) -> void
        interpreter->registerNativeFunction("_native_vfx_setAutoPlay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
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
                if (value::isBool(args[1]))
                {
                    autoPlay = value::asBool(args[1]);
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
            }});

        // ============================================================
        // Instance-based natives: fire-and-forget effects at a world
        // position, no entity/VFXComponent required
        // ============================================================

        // _native_vfx_spawnAt(path, x, y, z [, loop]) -> int instanceId (0 on failure)
        // Non-looping spawns auto-destroy once finished; looping spawns must be
        // destroyed via _native_vfx_destroyInstance
        interpreter->registerNativeFunction("_native_vfx_spawnAt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                std::string path = extractString(args[0], "VFX.spawnAt");
                if (path.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                glm::vec3 position{
                    extractFloat(args[1]),
                    extractFloat(args[2]),
                    extractFloat(args[3])
                };

                bool loop = false;
                if (args.size() >= 5 && value::isBool(args[4]))
                {
                    loop = value::asBool(args[4]);
                }

                services::events::vfxruntime::CreateVFXInstanceCommand createCmd;
                createCmd.params.vfxAssetPath = path;
                createCmd.params.worldTransform = glm::translate(glm::mat4(1.0f), position);
                createCmd.params.loop = loop;
                createCmd.params.autoDestroy = !loop;

                auto& dispatcher = events::EventDispatcher::instance();
                services::VFXInstanceId instanceId = dispatcher.execute(createCmd);
                if (instanceId != 0)
                {
                    services::events::vfxruntime::PlayVFXInstanceCommand playCmd;
                    playCmd.instanceId = instanceId;
                    dispatcher.execute(playCmd);
                }

                return value::Value(static_cast<int64_t>(instanceId));
            }});

        // _native_vfx_destroyInstance(instanceId) -> void
        interpreter->registerNativeFunction("_native_vfx_destroyInstance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::DestroyVFXInstanceCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                events::EventDispatcher::instance().execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_vfx_stopInstance(instanceId) -> void
        interpreter->registerNativeFunction("_native_vfx_stopInstance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::StopVFXInstanceCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                events::EventDispatcher::instance().execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_vfx_setInstancePosition(instanceId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_vfx_setInstancePosition",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                {
                    return value::Value(std::monostate{});
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(std::monostate{});
                }

                glm::vec3 position{
                    extractFloat(args[1]),
                    extractFloat(args[2]),
                    extractFloat(args[3])
                };

                services::events::vfxruntime::SetVFXInstanceTransformCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                cmd.worldTransform = glm::translate(glm::mat4(1.0f), position);
                events::EventDispatcher::instance().execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_vfx_attachToSocket(instanceId, parentEntityId, socketName) -> void
        // Attach a spawned instance to an entity socket. The instance follows the
        // socket's world transform each frame; auto-clears if the entity/socket
        // becomes invalid or the instance is destroyed.
        interpreter->registerNativeFunction("_native_vfx_attachToSocket",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3)
                {
                    return value::Value(std::monostate{});
                }
                int64_t instanceId = extractInt64(args[0]);
                int64_t parentEntityId = extractInt64(args[1]);
                if (instanceId <= 0 || parentEntityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                std::string socketName = extractString(args[2], "VFX.attachToSocket");
                if (socketName.empty())
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::AttachVFXInstanceToSocketCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                cmd.entityHandle = services::EntityHandle{
                    static_cast<uint64_t>(parentEntityId)
                }.id;
                cmd.socketName = std::move(socketName);
                events::EventDispatcher::instance().execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_vfx_detach(instanceId) -> void
        interpreter->registerNativeFunction("_native_vfx_detach",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxruntime::DetachVFXInstanceCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                events::EventDispatcher::instance().execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_vfx_setOverride(instanceId, name, value) -> bool
        // Scalar runtime overrides by name. Supported names:
        //   spawnRate, lifetime, startSize, startSpeed, stretchMultiplier,
        //   windStrength, gravityStrength, softParticleDistance,
        //   lightingInfluence, collisionLifetimeLoss, coneSpread,
        //   renderMode (int), collisionEnabled (0/1)
        interpreter->registerNativeFunction("_native_vfx_setOverride",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3)
                {
                    return value::Value(false);
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(false);
                }

                std::string name = extractString(args[1], "VFX.setOverride");
                float val = extractFloat(args[2]);

                services::events::vfxruntime::ApplyVFXInstanceOverridesCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);

                if (name == "spawnRate")                 cmd.overrides.spawnRate = val;
                else if (name == "lifetime")             cmd.overrides.lifetime = val;
                else if (name == "startSize")            cmd.overrides.startSize = val;
                else if (name == "startSpeed")           cmd.overrides.startSpeed = val;
                else if (name == "stretchMultiplier")    cmd.overrides.stretchMultiplier = val;
                else if (name == "windStrength")         cmd.overrides.windStrength = val;
                else if (name == "gravityStrength")      cmd.overrides.gravityStrength = val;
                else if (name == "softParticleDistance") cmd.overrides.softParticleDistance = val;
                else if (name == "lightingInfluence")    cmd.overrides.lightingInfluence = val;
                else if (name == "collisionLifetimeLoss") cmd.overrides.collisionLifetimeLoss = val;
                else if (name == "coneSpread")           cmd.overrides.coneSpread = val;
                else if (name == "renderMode")           cmd.overrides.renderMode = static_cast<int>(val);
                else if (name == "collisionEnabled")     cmd.overrides.collisionEnabled = (val != 0.0f);
                else
                {
                    return value::Value(false);
                }

                events::EventDispatcher::instance().execute(cmd);
                return value::Value(true);
            }});

        // _native_vfx_setOverrideVec(instanceId, name, x, y, z [, w]) -> bool
        // Vector runtime overrides by name. Supported names:
        //   emitDirection, windDirection, gravityDirection, shapeDimensions (vec3)
        //   startColor (vec4, w defaults to 1)
        interpreter->registerNativeFunction("_native_vfx_setOverrideVec",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5)
                {
                    return value::Value(false);
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(false);
                }

                std::string name = extractString(args[1], "VFX.setOverrideVec");
                glm::vec3 vec{
                    extractFloat(args[2]),
                    extractFloat(args[3]),
                    extractFloat(args[4])
                };
                float w = (args.size() >= 6) ? extractFloat(args[5]) : 1.0f;

                services::events::vfxruntime::ApplyVFXInstanceOverridesCommand cmd;
                cmd.instanceId = static_cast<services::VFXInstanceId>(instanceId);

                if (name == "emitDirection")          cmd.overrides.emitDirection = vec;
                else if (name == "windDirection")     cmd.overrides.windDirection = vec;
                else if (name == "gravityDirection")  cmd.overrides.gravityDirection = vec;
                else if (name == "shapeDimensions")   cmd.overrides.shapeDimensions = vec;
                else if (name == "startColor")        cmd.overrides.startColor = glm::vec4(vec, w);
                else
                {
                    return value::Value(false);
                }

                events::EventDispatcher::instance().execute(cmd);
                return value::Value(true);
            }});

        // _native_vfx_instanceIsPlaying(instanceId) -> bool
        interpreter->registerNativeFunction("_native_vfx_instanceIsPlaying",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t instanceId = extractInt64(args[0]);
                if (instanceId <= 0)
                {
                    return value::Value(false);
                }

                services::events::vfxruntime::IsVFXInstancePlayingQuery query;
                query.instanceId = static_cast<services::VFXInstanceId>(instanceId);
                return value::Value(events::EventDispatcher::instance().query(query));
            }});
    }
}
