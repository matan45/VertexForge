#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    namespace
    {
        std::string decalShapeToString(components::DecalShape shape)
        {
            switch (shape)
            {
            case components::DecalShape::Circle: return "Circle";
            case components::DecalShape::Triangle: return "Triangle";
            case components::DecalShape::Rectangle:
            default: return "Rectangle";
            }
        }

        components::DecalShape stringToDecalShape(const std::string& str)
        {
            if (str == "Circle" || str == "circle") return components::DecalShape::Circle;
            if (str == "Triangle" || str == "triangle") return components::DecalShape::Triangle;
            return components::DecalShape::Rectangle;
        }

        components::DecalShape intToDecalShape(int32_t shape)
        {
            switch (shape)
            {
            case 1: return components::DecalShape::Circle;
            case 2: return components::DecalShape::Triangle;
            case 0:
            default: return components::DecalShape::Rectangle;
            }
        }
    }

    json SceneSerialization::serializeDecal(const components::DecalComponent& decal)
    {
        json j;
        j["shape"] = decalShapeToString(decal.shape);
        j["halfExtents"] = json::array({decal.halfExtents.x, decal.halfExtents.y, decal.halfExtents.z});

        if (decal.albedoTextureRef.isValid())
            writeAssetRef(j, "albedoTextureRef", decal.albedoTextureRef);
        if (decal.normalTextureRef.isValid())
            writeAssetRef(j, "normalTextureRef", decal.normalTextureRef);
        if (decal.ormTextureRef.isValid())
            writeAssetRef(j, "ormTextureRef", decal.ormTextureRef);

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

        if (auto it = j.find("shape"); it != j.end())
        {
            if (it->is_string())
                decal.shape = stringToDecalShape(it->get<std::string>());
            else if (it->is_number_integer())
                decal.shape = intToDecalShape(it->get<int32_t>());
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
