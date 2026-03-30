#pragma once
#include "PreviewEnvironment.hpp"
#include <math/Frustum.hpp>

namespace editor
{
    class OrbitCamera;
}

namespace editor::preview
{
    class PreviewToolbar
    {
    public:
        // Renders a horizontal toolbar at the top of a preview viewport.
        // Returns true if any setting changed.
        static bool draw(PreviewEnvironment& env, OrbitCamera* camera, const math::AABB* bounds = nullptr);
    };
}
