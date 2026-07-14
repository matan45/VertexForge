// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "VFXAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/vfx/VFXSequenceRuntimeEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include "data/VFXOverrideApplier.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace core::api
{
    namespace
    {
        bool isFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFiniteColor(float r, float g, float b, float a)
        {
            return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
        }

        uint32_t packRGBA8(float r, float g, float b, float a)
        {
            const auto toByte = [](float channel)
            {
                return static_cast<uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
            };

            return toByte(r) |
                   (toByte(g) << 8u) |
                   (toByte(b) << 16u) |
                   (toByte(a) << 24u);
        }

        bool tryGetChannelId(const value::Value& value, services::VFXInstanceId& outId)
        {
            const int64_t id = extractInt64(value);
            if (id <= 0 || static_cast<uint64_t>(id) > std::numeric_limits<services::VFXInstanceId>::max())
            {
                return false;
            }
            outId = static_cast<services::VFXInstanceId>(id);
            return true;
        }

        void submitChannelEmit(services::VFXInstanceId channelId,
                               const glm::vec3& position,
                               float scale,
                               const glm::vec3& direction,
                               uint32_t packedTint,
                               bool hasTint)
        {
            services::events::vfxruntime::EmitToVFXChannelCommand cmd;
            cmd.channelId = channelId;
            cmd.params.position = position;
            cmd.params.scale = scale;
            cmd.params.direction = direction;
            cmd.params.packedTint = packedTint;
            cmd.params.hasTint = hasTint;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

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

        // _native_vfx_createChannel(path) -> int channelId (0 on failure)
        interpreter->registerNativeFunction("_native_vfx_createChannel",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() != 1)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                const std::string path = extractString(args[0], "VFX.createChannel");
                if (path.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                services::events::vfxruntime::CreateVFXChannelCommand cmd;
                cmd.vfxAssetPath = path;
                const services::VFXInstanceId channelId =
                    events::EventDispatcher::instance().execute(cmd);
                return value::Value(static_cast<int64_t>(channelId));
            }});

        // _native_vfx_createChannelWithCount(path, particlesPerRequest) -> int channelId
        interpreter->registerNativeFunction("_native_vfx_createChannelWithCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() != 2)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                const std::string path = extractString(args[0], "VFX.createChannelWithCount");
                const int64_t count = extractInt64(args[1]);
                if (path.empty() || count <= 0 ||
                    static_cast<uint64_t>(count) > std::numeric_limits<uint32_t>::max())
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                services::events::vfxruntime::CreateVFXChannelCommand cmd;
                cmd.vfxAssetPath = path;
                cmd.particlesPerRequest = static_cast<uint32_t>(count);
                const services::VFXInstanceId channelId =
                    events::EventDispatcher::instance().execute(cmd);
                return value::Value(static_cast<int64_t>(channelId));
            }});

        // _native_vfx_channelEmit(channelId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_vfx_channelEmit",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                services::VFXInstanceId channelId = 0;
                if (args.size() != 4 || !tryGetChannelId(args[0], channelId))
                {
                    return value::Value(std::monostate{});
                }

                const glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};
                if (isFinite(position))
                {
                    submitChannelEmit(channelId, position, 1.0f, glm::vec3(0.0f), 0xFFFFFFFFu, false);
                }
                return value::Value(std::monostate{});
            }});

        // _native_vfx_channelEmitTinted(channelId, x, y, z, r, g, b, a) -> void
        interpreter->registerNativeFunction("_native_vfx_channelEmitTinted",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                services::VFXInstanceId channelId = 0;
                if (args.size() != 8 || !tryGetChannelId(args[0], channelId))
                {
                    return value::Value(std::monostate{});
                }

                const glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};
                const float r = extractFloat(args[4]);
                const float g = extractFloat(args[5]);
                const float b = extractFloat(args[6]);
                const float a = extractFloat(args[7]);
                if (isFinite(position) && isFiniteColor(r, g, b, a))
                {
                    submitChannelEmit(channelId, position, 1.0f, glm::vec3(0.0f),
                                      packRGBA8(r, g, b, a), true);
                }
                return value::Value(std::monostate{});
            }});

        // _native_vfx_channelEmitFull(channelId, x, y, z, scale, dx, dy, dz, r, g, b, a) -> void
        interpreter->registerNativeFunction("_native_vfx_channelEmitFull",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                services::VFXInstanceId channelId = 0;
                if (args.size() != 12 || !tryGetChannelId(args[0], channelId))
                {
                    return value::Value(std::monostate{});
                }

                const glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};
                const float scale = extractFloat(args[4]);
                const glm::vec3 direction{extractFloat(args[5]), extractFloat(args[6]), extractFloat(args[7])};
                const float r = extractFloat(args[8]);
                const float g = extractFloat(args[9]);
                const float b = extractFloat(args[10]);
                const float a = extractFloat(args[11]);
                if (isFinite(position) && std::isfinite(scale) && scale > 0.0f &&
                    isFinite(direction) && isFiniteColor(r, g, b, a))
                {
                    submitChannelEmit(channelId, position, scale, direction,
                                      packRGBA8(r, g, b, a), true);
                }
                return value::Value(std::monostate{});
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

        // ============================================================
        // VFX COMBO SEQUENCES (VK-1425)
        // A combo plays multiple .vfVFX steps from a .vfVFXSequence asset; the
        // returned int is a VFXComboInstanceId (0 on failure), distinct from the
        // per-instance ids above.
        // ============================================================

        // _native_vfx_spawnCombo(path, x, y, z [, keepAlive]) -> int comboId
        // keepAlive=true keeps the combo alive after all steps finish (default
        // false => auto-destroy when complete). Looping steps keep playing regardless.
        interpreter->registerNativeFunction("_native_vfx_spawnCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                std::string path = extractString(args[0], "VFX.spawnCombo");
                if (path.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};

                bool keepAlive = false;
                if (args.size() >= 5 && value::isBool(args[4]))
                {
                    keepAlive = value::asBool(args[4]);
                }

                services::events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
                createCmd.sequenceAssetPath = path;
                createCmd.worldTransform = glm::translate(glm::mat4(1.0f), position);
                createCmd.autoDestroyOnFinish = !keepAlive;

                auto& dispatcher = events::EventDispatcher::instance();
                services::VFXComboInstanceId comboId = dispatcher.execute(createCmd);
                if (comboId != 0)
                {
                    services::events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
                    playCmd.comboId = comboId;
                    dispatcher.execute(playCmd);
                }

                return value::Value(static_cast<int64_t>(comboId));
            }});

        // _native_vfx_destroyCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_destroyCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::DestroyVFXComboInstanceCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_stopCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_stopCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::StopVFXComboInstanceCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_resetCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_resetCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::ResetVFXComboInstanceCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_setComboPosition(comboId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_vfx_setComboPosition",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4)
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};
                services::events::vfxsequence::SetVFXComboInstanceTransformCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.worldTransform = glm::translate(glm::mat4(1.0f), position);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_attachComboToSocket(comboId, parentEntityId, socketName) -> void
        interpreter->registerNativeFunction("_native_vfx_attachComboToSocket",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3)
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                int64_t parentEntityId = extractInt64(args[1]);
                if (comboId <= 0 || parentEntityId < 0)
                {
                    return value::Value(std::monostate{});
                }
                std::string socketName = extractString(args[2], "VFX.attachComboToSocket");
                if (socketName.empty())
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::AttachVFXComboInstanceToSocketCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.entityHandle = services::EntityHandle{static_cast<uint64_t>(parentEntityId)}.id;
                cmd.socketName = std::move(socketName);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_detachCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_detachCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::DetachVFXComboInstanceCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_triggerComboCue(comboId, cueName) -> void
        interpreter->registerNativeFunction("_native_vfx_triggerComboCue",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                std::string cueName = extractString(args[1], "VFX.triggerComboCue");
                if (cueName.empty())
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::TriggerVFXComboCueCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.cueName = std::move(cueName);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_triggerComboCuePayload(comboId, cueName, x, y, z, r, g, b, a [, scalar]) -> void
        interpreter->registerNativeFunction("_native_vfx_triggerComboCuePayload",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 9)
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                std::string cueName = extractString(args[1], "VFX.triggerComboCuePayload");
                if (cueName.empty())
                {
                    return value::Value(std::monostate{});
                }

                services::events::vfxsequence::TriggerVFXComboCueCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.cueName = std::move(cueName);
                cmd.payload.position = glm::vec3(extractFloat(args[2]), extractFloat(args[3]), extractFloat(args[4]));
                cmd.payload.color = glm::vec4(extractFloat(args[5]), extractFloat(args[6]),
                                               extractFloat(args[7]), extractFloat(args[8]));
                if (args.size() >= 10)
                    cmd.payload.scalar = extractFloat(args[9]);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_comboIsPlaying(comboId) -> bool
        interpreter->registerNativeFunction("_native_vfx_comboIsPlaying",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(false);
                }
                services::events::vfxsequence::IsVFXComboInstancePlayingQuery query;
                query.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                bool playing = events::EventDispatcher::instance().query(query);
                return value::Value(playing);
            }});

        // ============================================================
        // VFX COMBO DETERMINISTIC TRANSPORT (VK-1451)
        // Stable runtime controls only — live mid-game seek is intentionally NOT
        // scripted (GPU particles cannot be visually rewound).
        // ============================================================

        // _native_vfx_spawnComboSeeded(path, x, y, z, seed) -> int comboId
        // Same as spawnCombo but with an explicit RNG seed for a reproducible schedule.
        interpreter->registerNativeFunction("_native_vfx_spawnComboSeeded",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5)
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                std::string path = extractString(args[0], "VFX.spawnComboSeeded");
                if (path.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};

                services::events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
                createCmd.sequenceAssetPath = path;
                createCmd.worldTransform = glm::translate(glm::mat4(1.0f), position);
                createCmd.seed = static_cast<uint32_t>(extractInt64(args[4]));

                auto& dispatcher = events::EventDispatcher::instance();
                services::VFXComboInstanceId comboId = dispatcher.execute(createCmd);
                if (comboId != 0)
                {
                    services::events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
                    playCmd.comboId = comboId;
                    dispatcher.execute(playCmd);
                }
                return value::Value(static_cast<int64_t>(comboId));
            }});

        // _native_vfx_spawnComboPrewarmed(path, x, y, z, prewarm) -> int comboId
        // Spawn and fast-forward the schedule by `prewarm` seconds before the first frame.
        interpreter->registerNativeFunction("_native_vfx_spawnComboPrewarmed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5)
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                std::string path = extractString(args[0], "VFX.spawnComboPrewarmed");
                if (path.empty())
                {
                    return value::Value(static_cast<int64_t>(0));
                }
                glm::vec3 position{extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])};

                services::events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
                createCmd.sequenceAssetPath = path;
                createCmd.worldTransform = glm::translate(glm::mat4(1.0f), position);
                createCmd.prewarm = extractFloat(args[4]);

                auto& dispatcher = events::EventDispatcher::instance();
                services::VFXComboInstanceId comboId = dispatcher.execute(createCmd);
                if (comboId != 0)
                {
                    services::events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
                    playCmd.comboId = comboId;
                    dispatcher.execute(playCmd);
                }
                return value::Value(static_cast<int64_t>(comboId));
            }});

        // _native_vfx_pauseCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_pauseCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::SetVFXComboPausedCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.paused = true;
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_resumeCombo(comboId) -> void
        interpreter->registerNativeFunction("_native_vfx_resumeCombo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::SetVFXComboPausedCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.paused = false;
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_vfx_setComboRate(comboId, rate) -> void
        interpreter->registerNativeFunction("_native_vfx_setComboRate",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t comboId = extractInt64(args[0]);
                if (comboId <= 0)
                {
                    return value::Value(std::monostate{});
                }
                services::events::vfxsequence::SetVFXComboPlaybackRateCommand cmd;
                cmd.comboId = static_cast<services::VFXComboInstanceId>(comboId);
                cmd.rate = extractFloat(args[1]);
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

                if (!services::applyScalarOverride(cmd.overrides, name, val))
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

                if (!services::applyVectorOverride(cmd.overrides, name, glm::vec4(vec, w)))
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
