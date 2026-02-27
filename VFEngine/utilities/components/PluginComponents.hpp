#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace components {

    // Generic data container for plugin-defined per-entity data.
    // Plugins store opaque byte blobs keyed by plugin name.
    struct PluginComponentData
    {
        std::unordered_map<std::string, std::vector<uint8_t>> pluginData;
    };

}
