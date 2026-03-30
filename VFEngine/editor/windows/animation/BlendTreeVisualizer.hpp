#pragma once

#include "animator/BlendTreeTypes.hpp"
#include "data/AnimatorDebugTypes.hpp"
#include <imgui.h>

namespace windows::animation
{
    class BlendTreeVisualizer
    {
    public:
        // 1D blend tree: horizontal axis with threshold markers and current position
        static void draw1D(const animator::BlendTreeData& blendTree,
                           float currentParamValue,
                           const ImVec2& availSize);

        // 2D blend tree: interactive 2D space with sample points and current blend position
        static void draw2D(const animator::BlendTreeData& blendTree,
                           float currentParamX, float currentParamY,
                           const ImVec2& availSize);
    };
}
