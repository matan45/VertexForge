#pragma once

#include "../compute/GPUVFXTypes.hpp"
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
        // VK-1474: ribbon width/tail come from emitter properties, not the modifier chain, so
        // they are passed alongside the chain. Null pointers bake a flat default (channel present,
        // flag off).
        struct RibbonLUTInputs
        {
            const ::vfx::VFXCurve* widthCurve = nullptr;      // channel 7 (null => width 1.0, flag off)
            const ::vfx::VFXGradient* tailGradient = nullptr; // channel 8 (null => white, flag off)
        };

        static LUTBakeResult bake(const ::vfx::VFXModifierChain& modifiers,
                                  const RibbonLUTInputs& ribbon = {});

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
