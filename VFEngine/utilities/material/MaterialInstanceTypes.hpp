#pragma once

#include "MaterialTypes.hpp"
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

        // Reference to parent material (.vfMat file path)
        std::string parentMaterialPath;

        // Overridden textures by slot (only stores slots that differ from parent)
        std::map<TextureSlot, std::string> textureOverrides;

        // Overridden PBR scalar values
        std::optional<glm::vec4> albedoOverride;
        std::optional<float> metallicOverride;
        std::optional<float> roughnessOverride;
        std::optional<float> aoOverride;
        std::optional<float> emissionOverride;
        std::optional<float> iblDiffuseOverride;
        std::optional<float> iblSpecularOverride;

        // Helper to check if a texture slot is overridden
        bool isTextureOverridden(TextureSlot slot) const
        {
            return textureOverrides.contains(slot);
        }

        // Get texture path for a slot (empty if not overridden)
        std::string getTextureOverride(TextureSlot slot) const
        {
            auto it = textureOverrides.find(slot);
            return (it != textureOverrides.end()) ? it->second : "";
        }

        // Set or clear texture override
        void setTextureOverride(TextureSlot slot, const std::string& path)
        {
            if (path.empty())
            {
                textureOverrides.erase(slot);
            }
            else
            {
                textureOverrides[slot] = path;
            }
        }

        // Check if any overrides are set
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

        // Clear all overrides
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

    // Helper to detect material instance files by extension
    inline bool isInstanceFile(std::string_view path)
    {
        return path.ends_with(".vfMatInstance");
    }

    // Helper to detect regular material files
    inline bool isMaterialFile(std::string_view path)
    {
        return path.ends_with(".vfMat");
    }

    // Helper to get instance path from parent path
    inline std::string getDefaultInstancePath(const std::string& parentPath)
    {
        if (parentPath.ends_with(".vfMat"))
        {
            return parentPath.substr(0, parentPath.length() - 6) + "_inst.vfMatInstance";
        }
        return parentPath + "_inst.vfMatInstance";
    }
}
