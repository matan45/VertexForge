// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <environment/registry/ClassDefinition.hpp>
#include <environment/registry/FieldDefinition.hpp>
#include <value/ObjectInstance.hpp>
#include <environment/Environment.hpp>
#include <value/ValueShim.hpp>

#include "PluginComponentAPI.hpp"
#include "NativeHelpers.hpp"

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <sstream>
#include <span>

namespace core::api
{
    using runtimeTypes::klass::ClassDefinition;
    using runtimeTypes::klass::FieldDefinition;
    using runtimeTypes::klass::ObjectInstance;

    // ── Struct type cache (computed once by registerStructClasses) ──

    enum class FieldKind {
        SCALAR_INT, SCALAR_FLOAT, SCALAR_BOOL, SCALAR_STRING,
        VEC2, VEC3, VEC4, ARRAY, MAP, STRUCT
    };

    struct StructTypeMapping;

    struct StructFieldMapping {
        std::string name;
        entt::id_type metaId;
        entt::meta_data metaMember;      // cached meta_data handle (lightweight, no lifetime issues)
        FieldKind kind;
        StructTypeMapping* nestedMapping = nullptr;
        entt::meta_type elementType;     // for ARRAY: element type
        entt::meta_type keyType;         // for MAP: key type
        entt::meta_type mappedType;      // for MAP: value type
    };

    struct StructTypeMapping {
        std::string mtypeName;
        entt::meta_type metaType;
        std::shared_ptr<ClassDefinition> classDef;
        std::vector<StructFieldMapping> fields;
    };

    static std::unordered_map<entt::id_type, StructTypeMapping> structMappings;
    static std::shared_ptr<std::vector<plugin::MetaComponentBridge>> storedBridges;
    // Raw pointer — valid between registerStructClasses() and cleanup().
    // cleanup() is called before interpreter is destroyed (see ScriptingAdapter::cleanUp).
    static services::ScriptInterpreter* storedInterpreter = nullptr;

    // Extract short class name from C++ type name (strip namespaces and struct/class prefix)
    static std::string extractShortName(std::string_view fullName)
    {
        // Strip namespace (last ::)
        auto pos = fullName.rfind(':');
        if (pos != std::string_view::npos)
            fullName = fullName.substr(pos + 1);
        // Strip MSVC "struct " / "class " / "enum " prefix
        for (auto prefix : {"struct ", "class ", "enum "}) {
            auto len = std::string_view(prefix).size();
            if (fullName.size() > len && fullName.substr(0, len) == prefix)
                return std::string(fullName.substr(len));
        }
        return std::string(fullName);
    }

    // Classify an entt::meta_type into a FieldKind
    static FieldKind classifyType(const entt::meta_type& type)
    {
        if (type.info() == entt::type_id<int>())         return FieldKind::SCALAR_INT;
        if (type.info() == entt::type_id<float>())       return FieldKind::SCALAR_FLOAT;
        if (type.info() == entt::type_id<bool>())        return FieldKind::SCALAR_BOOL;
        if (type.info() == entt::type_id<std::string>()) return FieldKind::SCALAR_STRING;
        if (type.info() == entt::type_id<glm::vec2>())   return FieldKind::VEC2;
        if (type.info() == entt::type_id<glm::vec3>())   return FieldKind::VEC3;
        if (type.info() == entt::type_id<glm::vec4>())   return FieldKind::VEC4;
        if (type.is_sequence_container())                 return FieldKind::ARRAY;
        if (type.is_associative_container())              return FieldKind::MAP;
        if (type.is_enum())                               return FieldKind::SCALAR_INT; // enums handled via getEnum/setEnum
        if (type.is_class())                              return FieldKind::STRUCT;
        vfLogWarning("[Plugin] classifyType: unrecognised type '{}', treating as int", type.info().name());
        return FieldKind::SCALAR_INT;
    }

    // Map a FieldKind to mType ValueType for FieldDefinition
    static value::ValueType kindToValueType(FieldKind kind)
    {
        switch (kind) {
            case FieldKind::SCALAR_INT:    return value::ValueType::INT;
            case FieldKind::SCALAR_FLOAT:  return value::ValueType::FLOAT;
            case FieldKind::SCALAR_BOOL:   return value::ValueType::BOOL;
            case FieldKind::SCALAR_STRING: return value::ValueType::STRING;
            case FieldKind::VEC2:
            case FieldKind::VEC3:
            case FieldKind::VEC4:
            case FieldKind::ARRAY:         return value::ValueType::ARRAY;
            case FieldKind::MAP:
            case FieldKind::STRUCT:        return value::ValueType::OBJECT;
        }
        return value::ValueType::INT;
    }

    // Recursively register a struct type as an mType ClassDefinition
    static StructTypeMapping* registerStructType(
        const entt::meta_type& type,
        std::shared_ptr<environment::Environment> env)
    {
        auto hash = type.info().hash();
        auto it = structMappings.find(hash);
        if (it != structMappings.end())
            return &it->second;

        std::string className = extractShortName(type.info().name());

        // Create ClassDefinition and register in environment
        auto classDef = std::make_shared<ClassDefinition>(className);

        StructTypeMapping mapping;
        mapping.mtypeName = className;
        mapping.metaType = type;
        mapping.classDef = classDef;

        // Construct one temp instance for container element-type introspection.
        // EnTT doesn't expose container element types statically — an instance is needed.
        auto tempInstance = type.construct();

        // Scan fields
        for (auto&& [id, member] : type.data())
        {
            const char* n = member.name();
            if (!n) continue;

            StructFieldMapping field;
            field.name = n;
            field.metaId = id;
            field.metaMember = member;
            field.kind = classifyType(member.type());

            // For container fields, cache element/key/value types
            if (field.kind == FieldKind::ARRAY && tempInstance)
            {
                auto fieldVal = member.get(tempInstance);
                if (fieldVal) {
                    auto view = fieldVal.as_sequence_container();
                    field.elementType = view.value_type();
                    if (field.elementType.is_class())
                        field.nestedMapping = registerStructType(field.elementType, env);
                }
            }
            else if (field.kind == FieldKind::MAP && tempInstance)
            {
                auto fieldVal = member.get(tempInstance);
                if (fieldVal) {
                    auto view = fieldVal.as_associative_container();
                    field.keyType = view.key_type();
                    field.mappedType = view.mapped_type();
                    if (field.mappedType.is_class())
                        field.nestedMapping = registerStructType(field.mappedType, env);
                }
            }
            else if (field.kind == FieldKind::STRUCT)
            {
                field.nestedMapping = registerStructType(member.type(), env);
            }

            // Add field to ClassDefinition
            auto fieldDef = std::make_shared<FieldDefinition>(
                n, kindToValueType(field.kind), value::Value{},
                false, false, ast::AccessModifier::PUBLIC);
            classDef->addInstanceField(n, fieldDef);

            mapping.fields.push_back(std::move(field));
        }

        // Register class in mType environment (required for (ClassName) downcasts)
        if (env)
            env->registerClass(className, classDef);

        auto [insertIt, _] = structMappings.emplace(hash, std::move(mapping));
        vfLogDebug("[Plugin] Registered struct class '{}' with {} fields", className, insertIt->second.fields.size());
        return &insertIt->second;
    }

    // ── File-static helpers ──

    static const plugin::MetaComponentBridge* findBridge(
        const std::vector<plugin::MetaComponentBridge>& bridges, std::string_view name)
    {
        for (const auto& bridge : bridges)
        {
            if (name == bridge.name)
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

    // Convert entt::meta_any (scalar or struct) to mType Value
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

        // Struct types — look up cached mapping, create ObjectInstance
        auto mappingIt = structMappings.find(type.info().hash());
        if (mappingIt != structMappings.end())
        {
            auto& mapping = mappingIt->second;
            auto instance = std::make_shared<ObjectInstance>(mapping.classDef);
            for (auto& field : mapping.fields)
            {
                auto member = field.metaMember;
                if (!member) continue;
                auto fieldVal = member.get(val);
                if (!fieldVal) continue;

                switch (field.kind) {
                    case FieldKind::SCALAR_INT:
                        instance->setField(field.name, value::Value(static_cast<int64_t>(fieldVal.cast<int>())));
                        break;
                    case FieldKind::SCALAR_FLOAT:
                        instance->setField(field.name, value::Value(static_cast<double>(fieldVal.cast<float>())));
                        break;
                    case FieldKind::SCALAR_BOOL:
                        instance->setField(field.name, value::Value(fieldVal.cast<bool>()));
                        break;
                    case FieldKind::SCALAR_STRING:
                        instance->setField(field.name, value::Value(fieldVal.cast<std::string>()));
                        break;
                    case FieldKind::VEC2:
                    case FieldKind::VEC3:
                    case FieldKind::VEC4:
                        instance->setField(field.name, metaToValue(fieldVal, member.type()));
                        break;
                    case FieldKind::ARRAY: {
                        auto view = fieldVal.as_sequence_container();
                        auto arr = std::make_shared<value::NativeArray>(
                            view.size(), value::ValueType::OBJECT);
                        for (std::size_t i = 0; i < view.size(); ++i)
                            arr->set(i, metaToValue(view[i], view.value_type()));
                        instance->setField(field.name, value::Value(arr));
                        break;
                    }
                    case FieldKind::MAP: {
                        if (!storedInterpreter) break;
                        auto mapView = fieldVal.as_associative_container();
                        auto hashMap = storedInterpreter->createObject("HashMap");
                        if (value::isVoid(hashMap)) break;
                        for (auto mapIt = mapView.begin(); mapIt != mapView.end(); ++mapIt) {
                            auto [k, v] = *mapIt;
                            auto keyVal = metaToValue(k, mapView.key_type());
                            auto valVal = metaToValue(v, mapView.mapped_type());
                            storedInterpreter->callMethod(hashMap, "put", {keyVal, valVal});
                        }
                        instance->setField(field.name, hashMap);
                        break;
                    }
                    case FieldKind::STRUCT:
                        instance->setField(field.name, metaToValue(fieldVal, member.type()));
                        break;
                }
            }
            return value::Value(instance);
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
            if (value::isNativeArray(val)) {
                const auto& arr = value::asNativeArray(val);
                if (arr && arr->size() >= 3)
                    return glm::vec3(
                        static_cast<float>(value::asFloat((*arr)[0])),
                        static_cast<float>(value::asFloat((*arr)[1])),
                        static_cast<float>(value::asFloat((*arr)[2])));
            }
        }
        if (type.info() == entt::type_id<glm::vec2>()) {
            if (value::isNativeArray(val)) {
                const auto& arr = value::asNativeArray(val);
                if (arr && arr->size() >= 2)
                    return glm::vec2(
                        static_cast<float>(value::asFloat((*arr)[0])),
                        static_cast<float>(value::asFloat((*arr)[1])));
            }
        }
        if (type.info() == entt::type_id<glm::vec4>()) {
            if (value::isNativeArray(val)) {
                const auto& arr = value::asNativeArray(val);
                if (arr && arr->size() >= 4)
                    return glm::vec4(
                        static_cast<float>(value::asFloat((*arr)[0])),
                        static_cast<float>(value::asFloat((*arr)[1])),
                        static_cast<float>(value::asFloat((*arr)[2])),
                        static_cast<float>(value::asFloat((*arr)[3])));
            }
        }

        // Struct types — read ObjectInstance fields back into default-constructed struct
        auto mappingIt = structMappings.find(type.info().hash());
        if (mappingIt != structMappings.end())
        {
            if (!value::isObject(val)) return {};
            const auto& obj = value::asObject(val);
            if (!obj) return {};
            auto constructed = type.construct();
            if (!constructed) return {};

            for (auto& field : mappingIt->second.fields)
            {
                auto member = field.metaMember;
                if (!member) continue;

                auto fieldVal = obj->getFieldValue(field.name);
                if (value::isVoid(fieldVal)) continue;

                switch (field.kind) {
                    case FieldKind::SCALAR_INT:
                    case FieldKind::SCALAR_FLOAT:
                    case FieldKind::SCALAR_BOOL:
                    case FieldKind::SCALAR_STRING:
                    case FieldKind::VEC2:
                    case FieldKind::VEC3:
                    case FieldKind::VEC4:
                    case FieldKind::STRUCT: {
                        auto converted = valueToMeta(fieldVal, member.type());
                        if (converted) member.set(constructed, converted);
                        break;
                    }
                    case FieldKind::ARRAY: {
                        // Rebuild vector from NativeArray
                        auto fieldMeta = member.get(constructed);
                        auto view = fieldMeta.as_sequence_container();
                        if (value::isNativeArray(fieldVal)) {
                            const auto& arr = value::asNativeArray(fieldVal);
                            if (arr) {
                                view.clear();
                                for (std::size_t i = 0; i < arr->size(); ++i) {
                                    auto elemMeta = valueToMeta((*arr)[i], view.value_type());
                                    if (elemMeta) view.insert(view.end(), elemMeta);
                                }
                            }
                        }
                        member.set(constructed, fieldMeta);
                        break;
                    }
                    case FieldKind::MAP: {
                        // Rebuild C++ map from HashMap using stable public API (getKeys/get)
                        if (!storedInterpreter) break;
                        auto fieldMeta = member.get(constructed);
                        auto view = fieldMeta.as_associative_container();
                        view.clear();
                        // Use HashMap.getKeys() -> K[] and HashMap.get(key) -> V
                        auto keysResult = storedInterpreter->callMethod(fieldVal, "getKeys", {});
                        if (value::isNativeArray(keysResult)) {
                            const auto& keysArr = value::asNativeArray(keysResult);
                            if (keysArr) {
                                for (std::size_t i = 0; i < keysArr->size(); ++i) {
                                    auto keyVal = (*keysArr)[i];
                                    auto valResult = storedInterpreter->callMethod(fieldVal, "get", {keyVal});
                                    auto keyMeta = valueToMeta(keyVal, view.key_type());
                                    auto valMeta = valueToMeta(valResult, view.mapped_type());
                                    if (keyMeta && valMeta)
                                        view.insert(keyMeta, valMeta);
                                }
                            }
                        }
                        member.set(constructed, fieldMeta);
                        break;
                    }
                }
            }
            return constructed;
        }

        return {};
    }

    // ── Field resolution with dot-path support ──

    struct FieldResolution {
        entt::meta_any instance;
        entt::meta_data member;
        // For dot-path write-back: intermediate {member, parentCopy} pairs
        std::vector<std::pair<entt::meta_data, entt::meta_any>> chain;
    };

    // Split string by delimiter
    static std::vector<std::string> splitString(const std::string& s, char delim)
    {
        std::vector<std::string> parts;
        std::istringstream stream(s);
        std::string part;
        while (std::getline(stream, part, delim))
            if (!part.empty()) parts.push_back(part);
        return parts;
    }

    static std::optional<FieldResolution> resolveField(
        const std::vector<plugin::MetaComponentBridge>& bridges,
        std::span<const value::Value> args,
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

        std::string fieldPath = extractString(args[2]);
        auto parts = splitString(fieldPath, '.');

        if (parts.empty()) return std::nullopt;

        entt::meta_any current = instance;
        entt::meta_type currentType = bridge->metaType;
        std::vector<std::pair<entt::meta_data, entt::meta_any>> chain;
        entt::meta_data finalMember{};

        for (size_t i = 0; i < parts.size(); ++i)
        {
            bool found = false;
            for (auto&& [id, member] : currentType.data())
            {
                const char* n = member.name();
                if (n && parts[i] == n)
                {
                    if (i == parts.size() - 1) {
                        finalMember = member;
                    } else {
                        // Intermediate segment — drill into nested struct
                        chain.push_back({member, current});
                        current = member.get(current); // returns COPY
                        currentType = member.type();
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                vfLogWarning("[PluginScript] {}: field '{}' not found on '{}'", context, parts[i], compName);
                return std::nullopt;
            }
        }

        if (!finalMember) return std::nullopt;
        return FieldResolution{current, finalMember, std::move(chain)};
    }

    // Write back dot-path chain after setting a leaf field.
    // member.get() returns by value (entt default as_value_t policy), so nested struct
    // modifications must be propagated back up. This is the same pattern used in
    // MetaComponentDrawer.cpp:86-111 (drawElementValue returns modified parent).
    // Note: the root instance (chain[0].second) was created via from_void(ptr) which
    // wraps the live component memory — changes to it propagate to the ECS registry.
    static void writeBackChain(FieldResolution& res)
    {
        if (res.chain.empty()) return;
        entt::meta_any childCopy = res.instance;
        for (int i = static_cast<int>(res.chain.size()) - 1; i >= 0; --i)
        {
            auto& [parentMember, parentCopy] = res.chain[i];
            parentMember.set(parentCopy, childCopy);
            childCopy = parentCopy;
        }
    }

    // ── Registration ──

    void PluginComponentAPI::registerAPI(
        services::ScriptInterpreter* interpreter,
        const std::vector<plugin::MetaComponentBridge>& bridges)
    {
        // Capture bridges into the static storedBridges so capture-less native
        // function pointers can access them. Lifetime extends until cleanup().
        storedBridges = std::make_shared<std::vector<plugin::MetaComponentBridge>>(bridges);

        // _plugin_has(entityId, componentName) -> bool
        interpreter->registerNativeFunction("_plugin_has",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(false);
                auto entityOpt = resolveEntity(extractInt64(args[0]));
                if (!entityOpt) return value::Value(false);
                const auto* bridge = findBridge(*storedBridges, extractString(args[1]));
                if (!bridge) return value::Value(false);
                auto& reg = scene::EntityRegistry::getRegistry();
                return value::Value(bridge->has(reg, *entityOpt));
            }});

        // _plugin_addComponent(entityId, componentName) -> bool
        interpreter->registerNativeFunction("_plugin_addComponent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(false);
                auto entityOpt = resolveEntity(extractInt64(args[0]));
                if (!entityOpt) return value::Value(false);
                std::string compName = extractString(args[1]);
                const auto* bridge = findBridge(*storedBridges, compName);
                if (!bridge || !bridge->emplace) {
                    vfLogWarning("[PluginScript] _plugin_addComponent: unknown component '{}'", compName);
                    return value::Value(false);
                }
                auto& reg = scene::EntityRegistry::getRegistry();
                bridge->emplace(reg, *entityOpt);
                return value::Value(true);
            }});

        // _plugin_removeComponent(entityId, componentName) -> bool
        interpreter->registerNativeFunction("_plugin_removeComponent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(false);
                auto entityOpt = resolveEntity(extractInt64(args[0]));
                if (!entityOpt) return value::Value(false);
                std::string compName = extractString(args[1]);
                const auto* bridge = findBridge(*storedBridges, compName);
                if (!bridge || !bridge->remove) {
                    vfLogWarning("[PluginScript] _plugin_removeComponent: unknown component '{}'", compName);
                    return value::Value(false);
                }
                auto& reg = scene::EntityRegistry::getRegistry();
                bridge->remove(reg, *entityOpt);
                return value::Value(true);
            }});

        // _plugin_findAll(componentName) -> int[] (entity IDs holding the component)
        interpreter->registerNativeFunction("_plugin_findAll",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto empty = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                if (args.empty()) return value::Value(empty);
                std::string compName = extractString(args[0]);
                const auto* bridge = findBridge(*storedBridges, compName);
                if (!bridge || !bridge->findAll) {
                    vfLogWarning("[PluginScript] _plugin_findAll: unknown component '{}'", compName);
                    return value::Value(empty);
                }
                auto& reg = scene::EntityRegistry::getRegistry();
                auto entities = bridge->findAll(reg);
                auto arr = std::make_shared<value::NativeArray>(entities.size(), value::ValueType::INT);
                for (std::size_t i = 0; i < entities.size(); ++i)
                    arr->set(i, value::Value(static_cast<int64_t>(static_cast<uint32_t>(entities[i]))));
                return value::Value(arr);
            }});

        // _plugin_getInt
        interpreter->registerNativeFunction("_plugin_getInt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getInt");
                if (!res) return value::Value(int64_t(0));
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<int>())
                    return value::Value(int64_t(0));
                return value::Value(static_cast<int64_t>(val.cast<int>()));
            }});

        // _plugin_setInt
        interpreter->registerNativeFunction("_plugin_setInt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setInt");
                if (!res || res->member.type().info() != entt::type_id<int>())
                    return value::Value(false);
                res->member.set(res->instance, static_cast<int>(extractInt64(args[3])));
                return value::Value(true);
            }});

        // _plugin_getFloat
        interpreter->registerNativeFunction("_plugin_getFloat",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getFloat");
                if (!res) return value::Value(0.0);
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<float>())
                    return value::Value(0.0);
                return value::Value(static_cast<double>(val.cast<float>()));
            }});

        // _plugin_setFloat
        interpreter->registerNativeFunction("_plugin_setFloat",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setFloat");
                if (!res || res->member.type().info() != entt::type_id<float>())
                    return value::Value(false);
                res->member.set(res->instance, extractFloat(args[3]));
                return value::Value(true);
            }});

        // _plugin_getBool
        interpreter->registerNativeFunction("_plugin_getBool",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getBool");
                if (!res) return value::Value(false);
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<bool>())
                    return value::Value(false);
                return value::Value(val.cast<bool>());
            }});

        // _plugin_setBool
        interpreter->registerNativeFunction("_plugin_setBool",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setBool");
                if (!res || res->member.type().info() != entt::type_id<bool>())
                    return value::Value(false);
                res->member.set(res->instance, extractBool(args[3]));
                return value::Value(true);
            }});

        // _plugin_getString
        interpreter->registerNativeFunction("_plugin_getString",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getString");
                if (!res) return value::Value(std::string(""));
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<std::string>())
                    return value::Value(std::string(""));
                return value::Value(val.cast<std::string>());
            }});

        // _plugin_setString
        interpreter->registerNativeFunction("_plugin_setString",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setString");
                if (!res || res->member.type().info() != entt::type_id<std::string>())
                    return value::Value(false);
                res->member.set(res->instance, extractString(args[3]));
                return value::Value(true);
            }});

        // _plugin_getVec3
        interpreter->registerNativeFunction("_plugin_getVec3",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getVec3");
                if (!res) return value::Value(std::monostate{});
                auto val = res->member.get(res->instance);
                if (!val || res->member.type().info() != entt::type_id<glm::vec3>())
                    return value::Value(std::monostate{});
                return makeVec3Array(val.cast<glm::vec3>());
            }});

        // _plugin_setVec3
        interpreter->registerNativeFunction("_plugin_setVec3",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setVec3");
                if (!res || res->member.type().info() != entt::type_id<glm::vec3>())
                    return value::Value(false);
                if (!value::isNativeArray(args[3])) return value::Value(false);
                const auto& arrPtr = value::asNativeArray(args[3]);
                if (!arrPtr || arrPtr->size() < 3)
                    return value::Value(false);
                auto& arr = *arrPtr;
                glm::vec3 v(
                    static_cast<float>(value::asFloat(arr[0])),
                    static_cast<float>(value::asFloat(arr[1])),
                    static_cast<float>(value::asFloat(arr[2])));
                res->member.set(res->instance, v);
                return value::Value(true);
            }});

        // ── Container bindings ──

        // _plugin_getArraySize
        interpreter->registerNativeFunction("_plugin_getArraySize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getArraySize");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(int64_t(0));
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                return value::Value(static_cast<int64_t>(view.size()));
            }});

        // _plugin_getArrayElement
        interpreter->registerNativeFunction("_plugin_getArrayElement",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(std::monostate{});
                auto res = resolveField(*storedBridges, args, "_plugin_getArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(std::monostate{});
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto idx = static_cast<std::size_t>(extractInt64(args[3]));
                if (idx >= view.size()) return value::Value(std::monostate{});
                return metaToValue(view[idx], view.value_type());
            }});

        // _plugin_setArrayElement
        interpreter->registerNativeFunction("_plugin_setArrayElement",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setArrayElement");
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
            }});

        // _plugin_addArrayElement
        interpreter->registerNativeFunction("_plugin_addArrayElement",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_addArrayElement");
                if (!res || !res->member.type().is_sequence_container())
                    return value::Value(false);
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_sequence_container();
                auto converted = valueToMeta(args[3], view.value_type());
                if (!converted) return value::Value(false);
                view.insert(view.end(), converted);
                return value::Value(true);
            }});

        // _plugin_removeArrayElement
        interpreter->registerNativeFunction("_plugin_removeArrayElement",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_removeArrayElement");
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
            }});

        // _plugin_getMapSize
        interpreter->registerNativeFunction("_plugin_getMapSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getMapSize");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(int64_t(0));
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                return value::Value(static_cast<int64_t>(view.size()));
            }});

        // _plugin_getMapKeys
        interpreter->registerNativeFunction("_plugin_getMapKeys",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getMapKeys");
                if (!res || !res->member.type().is_associative_container())
                    return value::Value(std::monostate{});
                auto fieldVal = res->member.get(res->instance);
                auto view = fieldVal.as_associative_container();
                auto arr = std::make_shared<value::NativeArray>(view.size(), value::ValueType::STRING);
                std::size_t i = 0;
                for (auto it = view.begin(), last = view.end(); it != last; ++it, ++i)
                {
                    auto [key, val] = *it;
                    if (view.key_type().info() == entt::type_id<std::string>())
                        arr->set(i, value::Value(key.cast<std::string>()));
                    else if (view.key_type().info() == entt::type_id<int>())
                        arr->set(i, value::Value(std::to_string(key.cast<int>())));
                    else
                        arr->set(i, value::Value(std::string("?")));
                }
                return value::Value(arr);
            }});

        // _plugin_getMapValue
        interpreter->registerNativeFunction("_plugin_getMapValue",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(std::monostate{});
                auto res = resolveField(*storedBridges, args, "_plugin_getMapValue");
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
            }});

        // _plugin_setMapValue
        interpreter->registerNativeFunction("_plugin_setMapValue",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setMapValue");
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
            }});

        // _plugin_removeMapEntry
        interpreter->registerNativeFunction("_plugin_removeMapEntry",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_removeMapEntry");
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
            }});

        // ── Enum bindings ──

        // _plugin_getEnum(entityId, componentName, fieldName) -> string (current enum value name)
        interpreter->registerNativeFunction("_plugin_getEnum",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getEnum");
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
            }});

        // _plugin_setEnum(entityId, componentName, fieldName, valueName) -> bool
        interpreter->registerNativeFunction("_plugin_setEnum",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_setEnum");
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
            }});

        // _plugin_getEnumValues(entityId, componentName, fieldName) -> string[] (all enum value names)
        interpreter->registerNativeFunction("_plugin_getEnumValues",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_getEnumValues");
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
            }});

        // ── Unified get/set (Godot-style: returns Object, script casts) ──

        // _plugin_get(entityId, componentName, fieldName) -> Object (any type)
        interpreter->registerNativeFunction("_plugin_get",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto res = resolveField(*storedBridges, args, "_plugin_get");
                if (!res) return value::Value(std::monostate{});
                auto val = res->member.get(res->instance);
                return metaToValue(val, res->member.type());
            }});

        // _plugin_set(entityId, componentName, fieldName, value) -> bool
        interpreter->registerNativeFunction("_plugin_set",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(false);
                auto res = resolveField(*storedBridges, args, "_plugin_set");
                if (!res) return value::Value(false);
                auto converted = valueToMeta(args[3], res->member.type());
                if (!converted) return value::Value(false);
                res->member.set(res->instance, converted);
                writeBackChain(*res);
                return value::Value(true);
            }});

        vfLogInfo("[Plugin] Registered 29 plugin component script bindings");
    }

    void PluginComponentAPI::registerStructClasses(services::ScriptInterpreter* interpreter)
    {
        if (!storedBridges || !interpreter) return;

        // Clear previous mappings (may be stale from prior build cycle)
        structMappings.clear();

        storedInterpreter = interpreter;
        auto env = interpreter->getEnvironment();
        if (!env) return;

        // Scan all bridge meta types for struct fields
        for (const auto& bridge : *storedBridges)
        {
            if (!bridge.metaType) continue;

            for (auto&& [id, member] : bridge.metaType.data())
            {
                const char* n = member.name();
                if (!n) continue;

                auto memberType = member.type();

                // Direct struct fields — only register user-defined structs
                // (must have reflected data members and not be a container or known engine type)
                if (memberType.is_class()
                    && !memberType.is_sequence_container()
                    && !memberType.is_associative_container()
                    && classifyType(memberType) == FieldKind::STRUCT)
                    registerStructType(memberType, env);

                // Container element types
                if (memberType.is_sequence_container())
                {
                    // Need a temp instance to get element type from view
                    auto temp = bridge.metaType.construct();
                    if (temp) {
                        auto fieldVal = member.get(temp);
                        if (fieldVal) {
                            auto view = fieldVal.as_sequence_container();
                            if (view.value_type().is_class())
                                registerStructType(view.value_type(), env);
                        }
                    }
                }

                if (memberType.is_associative_container())
                {
                    auto temp = bridge.metaType.construct();
                    if (temp) {
                        auto fieldVal = member.get(temp);
                        if (fieldVal) {
                            auto view = fieldVal.as_associative_container();
                            if (view.mapped_type().is_class())
                                registerStructType(view.mapped_type(), env);
                        }
                    }
                }
            }
        }

        vfLogInfo("[Plugin] Registered {} struct classes for script access", structMappings.size());
    }

    void PluginComponentAPI::cleanup()
    {
        structMappings.clear();
        storedBridges.reset();
        storedInterpreter = nullptr;
    }
}
