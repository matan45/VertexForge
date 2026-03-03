#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace animator
{
    enum class BlendTreeType : uint8_t
    {
        BlendTree1D,
        BlendTree2D
    };

    struct BlendTreeEntry
    {
        std::string animationPath;
        float threshold = 0.0f;       // For 1D blend trees
        glm::vec2 position{0.0f};     // For 2D blend trees
    };

    struct BlendTreeData
    {
        BlendTreeType type = BlendTreeType::BlendTree1D;
        std::string parameterName;       // For 1D (and X axis of 2D)
        std::string parameterNameY;      // For 2D Y axis
        std::vector<BlendTreeEntry> entries;
    };
}
