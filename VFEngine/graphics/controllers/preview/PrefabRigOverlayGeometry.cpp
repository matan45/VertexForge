#include "PrefabRigOverlayGeometry.hpp"

#include <array>

namespace controllers::prefabrigoverlay
{
    glm::vec4 partColor(size_t partIndex)
    {
        // Distinct, readable hues; cycles for >palette parts.
        static const std::array<glm::vec4, 6> palette = {
            glm::vec4(0.30f, 0.80f, 1.00f, 1.0f), // cyan
            glm::vec4(1.00f, 0.65f, 0.20f, 1.0f), // orange
            glm::vec4(0.65f, 1.00f, 0.40f, 1.0f), // lime
            glm::vec4(1.00f, 0.45f, 0.80f, 1.0f), // pink
            glm::vec4(0.80f, 0.70f, 1.00f, 1.0f), // violet
            glm::vec4(1.00f, 0.90f, 0.40f, 1.0f), // yellow
        };
        return palette[partIndex % palette.size()];
    }

    void addLine(render::mesh::ImmediateDebugDrawList& out,
                 const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)
    {
        out.lineVertices.push_back({a, color});
        out.lineVertices.push_back({b, color});
    }

    void addAxisTriad(render::mesh::ImmediateDebugDrawList& out,
                      const glm::mat4& transform, float axisLength)
    {
        const glm::vec3 origin = glm::vec3(transform[3]);
        const glm::vec3 x = glm::vec3(transform[0]) * axisLength;
        const glm::vec3 y = glm::vec3(transform[1]) * axisLength;
        const glm::vec3 z = glm::vec3(transform[2]) * axisLength;

        addLine(out, origin, origin + x, glm::vec4(1.0f, 0.15f, 0.15f, 1.0f));
        addLine(out, origin, origin + y, glm::vec4(0.15f, 1.0f, 0.15f, 1.0f));
        addLine(out, origin, origin + z, glm::vec4(0.15f, 0.35f, 1.0f, 1.0f));
    }

    void addMarker(render::mesh::ImmediateDebugDrawList& out,
                   const glm::vec3& position, float halfSize, const glm::vec4& color)
    {
        addLine(out, position - glm::vec3(halfSize, 0.0f, 0.0f), position + glm::vec3(halfSize, 0.0f, 0.0f), color);
        addLine(out, position - glm::vec3(0.0f, halfSize, 0.0f), position + glm::vec3(0.0f, halfSize, 0.0f), color);
        addLine(out, position - glm::vec3(0.0f, 0.0f, halfSize), position + glm::vec3(0.0f, 0.0f, halfSize), color);
    }
}
