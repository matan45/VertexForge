#include "VFXLUTBaker.hpp"
#include <glm/gtc/constants.hpp>

namespace render::vfx
{
    template<typename ModifierType>
    bool VFXLUTBaker::bakeChannel(const ::vfx::VFXModifierChain& modifiers,
                                   uint32_t lutFlag,
                                   const std::function<void(const ModifierType&, std::vector<glm::vec4>&)>& bakeFn,
                                   LUTBakeResult& result)
    {
        for (const auto& modifier : modifiers.modifiers)
        {
            bool found = false;
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (std::is_same_v<T, ModifierType>)
                {
                    bakeFn(mod, result.data);
                    result.lutFlags |= lutFlag;
                    found = true;
                }
            }, modifier);
            if (found) return true;
        }
        return false;
    }

    void VFXLUTBaker::fillDefault(std::vector<glm::vec4>& out, const glm::vec4& value)
    {
        constexpr uint32_t res = GPUVFXConstants::LUT_RESOLUTION;
        for (uint32_t i = 0; i < res; ++i)
            out.emplace_back(value);
    }

    LUTBakeResult VFXLUTBaker::bake(const ::vfx::VFXModifierChain& modifiers,
                                    const RibbonLUTInputs& ribbon)
    {
        LUTBakeResult result;
        result.data.reserve(GPUVFXConstants::LUT_CHANNELS * GPUVFXConstants::LUT_RESOLUTION);

        if (!bakeChannel<::vfx::ColorOverLifetimeConfig>(modifiers, LUTFlags::Color,
            [](const auto& mod, auto& out) { bakeGradient(mod.gradient, out); }, result))
        {
            fillDefault(result.data, glm::vec4(1.0f));
        }

        if (!bakeChannel<::vfx::SizeOverLifetimeConfig>(modifiers, LUTFlags::Size,
            [](const auto& mod, auto& out) { bakeCurve(mod.curve, out); }, result))
        {
            fillDefault(result.data, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        }

        if (!bakeChannel<::vfx::SpeedOverLifetimeConfig>(modifiers, LUTFlags::Speed,
            [](const auto& mod, auto& out) { bakeCurve(mod.curve, out); }, result))
        {
            fillDefault(result.data, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        }

        if (!bakeChannel<::vfx::RotationOverLifetimeConfig>(modifiers, LUTFlags::Rotation,
            [](const auto& mod, auto& out) { bakeRotationCurve(mod.curve, out); }, result))
        {
            fillDefault(result.data, glm::vec4(0.0f));
        }

        if (!bakeChannel<::vfx::GlowOverLifetimeConfig>(modifiers, LUTFlags::Glow,
            [](const auto& mod, auto& out) { bakeCurve(mod.curve, out); }, result))
        {
            fillDefault(result.data, glm::vec4(0.0f));
        }

        // VK-1473 channel 5: SizeBySpeed curve (multiply on top of over-lifetime size).
        if (!bakeChannel<::vfx::SizeBySpeedConfig>(modifiers, LUTFlags::SizeBySpeed,
            [](const auto& mod, auto& out) { bakeCurve(mod.curve, out); }, result))
        {
            fillDefault(result.data, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        }

        // VK-1473 channel 6: ColorBySpeed gradient (multiply on top of over-lifetime color).
        if (!bakeChannel<::vfx::ColorBySpeedConfig>(modifiers, LUTFlags::ColorBySpeed,
            [](const auto& mod, auto& out) { bakeGradient(mod.gradient, out); }, result))
        {
            fillDefault(result.data, glm::vec4(1.0f));
        }

        // VK-1474 channel 7: ribbon width curve (emitter property, sampled by trail position).
        if (ribbon.widthCurve != nullptr)
        {
            bakeCurve(*ribbon.widthCurve, result.data);
            result.lutFlags |= LUTFlags::RibbonWidth;
        }
        else
        {
            fillDefault(result.data, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        }

        // VK-1474 channel 8: ribbon tail gradient (emitter property, sampled by trail position).
        if (ribbon.tailGradient != nullptr)
        {
            bakeGradient(*ribbon.tailGradient, result.data);
            result.lutFlags |= LUTFlags::RibbonTailGradient;
        }
        else
        {
            fillDefault(result.data, glm::vec4(1.0f));
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
