#pragma once
#include "MaterialTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>

namespace material {

    class MaterialAsset {
    public:
        static std::optional<MaterialData> load(std::string_view path);
        static bool save(std::string_view path, const MaterialData& material);
        static MaterialData createDefault(const std::string& name = "New Material");

        // Shared graph property serialization (used by TerrainMaterialAsset too)
        static nlohmann::json serializeProperty(const NodeProperty& prop);
        static NodeProperty deserializeProperty(const nlohmann::json& j, const std::string& context = "");

    private:
        static nlohmann::json serializeParamValue(const ParameterValue& val);
        static ParameterValue deserializeParamValue(const nlohmann::json& j, ParameterType type, const std::string& paramName = "");
    };

}
