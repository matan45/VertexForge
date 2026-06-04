#pragma once
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace serialization {
    
    inline void to_json(nlohmann::json& j, const glm::vec3& v) {
        j = nlohmann::json::array({ v.x, v.y, v.z });
    }

    inline void from_json(const nlohmann::json& j, glm::vec3& v) {
        v.x = j.at(0).get<float>();
        v.y = j.at(1).get<float>();
        v.z = j.at(2).get<float>();
    }

    inline void readVec2(const nlohmann::json& j, const std::string& key, glm::vec2& out) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 2) {
            out = glm::vec2(j[key][0].get<float>(), j[key][1].get<float>());
        }
    }

    inline nlohmann::json writeVec2(const glm::vec2& v) {
        return nlohmann::json::array({v.x, v.y});
    }

    inline void readVec4(const nlohmann::json& j, const std::string& key, glm::vec4& out) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
            out = glm::vec4(j[key][0].get<float>(), j[key][1].get<float>(),
                           j[key][2].get<float>(), j[key][3].get<float>());
        }
    }

    inline nlohmann::json writeVec4(const glm::vec4& v) {
        return nlohmann::json::array({v.x, v.y, v.z, v.w});
    }

}
