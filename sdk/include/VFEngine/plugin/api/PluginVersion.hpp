#pragma once
#include <cstdint>

namespace plugin {

    // Increment this when the plugin API changes in an incompatible way.
    // Plugins built against a different version will be rejected at load time.
    // v10: script Value access (scriptArray*/scriptObject*/scriptMake*) on PluginContext.
    // v11: onActivate/onDeactivate lifecycle hooks on IPlugin (per-scene soft-disable).
    // v12: registerAssetImporter on PluginContext (full custom asset importers).
    // v13: registerPostProcessEffect on PluginContext (scene-color read-modify-write
    //      full-screen post-process effects: tone mapping, grading, LUT, sharpen).
    // v14: setFieldAttributes on PluginContext + FieldAttributes/Field (per-field
    //      inspector metadata: label, tooltip, group, units, range/slider, color,
    //      multiline, read-only/hidden, asset-filter) for native plugin components.
    constexpr uint32_t VF_PLUGIN_API_VERSION = 14;

}
