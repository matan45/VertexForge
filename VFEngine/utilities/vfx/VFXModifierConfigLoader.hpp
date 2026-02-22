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

    private:
        static std::vector<const VFXNode*> getModifierNodeChain(const VFXGraph& graph);
        static VFXModifierConfig nodeToConfig(const VFXNode& node);

        static ColorOverLifetimeConfig extractColorConfig(const VFXNode& node);
        static SizeOverLifetimeConfig extractSizeConfig(const VFXNode& node);
        static SpeedOverLifetimeConfig extractSpeedConfig(const VFXNode& node);
        static RotationOverLifetimeConfig extractRotationConfig(const VFXNode& node);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static glm::vec4 getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue);
        static VFXCurve getCurve(const VFXNode& node, const std::string& propName, const VFXCurve& defaultValue);
        static VFXGradient getGradient(const VFXNode& node, const std::string& propName, const VFXGradient& defaultValue);
    };
}
