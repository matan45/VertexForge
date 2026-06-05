#pragma once

#include "MaterialTypes.hpp"
#include "../asset/AssetRef.hpp"
#include <glm/glm.hpp>
#include <string>
#include <map>
#include <optional>
#include <memory>

namespace material
{
    constexpr const char* MATERIAL_INSTANCE_FORMAT_VERSION = "1.0";

    struct MaterialInstanceData
    {
        std::string uuid;
        std::string name;

        asset::AssetRef parentMaterialRef;

        std::map<TextureSlot, asset::AssetRef> textureOverrides;

        // Overridden PBR scalar values
        std::optional<glm::vec4> albedoOverride;
        std::optional<float> metallicOverride;
        std::optional<float> roughnessOverride;
        std::optional<float> aoOverride;
        std::optional<float> emissionOverride;
        std::optional<float> iblDiffuseOverride;
        std::optional<float> iblSpecularOverride;

        bool isTextureOverridden(TextureSlot slot) const
        {
            return textureOverrides.contains(slot);
        }

        asset::AssetRef getTextureOverride(TextureSlot slot) const
        {
            auto it = textureOverrides.find(slot);
            return (it != textureOverrides.end()) ? it->second : asset::AssetRef::invalid();
        }

        void setTextureOverride(TextureSlot slot, const asset::AssetRef& ref)
        {
            if (!ref.isValid())
            {
                textureOverrides.erase(slot);
            }
            else
            {
                textureOverrides[slot] = ref;
            }
        }

        bool hasOverrides() const
        {
            return !textureOverrides.empty() ||
                albedoOverride.has_value() ||
                metallicOverride.has_value() ||
                roughnessOverride.has_value() ||
                aoOverride.has_value() ||
                emissionOverride.has_value() ||
                iblDiffuseOverride.has_value() ||
                iblSpecularOverride.has_value();
        }

        void clearAllOverrides()
        {
            textureOverrides.clear();
            albedoOverride.reset();
            metallicOverride.reset();
            roughnessOverride.reset();
            aoOverride.reset();
            emissionOverride.reset();
            iblDiffuseOverride.reset();
            iblSpecularOverride.reset();
        }
    };

    inline bool isInstanceFile(std::string_view path)
    {
        return path.ends_with(".vfMatInstance");
    }

    inline bool isMaterialFile(std::string_view path)
    {
        return path.ends_with(".vfMat");
    }

    inline std::string getDefaultInstancePath(const std::string& parentPath)
    {
        if (parentPath.ends_with(".vfMat"))
        {
            return parentPath.substr(0, parentPath.length() - 6) + "_inst.vfMatInstance";
        }
        return parentPath + "_inst.vfMatInstance";
    }
}
