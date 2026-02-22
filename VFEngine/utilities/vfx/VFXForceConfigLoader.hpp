#pragma once

#include "VFXTypes.hpp"
#include "VFXForceTypes.hpp"
#include <vector>

namespace vfx
{
    class VFXForceConfigLoader
    {
    public:
        static VFXForceChain fromGraph(const VFXGraph& graph);

    private:
        static std::vector<const VFXNode*> getForceNodeChain(const VFXGraph& graph);
        static VFXForceConfig nodeToConfig(const VFXNode& node);

        static GravityForceConfig extractGravityConfig(const VFXNode& node);
        static WindForceConfig extractWindConfig(const VFXNode& node);
        static TurbulenceForceConfig extractTurbulenceConfig(const VFXNode& node);
        static VortexForceConfig extractVortexConfig(const VFXNode& node);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static int getInt(const VFXNode& node, const std::string& propName, int defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
    };
}
