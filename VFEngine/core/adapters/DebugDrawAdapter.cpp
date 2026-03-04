#include "DebugDrawAdapter.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace core
{
    void DebugDrawAdapter::addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color)
    {
        if (drawList.lineVertices.size() >= MAX_LINE_VERTICES)
        {
            if (!budgetWarned)
            {
                vfLogWarning("Debug draw budget exceeded ({} vertices). Additional lines will be dropped this frame.", MAX_LINE_VERTICES);
                budgetWarned = true;
            }
            return;
        }
        drawList.lineVertices.push_back({start, color});
        drawList.lineVertices.push_back({end, color});
    }

    void DebugDrawAdapter::drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color)
    {
        if (!enabled) return;
        addLine(start, end, color);
    }

    void DebugDrawAdapter::drawRay(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color)
    {
        if (!enabled) return;
        addLine(origin, origin + direction * length, color);
    }

    void DebugDrawAdapter::drawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color)
    {
        if (!enabled) return;

        const glm::vec3 h = halfExtents;
        // 8 corners
        const glm::vec3 corners[8] = {
            center + glm::vec3(-h.x, -h.y, -h.z),
            center + glm::vec3( h.x, -h.y, -h.z),
            center + glm::vec3( h.x,  h.y, -h.z),
            center + glm::vec3(-h.x,  h.y, -h.z),
            center + glm::vec3(-h.x, -h.y,  h.z),
            center + glm::vec3( h.x, -h.y,  h.z),
            center + glm::vec3( h.x,  h.y,  h.z),
            center + glm::vec3(-h.x,  h.y,  h.z),
        };

        // 12 edges
        // Bottom face
        addLine(corners[0], corners[1], color);
        addLine(corners[1], corners[2], color);
        addLine(corners[2], corners[3], color);
        addLine(corners[3], corners[0], color);
        // Top face
        addLine(corners[4], corners[5], color);
        addLine(corners[5], corners[6], color);
        addLine(corners[6], corners[7], color);
        addLine(corners[7], corners[4], color);
        // Vertical edges
        addLine(corners[0], corners[4], color);
        addLine(corners[1], corners[5], color);
        addLine(corners[2], corners[6], color);
        addLine(corners[3], corners[7], color);
    }

    void DebugDrawAdapter::drawSphere(const glm::vec3& center, float radius, const glm::vec4& color)
    {
        if (!enabled) return;

        constexpr int segments = 32;
        constexpr float step = glm::two_pi<float>() / static_cast<float>(segments);

        // Draw 3 circles (XY, XZ, YZ planes)
        for (int i = 0; i < segments; ++i)
        {
            float a0 = static_cast<float>(i) * step;
            float a1 = static_cast<float>(i + 1) * step;
            float c0 = std::cos(a0) * radius;
            float s0 = std::sin(a0) * radius;
            float c1 = std::cos(a1) * radius;
            float s1 = std::sin(a1) * radius;

            // XY plane
            addLine(center + glm::vec3(c0, s0, 0.0f), center + glm::vec3(c1, s1, 0.0f), color);
            // XZ plane
            addLine(center + glm::vec3(c0, 0.0f, s0), center + glm::vec3(c1, 0.0f, s1), color);
            // YZ plane
            addLine(center + glm::vec3(0.0f, c0, s0), center + glm::vec3(0.0f, c1, s1), color);
        }
    }

    render::mesh::ImmediateDebugDrawList DebugDrawAdapter::consumeDrawList()
    {
        render::mesh::ImmediateDebugDrawList result = std::move(drawList);
        drawList = render::mesh::ImmediateDebugDrawList{};
        budgetWarned = false;
        return result;
    }
}
