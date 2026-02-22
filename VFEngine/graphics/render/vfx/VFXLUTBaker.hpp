#pragma once

#include "GPUVFXTypes.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>

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
        template<typename ModifierType>
        static bool bakeChannel(const ::vfx::VFXModifierChain& modifiers,
                                uint32_t lutFlag,
                                const std::function<void(const ModifierType&, std::vector<glm::vec4>&)>& bakeFn,
                                LUTBakeResult& result);

        static void fillDefault(std::vector<glm::vec4>& out, const glm::vec4& value);
        static void bakeGradient(const ::vfx::VFXGradient& gradient, std::vector<glm::vec4>& out);
        static void bakeCurve(const ::vfx::VFXCurve& curve, std::vector<glm::vec4>& out);
        static void bakeRotationCurve(const ::vfx::VFXCurve& curve, std::vector<glm::vec4>& out);
    };
}
