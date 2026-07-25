#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    // VK-1607: WaterBodyComponent. Absent keys leave the component's own defaults, so scenes saved
    // before this story load unchanged and a field added later stays backward compatible without a
    // version number.

    json SceneSerialization::serializeWaterBody(const components::WaterBodyComponent& body)
    {
        json j;
        j["type"] = static_cast<uint32_t>(body.type);
        j["waterHeight"] = body.waterHeight;
        j["halfExtents"] = {body.halfExtents.x, body.halfExtents.y};
        j["depth"] = body.depth;
        j["bandMask"] = body.bandMask;
        j["physicsEnabled"] = body.physicsEnabled;
        j["isActive"] = body.isActive;
        return j;
    }

    void SceneSerialization::deserializeWaterBody(const json& j, components::WaterBodyComponent& body)
    {
        if (auto it = j.find("type"); it != j.end() && it->is_number_unsigned())
        {
            body.type = it->get<uint32_t>() == 1u ? components::WaterBodyType::Pool
                                                  : components::WaterBodyType::Lake;
        }
        if (auto it = j.find("waterHeight"); it != j.end() && it->is_number())
            body.waterHeight = it->get<float>();
        if (auto it = j.find("halfExtents"); it != j.end() && it->is_array() && it->size() >= 2)
            body.halfExtents = glm::vec2((*it)[0].get<float>(), (*it)[1].get<float>());
        if (auto it = j.find("depth"); it != j.end() && it->is_number())
            body.depth = it->get<float>();
        if (auto it = j.find("bandMask"); it != j.end() && it->is_number_unsigned())
            body.bandMask = it->get<uint32_t>();
        if (auto it = j.find("physicsEnabled"); it != j.end() && it->is_boolean())
            body.physicsEnabled = it->get<bool>();
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            body.isActive = it->get<bool>();
    }
}
