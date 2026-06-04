#pragma once

#include "../EventTypes.hpp"
#include <glm/glm.hpp>

namespace events::debugdraw
{
    struct DrawLineCommand : ICommand<>
    {
        glm::vec3 start;
        glm::vec3 end;
        glm::vec4 color;

        std::string_view getName() const override { return "DrawLine"; }
    };

    struct DrawRayCommand : ICommand<>
    {
        glm::vec3 origin;
        glm::vec3 direction;
        float length;
        glm::vec4 color;

        std::string_view getName() const override { return "DrawRay"; }
    };

    struct DrawBoxCommand : ICommand<>
    {
        glm::vec3 center;
        glm::vec3 halfExtents;
        glm::vec4 color;

        std::string_view getName() const override { return "DrawBox"; }
    };

    struct DrawSphereCommand : ICommand<>
    {
        glm::vec3 center;
        float radius;
        glm::vec4 color;

        std::string_view getName() const override { return "DrawSphere"; }
    };

    struct SetDebugDrawEnabledCommand : ICommand<>
    {
        bool enabled;

        std::string_view getName() const override { return "SetDebugDrawEnabled"; }
    };

    struct GetDebugDrawEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetDebugDrawEnabled"; }
    };
}
