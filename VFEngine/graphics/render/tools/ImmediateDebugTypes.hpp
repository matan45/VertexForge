#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace render::mesh
{
    struct DebugLineVertex
    {
        glm::vec3 position;
        glm::vec4 color;
    };

    struct ImmediateDebugDrawList
    {
        std::vector<DebugLineVertex> lineVertices;

        void clear() { lineVertices.clear(); }
        [[nodiscard]] bool empty() const { return lineVertices.empty(); }
        [[nodiscard]] size_t vertexCount() const { return lineVertices.size(); }
    };
}
