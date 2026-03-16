#pragma once
#include "../data/ActionMappingTypes.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace serialization
{
    struct InputMappingData
    {
        struct ActionData
        {
            std::vector<services::InputBinding> bindings;
        };

        std::unordered_map<std::string, ActionData> actions;
        std::unordered_map<std::string, services::Axis1DDefinition> axes1D;
        std::unordered_map<std::string, services::Axis2DDefinition> axes2D;
    };

    class InputMappingSerialization
    {
    public:
        static bool save(const InputMappingData& data, const std::string& filePath);
        static bool load(const std::string& filePath, InputMappingData& outData);
    };
}
