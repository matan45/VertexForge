#pragma once
#include <cstdint>

namespace plugin {

    // Increment this when the plugin API changes in an incompatible way.
    // Plugins built against a different version will be rejected at load time.
    // v10: script Value access (scriptArray*/scriptObject*/scriptMake*) on PluginContext.
    // v11: onActivate/onDeactivate lifecycle hooks on IPlugin (per-scene soft-disable).
    constexpr uint32_t VF_PLUGIN_API_VERSION = 11;

}
