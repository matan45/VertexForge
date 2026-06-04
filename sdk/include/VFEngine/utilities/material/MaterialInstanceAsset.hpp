#pragma once

#include "MaterialInstanceTypes.hpp"
#include "../asset/AssetRef.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>

namespace material
{
    class MaterialInstanceAsset
    {
    public:
        static std::optional<MaterialInstanceData> load(std::string_view path);
        static bool save(std::string_view path, const MaterialInstanceData& instance);
        static MaterialInstanceData createDefault(const std::string& name, const asset::AssetRef& parentRef);

    private:
        static nlohmann::json serializeTextureOverrides(const std::map<TextureSlot, asset::AssetRef>& overrides);
        static std::map<TextureSlot, asset::AssetRef> deserializeTextureOverrides(const nlohmann::json& j);
        static std::string textureSlotToString(TextureSlot slot);
        static TextureSlot stringToTextureSlot(const std::string& str);
    };
}
