#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace plugin {

    // Pure-data descriptor for a plugin-registered asset type (VK-1449). It lets
    // a plugin add a first-class asset family (new extension, Content Browser
    // metadata, Create-menu entry, dependency scanning, export inclusion)
    // without editing the engine's hardcoded asset tables.
    //
    // DELIBERATELY callback-free: keeping the descriptor pure data minimises the
    // plugin ABI surface and means the engine never holds a plugin-owned
    // function object (sidesteps the unload/shutdown-crash class). Behaviour the
    // engine cannot express as data is routed differently:
    //   - Creation writes `defaultTemplate` (a JSON string) to the new file.
    //   - Double-click open-routing is delivered to the plugin as an event
    //     (the plugin subscribes via ctx->subscribeEvent and opens its own
    //     editor window), not via a stored callback.
    struct PluginAssetTypeDesc {
        std::string typeId;                  // stable unique id, e.g. "gas.ability"
        std::string displayName;             // Content Browser label
        std::string category;                // Create-menu submenu / grouping
        std::vector<std::string> extensions; // e.g. {".vfability"} — lowercased engine-side
        bool jsonContainer = true;           // participate in dependency/reference scanning
        bool createMenuEntry = true;         // show under the Create menu
        std::string icon;                    // icon hint (engine maps to an atlas glyph)
        uint32_t badgeColor = 0xFFFFFFFFu;   // Content Browser type-badge color
        std::string defaultTemplate;         // JSON written when created from the Create menu
        std::vector<std::string> alwaysIncludeGlobs; // extra export always-include glob patterns
    };

    // Opaque handle to a registered asset type. Pass to unregisterAssetType.
    // id == 0 means "invalid / registration rejected".
    struct PluginAssetTypeHandle {
        uint32_t id = 0;
        explicit operator bool() const { return id != 0; }
        bool operator==(const PluginAssetTypeHandle& o) const { return id == o.id; }
    };

}
