#pragma once
#include "MetaJsonSerializer.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VK-1651: strict, by-name field access for EnTT-meta reflected (plugin) components.
//
// serializeFields emits exactly the JSON the PluginManager scene-serialize hook writes
// for one component (AssetRef members carry a "<name>Path" sibling). applyFields is the
// strict counterpart of the deserialize hook: every field is converted and validated
// BEFORE any is written, so a bad field leaves the instance untouched.
//
// `ref` MUST be a reference meta_any into the live object (bridge.metaType.from_void(ptr)).
// Never copy-construct it: copying a reference meta_any makes an owning deep copy in
// EnTT 4, and writes would silently land on the copy.
namespace serialization::meta
{
    struct PatchResult
    {
        bool ok = false;
        std::string error;  // set when !ok
    };

    // Human-readable name of a reflected field type, used in schemas and error messages.
    inline std::string metaTypeName(const entt::meta_type& type)
    {
        if (!type) return "unknown";
        if (type.info() == entt::type_id<int>()) return "int";
        if (type.info() == entt::type_id<float>()) return "float";
        if (type.info() == entt::type_id<bool>()) return "bool";
        if (type.info() == entt::type_id<std::string>()) return "string";
        if (type.info() == entt::type_id<glm::vec2>()) return "vec2";
        if (type.info() == entt::type_id<glm::vec3>()) return "vec3";
        if (type.info() == entt::type_id<glm::vec4>()) return "vec4";
        if (type.info() == entt::type_id<glm::quat>()) return "quat";
        if (type.info() == entt::type_id<asset::AssetRef>()) return "AssetRef";
        if (type.is_enum())
            return "enum:" + (type.name().empty() ? std::string(type.info().name()) : std::string(type.name()));
        if (type.is_sequence_container())
        {
            // value_type() lives on the container proxy; a default-constructed value gives one.
            auto sample = type.construct();
            auto view = sample.as_sequence_container();
            return view ? "array<" + metaTypeName(view.value_type()) + ">" : "array";
        }
        if (type.is_associative_container())
        {
            auto sample = type.construct();
            auto view = sample.as_associative_container();
            return view ? "map<" + metaTypeName(view.key_type()) + "," + metaTypeName(view.mapped_type()) + ">" : "map";
        }
        if (type.is_class())
            return "struct:" + (type.name().empty() ? std::string(type.info().name()) : std::string(type.name()));
        return std::string(type.info().name());
    }

    // Serialize every reflected member of one instance. Same format as the PluginManager
    // scene-serialize hook (PluginManager::initializeAll). Always returns an object.
    inline nlohmann::json serializeFields(entt::meta_any& ref, const entt::meta_type& type)
    {
        nlohmann::json out = nlohmann::json::object();
        for (auto&& [id, member] : type.data())
        {
            auto val = member.get(ref);
            if (!val) continue;
            const char* name = member.name().data();
            if (!name) continue;
            if (member.type().info() == entt::type_id<asset::AssetRef>())
            {
                serialization::writeAssetRef(out, name, val.cast<asset::AssetRef>());
                continue;
            }
            auto serialized = serializeMetaAny(val, member.type());
            if (!serialized.is_null())
                out[name] = std::move(serialized);
        }
        return out;
    }

    inline PatchResult applyFields(entt::meta_any& ref, const entt::meta_type& type, const nlohmann::json& fields,
                                   const std::function<bool(std::string_view field)>& isReadOnly = {});

    namespace detail
    {
        inline std::size_t vectorArity(const entt::meta_type& type)
        {
            if (type.info() == entt::type_id<glm::vec2>()) return 2;
            if (type.info() == entt::type_id<glm::vec3>()) return 3;
            if (type.info() == entt::type_id<glm::vec4>()) return 4;
            if (type.info() == entt::type_id<glm::quat>()) return 4;
            return 0;
        }

        inline bool isLeafScalar(const entt::meta_type& type)
        {
            return type.info() == entt::type_id<int>() || type.info() == entt::type_id<float>()
                || type.info() == entt::type_id<bool>() || type.info() == entt::type_id<std::string>()
                || vectorArity(type) != 0;
        }

        // Owning copy of a member's current value. The copy-construction is deliberate:
        // if a plugin registered the member with a reference policy, get() returns a
        // reference meta_any, and copying it is what yields an independent value.
        inline entt::meta_any ownedMemberValue(const entt::meta_data& member, entt::meta_any& instance)
        {
            const entt::meta_any current = member.get(instance);
            entt::meta_any owned{current};
            return owned;
        }

        // "" and the all-zero GUID (what writeAssetRef emits for an unset ref) both mean
        // "no asset", so serializeFields output for an empty AssetRef patches back cleanly.
        inline bool isUnsetAssetRefText(const std::string& text)
        {
            return text.empty() || text == asset::AssetGUID::invalid().toString();
        }

        inline std::string enumNames(const entt::meta_type& type)
        {
            std::string names;
            for (auto [id, member] : type.data())
            {
                const char* n = member.name().data();
                if (!n) continue;
                if (!names.empty()) names += ", ";
                names += n;
            }
            return names;
        }

        // AssetRef from a JSON leaf (container element): GUID hex string, path, or null.
        inline bool convertAssetRefLeaf(const nlohmann::json& j, entt::meta_any& out, std::string& error,
                                        const std::string& path)
        {
            if (j.is_null()) { out = asset::AssetRef::invalid(); return true; }
            if (!j.is_string())
            {
                error = "field '" + path + "' expects AssetRef (GUID hex or asset path string)";
                return false;
            }
            const std::string text = j.get<std::string>();
            nlohmann::json wrapper = nlohmann::json::object();
            wrapper["ref"] = text;
            auto ref = serialization::readAssetRef(wrapper, "ref", "");
            if (!isUnsetAssetRefText(text) && !ref.isValid())
            {
                error = "field '" + path + "': '" + text + "' is not a known asset GUID or path";
                return false;
            }
            out = ref;
            return true;
        }

        // Converts `j` to a value of `type`. `current` is an owning copy of the existing
        // value (empty for new container elements); containers and structs are built on
        // it so a struct patch merges instead of replacing.
        inline bool convertValue(const nlohmann::json& j, const entt::meta_type& type, entt::meta_any current,
                                 entt::meta_any& out, std::string& error, const std::string& path)
        {
            const std::string expected = metaTypeName(type);

            if (type.info() == entt::type_id<asset::AssetRef>())
                return convertAssetRefLeaf(j, out, error, path);

            if (isLeafScalar(type) || type.is_enum())
            {
                if (const std::size_t arity = vectorArity(type); arity != 0 && (!j.is_array() || j.size() != arity))
                {
                    error = "field '" + path + "' expects " + expected + " (array of " + std::to_string(arity) + " numbers)";
                    return false;
                }
                entt::meta_any converted;
                try
                {
                    converted = jsonToMetaAny(j, type);
                }
                catch (const nlohmann::json::exception&)
                {
                    converted = {};
                }
                if (!converted)
                {
                    error = "field '" + path + "' expects " + expected;
                    if (type.is_enum()) error += " (one of: " + enumNames(type) + ")";
                    error += ", got " + j.dump();
                    return false;
                }
                out = std::move(converted);
                return true;
            }

            if (type.is_sequence_container())
            {
                if (!j.is_array())
                {
                    error = "field '" + path + "' expects " + expected + " (JSON array)";
                    return false;
                }
                entt::meta_any container = current ? std::move(current) : type.construct();
                auto view = container.as_sequence_container();
                if (!view || !view.clear())
                {
                    error = "field '" + path + "': " + expected + " cannot be resized";
                    return false;
                }
                for (std::size_t i = 0; i < j.size(); ++i)
                {
                    entt::meta_any element;
                    const std::string elementPath = path + "[" + std::to_string(i) + "]";
                    if (!convertValue(j[i], view.value_type(), {}, element, error, elementPath))
                        return false;
                    if (!view.insert(view.end(), std::move(element)))
                    {
                        error = "field '" + elementPath + "' could not be inserted into " + expected;
                        return false;
                    }
                }
                out = std::move(container);
                return true;
            }

            if (type.is_associative_container())
            {
                if (!j.is_object())
                {
                    error = "field '" + path + "' expects " + expected + " (JSON object)";
                    return false;
                }
                entt::meta_any container = current ? std::move(current) : type.construct();
                auto view = container.as_associative_container();
                if (!view || !view.clear())
                {
                    error = "field '" + path + "': " + expected + " cannot be modified";
                    return false;
                }
                for (auto& [key, value] : j.items())
                {
                    const std::string entryPath = path + "[" + key + "]";
                    auto keyAny = jsonKeyToMetaAny(key, view.key_type());
                    if (!keyAny)
                    {
                        error = "field '" + entryPath + "': key is not a valid " + metaTypeName(view.key_type());
                        return false;
                    }
                    entt::meta_any mapped;
                    if (!convertValue(value, view.mapped_type(), {}, mapped, error, entryPath))
                        return false;
                    if (!view.insert(std::move(keyAny), std::move(mapped)))
                    {
                        error = "field '" + entryPath + "' could not be inserted into " + expected;
                        return false;
                    }
                }
                out = std::move(container);
                return true;
            }

            if (type.is_class())
            {
                if (!j.is_object())
                {
                    error = "field '" + path + "' expects " + expected + " (JSON object of sub-fields)";
                    return false;
                }
                entt::meta_any instance = current ? std::move(current) : type.construct();
                if (!instance)
                {
                    error = "field '" + path + "': " + expected + " is not default-constructible";
                    return false;
                }
                auto nested = applyFields(instance, type, j);
                if (!nested.ok)
                {
                    error = "field '" + path + "': " + nested.error;
                    return false;
                }
                out = std::move(instance);
                return true;
            }

            error = "field '" + path + "' has unsupported type " + expected;
            return false;
        }

        // A direct AssetRef member: "<name>" (GUID hex / path / null) and/or "<name>Path".
        // Mirrors the deserialize hook's readAssetRef(compJson, name) call.
        inline bool convertAssetRefMember(const nlohmann::json& fields, const std::string& name, entt::meta_any& out,
                                          std::string& error)
        {
            const std::string pathKey = name + "Path";
            if (auto it = fields.find(name); it != fields.end())
            {
                if (it->is_null()) { out = asset::AssetRef::invalid(); return true; }
                if (!it->is_string())
                {
                    error = "field '" + name + "' expects AssetRef (GUID hex or asset path string)";
                    return false;
                }
                if (auto pathIt = fields.find(pathKey); pathIt != fields.end() && !pathIt->is_string())
                {
                    error = "field '" + pathKey + "' expects a string";
                    return false;
                }
                const std::string text = it->get<std::string>();
                auto ref = serialization::readAssetRef(fields, name, "");
                if (!isUnsetAssetRefText(text) && !ref.isValid())
                {
                    error = "field '" + name + "': '" + text + "' is not a known asset GUID or path";
                    return false;
                }
                out = ref;
                return true;
            }

            const auto& pathValue = fields.at(pathKey);
            if (!pathValue.is_string())
            {
                error = "field '" + pathKey + "' expects a string";
                return false;
            }
            const std::string assetPath = pathValue.get<std::string>();
            auto ref = asset::AssetRef::fromPath(assetPath);
            if (!assetPath.empty() && !ref.isValid())
            {
                error = "field '" + pathKey + "': '" + assetPath + "' is not a known asset path";
                return false;
            }
            out = ref;
            return true;
        }

        inline PatchResult applyFieldsImpl(entt::meta_any& ref, const entt::meta_type& type,
                                           const nlohmann::json& fields,
                                           const std::function<bool(std::string_view field)>& isReadOnly)
        {
            if (!fields.is_object())
                return {false, "fields must be a JSON object, got " + std::string(fields.type_name())};

            struct Member
            {
                std::string name;
                entt::meta_data data;
                bool isAssetRef = false;
            };
            std::vector<Member> members;
            for (auto&& [id, member] : type.data())
            {
                const char* n = member.name().data();
                if (!n) continue;
                members.push_back({n, member, member.type().info() == entt::type_id<asset::AssetRef>()});
            }

            auto validNames = [&members]() {
                std::string names;
                for (const auto& m : members)
                {
                    if (!names.empty()) names += ", ";
                    names += m.name;
                    if (m.isAssetRef) names += ", " + m.name + "Path";
                }
                return names;
            };

            // Resolve every key to a member first: an unknown key fails the whole patch.
            std::vector<const Member*> targets;
            for (auto it = fields.begin(); it != fields.end(); ++it)
            {
                const std::string& key = it.key();
                const Member* match = nullptr;
                for (const auto& m : members)
                {
                    if (key == m.name || (m.isAssetRef && key == m.name + "Path"))
                    {
                        match = &m;
                        break;
                    }
                }
                if (!match)
                    return {false, "unknown field '" + key + "'; valid fields: " + validNames()};
                if (match->data.is_const() || (isReadOnly && isReadOnly(match->name)))
                    return {false, "field '" + match->name + "' is read-only"};
                // An AssetRef given as both "<name>" and "<name>Path" resolves once.
                if (std::find(targets.begin(), targets.end(), match) == targets.end())
                    targets.push_back(match);
            }

            // Convert everything before writing anything.
            std::vector<std::pair<entt::meta_data, entt::meta_any>> pending;
            pending.reserve(targets.size());
            for (const Member* m : targets)
            {
                entt::meta_any value;
                std::string error;
                const bool converted = m->isAssetRef
                    ? convertAssetRefMember(fields, m->name, value, error)
                    : convertValue(fields.at(m->name), m->data.type(), ownedMemberValue(m->data, ref), value, error, m->name);
                if (!converted)
                    return {false, std::move(error)};
                pending.emplace_back(m->data, std::move(value));
            }

            // Write. A setter rejecting its value restores the members already written.
            std::vector<std::pair<entt::meta_data, entt::meta_any>> originals;
            originals.reserve(pending.size());
            for (auto& [data, value] : pending)
            {
                entt::meta_any original = ownedMemberValue(data, ref);
                if (!data.set(ref, value))
                {
                    for (auto it = originals.rbegin(); it != originals.rend(); ++it)
                        it->first.set(ref, it->second);
                    const char* n = data.name().data();
                    return {false, "field '" + std::string(n ? n : "<unnamed>") + "' rejected value of type "
                                   + metaTypeName(value.type())};
                }
                originals.emplace_back(data, std::move(original));
            }
            return {true, {}};
        }
    }

    // Strict partial update of the instance behind `ref`. `fields` must be an object of
    // reflected member names (plus "<name>Path" for AssetRef members). Unknown, read-only
    // (const member or isReadOnly(name)) and mistyped fields fail the whole patch with no
    // member written. Containers are replaced wholesale; nested structs are merged.
    inline PatchResult applyFields(entt::meta_any& ref, const entt::meta_type& type, const nlohmann::json& fields,
                                   const std::function<bool(std::string_view field)>& isReadOnly)
    {
        if (!ref)
            return {false, "invalid component instance"};
        return detail::applyFieldsImpl(ref, type, fields, isReadOnly);
    }
}
