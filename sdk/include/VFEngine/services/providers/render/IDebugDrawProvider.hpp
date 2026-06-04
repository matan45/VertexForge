#pragma once

#include <glm/glm.hpp>
#include "../../graphics/render/tools/ImmediateDebugTypes.hpp"

namespace services
{
    class IDebugDrawProvider
    {
    public:
        virtual ~IDebugDrawProvider() = default;

        virtual void drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) = 0;
        virtual void drawRay(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color) = 0;
        virtual void drawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color) = 0;
        virtual void drawSphere(const glm::vec3& center, float radius, const glm::vec4& color) = 0;

        virtual void setEnabled(bool enabled) = 0;
        virtual bool isEnabled() const = 0;

        // Returns the current draw list and clears the internal buffer.
        virtual render::mesh::ImmediateDebugDrawList consumeDrawList() = 0;
    };
}
