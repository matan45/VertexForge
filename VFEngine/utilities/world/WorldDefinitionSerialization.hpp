#pragma once

#include "WorldDefinition.hpp"
#include <string>

namespace world
{
    class WorldDefinitionSerialization
    {
    public:
        static bool save(const WorldDefinition& definition, const std::string& filePath);
        static bool load(const std::string& filePath, WorldDefinition& outDefinition);
    };

} // namespace world
