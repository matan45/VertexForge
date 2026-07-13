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
    // v15: registerAssetType/unregisterAssetType on PluginContext +
    //      PluginAssetTypeDesc/PluginAssetTypeHandle — plugins declare custom
    //      asset types (extension, Content Browser metadata, Create entry,
    //      dependency scanning, export) without editing engine asset tables.
    //      Also resolveAssetPath(guidHex) + resolveProjectPath(relPath) so
    //      plugins can load assets dragged into their AssetRef component fields
    //      and assets they reference by project-relative path at runtime.
    // v16: findAssetPathsByExtension on PluginContext so plugins can build
    //      read-only indexes over project assets without guessing project roots.
    // v17: registerUITexture/unregisterUITexture on PluginContext — expose a plugin
    //      2D texture as a generic UI image source (bind any plugin/GPU texture to a
    //      UIImageComponent by key; used for a smooth minimap fog-of-war overlay).
    // v18: VFXPreviewParams appends loopDuration for finite-burst loop previews.
    constexpr uint32_t VF_PLUGIN_API_VERSION = 18;

}
