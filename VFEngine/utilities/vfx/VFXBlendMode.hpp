#pragma once

#include <cstdint>
#include <string>

namespace vfx
{
    // VK-1472 — per-emitter blend mode. The integer values are the on-GPU
    // push-constant contract consumed by the VFX color shaders:
    //   0 = Alpha         straight alpha-over          (shared premultiplied pipeline)
    //   1 = Additive      glow, no occlusion           (shared premultiplied pipeline)
    //   2 = Premultiplied fire->smoke gradient trick   (shared premultiplied pipeline)
    //   3 = Multiply      darken destination (dst*src) (dedicated Multiply pipeline)
    // Alpha/Additive/Premultiplied share one premultiplied blend state and differ
    // only in the fragment shader's output; Multiply needs its own VkPipeline
    // color-blend variant (srcColor=eDstColor, dstColor=eZero). Do NOT renumber:
    // 0/1 preserve the historical additiveBlend bool (false=Alpha, true=Additive).
    enum class VFXBlendMode : uint8_t
    {
        Alpha = 0,
        Additive = 1,
        Premultiplied = 2,
        Multiply = 3
    };

    inline const char* blendModeToString(VFXBlendMode mode)
    {
        switch (mode)
        {
        case VFXBlendMode::Alpha:         return "alpha";
        case VFXBlendMode::Additive:      return "additive";
        case VFXBlendMode::Premultiplied: return "premultiplied";
        case VFXBlendMode::Multiply:      return "multiply";
        default:                          return "alpha";
        }
    }

    inline VFXBlendMode stringToBlendMode(const std::string& str)
    {
        if (str == "additive")      return VFXBlendMode::Additive;
        if (str == "premultiplied") return VFXBlendMode::Premultiplied;
        if (str == "multiply")      return VFXBlendMode::Multiply;
        return VFXBlendMode::Alpha;
    }

    // Back-compat: legacy assets carry only the bool `additiveBlend`.
    inline VFXBlendMode blendModeFromLegacy(bool additiveBlend)
    {
        return additiveBlend ? VFXBlendMode::Additive : VFXBlendMode::Alpha;
    }

    // The value pushed to the GPU `blendMode` push constant (0..3). Kept as a
    // function so the enum-to-uint contract has a single, testable home.
    inline uint32_t blendModeToGpuValue(VFXBlendMode mode)
    {
        return static_cast<uint32_t>(mode);
    }

    // Only Multiply requires binding the dedicated Multiply-blend pipeline variant;
    // the other three share the premultiplied pipeline.
    inline bool blendModeNeedsMultiplyPipeline(VFXBlendMode mode)
    {
        return mode == VFXBlendMode::Multiply;
    }
}
