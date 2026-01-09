#pragma once
#include <string_view>
#include <string>
#include "../types/PhysicsTypes.hpp"

namespace serialization
{
    class PhysicsSettingsSerialization
    {
    public:
        // Save physics settings to a .vfPhysicsConfig file
        static bool save(const types::PhysicsSettings& settings, std::string_view filename);

        // Load physics settings from a .vfPhysicsConfig file
        static bool load(std::string_view filename, types::PhysicsSettings& settings);

        // Get default filename for physics config
        static std::string getDefaultFilename();
    };
}
