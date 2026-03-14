#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeDecal(const components::DecalComponent& decal)
    {
        json j;
        j["halfExtents"] = json::array({decal.halfExtents.x, decal.halfExtents.y, decal.halfExtents.z});

        if (!decal.albedoTexture.empty())
            j["albedoTexture"] = decal.albedoTexture;
        if (!decal.normalTexture.empty())
            j["normalTexture"] = decal.normalTexture;
        if (!decal.ormTexture.empty())
            j["ormTexture"] = decal.ormTexture;

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

        if (auto it = j.find("albedoTexture"); it != j.end() && it->is_string())
        {
            decal.albedoTexture = it->get<std::string>();
        }

        if (auto it = j.find("normalTexture"); it != j.end() && it->is_string())
        {
            decal.normalTexture = it->get<std::string>();
        }

        if (auto it = j.find("ormTexture"); it != j.end() && it->is_string())
        {
            decal.ormTexture = it->get<std::string>();
        }

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
