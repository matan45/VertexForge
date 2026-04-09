// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PluginComponentAPI.hpp"
#include "NativeHelpers.hpp"

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace core::api
{
    // ── File-static helpers ──

    static const plugin::MetaComponentBridge* findBridge(
        const std::vector<plugin::MetaComponentBridge>& bridges, const std::string& name)
    {
        for (const auto& bridge : bridges)
        {
            if (bridge.name && name == bridge.name)
                return &bridge;
        }
        return nullptr;
    }

    static std::optional<entt::entity> resolveEntity(int64_t id)
    {
        if (id < 0) return std::nullopt;
        auto entity = static_cast<entt::entity>(static_cast<uint32_t>(static_cast<uint64_t>(id)));
        auto& reg = scene::EntityRegistry::getRegistry();
        if (!reg.valid(entity)) return std::nullopt;
        return entity;
    }

    // Convert entt::meta_any (scalar) to mType Value
    static value::Value metaToValue(const entt::meta_any& val, const entt::meta_type& type)
    {
        if (!val) return value::Value(std::monostate{});
        if (type.info() == entt::type_id<int>())
            return value::Value(static_cast<int64_t>(val.cast<int>()));
        if (type.info() == entt::type_id<float>())
            return value::Value(static_cast<double>(val.cast<float>()));
        if (type.info() == entt::type_id<bool>())
            return value::Value(val.cast<bool>());
        if (type.info() == entt::type_id<std::string>())
            return value::Value(val.cast<std::string>());
        if (type.info() == entt::type_id<glm::vec3>())
            return makeVec3Array(val.cast<glm::vec3>());
        if (type.info() == entt::type_id<glm::vec2>()) {
            auto v = val.cast<glm::vec2>();
            auto arr = std::make_shared<value::NativeArray>(2, value::ValueType::FLOAT);
            arr->set(0, value::Value(static_cast<double>(v.x)));
            arr->set(1, value::Value(static_cast<double>(v.y)));
            return value::Value(arr);
        }
        if (type.info() == entt::type_id<glm::vec4>()) {
            auto v = val.cast<glm::vec4>();
            auto arr = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
            arr->set(0, value::Value(static_cast<double>(v.x)));
            arr->set(1, value::Value(static_cast<double>(v.y)));
            arr->set(2, value::Value(static_cast<double>(v.z)));
            arr->set(3, value::Value(static_cast<double>(v.w)));
            return value::Value(arr);
        }
        return value::Value(std::monostate{});
    }

    // Convert mType Value to entt::meta_any matching expected type
    static entt::meta_any valueToMeta(const value::Value& val, const entt::meta_type& type)
    {
        if (type.info() == entt::type_id<int>())
            return static_cast<int>(extractInt64(val));
        if (type.info() == entt::type_id<float>())
            return extractFloat(val);
        if (type.info() == entt::type_id<bool>())
            return extractBool(val);
        if (type.info() == entt::type_id<std::string>())
            return extractString(val);
        if (type.info() == entt::type_id<glm::vec3>()) {
            if (auto* arr = std::get_if<std::shared_ptr<value::NativeArray>>(&val)) {
                if (*arr && (*arr)->size() >= 3)
                    return glm::vec3(
                        static_cast<float>(std::get<double>((**arr)[0])),
                        static_cast<float>(std::get<double>((**arr)[1])),
                        static_cast<float>(std::get<double>((**arr)[2])));
            }
        }
        if (type.info() == entt::type_id<glm::vec2>()) {
            if (auto* arr = std::get_if<std::shared_ptr<value::NativeArray>>(&val)) {
                if (*arr && (*arr)->size() >= 2)
                    return glm::vec2(
                        static_cast<float>(std::get<double>((**arr)[0])),
                        static_cast<float>(std::get<double>((**arr)[1])));
            }
        }
        if (type.info() == entt::type_id<glm::vec4>()) {
            if (auto* arr = std::get_if<std::shared_ptr<value::NativeArray>>(&val)) {
                if (*arr && (*arr)->size() >= 4)
                    return glm::vec4(
                        static_cast<float>(std::get<double>((**arr)[0])),
                        static_cast<float>(std::get<double>((**arr)[1])),
                        static_cast<float>(std::get<double>((**arr)[2])),
                        static_cast<float>(std::get<double>((**arr)[3])));
            }
        }
        return {};
    }

    struct FieldResolution {
        entt::meta_any instance;
        entt::meta_data member;
    };

    static std::optional<FieldResolution> resolveField(
        const std::vector<plugin::MetaComponentBridge>& bridges,
        const std::vector<value::Value>& args,
        const char* context)
    {
        if (args.size() < 3) return std::nullopt;
        auto entityOpt = resolveEntity(extractInt64(args[0]));
        if (!entityOpt) return std::nullopt;

        std::string compName = extractString(args[1]);
        const auto* bridge = findBridge(bridges, compName);
        if (!bridge) {
            vfLogWarning("[PluginScript] {}: unknown component '{}'", context, compName);
            return std::nullopt;
        }

        auto& reg = scene::EntityRegistry::getRegistry();
        if (!bridge->has(reg, *entityOpt)) return std::nullopt;
        void* ptr = bridge->tryGet(reg, *entityOpt);
        if (!ptr || !bridge->metaType) return std::nullopt;

        auto instance = bridge->metaType.from_void(ptr);
        if (!instance) return std::nullopt;

        std::string fieldName = extractString(args[2]);
        for (auto&& [id, member] : bridge->metaType.data())
        {
            const char* name = member.name();
            if (name && fieldName == name)
                return FieldResolution{instance, member};
        }
        vfLogWarning("[PluginScript] {}: field '{}' not found on '{}'", context, fieldName, compName);
        return std::nullopt;
    }

    // ── Registration ──

    void PluginComponentAPI::registerAPI(
        services::ScriptInterpreter* interpreter,
        const std::vector<plugin::MetaComponentBridge>& bridges)
    {
        // Capture bridges by value (shared copy) so lambdas remain valid
        auto bridgesCopy = std::make_shared<std::vector<plugin::MetaComponentBridge>>(bridges);

        // _plugin_has(entityId, componentName) -> bool
        interpreter->registerNativeFunction("_plugin_has",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) return value::Value(false);
                auto entityOpt = resolveEntity(extractInt64(args[0]));
                if (!entityOpt) return value::Value(false);
                const auto* bridge = findBridge(*bridgesCopy, extractString(args[1]));
                if (!bridge) return value::Value(false);
                auto& reg = scene::EntityRegistry::getRegistry();
                return value::Value(bridge->has(reg, *entityOpt));
            });

        // _plugin_getInt
        interpreter->registerNativeFunction("_plugin_getInt",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getInt");
                if (!res) return value::Value(int64_t(0));
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<int>())
                    return value::Value(int64_t(0));
                return value::Value(static_cast<int64_t>(val.cast<int>()));
            });

        // _plugin_setInt
        interpreter->registerNativeFunction("_plugin_setInt",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setInt");
                if (!res || res->member.type().info() != entt::type_id<int>())
                    return value::Value(false);
                res->member.set(res->instance, static_cast<int>(extractInt64(args[3])));
                return value::Value(true);
            });

        // _plugin_getFloat
        interpreter->registerNativeFunction("_plugin_getFloat",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getFloat");
                if (!res) return value::Value(0.0);
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<float>())
                    return value::Value(0.0);
                return value::Value(static_cast<double>(val.cast<float>()));
            });

        // _plugin_setFloat
        interpreter->registerNativeFunction("_plugin_setFloat",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setFloat");
                if (!res || res->member.type().info() != entt::type_id<float>())
                    return value::Value(false);
                res->member.set(res->instance, extractFloat(args[3]));
                return value::Value(true);
            });

        // _plugin_getBool
        interpreter->registerNativeFunction("_plugin_getBool",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getBool");
                if (!res) return value::Value(false);
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<bool>())
                    return value::Value(false);
                return value::Value(val.cast<bool>());
            });

        // _plugin_setBool
        interpreter->registerNativeFunction("_plugin_setBool",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setBool");
                if (!res || res->member.type().info() != entt::type_id<bool>())
                    return value::Value(false);
                res->member.set(res->instance, extractBool(args[3]));
                return value::Value(true);
            });

        // _plugin_getString
        interpreter->registerNativeFunction("_plugin_getString",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getString");
                if (!res) return value::Value(std::string(""));
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<std::string>())
                    return value::Value(std::string(""));
                return value::Value(val.cast<std::string>());
            });

        // _plugin_setString
        interpreter->registerNativeFunction("_plugin_setString",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setString");
                if (!res || res->member.type().info() != entt::type_id<std::string>())
                    return value::Value(false);
                res->member.set(res->instance, extractString(args[3]));
                return value::Value(true);
            });

        // _plugin_getVec3
        interpreter->registerNativeFunction("_plugin_getVec3",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getVec3");
                if (!res) return value::Value(std::monostate{});
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<glm::vec3>())
                    return value::Value(std::monostate{});
                return makeVec3Array(val.cast<glm::vec3>());
            });

        // _plugin_setVec3
        interpreter->registerNativeFunction("_plugin_setVec3",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setVec3");
                if (!res || res->member.type().info() != entt::type_id<glm::vec3>())
                    return value::Value(false);
                auto* arrPtr = std::get_if<std::shared_ptr<value::NativeArray>>(&args[3]);
                if (!arrPtr || !*arrPtr || (*arrPtr)->size() < 3)
                    return value::Value(false);
                auto& arr = **arrPtr;
                glm::vec3 v(
                    static_cast<float>(std::get<double>(arr[0])),
                    static_cast<float>(std::get<double>(arr[1])),
                    static_cast<float>(std::get<double>(arr[2])));
                res->member.set(res->instance, v);
                return value::Value(true);
            });

        // ── Container bindings ──

        // _plugin_getArraySize
        interpreter->registerNativeFunction("_plugin_getArraySize",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getArraySize");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(int64_t(0));
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                return value::Value(static_cast<int64_t>(view.size()));
            });

        // _plugin_getArrayElement
        interpreter->registerNativeFunction("_plugin_getArrayElement",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(std::monostate{});
                auto res = resolveField(*bridgesCopy, args, "_plugin_getArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(std::monostate{});
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto idx = static_cast<std::size_t>(extractInt64(args[3]));
                if (idx >= view.size()) return value::Value(std::monostate{});
                return metaToValue(view[idx], view.value_type());
            });

        // _plugin_setArrayElement
        interpreter->registerNativeFunction("_plugin_setArrayElement",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 5) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto idx = static_cast<std::size_t>(extractInt64(args[3]));
                if (idx >= view.size()) return value::Value(false);
                auto converted = valueToMeta(args[4], view.value_type());
                if (!converted) return value::Value(false);
                view[idx].assign(converted);
                return value::Value(true);
            });

        // _plugin_addArrayElement
        interpreter->registerNativeFunction("_plugin_addArrayElement",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_addArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto converted = valueToMeta(args[3], view.value_type());
                if (!converted) return value::Value(false);
                view.insert(view.end(), converted);
                return value::Value(true);
            });

        // _plugin_removeArrayElement
        interpreter->registerNativeFunction("_plugin_removeArrayElement",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_removeArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto idx = static_cast<std::size_t>(extractInt64(args[3]));
                if (idx >= view.size()) return value::Value(false);
                auto it = view.begin();
                for (std::size_t i = 0; i < idx; ++i) ++it;
                view.erase(it);
                return value::Value(true);
            });

        // _plugin_getMapSize
        interpreter->registerNativeFunction("_plugin_getMapSize",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getMapSize");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(int64_t(0));
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                return value::Value(static_cast<int64_t>(view.size()));
            });

        // _plugin_getMapKeys
        interpreter->registerNativeFunction("_plugin_getMapKeys",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getMapKeys");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(std::monostate{});
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                auto arr = std::make_shared<value::NativeArray>(view.size(), value::ValueType::STRING);
                std::size_t i = 0;
                for (auto it = view.begin(), last = view.end(); it != last; ++it, ++i)
                {
                    auto [key, val] = *it;
                    if (auto* s = key.try_cast<std::string>())
                        arr->set(i, value::Value(*s));
                    else if (auto* n = key.try_cast<int>())
                        arr->set(i, value::Value(std::to_string(*n)));
                    else
                        arr->set(i, value::Value(std::string("?")));
                }
                return value::Value(arr);
            });

        // _plugin_getMapValue
        interpreter->registerNativeFunction("_plugin_getMapValue",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(std::monostate{});
                auto res = resolveField(*bridgesCopy, args, "_plugin_getMapValue");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(std::monostate{});
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                std::string keyStr = extractString(args[3]);
                entt::meta_any keyAny;
                if (view.key_type().info() == entt::type_id<std::string>())
                    keyAny = keyStr;
                else if (view.key_type().info() == entt::type_id<int>()) {
                    try { keyAny = std::stoi(keyStr); } catch (...) { return value::Value(std::monostate{}); }
                }
                if (!keyAny) return value::Value(std::monostate{});
                auto it = view.find(keyAny);
                if (it == view.end()) return value::Value(std::monostate{});
                auto [k, v] = *it;
                return metaToValue(v, view.mapped_type());
            });

        // _plugin_setMapValue
        interpreter->registerNativeFunction("_plugin_setMapValue",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 5) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setMapValue");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                std::string keyStr = extractString(args[3]);
                entt::meta_any keyAny;
                if (view.key_type().info() == entt::type_id<std::string>())
                    keyAny = keyStr;
                else if (view.key_type().info() == entt::type_id<int>()) {
                    try { keyAny = std::stoi(keyStr); } catch (...) { return value::Value(false); }
                }
                if (!keyAny) return value::Value(false);
                auto valAny = valueToMeta(args[4], view.mapped_type());
                if (!valAny) return value::Value(false);
                view.insert(keyAny, valAny);
                return value::Value(true);
            });

        // _plugin_removeMapEntry
        interpreter->registerNativeFunction("_plugin_removeMapEntry",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_removeMapEntry");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                std::string keyStr = extractString(args[3]);
                entt::meta_any keyAny;
                if (view.key_type().info() == entt::type_id<std::string>())
                    keyAny = keyStr;
                else if (view.key_type().info() == entt::type_id<int>()) {
                    try { keyAny = std::stoi(keyStr); } catch (...) { return value::Value(false); }
                }
                if (!keyAny) return value::Value(false);
                return value::Value(view.erase(keyAny) > 0);
            });

        // ── Enum bindings ──

        // _plugin_getEnum(entityId, componentName, fieldName) -> string (current enum value name)
        interpreter->registerNativeFunction("_plugin_getEnum",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getEnum");
                if (!res || !res->member.type().is_enum())
                    return value::Value(std::string(""));
                auto val = res->member.get(res->instance);
                for (auto [id, member] : res->member.type().data())
                {
                    if (member.get({}) == val)
                    {
                        const char* n = member.name();
                        if (n) return value::Value(std::string(n));
                    }
                }
                return value::Value(std::string(""));
            });

        // _plugin_setEnum(entityId, componentName, fieldName, valueName) -> bool
        interpreter->registerNativeFunction("_plugin_setEnum",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*bridgesCopy, args, "_plugin_setEnum");
                if (!res || !res->member.type().is_enum())
                    return value::Value(false);
                std::string valName = extractString(args[3]);
                for (auto [id, member] : res->member.type().data())
                {
                    const char* n = member.name();
                    if (n && valName == n)
                    {
                        res->member.set(res->instance, member.get({}));
                        return value::Value(true);
                    }
                }
                vfLogWarning("[PluginScript] _plugin_setEnum: unknown value '{}' for enum field", valName);
                return value::Value(false);
            });

        // _plugin_getEnumValues(entityId, componentName, fieldName) -> string[] (all enum value names)
        interpreter->registerNativeFunction("_plugin_getEnumValues",
            [bridgesCopy](const std::vector<value::Value>& args) -> value::Value {
                auto res = resolveField(*bridgesCopy, args, "_plugin_getEnumValues");
                if (!res || !res->member.type().is_enum())
                    return value::Value(std::monostate{});
                // Collect names
                std::vector<std::string> names;
                for (auto [id, member] : res->member.type().data())
                {
                    const char* n = member.name();
                    if (n) names.emplace_back(n);
                }
                auto arr = std::make_shared<value::NativeArray>(names.size(), value::ValueType::STRING);
                for (std::size_t i = 0; i < names.size(); ++i)
                    arr->set(i, value::Value(names[i]));
                return value::Value(arr);
            });

        vfLogInfo("[Plugin] Registered 24 plugin component script bindings");
    }
}
