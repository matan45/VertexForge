// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "RenderTextureAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderTextureEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/CoreComponents.hpp"
#include "data/EntityConversion.hpp"

namespace core::api
{
    void RenderTextureAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_rtt_requestRender(entityId) -> void
        interpreter->registerNativeFunction("_native_rtt_requestRender",
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
                    components::RenderTextureComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                const auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                if (rttComp.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                {
                    return value::Value(std::monostate{});
                }

                services::events::rendertexture::RequestRenderTextureRenderCommand cmd;
                cmd.textureId = rttComp.textureId;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_rtt_setEnabled(entityId, enabled) -> void
        interpreter->registerNativeFunction("_native_rtt_setEnabled",
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

                bool enabled = false;
                if (std::holds_alternative<bool>(args[1]))
                {
                    enabled = std::get<bool>(args[1]);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::RenderTextureComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                const auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                if (rttComp.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                {
                    return value::Value(std::monostate{});
                }

                services::events::rendertexture::SetRenderTextureEnabledCommand cmd;
                cmd.textureId = rttComp.textureId;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_rtt_isEnabled(entityId) -> bool
        interpreter->registerNativeFunction("_native_rtt_isEnabled",
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
                    components::RenderTextureComponent>(entity))
                {
                    return value::Value(false);
                }

                const auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                return value::Value(rttComp.enabled);
            });

        // _native_rtt_create(entityId, width, height, updateMode) -> void
        // Creates a render texture for an entity that already has a RenderTextureComponent.
        // The component's textureId is populated by the play-mode handler, but this allows
        // scripts to create one explicitly at runtime.
        interpreter->registerNativeFunction("_native_rtt_create",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity)) return value::Value(std::monostate{});
                if (!registry.all_of<components::RenderTextureComponent>(entity))
                    return value::Value(std::monostate{});

                auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                if (rttComp.textureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
                    return value::Value(std::monostate{}); // already created

                uint32_t width = static_cast<uint32_t>(extractInt64(args[1]));
                uint32_t height = static_cast<uint32_t>(extractInt64(args[2]));
                uint32_t mode = static_cast<uint32_t>(extractInt64(args[3]));

                rendertexture::RenderTextureDesc desc;
                desc.width = width > 0 ? width : 512;
                desc.height = height > 0 ? height : 512;
                desc.updateMode = static_cast<rendertexture::UpdateMode>(mode);
                desc.priority = rttComp.priority;

                services::events::rendertexture::CreateRenderTextureCommand cmd;
                cmd.desc = desc;
                auto textureId = dispatcher.execute(cmd);
                rttComp.textureId = textureId;
                rttComp.width = desc.width;
                rttComp.height = desc.height;
                rttComp.updateMode = desc.updateMode;

                return value::Value(std::monostate{});
            });

        // _native_rtt_destroy(entityId) -> void
        interpreter->registerNativeFunction("_native_rtt_destroy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) ||
                    !registry.all_of<components::RenderTextureComponent>(entity))
                    return value::Value(std::monostate{});

                auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                if (rttComp.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                    return value::Value(std::monostate{});

                services::events::rendertexture::DestroyRenderTextureCommand cmd;
                cmd.textureId = rttComp.textureId;
                dispatcher.execute(cmd);
                rttComp.textureId = rendertexture::INVALID_RENDER_TEXTURE_ID;

                return value::Value(std::monostate{});
            });

        // _native_rtt_resize(entityId, width, height) -> void
        interpreter->registerNativeFunction("_native_rtt_resize",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) ||
                    !registry.all_of<components::RenderTextureComponent>(entity))
                    return value::Value(std::monostate{});

                auto& rttComp = registry.get<components::RenderTextureComponent>(entity);
                if (rttComp.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                    return value::Value(std::monostate{});

                uint32_t w = static_cast<uint32_t>(extractInt64(args[1]));
                uint32_t h = static_cast<uint32_t>(extractInt64(args[2]));

                services::events::rendertexture::ResizeRenderTextureCommand cmd;
                cmd.textureId = rttComp.textureId;
                cmd.width = w;
                cmd.height = h;
                dispatcher.execute(cmd);
                rttComp.width = w;
                rttComp.height = h;

                return value::Value(std::monostate{});
            });

        // _native_rtt_setCamera(rttEntityId, cameraEntityId) -> void
        // Reads view/projection from the camera entity and pipes to the RTT.
        interpreter->registerNativeFunction("_native_rtt_setCamera",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});
                int64_t rttId = extractInt64(args[0]);
                int64_t camId = extractInt64(args[1]);
                if (rttId < 0 || camId < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();

                auto rttEntity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(rttId)
                });
                if (!registry.valid(rttEntity) ||
                    !registry.all_of<components::RenderTextureComponent>(rttEntity))
                    return value::Value(std::monostate{});

                auto camEntity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(camId)
                });
                if (!registry.valid(camEntity) ||
                    !registry.all_of<components::CameraComponent, components::TransformComponent>(camEntity))
                    return value::Value(std::monostate{});

                const auto& rttComp = registry.get<components::RenderTextureComponent>(rttEntity);
                if (rttComp.textureId == rendertexture::INVALID_RENDER_TEXTURE_ID)
                    return value::Value(std::monostate{});

                const auto& cam = registry.get<components::CameraComponent>(camEntity);
                const auto& transform = registry.get<components::TransformComponent>(camEntity);

                services::events::rendertexture::UpdateRenderTextureCameraCommand cmd;
                cmd.textureId = rttComp.textureId;
                cmd.view = cam.viewMatrix;
                cmd.projection = cam.projectionMatrix;
                cmd.cameraPos = transform.position;
                cmd.nearPlane = cam.nearPlane;
                cmd.farPlane = cam.farPlane;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_rtt_setPriority(entityId, priority) -> void
        interpreter->registerNativeFunction("_native_rtt_setPriority",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                uint32_t priority = static_cast<uint32_t>(extractInt64(args[1]));

                events::scene::GetRenderTextureDataQuery q;
                q.entity = handle;
                auto data = dispatcher.query(q);
                if (!data.has_value()) return value::Value(std::monostate{});

                data->priority = priority;

                events::scene::SetRenderTextureDataCommand cmd;
                cmd.entity = handle;
                cmd.renderTextureData = *data;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_rtt_setUpdateMode(entityId, mode) -> void
        interpreter->registerNativeFunction("_native_rtt_setUpdateMode",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});
                auto handle = intToEntity(extractInt64(args[0]));
                uint8_t mode = static_cast<uint8_t>(extractInt64(args[1]));

                events::scene::GetRenderTextureDataQuery q;
                q.entity = handle;
                auto data = dispatcher.query(q);
                if (!data.has_value()) return value::Value(std::monostate{});

                data->updateMode = mode;

                events::scene::SetRenderTextureDataCommand cmd;
                cmd.entity = handle;
                cmd.renderTextureData = *data;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_rtt_getWidth(entityId) -> int
        interpreter->registerNativeFunction("_native_rtt_getWidth",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(static_cast<int64_t>(0));
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::RenderTextureComponent>(*entityOpt))
                    return value::Value(static_cast<int64_t>(0));
                const auto& rttComp = registry.get<components::RenderTextureComponent>(*entityOpt);
                return value::Value(static_cast<int64_t>(rttComp.width));
            });

        // _native_rtt_getHeight(entityId) -> int
        interpreter->registerNativeFunction("_native_rtt_getHeight",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                auto entityOpt = resolveEntity(args[0]);
                if (!entityOpt) return value::Value(static_cast<int64_t>(0));
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::RenderTextureComponent>(*entityOpt))
                    return value::Value(static_cast<int64_t>(0));
                const auto& rttComp = registry.get<components::RenderTextureComponent>(*entityOpt);
                return value::Value(static_cast<int64_t>(rttComp.height));
            });
    }
}
