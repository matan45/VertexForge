#pragma once

#include "VFXTypes.hpp"
#include "VFXModifierTypes.hpp"
#include <vector>

namespace vfx
{
    class VFXModifierConfigLoader
    {
    public:
        static VFXModifierChain fromGraph(const VFXGraph& graph);

        static glm::vec3 getGlowColorFromChain(const VFXModifierChain& chain)
        {
            for (const auto& mod : chain.modifiers)
            {
                if (auto* glow = std::get_if<GlowOverLifetimeConfig>(&mod))
                    return glow->glowColor;
            }
            return glm::vec3(1.0f);
        }

    private:
        static std::vector<const VFXNode*> getModifierNodeChain(const VFXGraph& graph);
        static VFXModifierConfig nodeToConfig(const VFXNode& node);

        static ColorOverLifetimeConfig extractColorConfig(const VFXNode& node);
        static SizeOverLifetimeConfig extractSizeConfig(const VFXNode& node);
        static SpeedOverLifetimeConfig extractSpeedConfig(const VFXNode& node);
        static RotationOverLifetimeConfig extractRotationConfig(const VFXNode& node);
        static GlowOverLifetimeConfig extractGlowConfig(const VFXNode& node);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static glm::vec4 getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue);
        static VFXCurve getCurve(const VFXNode& node, const std::string& propName, const VFXCurve& defaultValue);
        static VFXGradient getGradient(const VFXNode& node, const std::string& propName, const VFXGradient& defaultValue);
    };
}
