#include "SceneSerialization.hpp"
#include "../components/Components.hpp"
#include "../asset/AssetRef.hpp"

namespace serialization
{
    // Helper: read an AssetRef from JSON, supporting both new GUID format and legacy path format
    static asset::AssetRef readAssetRef(const nlohmann::json& j, const std::string& newKey, const std::string& legacyKey = "")
    {
        // Try new GUID key first
        if (auto it = j.find(newKey); it != j.end() && it->is_string())
        {
            std::string val = it->get<std::string>();
            if (!val.empty())
            {
                // Detect if it's a hex GUID or a file path
                bool isPath = val.find('.') != std::string::npos
                           || val.find('/') != std::string::npos
                           || val.find('\\') != std::string::npos;
                return isPath ? asset::AssetRef::fromPath(val) : asset::AssetRef::fromHexString(val);
            }
        }
        // Try legacy path key
        if (!legacyKey.empty())
        {
            if (auto it = j.find(legacyKey); it != j.end() && it->is_string())
            {
                std::string val = it->get<std::string>();
                if (!val.empty()) return asset::AssetRef::fromPath(val);
            }
        }
        return asset::AssetRef::invalid();
    }

    json SceneSerialization::serializeDecal(const components::DecalComponent& decal)
    {
        json j;
        j["halfExtents"] = json::array({decal.halfExtents.x, decal.halfExtents.y, decal.halfExtents.z});

        if (decal.albedoTextureRef.isValid())
            j["albedoTextureRef"] = decal.albedoTextureRef.toHexString();
        if (decal.normalTextureRef.isValid())
            j["normalTextureRef"] = decal.normalTextureRef.toHexString();
        if (decal.ormTextureRef.isValid())
            j["ormTextureRef"] = decal.ormTextureRef.toHexString();

        j["color"] = json::array({decal.color.r, decal.color.g, decal.color.b, decal.color.a});
        j["angleFadeStart"] = decal.angleFadeStart;
        j["angleFadeEnd"] = decal.angleFadeEnd;
        j["edgeFalloff"] = decal.edgeFalloff;
        j["sortPriority"] = decal.sortPriority;
        j["modifyNormals"] = decal.modifyNormals;
        j["normalStrength"] = decal.normalStrength;

        return j;
    }

    void SceneSerialization::deserializeDecal(const json& j, components::DecalComponent& decal)
    {
        if (auto it = j.find("halfExtents"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            decal.halfExtents = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }

        decal.albedoTextureRef = readAssetRef(j, "albedoTextureRef", "albedoTexture");
        decal.normalTextureRef = readAssetRef(j, "normalTextureRef", "normalTexture");
        decal.ormTextureRef = readAssetRef(j, "ormTextureRef", "ormTexture");

        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 4)
        {
            decal.color = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                     (*it)[2].get<float>(), (*it)[3].get<float>());
        }

        if (auto it = j.find("angleFadeStart"); it != j.end() && it->is_number())
            decal.angleFadeStart = it->get<float>();

        if (auto it = j.find("angleFadeEnd"); it != j.end() && it->is_number())
            decal.angleFadeEnd = it->get<float>();

        if (auto it = j.find("edgeFalloff"); it != j.end() && it->is_number())
            decal.edgeFalloff = it->get<float>();

        if (auto it = j.find("sortPriority"); it != j.end() && it->is_number_integer())
            decal.sortPriority = it->get<int32_t>();

        if (auto it = j.find("modifyNormals"); it != j.end() && it->is_boolean())
            decal.modifyNormals = it->get<bool>();

        if (auto it = j.find("normalStrength"); it != j.end() && it->is_number())
            decal.normalStrength = it->get<float>();
    }
}
