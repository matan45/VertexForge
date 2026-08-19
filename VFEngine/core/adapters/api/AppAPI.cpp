// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <exception>
#include <span>

#include "AppAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/project/ApplicationEvents.hpp"

namespace core::api
{
    void AppAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // App.quit(exitCode) — end the game.
        //   Runtime: closes the window; the process returns exitCode from main().
        //   Editor:  stops Play mode (the editor itself never exits); exitCode is logged.
        // The command is only recorded by its handler — the actual shutdown/stop happens on
        // the main thread after the frame completes, so calling this from onUpdate is safe.
        interpreter->registerNativeFunction("_native_app_quit",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                events::application::QuitGameCommand cmd;
                cmd.exitCode = args.empty()
                    ? 0
                    : static_cast<int>(extractInt64(args[0], "App.quit"));
                // execute() throws when nothing registered the handler (a host that embeds the
                // interpreter without the Runtime/Editor handler); never let that unwind
                // through the mType VM.
                try
                {
                    events::EventDispatcher::instance().execute(cmd);
                }
                catch (const std::exception& e)
                {
                    vfLogError("[Script] App.quit: {}", e.what());
                }
                return value::Value(std::monostate{});
            }});
    }
}
