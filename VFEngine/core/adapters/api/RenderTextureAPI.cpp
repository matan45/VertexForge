// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "RenderTextureAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderTextureEvents.hpp"
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
    }
}
