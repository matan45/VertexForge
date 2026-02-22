#include "VFXLUTBaker.hpp"
#include <glm/gtc/constants.hpp>

namespace render::vfx
{
    LUTBakeResult VFXLUTBaker::bake(const ::vfx::VFXModifierChain& modifiers)
    {
        LUTBakeResult result;
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;

        // Reserve space: exactly 4 channels * resolution entries
        result.data.reserve(GPUVFXConstants::LUT_CHANNELS * res);

        // Bake each channel in order: color, size, speed, rotation
        // Always bake all 4 channels to maintain fixed stride layout
        // Only the first modifier of each type is used; duplicates are skipped

        bool bakedColor = false;
        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::ColorOverLifetimeConfig>)
                {
                    if (!bakedColor)
                    {
                        bakeGradient(mod.gradient, result.data);
                        result.lutFlags |= LUTFlags::Color;
                        bakedColor = true;
                    }
                }
            }, modifier);
        }
        if (!bakedColor)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
        }

        bool bakedSize = false;
        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::SizeOverLifetimeConfig>)
                {
                    if (!bakedSize)
                    {
                        bakeCurve(mod.curve, result.data);
                        result.lutFlags |= LUTFlags::Size;
                        bakedSize = true;
                    }
                }
            }, modifier);
        }
        if (!bakedSize)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }

        bool bakedSpeed = false;
        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::SpeedOverLifetimeConfig>)
                {
                    if (!bakedSpeed)
                    {
                        bakeCurve(mod.curve, result.data);
                        result.lutFlags |= LUTFlags::Speed;
                        bakedSpeed = true;
                    }
                }
            }, modifier);
        }
        if (!bakedSpeed)
        {
            for (uint32_t i = 0; i < res; ++i)
                result.data.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);
        }

        bool bakedRotation = false;
        for (const auto& modifier : modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ::vfx::RotationOverLifetimeConfig>)
                {
                    if (!bakedRotation)
                    {
                        bakeRotationCurve(mod.curve, result.data);
                        result.lutFlags |= LUTFlags::Rotation;
                        bakedRotation = true;
                    }
                }
            }, modifier);
        }
        if (!bakedRotation)
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

    void VFXLUTBaker::bakeRotationCurve(const ::vfx::VFXCurve& curve, std::vector<glm::vec4>& out)
    {
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;
        for (uint32_t i = 0; i < res; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(res - 1);
            float value = glm::radians(curve.evaluate(t));
            out.emplace_back(value, 0.0f, 0.0f, 0.0f);
        }
    }
}
