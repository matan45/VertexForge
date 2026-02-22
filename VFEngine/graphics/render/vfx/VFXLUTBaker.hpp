#pragma once

#include "GPUVFXTypes.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace render::vfx
{
    struct LUTBakeResult
    {
        std::vector<glm::vec4> data;
        uint32_t lutFlags = 0;
        uint32_t totalEntries = 0;
    };

    class VFXLUTBaker
    {
    public:
        static LUTBakeResult bake(const ::vfx::VFXModifierChain& modifiers);

    private:
        static void bakeGradient(const ::vfx::VFXGradient& gradient, std::vector<glm::vec4>& out);
        static void bakeCurve(const ::vfx::VFXCurve& curve, std::vector<glm::vec4>& out);
    };
}
