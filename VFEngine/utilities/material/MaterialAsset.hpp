#pragma once
#include "MaterialTypes.hpp"
#include <string_view>
#include <optional>

namespace material {

    class MaterialAsset {
    public:
        // Load material from .vfMat file
        static std::optional<MaterialData> load(std::string_view path);

        // Save material to .vfMat file
        static bool save(std::string_view path, const MaterialData& material);

        // Create a default material with basic PBR setup
        static MaterialData createDefault(const std::string& name = "New Material");

    private:
        // Migrate material from older format versions
        static void migrateFromVersion(MaterialData& material, const std::string& fromVersion);
    };

}
