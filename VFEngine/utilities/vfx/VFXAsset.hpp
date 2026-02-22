#pragma once

#include "VFXTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>
#include <functional>

namespace vfx
{
    inline constexpr const char* VFX_FORMAT_VERSION = "1.0";

    class VFXAsset
    {
    public:
        static std::optional<VFXData> load(std::string_view path);
        static bool save(std::string_view path, const VFXData& vfxData);
        static VFXData createDefault(const std::string& name = "New VFX");

    private:
        static nlohmann::json serializeProperty(const VFXProperty& prop);
        static VFXProperty deserializeProperty(const nlohmann::json& j, const std::string& propName);

        static nlohmann::json serializePropertyValue(const VFXPropertyValue& val, VFXPropertyType type);
        static VFXPropertyValue deserializePropertyValue(const nlohmann::json& j, VFXPropertyType type);

        static nlohmann::json serializeNode(const VFXNode& node);
        static VFXNode deserializeNode(const nlohmann::json& j);

        static nlohmann::json serializeLink(const VFXNodeLink& link);
        static VFXNodeLink deserializeLink(const nlohmann::json& j);

        using WarningLogger = std::function<void(const std::string&)>;
        static void parseVFXNodes(const nlohmann::json& graphJson, VFXGraph& graph,
                                  const WarningLogger& logWarning);
        static void parseVFXLinks(const nlohmann::json& graphJson, VFXGraph& graph,
                                  const WarningLogger& logWarning);
    };
}
