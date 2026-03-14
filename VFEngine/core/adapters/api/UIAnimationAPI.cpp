// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "UIAnimationAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"

namespace core::api
{
    void UIAnimationAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_ui_playAnimation(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_playAnimation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    vfLogError("[Script] UI.playAnimation: missing entityId argument");
                                                    return value::Value(std::monostate{});
                                                }

                                                int64_t entityId = extractInt64(args[0], "UI.playAnimation");
                                                if (entityId < 0)
                                                    return value::Value(std::monostate{});

                                                events::ui::PlayUIAnimationCommand cmd;
                                                cmd.entity = intToEntity(entityId);
                                                dispatcher.execute(cmd);

                                                return value::Value(std::monostate{});
                                            });

        // _native_ui_stopAnimation(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_stopAnimation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    vfLogError("[Script] UI.stopAnimation: missing entityId argument");
                                                    return value::Value(std::monostate{});
                                                }

                                                int64_t entityId = extractInt64(args[0], "UI.stopAnimation");
                                                if (entityId < 0)
                                                    return value::Value(std::monostate{});

                                                events::ui::StopUIAnimationCommand cmd;
                                                cmd.entity = intToEntity(entityId);
                                                dispatcher.execute(cmd);

                                                return value::Value(std::monostate{});
                                            });

        // _native_ui_pauseAnimation(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_pauseAnimation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    vfLogError("[Script] UI.pauseAnimation: missing entityId argument");
                                                    return value::Value(std::monostate{});
                                                }

                                                int64_t entityId = extractInt64(args[0], "UI.pauseAnimation");
                                                if (entityId < 0)
                                                    return value::Value(std::monostate{});

                                                events::ui::PauseUIAnimationCommand cmd;
                                                cmd.entity = intToEntity(entityId);
                                                dispatcher.execute(cmd);

                                                return value::Value(std::monostate{});
                                            });

        // _native_ui_resumeAnimation(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_resumeAnimation",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    vfLogError("[Script] UI.resumeAnimation: missing entityId argument");
                                                    return value::Value(std::monostate{});
                                                }

                                                int64_t entityId = extractInt64(args[0], "UI.resumeAnimation");
                                                if (entityId < 0)
                                                    return value::Value(std::monostate{});

                                                events::ui::ResumeUIAnimationCommand cmd;
                                                cmd.entity = intToEntity(entityId);
                                                dispatcher.execute(cmd);

                                                return value::Value(std::monostate{});
                                            });

        // _native_ui_isAnimationPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_ui_isAnimationPlaying",
                                            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
                                            {
                                                if (args.empty())
                                                {
                                                    vfLogError("[Script] UI.isAnimationPlaying: missing entityId argument");
                                                    return value::Value(false);
                                                }

                                                int64_t entityId = extractInt64(args[0], "UI.isAnimationPlaying");
                                                if (entityId < 0)
                                                    return value::Value(false);

                                                events::ui::IsUIAnimationPlayingQuery query;
                                                query.entity = intToEntity(entityId);
                                                bool playing = dispatcher.query(query);

                                                return value::Value(playing);
                                            });
    }
}
