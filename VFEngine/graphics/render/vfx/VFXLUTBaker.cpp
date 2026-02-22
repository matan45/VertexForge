#include "VFXLUTBaker.hpp"

namespace render::vfx
{
    LUTBakeResult VFXLUTBaker::bake(const ::vfx::VFXModifierChain& modifiers)
    {
        LUTBakeResult result;
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;

        // Reserve space: up to 4 channels * resolution entries
        result.data.reserve(GPUVFXConstants::LUT_CHANNELS * res);

        // Channel 0: Color (gradient)
        bool hasColor = false;
        // Channel 1: Size (curve)
        bool hasSize = false;
        // Channel 2: Speed (curve)
        bool hasSpeed = false;
        // Channel 3: Rotation (curve)
        bool hasRotation = false;

        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::ColorOverLifetimeConfig>)
                {
                    hasColor = true;
                }
                else if constexpr (std::is_same_v<T, ::vfx::SizeOverLifetimeConfig>)
                {
                    hasSize = true;
                }
                else if constexpr (std::is_same_v<T, ::vfx::SpeedOverLifetimeConfig>)
                {
                    hasSpeed = true;
                }
                else if constexpr (std::is_same_v<T, ::vfx::RotationOverLifetimeConfig>)
                {
                    hasRotation = true;
                }
            }, modifier);
        }

        // Bake each channel in order: color, size, speed, rotation
        // Always bake all 4 channels to maintain fixed stride layout
        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::ColorOverLifetimeConfig>)
                {
                    bakeGradient(mod.gradient, result.data);
                    result.lutFlags |= LUTFlags::Color;
                }
            }, modifier);
        }
        if (!hasColor)
        {
            // Pad with identity color
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
        }

        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::SizeOverLifetimeConfig>)
                {
                    bakeCurve(mod.curve, result.data);
                    result.lutFlags |= LUTFlags::Size;
                }
            }, modifier);
        }
        if (!hasSize)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }

        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::SpeedOverLifetimeConfig>)
                {
                    bakeCurve(mod.curve, result.data);
                    result.lutFlags |= LUTFlags::Speed;
                }
            }, modifier);
        }
        if (!hasSpeed)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }

        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::RotationOverLifetimeConfig>)
                {
                    bakeCurve(mod.curve, result.data);
                    result.lutFlags |= LUTFlags::Rotation;
                }
            }, modifier);
        }
        if (!hasRotation)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
        }

        result.totalEntries = static_cast<uint32_t>(result.data.size());
        return result;
    }

    void VFXLUTBaker::bakeGradient(const ::vfx::VFXGradient& gradient, std::vector<glm::vec4>& out)
    {
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;
        for (uint32_t i = 0; i < res; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(res - 1);
            out.push_back(gradient.evaluate(t));
        }
    }

    void VFXLUTBaker::bakeCurve(const ::vfx::VFXCurve& curve, std::vector<glm::vec4>& out)
    {
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;
        for (uint32_t i = 0; i < res; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(res - 1);
            float value = curve.evaluate(t);
            out.emplace_back(value, 0.0f, 0.0f, 0.0f);
        }
    }
}
