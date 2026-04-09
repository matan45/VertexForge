#pragma once

#include "../../../plugin/api/PluginContext.hpp"
#include <vector>

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PluginComponentAPI
    {
    public:
        // Register all _plugin_* native functions using the given bridges for component access.
        static void registerAPI(services::ScriptInterpreter* interpreter,
                                const std::vector<plugin::MetaComponentBridge>& bridges);
    };
}
