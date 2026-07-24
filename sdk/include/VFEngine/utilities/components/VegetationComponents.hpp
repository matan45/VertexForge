#pragma once
#include "../vegetation/GrassConfig.hpp"
#include "../vegetation/VegetationTypes.hpp"
#include "../vegetation/VegetationScatterTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace components
{
    struct GrassComponent
    {
        vegetation::GrassRenderConfig config;
        std::vector<vegetation::BillboardPaletteEntry> billboardPalette;
        vegetation::ScatterProfile scatterProfile; // VK-1581 procedural scatter rules (inline snapshot)
        // VK-1585: optional path to a reusable .vfScatterProfile asset. When non-empty it is
        // resolved into scatterProfile on scene load (mirrors Material -> ToonProfile); empty ⇒
        // the inline scatterProfile above is authoritative (backward compatible).
        std::string scatterProfilePath;
        bool enabled = true;
    };

    struct VegetationComponent
    {
        bool enabled = true;
    };
}
