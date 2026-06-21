#pragma once
#include <nlohmann/json.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

#include "AssetRefSerializationHelper.hpp"
#include "../asset/AssetRef.hpp"
#include "../print/Log.hpp"

// Generic EnTT-meta <-> JSON converters shared by the plugin-component serialization
// hooks. Extracted verbatim from PluginManager.cpp (Phase 0) so the AssetRef field
// type, enum int-round-trip fix, and diagnostics live in one place rather than being
// copy-pasted across the serialize/deserialize sites.
//
// Behavior note: glm::quat keeps the JSON layout [x,y,z,w] and the read order
// quat(w,x,y,z) verbatim — existing scenes depend on it.
namespace serialization::meta
{
    inline nlohmann::json serializeMetaAny(const entt::meta_any& value, const entt::meta_type& type)
    {
        if (type.info() == entt::type_id<int>()) return value.cast<int>();
        if (type.info() == entt::type_id<float>()) return value.cast<float>();
        if (type.info() == entt::type_id<bool>()) return value.cast<bool>();
        if (type.info() == entt::type_id<std::string>()) return value.cast<std::string>();
        if (type.info() == entt::type_id<glm::vec2>()) {
            auto v = value.cast<glm::vec2>();
            return nlohmann::json::array({v.x, v.y});
        }
        if (type.info() == entt::type_id<glm::vec3>()) {
            auto v = value.cast<glm::vec3>();
            return nlohmann::json::array({v.x, v.y, v.z});
        }
        if (type.info() == entt::type_id<glm::vec4>()) {
            auto v = value.cast<glm::vec4>();
            return nlohmann::json::array({v.x, v.y, v.z, v.w});
        }
        if (type.info() == entt::type_id<glm::quat>()) {
            auto q = value.cast<glm::quat>();
            return nlohmann::json::array({q.x, q.y, q.z, q.w});
        }
        // AssetRef as a leaf (e.g. element of std::vector<AssetRef>): emit the GUID hex.
        // Direct component/struct members go through the object-building loops, which also
        // emit the <key>Path sibling; a leaf has no sibling slot, so GUID-only is correct.
        if (type.info() == entt::type_id<asset::AssetRef>()) {
            return value.cast<asset::AssetRef>().toHexString();
        }

        // Enums — serialize as string (enum value name)
        if (type.is_enum())
        {
            for (auto [id, member] : type.data())
            {
                if (member.get({}) == value)
                {
                    const char* n = member.name();
                    if (n) return std::string(n);
                }
            }
            // Fallback: serialize as underlying integer
            return value.allow_cast<int>().cast<int>();
        }

        // Sequence containers (std::vector<T>, etc.)
        if (type.is_sequence_container())
        {
            auto view = value.as_sequence_container();
            auto arr = nlohmann::json::array();
            for (std::size_t i = 0, n = view.size(); i < n; ++i)
            {
                arr.push_back(serializeMetaAny(view[i], view.value_type()));
            }
            return arr;
        }

        // Associative containers (std::map<K,V>, std::unordered_map<K,V>, etc.)
        if (type.is_associative_container())
        {
            auto view = value.as_associative_container();
            auto obj = nlohmann::json::object();
            for (auto it = view.begin(), last = view.end(); it != last; ++it)
            {
                auto [key, val] = *it;
                // Map keys are const-qualified in EnTT's view (value_type is
                // std::pair<const Key, V>), so a non-const try_cast<K>() fails.
                // Read through a const-qualified cast to recover the key.
                std::string keyStr;
                if (auto* s = key.try_cast<const std::string>())
                    keyStr = *s;
                else if (auto* i = key.try_cast<const int>())
                    keyStr = std::to_string(*i);
                else
                    continue;
                obj[keyStr] = serializeMetaAny(val, view.mapped_type());
            }
            return obj;
        }

        // Structs/classes — serialize each reflected member
        if (type.is_class())
        {
            auto obj = nlohmann::json::object();
            for (auto&& [id, member] : type.data())
            {
                const char* n = member.name();
                if (!n) continue;
                auto val = member.get(value);
                if (!val) continue;
                // AssetRef member — emit GUID hex + <name>Path sibling inside this object
                // (engine handles all AssetRef serialization; plugins only declare the field).
                if (member.type().info() == entt::type_id<asset::AssetRef>())
                {
                    serialization::writeAssetRef(obj, n, val.cast<asset::AssetRef>());
                    continue;
                }
                auto serialized = serializeMetaAny(val, member.type());
                if (!serialized.is_null())
                    obj[n] = std::move(serialized);
            }
            return obj.empty() ? nullptr : obj;
        }

        return nullptr;
    }

    // Convert a JSON value to an entt::meta_any matching the expected meta type.
    inline entt::meta_any jsonToMetaAny(const nlohmann::json& j, const entt::meta_type& type)
    {
        if (type.info() == entt::type_id<int>() && j.is_number_integer())
            return j.get<int>();
        if (type.info() == entt::type_id<float>() && j.is_number())
            return j.get<float>();
        if (type.info() == entt::type_id<bool>() && j.is_boolean())
            return j.get<bool>();
        if (type.info() == entt::type_id<std::string>() && j.is_string())
            return j.get<std::string>();
        if (type.info() == entt::type_id<glm::vec2>() && j.is_array() && j.size() >= 2)
            return glm::vec2(j[0].get<float>(), j[1].get<float>());
        if (type.info() == entt::type_id<glm::vec3>() && j.is_array() && j.size() >= 3)
            return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
        if (type.info() == entt::type_id<glm::vec4>() && j.is_array() && j.size() >= 4)
            return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
        if (type.info() == entt::type_id<glm::quat>() && j.is_array() && j.size() >= 4)
            return glm::quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
        // AssetRef leaf — j is the GUID hex string. The <key>Path sibling, when present,
        // is read by the object-building member loops; a bare leaf recovers from GUID alone.
        if (type.info() == entt::type_id<asset::AssetRef>())
        {
            nlohmann::json wrapper = nlohmann::json::object();
            wrapper["ref"] = j;
            return serialization::readAssetRef(wrapper, "ref", "");
        }
        // Enum from string name, or from underlying integer (round-trips the
        // serializeMetaAny int fallback for values with no reflected name).
        if (type.is_enum() && j.is_string())
        {
            std::string valName = j.get<std::string>();
            for (auto [id, member] : type.data())
            {
                const char* n = member.name();
                if (n && valName == n)
                    return member.get({});
            }
        }
        if (type.is_enum() && j.is_number_integer())
        {
            int target = j.get<int>();
            for (auto [id, member] : type.data())
            {
                const auto memberVal = member.get({});
                if (memberVal && memberVal.allow_cast<int>().cast<int>() == target)
                    return member.get({});
            }
        }
        // Structs/classes — default-construct then populate members from JSON object
        if (type.is_class() && j.is_object())
        {
            auto instance = type.construct();
            if (!instance) return {};
            for (auto&& [id, member] : type.data())
            {
                const char* n = member.name();
                if (!n) continue;
                // AssetRef member — read GUID hex + <name>Path sibling from this object.
                if (member.type().info() == entt::type_id<asset::AssetRef>())
                {
                    member.set(instance, serialization::readAssetRef(j, n, ""));
                    continue;
                }
                if (!j.contains(n)) continue;
                auto converted = jsonToMetaAny(j[n], member.type());
                if (converted)
                    member.set(instance, converted);
            }
            return instance;
        }
        return {};
    }

    // Convert a JSON object key string to an entt::meta_any matching the expected key type.
    inline entt::meta_any jsonKeyToMetaAny(const std::string& key, const entt::meta_type& keyType)
    {
        if (keyType.info() == entt::type_id<std::string>())
            return key;
        if (keyType.info() == entt::type_id<int>())
        {
            try { return std::stoi(key); }
            catch (...) { return {}; }
        }
        return {};
    }

    inline void deserializeMetaData(entt::meta_data data, entt::meta_any& instance, const nlohmann::json& value)
    {
        auto type = data.type();
        // AssetRef leaf — value is the GUID hex string. The <key>Path sibling lives in the
        // parent object, which the PluginManager hook handles directly; when reached here
        // (e.g. a nested struct member) we recover the ref from the GUID hex alone.
        if (type.info() == entt::type_id<asset::AssetRef>())
        {
            nlohmann::json wrapper = nlohmann::json::object();
            wrapper["ref"] = value;
            data.set(instance, serialization::readAssetRef(wrapper, "ref", ""));
            return;
        }
        if (type.info() == entt::type_id<int>() && value.is_number_integer())
            data.set(instance, value.get<int>());
        else if (type.info() == entt::type_id<float>() && value.is_number())
            data.set(instance, value.get<float>());
        else if (type.info() == entt::type_id<bool>() && value.is_boolean())
            data.set(instance, value.get<bool>());
        else if (type.info() == entt::type_id<std::string>() && value.is_string())
            data.set(instance, value.get<std::string>());
        else if (type.info() == entt::type_id<glm::vec2>() && value.is_array() && value.size() >= 2)
            data.set(instance, glm::vec2(value[0].get<float>(), value[1].get<float>()));
        else if (type.info() == entt::type_id<glm::vec3>() && value.is_array() && value.size() >= 3)
            data.set(instance, glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
        else if (type.info() == entt::type_id<glm::vec4>() && value.is_array() && value.size() >= 4)
            data.set(instance, glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()));
        else if (type.info() == entt::type_id<glm::quat>() && value.is_array() && value.size() >= 4)
            data.set(instance, glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
        // Sequence containers (std::vector<T>, etc.)
        else if (type.is_sequence_container() && value.is_array())
        {
            auto fieldVal = data.get(instance);
            auto view = fieldVal.as_sequence_container();
            view.clear();
            for (const auto& elem : value)
            {
                auto converted = jsonToMetaAny(elem, view.value_type());
                if (converted)
                    view.insert(view.end(), converted);
            }
            data.set(instance, fieldVal);
        }
        // Associative containers (std::map<K,V>, etc.)
        else if (type.is_associative_container() && value.is_object())
        {
            auto fieldVal = data.get(instance);
            auto view = fieldVal.as_associative_container();
            view.clear();
            for (auto& [k, v] : value.items())
            {
                auto keyAny = jsonKeyToMetaAny(k, view.key_type());
                auto valAny = jsonToMetaAny(v, view.mapped_type());
                if (keyAny && valAny)
                    view.insert(keyAny, valAny);
            }
            data.set(instance, fieldVal);
        }
        // Enums — deserialize from string name or underlying integer (the int branch
        // round-trips serializeMetaAny's fallback for values with no reflected name).
        else if (type.is_enum() && value.is_string())
        {
            std::string valName = value.get<std::string>();
            for (auto [id, member] : type.data())
            {
                const char* n = member.name();
                if (n && valName == n)
                {
                    data.set(instance, member.get({}));
                    break;
                }
            }
        }
        else if (type.is_enum() && value.is_number_integer())
        {
            int target = value.get<int>();
            for (auto [id, member] : type.data())
            {
                const auto memberVal = member.get({});
                if (memberVal && memberVal.allow_cast<int>().cast<int>() == target)
                {
                    data.set(instance, member.get({}));
                    break;
                }
            }
        }
        // Structs/classes — deserialize from JSON object
        else if (type.is_class() && value.is_object())
        {
            auto constructed = jsonToMetaAny(value, type);
            if (constructed)
                data.set(instance, constructed);
        }
        // Diagnostics: nothing matched — warn so plugin authors can debug bad data.
        else
        {
            const char* fieldName = data.name();
            vfLogWarning("Plugin component field '{}' skipped: JSON value type does not match reflected field type",
                         fieldName ? fieldName : "<unnamed>");
        }
    }
}
