#pragma once

#include "VFXTypes.hpp"
#include "VFXShapeTypes.hpp"

namespace vfx
{
    class VFXShapeConfigLoader
    {
    public:
        static ShapeConfig fromGraph(const VFXGraph& graph);

    private:
        static const VFXNode* findShapeNode(const VFXGraph& graph);
        static ShapeConfig nodeToConfig(const VFXNode& node);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
        static std::string getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue);
    };
}
