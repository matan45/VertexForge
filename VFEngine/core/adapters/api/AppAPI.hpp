#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    // Application-lifetime natives (App.mt). Today just App.quit(exitCode), which lets an
    // automated match end its own process with a pass/fail code in the Runtime, and stops
    // Play mode (never closes the application) in the Editor. No plugin ABI involved.
    class AppAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
