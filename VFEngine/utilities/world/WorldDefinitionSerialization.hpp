#pragma once

// Compiled by the Serialization DLL (not the World DLL) — see premake5.lua removefiles in World project
#include "../serialization/SerializationExport.hpp"
#include "WorldDefinition.hpp"
#include <string>

namespace world
{
    class VF_SERIALIZATION_API WorldDefinitionSerialization
    {
    public:
        static bool save(const WorldDefinition& definition, const std::string& filePath);
        static bool load(const std::string& filePath, WorldDefinition& outDefinition);
    };

} // namespace world
