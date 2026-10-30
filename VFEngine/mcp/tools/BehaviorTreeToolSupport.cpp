#include "BehaviorTreeToolSupport.hpp"
#include "ContentHelpers.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "behaviortree/BehaviorTreeAsset.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeAuthoringEvents.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/ai/EQSEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace mcp::tools::bttool
{
    namespace
    {
        namespace fs = std::filesystem;
        using bt::catalog::NodeCategory;
        using bt::catalog::NodeSpec;
        using bt::catalog::ParamKind;
        using bt::catalog::ParamSpec;
        using Type = bt::BlackboardValueType;

        // Ids an agent may give; allocation continues above the largest one, far from uint32 overflow.
        constexpr int64_t maxGivenId = 1000000000;
        // Editor trees are walked (reported, laid out) down to this depth only: a recursion bound.
        constexpr std::size_t maxWalkDepth = 512;
        constexpr std::uintmax_t maxScriptBytes = 1024 * 1024;

        constexpr std::array<std::string_view, 8> nodeKeys{
            "type", "name", "id", "position", "params", "children", "child", "mappings"};
        // KeyRef parameters a node reads its target / route from, and those it writes.
        constexpr std::array<std::string_view, 2> targetParams{"targetKey", "routeKey"};
        constexpr std::array<std::string_view, 5> outputParams{
            "resultKey", "resultEntityKey", "distanceKey", "visibilityKey", "focusKey"};

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() &&
                std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
                {
                    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                });
        }

        template <typename Range>
        std::string joined(const Range& items, std::string_view separator = ", ")
        {
            std::string out;
            for (const auto& item : items)
            {
                if (!out.empty())
                {
                    out += separator;
                }
                out += item;
            }
            return out;
        }

        template <std::size_t N>
        bool listed(const std::array<std::string_view, N>& names, std::string_view name)
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        // The integral value of a JSON number (3 and 3.0 alike); nullopt for anything else.
        std::optional<int64_t> integralValue(const nlohmann::json& value)
        {
            if (value.is_number_unsigned())
            {
                const uint64_t number = value.get<uint64_t>();
                if (number > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                {
                    return std::nullopt;
                }
                return static_cast<int64_t>(number);
            }
            if (value.is_number_integer())
            {
                return value.get<int64_t>();
            }
            if (value.is_number_float())
            {
                const double number = value.get<double>();
                if (!std::isfinite(number) || number != std::trunc(number) || std::fabs(number) > 9.0e15)
                {
                    return std::nullopt;
                }
                return static_cast<int64_t>(number);
            }
            return std::nullopt;
        }

        std::string text(const nlohmann::json& value)
        {
            return env::detail::valueText(value);
        }

        std::string floatText(float value)
        {
            return env::detail::floatText(value);
        }

        std::string typeName(Type type)
        {
            return bt::blackboardValueTypeToString(type);
        }

        // "Vec3, Entity or Int"
        std::string typesText(const std::vector<Type>& types)
        {
            std::string out;
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                if (i > 0)
                {
                    out += i + 1 == types.size() ? " or " : ", ";
                }
                out += typeName(types[i]);
            }
            return out;
        }

        bool allows(const ParamSpec& param, Type type)
        {
            return std::find(param.keyTypes.begin(), param.keyTypes.end(), type) != param.keyTypes.end();
        }

        // The member `key` when present (null included), else nullptr.
        const nlohmann::json* rawMember(const nlohmann::json& object, const std::string& key)
        {
            if (!object.is_object())
            {
                return nullptr;
            }
            const auto it = object.find(key);
            return it == object.end() ? nullptr : &*it;
        }

        // The member `key` when present and not null, else nullptr.
        const nlohmann::json* member(const nlohmann::json& object, const std::string& key)
        {
            const nlohmann::json* value = rawMember(object, key);
            return value != nullptr && !value->is_null() ? value : nullptr;
        }

        std::string stringProperty(const bt::BTNode& node, const std::string& name)
        {
            const auto it = node.properties.find(name);
            if (it != node.properties.end())
            {
                if (const auto* value = std::get_if<std::string>(&it->second))
                {
                    return *value;
                }
            }
            return {};
        }

        std::string storedServiceType(const bt::BTNode& node)
        {
            return stringProperty(node, "serviceType");
        }

        // The canonical spelling of a service type, "" when unknown.
        std::string canonicalServiceType(std::string_view value)
        {
            for (const std::string& type : bt::catalog::serviceTypes())
            {
                if (equalsIgnoreCase(type, value))
                {
                    return type;
                }
            }
            return {};
        }

        std::vector<const ParamSpec*> allParams(const NodeSpec& spec)
        {
            std::vector<const ParamSpec*> params;
            for (const ParamSpec& param : spec.params)
            {
                params.push_back(&param);
            }
            return params;
        }

        // The parameters that apply to `node` (a Service with an unknown serviceType: every variant's).
        std::vector<const ParamSpec*> paramsOf(const NodeSpec& spec, const bt::BTNode& node)
        {
            if (node.type != bt::BTNodeType::Service)
            {
                return bt::catalog::paramsFor(spec);
            }
            const std::string serviceType = canonicalServiceType(storedServiceType(node));
            return serviceType.empty() ? allParams(spec) : bt::catalog::paramsFor(spec, serviceType);
        }

        const ParamSpec* findParam(const std::vector<const ParamSpec*>& params, std::string_view name)
        {
            for (const ParamSpec* param : params)
            {
                if (param->name == name)
                {
                    return param;
                }
            }
            return nullptr;
        }

        std::string paramNames(const std::vector<const ParamSpec*>& params)
        {
            std::vector<std::string> names;
            for (const ParamSpec* param : params)
            {
                if (std::find(names.begin(), names.end(), param->name) == names.end())
                {
                    names.push_back(param->name);
                }
            }
            return names.empty() ? std::string("none") : joined(names);
        }

        std::string childRule(const NodeSpec& spec)
        {
            const auto plural = [](uint32_t count) { return count == 1 ? std::string(" child") : std::string(" children"); };
            if (spec.maxChildren == 0)
            {
                return "cannot have children";
            }
            if (spec.minChildren == spec.maxChildren)
            {
                return "needs exactly " + std::to_string(spec.minChildren) + plural(spec.minChildren);
            }
            if (spec.maxChildren == bt::catalog::kUnlimitedChildren)
            {
                return "needs at least " + std::to_string(spec.minChildren) + plural(spec.minChildren);
            }
            return "needs " + std::to_string(spec.minChildren) + " to " + std::to_string(spec.maxChildren) + " children";
        }

        std::string nodeTypeList()
        {
            std::vector<std::string> names;
            for (const NodeSpec& spec : bt::catalog::nodes())
            {
                if (spec.category != NodeCategory::Root)
                {
                    names.push_back(spec.name);
                }
            }
            return joined(names);
        }

        std::string keyListText(const std::vector<bt::BlackboardKeyDef>& keys)
        {
            if (keys.empty())
            {
                return "the tree declares no keys";
            }
            std::vector<std::string> names;
            for (const bt::BlackboardKeyDef& key : keys)
            {
                names.push_back(key.name + " (" + typeName(key.type) + ")");
            }
            return "declared: " + joined(names);
        }

        std::optional<std::string> readSmallFile(const fs::path& path)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
            {
                return std::nullopt;
            }
            const std::uintmax_t size = fs::file_size(path, ec);
            if (ec || size > maxScriptBytes)
            {
                return std::nullopt;
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return std::nullopt;
            }
            return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }

        // Nodes by id and children in tick order (links to missing nodes are ignored). Read-only view.
        struct GraphIndex
        {
            explicit GraphIndex(const bt::BTGraph& graph)
            {
                for (const bt::BTNode& node : graph.nodes)
                {
                    nodes.emplace(node.id, &node);
                }
                std::unordered_map<uint32_t, std::vector<const bt::BTLink*>> links;
                for (const bt::BTLink& link : graph.links)
                {
                    if (nodes.contains(link.sourceNodeId) && nodes.contains(link.targetNodeId))
                    {
                        links[link.sourceNodeId].push_back(&link);
                    }
                }
                for (auto& [parent, list] : links)
                {
                    std::sort(list.begin(), list.end(), [](const bt::BTLink* a, const bt::BTLink* b)
                    {
                        return std::tie(a->sortOrder, a->id) < std::tie(b->sortOrder, b->id);
                    });
                    std::vector<uint32_t>& out = children[parent];
                    for (const bt::BTLink* link : list)
                    {
                        out.push_back(link->targetNodeId);
                    }
                }
            }

            const bt::BTNode* node(uint32_t id) const
            {
                const auto it = nodes.find(id);
                return it == nodes.end() ? nullptr : it->second;
            }

            const std::vector<uint32_t>& childrenOf(uint32_t id) const
            {
                static const std::vector<uint32_t> none;
                const auto it = children.find(id);
                return it == children.end() ? none : it->second;
            }

            std::unordered_map<uint32_t, const bt::BTNode*> nodes;
            std::unordered_map<uint32_t, std::vector<uint32_t>> children;
        };

        // ------------------------------------------------------------------
        // Paths
        // ------------------------------------------------------------------

        // The .vfBehaviorTree file `given` names inside the asset folder `root`, or nullopt with `error` set.
        std::optional<fs::path> locateTree(const fs::path& root, const std::string& given, bool mustExist,
                                           std::string& error)
        {
            if (given.empty())
            {
                error = "must be a .vfBehaviorTree path relative to the project's asset folder, e.g. "
                        "\"ai/EnemyAI.vfBehaviorTree\"";
                return std::nullopt;
            }
            std::string pathError;
            const std::optional<fs::path> inside = resolveInside(root, given, pathError, treeExtension);
            std::error_code ec;
            if (inside.has_value() && (!mustExist || fs::is_regular_file(*inside, ec)))
            {
                return inside;
            }
            if (!mustExist)
            {
                error = pathError;
                return std::nullopt;
            }

            // The engine's own spelling of an existing tree: relative to the project folder.
            const std::string engine = bt::BehaviorTreeAsset::canonicalPath(given);
            if (!engine.empty())
            {
                std::string engineError;
                const std::optional<fs::path> viaEngine =
                    resolveInside(root, pathToUtf8(fs::path(engine)), engineError, treeExtension);
                if (viaEngine.has_value() && fs::is_regular_file(*viaEngine, ec))
                {
                    return viaEngine;
                }
            }
            error = inside.has_value()
                ? "no behavior tree at '" + given + "' (paths are relative to the project's asset folder; bt_create "
                  "makes a new tree)"
                : pathError;
            return std::nullopt;
        }

        TreeFile makeTreeFile(const std::optional<fs::path>& root, const fs::path& file)
        {
            TreeFile out;
            out.file = file;
            out.enginePath = file.string();
            out.relative = projectRelativeUtf8(root, file);
            return out;
        }

        // ------------------------------------------------------------------
        // Stored keys and nodes
        // ------------------------------------------------------------------

        // validateStrict refuses an Entity key with a default (ids change on every load); clear such a default
        // so an editor-made tree stays editable.
        void normalizeKeys(bt::BTGraph& graph, std::vector<std::string>& warnings)
        {
            for (bt::BlackboardKeyDef& key : graph.blackboardKeys)
            {
                if (key.type != Type::Entity)
                {
                    continue;
                }
                const auto* handle = std::get_if<services::EntityHandle>(&key.defaultValue);
                if (handle == nullptr || handle->isValid())
                {
                    key.defaultValue = services::EntityHandle::invalid();
                    warnings.push_back("Entity key '" + key.name + "' had a default, which was cleared: an Entity key "
                                       "starts as no entity (give the value per entity)");
                }
            }
        }

        // A stored node an edit touches must pass the strict checks: drop the properties its type does not have,
        // store each parameter in its kind (an Int where a Float belongs is ignored by the runtime), spell enums
        // canonically and write the optional parameters it lacks.
        void normalizeNode(bt::BTNode& node, std::vector<std::string>& warnings)
        {
            const NodeSpec* spec = bt::catalog::find(node.type);
            if (spec == nullptr)
            {
                return;
            }
            if (node.type == bt::BTNodeType::Service)
            {
                const std::string canonical = canonicalServiceType(storedServiceType(node));
                if (!canonical.empty())
                {
                    node.properties["serviceType"] = canonical;
                }
            }
            const std::vector<const ParamSpec*> params = paramsOf(*spec, node);

            std::vector<std::string> dropped;
            for (auto it = node.properties.begin(); it != node.properties.end();)
            {
                const ParamSpec* param = findParam(params, it->first);
                if (param == nullptr || param->kind == ParamKind::ScriptPath)
                {
                    dropped.push_back(it->first);
                    it = node.properties.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            if (!dropped.empty())
            {
                std::sort(dropped.begin(), dropped.end());
                warnings.push_back(nodeLabel(node) + ": dropped " + joined(dropped) + " (not parameters of " +
                                   spec->name + ")");
            }

            std::vector<std::string> rewritten;
            for (const ParamSpec* param : params)
            {
                if (param->kind == ParamKind::ScriptPath || param->name == "serviceType")
                {
                    continue;
                }
                const auto it = node.properties.find(param->name);
                if (it == node.properties.end())
                {
                    if (!param->required)
                    {
                        node.properties[param->name] = param->defaultValue;
                    }
                    continue;
                }
                bt::BlackboardValue& value = it->second;
                if (param->kind == ParamKind::Float)
                {
                    if (const auto* integer = std::get_if<int32_t>(&value))
                    {
                        value = static_cast<float>(*integer);
                        rewritten.push_back(param->name);
                    }
                }
                else if (param->kind == ParamKind::Int)
                {
                    if (const auto* real = std::get_if<float>(&value))
                    {
                        if (std::isfinite(*real) && *real == std::trunc(*real) && std::fabs(*real) <= 2.0e9f)
                        {
                            value = static_cast<int32_t>(*real);
                            rewritten.push_back(param->name);
                        }
                    }
                }
                else if (param->kind == ParamKind::Enum)
                {
                    if (const auto* name = std::get_if<std::string>(&value))
                    {
                        for (const std::string& option : param->enumValues)
                        {
                            if (option != *name && equalsIgnoreCase(option, *name))
                            {
                                value = option;
                                break;
                            }
                        }
                    }
                }
            }
            if (!rewritten.empty())
            {
                warnings.push_back(nodeLabel(node) + ": stored " + joined(rewritten) +
                                   " with the parameter's type (the runtime ignores a value of the wrong type)");
            }
        }

        std::optional<std::vector<bt::BlackboardMapping>> parseMappings(const nlohmann::json& value,
                                                                        const std::string& path,
                                                                        const std::vector<bt::BlackboardKeyDef>& keys,
                                                                        SpecErrors& errors)
        {
            if (!value.is_array())
            {
                errors.add(path, "must be an array of {parent, child, dir}");
                return std::nullopt;
            }
            if (value.size() > maxMappings)
            {
                errors.add(path, "holds more than " + std::to_string(maxMappings) + " mappings");
                return std::nullopt;
            }
            std::vector<bt::BlackboardMapping> mappings;
            bool ok = true;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                const std::string itemPath = path + "[" + std::to_string(i) + "]";
                const nlohmann::json& item = value[i];
                if (!item.is_object())
                {
                    errors.add(itemPath, "must be {parent, child, dir}");
                    ok = false;
                    continue;
                }
                for (auto it = item.begin(); it != item.end(); ++it)
                {
                    if (it.key() != "parent" && it.key() != "child" && it.key() != "dir")
                    {
                        errors.add(itemPath + "." + it.key(), "unknown key; a mapping has parent, child and dir");
                        ok = false;
                    }
                }
                bt::BlackboardMapping mapping;
                const nlohmann::json* parent = member(item, "parent");
                if (parent == nullptr || !parent->is_string() || parent->get<std::string>().empty())
                {
                    errors.add(itemPath + ".parent", "must name a key of this tree");
                    ok = false;
                }
                else if (findKey(keys, parent->get<std::string>()) == nullptr)
                {
                    errors.add(itemPath + ".parent", "blackboard key '" + parent->get<std::string>() +
                                                     "' is not declared (" + keyListText(keys) + ")");
                    ok = false;
                }
                else
                {
                    mapping.parentKey = parent->get<std::string>();
                }
                const nlohmann::json* child = member(item, "child");
                if (child == nullptr || !child->is_string() || child->get<std::string>().empty())
                {
                    errors.add(itemPath + ".child", "must name a key of the tree the DynamicSubTree runs");
                    ok = false;
                }
                else
                {
                    mapping.childKey = child->get<std::string>();
                }
                if (const nlohmann::json* dir = member(item, "dir"))
                {
                    bool known = false;
                    for (const bt::MappingDirection direction :
                         {bt::MappingDirection::In, bt::MappingDirection::Out, bt::MappingDirection::InOut})
                    {
                        if (dir->is_string() && equalsIgnoreCase(dir->get<std::string>(),
                                                                 bt::mappingDirectionToString(direction)))
                        {
                            mapping.direction = direction;
                            known = true;
                        }
                    }
                    if (!known)
                    {
                        errors.add(itemPath + ".dir", "must be In, Out or InOut (got " + text(*dir) + ")");
                        ok = false;
                    }
                }
                mappings.push_back(std::move(mapping));
            }
            if (!ok)
            {
                return std::nullopt;
            }
            return mappings;
        }

        // ------------------------------------------------------------------
        // Parameters
        // ------------------------------------------------------------------

        bool compareOpFits(const bt::BTNode& node, Type keyType)
        {
            const std::string op = stringProperty(node, "compareOp");
            const bool numeric = op == ">" || op == "<" || op == ">=" || op == "<=";
            return !numeric || keyType == Type::Float || keyType == Type::Int;
        }

        // Writes agent parameters onto a node, strictly (the catalogue's kinds, ranges, enums and keys).
        class ParamWriter
        {
        public:
            ParamWriter(const std::vector<bt::BlackboardKeyDef>& keys, const std::string& self, SpecErrors& errors,
                        std::vector<std::string>& warnings)
                : keys(keys), self(self), errors(errors), warnings(warnings)
            {
            }

            // `params`: the JSON object, or nullptr. fresh = a new node: every parameter is written, an omitted
            // optional one at its seed. Otherwise only the given keys change (null resets an optional one).
            void write(bt::BTNode& node, const nlohmann::json* params, const std::string& path, bool fresh)
            {
                const NodeSpec* spec = bt::catalog::find(node.type);
                if (spec == nullptr)
                {
                    return;
                }

                // A Service's parameters follow its serviceType: read it first.
                std::string serviceType;
                bool variantKnown = true;
                bool variantChanged = false;
                if (node.type == bt::BTNodeType::Service)
                {
                    const std::string stored = fresh ? std::string() : canonicalServiceType(storedServiceType(node));
                    const nlohmann::json* given = params != nullptr ? rawMember(*params, "serviceType") : nullptr;
                    const ParamSpec* serviceParam = findParam(allParams(*spec), "serviceType");
                    if (given != nullptr && !given->is_null() && serviceParam != nullptr)
                    {
                        std::optional<bt::BlackboardValue> value =
                            convert(node, *serviceParam, *given, path + ".serviceType");
                        if (value.has_value())
                        {
                            serviceType = std::get<std::string>(*value);
                        }
                        else
                        {
                            variantKnown = false;
                        }
                    }
                    else if (fresh || given != nullptr)
                    {
                        errors.add(path + ".serviceType", "missing required parameter: " +
                                                          joined(bt::catalog::serviceTypes()) +
                                                          " (bt_list_node_types lists the parameters of each)");
                        variantKnown = false;
                    }
                    else
                    {
                        serviceType = stored;
                        variantKnown = !stored.empty();
                    }
                    if (variantKnown)
                    {
                        variantChanged = !fresh && serviceType != stored;
                        node.properties["serviceType"] = serviceType;
                    }
                }

                // Without a known serviceType only the common parameters are written; a variant's names are
                // still accepted as keys (the serviceType problem is the one reported).
                const std::vector<const ParamSpec*> allowed =
                    variantKnown ? bt::catalog::paramsFor(*spec, serviceType) : bt::catalog::paramsFor(*spec);
                const std::vector<const ParamSpec*> accepted = variantKnown ? allowed : allParams(*spec);

                if (params != nullptr)
                {
                    for (auto it = params->begin(); it != params->end(); ++it)
                    {
                        if (findParam(accepted, it.key()) == nullptr)
                        {
                            std::string owner = spec->name;
                            if (node.type == bt::BTNodeType::Service && variantKnown)
                            {
                                owner += " (serviceType " + serviceType + ")";
                            }
                            errors.add(path + "." + it.key(),
                                       "unknown parameter of " + owner + "; its parameters: " + paramNames(allowed));
                        }
                    }
                }

                if (variantChanged)
                {
                    std::vector<std::string> dropped;
                    for (auto it = node.properties.begin(); it != node.properties.end();)
                    {
                        if (it->first != "serviceType" && findParam(allowed, it->first) == nullptr)
                        {
                            dropped.push_back(it->first);
                            it = node.properties.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                    if (!dropped.empty())
                    {
                        std::sort(dropped.begin(), dropped.end());
                        warnings.push_back(nodeLabel(node) + ": serviceType " + serviceType + " dropped " +
                                           joined(dropped));
                    }
                }

                // A TypedValue follows the key another parameter names: write it in the second pass.
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (const ParamSpec* param : allowed)
                    {
                        if (param->name == "serviceType" || (param->kind == ParamKind::TypedValue) != (pass == 1))
                        {
                            continue;
                        }
                        const std::string paramPath = path + "." + param->name;
                        const nlohmann::json* given = params != nullptr ? rawMember(*params, param->name) : nullptr;
                        if (given != nullptr && !given->is_null())
                        {
                            if (param->kind == ParamKind::ScriptPath)
                            {
                                writeScript(node, *given, paramPath);
                            }
                            else if (std::optional<bt::BlackboardValue> value = convert(node, *param, *given, paramPath))
                            {
                                node.properties[param->name] = std::move(*value);
                            }
                            continue;
                        }

                        const bool reset = given != nullptr;
                        const bool missing = param->kind == ParamKind::ScriptPath
                            ? node.scriptPath.empty()
                            : !node.properties.contains(param->name);
                        if (!fresh && !reset && !(variantChanged && missing))
                        {
                            continue;
                        }
                        if (param->required)
                        {
                            errors.add(paramPath, reset ? std::string("is required and has no default to reset to")
                                                        : "missing required parameter: " + param->description);
                            continue;
                        }
                        writeSeed(node, *param, paramPath);
                    }
                }

                if (!fresh)
                {
                    crossCheck(node, params, path);
                }
                ruleChecks(node, serviceType, path);
            }

        private:
            const std::vector<bt::BlackboardKeyDef>& keys;
            const std::string& self;
            SpecErrors& errors;
            std::vector<std::string>& warnings;

            void writeSeed(bt::BTNode& node, const ParamSpec& param, const std::string& path)
            {
                if (param.kind == ParamKind::KeyRef)
                {
                    const auto* seed = std::get_if<std::string>(&param.defaultValue);
                    if (seed != nullptr && !seed->empty())
                    {
                        const bt::BlackboardKeyDef* key = findKey(keys, *seed);
                        if (key == nullptr || !allows(param, key->type))
                        {
                            errors.add(path, "not given, so it uses the key '" + *seed +
                                             "', which the tree does not declare as " + typesText(param.keyTypes) +
                                             ": declare that key in 'blackboard' or set " + param.name);
                            return;
                        }
                    }
                }
                node.properties[param.name] = param.defaultValue;
            }

            void writeScript(bt::BTNode& node, const nlohmann::json& value, const std::string& path)
            {
                if (!value.is_string())
                {
                    errors.add(path, "must be a script path such as \"game/ai/MeleeAttack.mt\" (got " + text(value) + ")");
                    return;
                }
                std::string error;
                const std::optional<ScriptTaskFile> script = resolveScriptTask(value.get<std::string>(), error);
                if (!script.has_value())
                {
                    errors.add(path, error);
                    return;
                }
                node.scriptPath = script->path;
                node.scriptClassName = script->info.className;
            }

            bool checkKey(const ParamSpec& param, const std::string& name, const std::string& path)
            {
                if (name.empty())
                {
                    if (param.allowEmptyKey)
                    {
                        return true;
                    }
                    errors.add(path, "must name a blackboard key (" + typesText(param.keyTypes) + ")");
                    return false;
                }
                const bt::BlackboardKeyDef* key = findKey(keys, name);
                if (key == nullptr)
                {
                    errors.add(path, "blackboard key '" + name + "' is not declared (" + keyListText(keys) + ")");
                    return false;
                }
                if (!allows(param, key->type))
                {
                    errors.add(path, "blackboard key '" + name + "' is " + typeName(key->type) + "; " + param.name +
                                     " needs " + typesText(param.keyTypes));
                    return false;
                }
                return true;
            }

            std::optional<bt::BlackboardValue> convert(const bt::BTNode& node, const ParamSpec& param,
                                                       const nlohmann::json& value, const std::string& path)
            {
                switch (param.kind)
                {
                case ParamKind::Float:
                {
                    if (!value.is_number())
                    {
                        errors.add(path, "must be a number (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    const float number = static_cast<float>(value.get<double>());
                    if (!std::isfinite(number))
                    {
                        errors.add(path, "must be a finite number");
                        return std::nullopt;
                    }
                    if (param.hasRange && (number < param.minValue || number > param.maxValue))
                    {
                        errors.add(path, "= " + text(value) + " is outside " + floatText(param.minValue) + ".." +
                                         floatText(param.maxValue));
                        return std::nullopt;
                    }
                    return number;
                }
                case ParamKind::Int:
                {
                    const std::optional<int64_t> number = integralValue(value);
                    if (!number.has_value() || *number < std::numeric_limits<int32_t>::min() ||
                        *number > std::numeric_limits<int32_t>::max())
                    {
                        errors.add(path, "must be an integer (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    if (param.hasRange && (*number < static_cast<int64_t>(param.minValue) ||
                                           *number > static_cast<int64_t>(param.maxValue)))
                    {
                        errors.add(path, "= " + std::to_string(*number) + " is outside " +
                                         std::to_string(static_cast<int64_t>(param.minValue)) + ".." +
                                         std::to_string(static_cast<int64_t>(param.maxValue)));
                        return std::nullopt;
                    }
                    return static_cast<int32_t>(*number);
                }
                case ParamKind::Bool:
                    if (!value.is_boolean())
                    {
                        errors.add(path, "must be true or false (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    return value.get<bool>();
                case ParamKind::String:
                {
                    if (!value.is_string())
                    {
                        errors.add(path, "must be a string (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    std::string result = value.get<std::string>();
                    if (param.required && result.empty())
                    {
                        errors.add(path, "must not be empty");
                        return std::nullopt;
                    }
                    return result;
                }
                case ParamKind::Enum:
                    if (value.is_string())
                    {
                        for (const std::string& option : param.enumValues)
                        {
                            if (equalsIgnoreCase(option, value.get<std::string>()))
                            {
                                return option;
                            }
                        }
                    }
                    errors.add(path, "must be one of " + joined(param.enumValues) + " (got " + text(value) + ")");
                    return std::nullopt;
                case ParamKind::KeyRef:
                {
                    if (!value.is_string())
                    {
                        errors.add(path, "must name a blackboard key (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    std::string name = value.get<std::string>();
                    if (!checkKey(param, name, path))
                    {
                        return std::nullopt;
                    }
                    return name;
                }
                case ParamKind::TypedValue:
                {
                    const std::string keyName = stringProperty(node, param.valueKeyParam);
                    const bt::BlackboardKeyDef* key = keyName.empty() ? nullptr : findKey(keys, keyName);
                    if (key == nullptr)
                    {
                        return std::nullopt;  // reported for the key parameter
                    }
                    std::optional<bt::BlackboardValue> typed = parseValue(value, key->type, path, errors);
                    if (typed.has_value() && param.name == "compareValue" && !compareOpFits(node, key->type))
                    {
                        errors.add(path, "compareOp '" + stringProperty(node, "compareOp") +
                                         "' compares numbers and needs a Float or Int key; key '" + key->name +
                                         "' is " + typeName(key->type));
                        return std::nullopt;
                    }
                    return typed;
                }
                case ParamKind::TreePath:
                {
                    if (!value.is_string())
                    {
                        errors.add(path, "must be a .vfBehaviorTree path (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    const std::string given = value.get<std::string>();
                    if (given.empty())
                    {
                        if (!param.required)
                        {
                            return std::string();
                        }
                        errors.add(path, "must name a .vfBehaviorTree file");
                        return std::nullopt;
                    }
                    const std::optional<fs::path> root = tryTerrainProjectRoot();
                    if (!root.has_value())
                    {
                        errors.add(path, "no project is loaded; open one with project_open first");
                        return std::nullopt;
                    }
                    std::string error;
                    const std::optional<fs::path> file = locateTree(*root, given, true, error);
                    if (!file.has_value())
                    {
                        errors.add(path, error);
                        return std::nullopt;
                    }
                    if (!self.empty() && sameTreePath(file->string(), self))
                    {
                        errors.add(path, "a tree cannot include itself");
                        return std::nullopt;
                    }
                    return storedTreePath(*file);
                }
                case ParamKind::EqsQuery:
                {
                    if (!value.is_string() || value.get<std::string>().empty())
                    {
                        errors.add(path, "must name an EQS query template (got " + text(value) + ")");
                        return std::nullopt;
                    }
                    std::string name = value.get<std::string>();
                    if (const std::optional<std::string> problem = eqsQueryProblem(name))
                    {
                        errors.add(path, *problem);
                        return std::nullopt;
                    }
                    return name;
                }
                case ParamKind::ScriptPath:
                    break;  // writeScript
                }
                return std::nullopt;
            }

            // After a merge: a TypedValue the op did not give must still match its key.
            void crossCheck(const bt::BTNode& node, const nlohmann::json* params, const std::string& path)
            {
                const NodeSpec* spec = bt::catalog::find(node.type);
                for (const ParamSpec* param : paramsOf(*spec, node))
                {
                    if (param->kind != ParamKind::TypedValue ||
                        (params != nullptr && rawMember(*params, param->name) != nullptr))
                    {
                        continue;
                    }
                    const auto valueIt = node.properties.find(param->name);
                    const std::string keyName = stringProperty(node, param->valueKeyParam);
                    const bt::BlackboardKeyDef* key = keyName.empty() ? nullptr : findKey(keys, keyName);
                    if (valueIt == node.properties.end() || key == nullptr)
                    {
                        continue;
                    }
                    if (valueType(valueIt->second) != key->type)
                    {
                        errors.add(path + "." + param->name,
                                   "is " + typeName(valueType(valueIt->second)) + " but key '" + key->name + "' is " +
                                   typeName(key->type) + ": give " + param->name + " together with " +
                                   param->valueKeyParam);
                    }
                    else if (param->name == "compareValue" && !compareOpFits(node, key->type))
                    {
                        errors.add(path + ".compareOp", "compares numbers and needs a Float or Int key; key '" +
                                                        key->name + "' is " + typeName(key->type));
                    }
                }
            }

            void ruleChecks(const bt::BTNode& node, const std::string& serviceType, const std::string& path)
            {
                if (node.type == bt::BTNodeType::DynamicSubTree && stringProperty(node, "selectionKey").empty() &&
                    stringProperty(node, "injectionTag").empty() && stringProperty(node, "defaultTreePath").empty())
                {
                    errors.add(path, "a DynamicSubTree needs at least one of selectionKey, injectionTag or "
                                     "defaultTreePath");
                }
                if (node.type == bt::BTNodeType::Service && serviceType == "EQSRefresh")
                {
                    const auto it = node.properties.find("interval");
                    if (it != node.properties.end() && std::holds_alternative<float>(it->second) &&
                        std::get<float>(it->second) < 0.1f)
                    {
                        errors.add(path + ".interval", "an EQSRefresh service needs interval >= 0.1 s (each run is a "
                                                       "full query)");
                    }
                }
            }
        };

        // ------------------------------------------------------------------
        // Node specs
        // ------------------------------------------------------------------

        struct SpecNode
        {
            bt::BTNode node;  // id 0 until assigned
            bool hasId = false;
            bool hasPosition = false;
            std::vector<SpecNode> children;
        };

        class SpecParser
        {
        public:
            // `reserved`: ids the graph already uses (bt_edit); a spec may not give them.
            SpecParser(const std::vector<bt::BlackboardKeyDef>& keys, const std::string& self,
                       std::unordered_set<uint32_t> reserved, SpecErrors& errors, std::vector<std::string>& warnings)
                : keys(keys), self(self), reserved(std::move(reserved)), errors(errors), warnings(warnings)
            {
            }

            // An id the spec gives at `path`; false (with an error) when it is not a free positive integer.
            bool claimId(const nlohmann::json& value, const std::string& path, uint32_t& out)
            {
                const std::optional<int64_t> id = integralValue(value);
                if (!id.has_value() || *id < 1 || *id > maxGivenId)
                {
                    errors.add(path, "must be a positive integer up to " + std::to_string(maxGivenId) + " (got " +
                                     text(value) + ")");
                    return false;
                }
                const uint32_t candidate = static_cast<uint32_t>(*id);
                if (reserved.contains(candidate))
                {
                    errors.add(path, "id " + std::to_string(candidate) + " already belongs to another node of the "
                                     "tree; omit it to get a new id");
                    return false;
                }
                const auto [it, inserted] = given.emplace(candidate, path);
                if (!inserted)
                {
                    errors.add(path, "id " + std::to_string(candidate) + " is also given at " + it->second);
                    return false;
                }
                out = candidate;
                return true;
            }

            const std::unordered_map<uint32_t, std::string>& givenIds() const { return given; }

            // A NodeSpec; nullopt when it cannot be read at all (the problem is recorded). checkChildCount false:
            // the caller checks the children (replace keeps the old ones; wrap adds the wrapped node).
            std::optional<SpecNode> parse(const nlohmann::json& spec, const std::string& path, std::size_t depth,
                                          bool checkChildCount = true)
            {
                if (errors.full())
                {
                    return std::nullopt;
                }
                if (!spec.is_object())
                {
                    errors.add(path, "must be a node object {type, name?, id?, params?, children? | child?} (got " +
                                     text(spec) + ")");
                    return std::nullopt;
                }
                if (depth > maxTreeDepth)
                {
                    if (!tooDeep)
                    {
                        errors.add(path, "the tree is nested deeper than " + std::to_string(maxTreeDepth) + " levels");
                        tooDeep = true;
                    }
                    return std::nullopt;
                }
                if (++count > maxTreeNodes)
                {
                    if (!tooMany)
                    {
                        errors.add(path, "the tree has more than " + std::to_string(maxTreeNodes) + " nodes");
                        tooMany = true;
                    }
                    return std::nullopt;
                }

                for (auto it = spec.begin(); it != spec.end(); ++it)
                {
                    if (!listed(nodeKeys, it.key()))
                    {
                        errors.add(path + "." + it.key(), "unknown node key; a node has type, name, id, position, "
                                                          "params, children (or child) and mappings");
                    }
                }

                SpecNode out;
                const NodeSpec* nodeSpec = nullptr;
                const nlohmann::json* type = member(spec, "type");
                if (type == nullptr)
                {
                    errors.add(path + ".type", "missing: every node needs a type (bt_list_node_types lists them)");
                }
                else if (!type->is_string())
                {
                    errors.add(path + ".type", "must be a node type name (got " + text(*type) + ")");
                }
                else
                {
                    nodeSpec = bt::catalog::findByName(type->get<std::string>());
                    if (nodeSpec == nullptr)
                    {
                        errors.add(path + ".type", "unknown node type '" + type->get<std::string>() + "'; types: " +
                                                   nodeTypeList());
                    }
                    else if (nodeSpec->category == NodeCategory::Root)
                    {
                        errors.add(path + ".type", "Root is implicit: give the node below it (only the tree's 'root' "
                                                   "may be a Root wrapper)");
                        nodeSpec = nullptr;
                    }
                    else
                    {
                        out.node.type = nodeSpec->type;
                    }
                }

                if (const nlohmann::json* name = member(spec, "name"))
                {
                    if (!name->is_string())
                    {
                        errors.add(path + ".name", "must be a string (got " + text(*name) + ")");
                    }
                    else
                    {
                        out.node.name = name->get<std::string>();
                    }
                }
                if (out.node.name.empty() && nodeSpec != nullptr)
                {
                    out.node.name = nodeSpec->name;
                }

                if (const nlohmann::json* id = member(spec, "id"))
                {
                    uint32_t value = 0;
                    if (claimId(*id, path + ".id", value))
                    {
                        out.node.id = value;
                        out.hasId = true;
                    }
                }

                if (const nlohmann::json* position = member(spec, "position"))
                {
                    if (!position->is_array() || position->size() != 2 || !(*position)[0].is_number() ||
                        !(*position)[1].is_number())
                    {
                        errors.add(path + ".position", "must be [x, y] (editor canvas units)");
                    }
                    else
                    {
                        const glm::vec2 value{static_cast<float>((*position)[0].get<double>()),
                                              static_cast<float>((*position)[1].get<double>())};
                        if (!std::isfinite(value.x) || !std::isfinite(value.y))
                        {
                            errors.add(path + ".position", "must hold finite numbers");
                        }
                        else
                        {
                            out.node.position = value;
                            out.hasPosition = true;
                        }
                    }
                }

                const nlohmann::json* params = member(spec, "params");
                if (params != nullptr && !params->is_object())
                {
                    errors.add(path + ".params", "must be an object of parameter -> value");
                    params = nullptr;
                }
                if (nodeSpec != nullptr)
                {
                    ParamWriter writer(keys, self, errors, warnings);
                    writer.write(out.node, params, path + ".params", true);
                }

                if (const nlohmann::json* mappings = member(spec, "mappings"))
                {
                    if (nodeSpec != nullptr && nodeSpec->type != bt::BTNodeType::DynamicSubTree)
                    {
                        errors.add(path + ".mappings", "only a DynamicSubTree has mappings");
                    }
                    else if (nodeSpec != nullptr)
                    {
                        if (auto parsed = parseMappings(*mappings, path + ".mappings", keys, errors))
                        {
                            out.node.blackboardMappings = std::move(*parsed);
                        }
                    }
                }

                const nlohmann::json* child = member(spec, "child");
                const nlohmann::json* children = member(spec, "children");
                std::optional<std::size_t> givenCount = 0;
                if (child != nullptr && children != nullptr)
                {
                    errors.add(path, "give either 'children' or 'child', not both");
                    givenCount.reset();
                }
                else if (child != nullptr)
                {
                    givenCount = 1;
                    if (auto parsed = parse(*child, path + ".child", depth + 1))
                    {
                        out.children.push_back(std::move(*parsed));
                    }
                }
                else if (children != nullptr)
                {
                    if (!children->is_array())
                    {
                        errors.add(path + ".children", "must be an array of nodes");
                        givenCount.reset();
                    }
                    else
                    {
                        givenCount = children->size();
                        for (std::size_t i = 0; i < children->size(); ++i)
                        {
                            if (auto parsed = parse((*children)[i], path + ".children[" + std::to_string(i) + "]",
                                                    depth + 1))
                            {
                                out.children.push_back(std::move(*parsed));
                            }
                        }
                    }
                }
                if (nodeSpec != nullptr && checkChildCount && givenCount.has_value() &&
                    (*givenCount < nodeSpec->minChildren || *givenCount > nodeSpec->maxChildren))
                {
                    errors.add(path, "'" + out.node.name + "' (" + nodeSpec->name + ") " + childRule(*nodeSpec) +
                                     " (has " + std::to_string(*givenCount) + ")");
                }
                return out;
            }

        private:
            const std::vector<bt::BlackboardKeyDef>& keys;
            const std::string& self;
            std::unordered_set<uint32_t> reserved;
            std::unordered_map<uint32_t, std::string> given;
            SpecErrors& errors;
            std::vector<std::string>& warnings;
            std::size_t count = 0;
            bool tooDeep = false;
            bool tooMany = false;
        };

        // Hands out node ids from `start` upwards, skipping the used ones.
        class IdAllocator
        {
        public:
            IdAllocator(uint32_t start, std::unordered_set<uint32_t> used)
                : next(std::max<uint32_t>(start, 1)), used(std::move(used))
            {
            }

            uint32_t take()
            {
                while (used.contains(next))
                {
                    ++next;
                }
                used.insert(next);
                return next++;
            }

            void claim(uint32_t id) { used.insert(id); }

        private:
            uint32_t next;
            std::unordered_set<uint32_t> used;
        };

        void collectGivenIds(const SpecNode& node, std::unordered_set<uint32_t>& ids, uint32_t& maxId)
        {
            if (node.hasId)
            {
                ids.insert(node.node.id);
                maxId = std::max(maxId, node.node.id);
            }
            for (const SpecNode& child : node.children)
            {
                collectGivenIds(child, ids, maxId);
            }
        }

        // Pre-order: a node, then its children in order.
        void assignIds(SpecNode& node, IdAllocator& ids)
        {
            if (!node.hasId)
            {
                node.node.id = ids.take();
            }
            for (SpecNode& child : node.children)
            {
                assignIds(child, ids);
            }
        }

        // Link ids for a rebuilt graph: an edge the previous graph had keeps its id.
        class LinkAllocator
        {
        public:
            explicit LinkAllocator(const bt::BTGraph* previous)
            {
                if (previous != nullptr)
                {
                    next = std::max<uint32_t>(next, previous->nextLinkId);
                    for (const bt::BTLink& link : previous->links)
                    {
                        reusable.emplace(edgeKey(link.sourceNodeId, link.targetNodeId), link.id);
                        next = std::max(next, link.id + 1);
                    }
                }
            }

            uint32_t take(uint32_t source, uint32_t target)
            {
                const auto it = reusable.find(edgeKey(source, target));
                if (it != reusable.end() && used.insert(it->second).second)
                {
                    return it->second;
                }
                while (used.contains(next))
                {
                    ++next;
                }
                used.insert(next);
                return next++;
            }

        private:
            static uint64_t edgeKey(uint32_t source, uint32_t target)
            {
                return (static_cast<uint64_t>(source) << 32) | target;
            }

            std::unordered_map<uint64_t, uint32_t> reusable;
            std::unordered_set<uint32_t> used;
            uint32_t next = 1;
        };

        void appendTree(bt::BTGraph& graph, const SpecNode& node, uint32_t parentId, uint32_t sortOrder,
                        LinkAllocator& links, std::unordered_set<uint32_t>& needsPosition)
        {
            const uint32_t id = node.node.id;
            if (!node.hasPosition)
            {
                needsPosition.insert(id);
            }
            graph.nodes.push_back(node.node);
            graph.links.push_back(bt::BTLink{links.take(parentId, id), parentId, id, sortOrder});
            for (std::size_t i = 0; i < node.children.size(); ++i)
            {
                appendTree(graph, node.children[i], id, static_cast<uint32_t>(i), links, needsPosition);
            }
        }

        uint32_t nextIdAfter(const bt::BTGraph& graph, uint32_t floor)
        {
            uint32_t next = std::max<uint32_t>(floor, 1);
            for (const bt::BTNode& node : graph.nodes)
            {
                next = std::max(next, node.id + 1);
            }
            return next;
        }

        uint32_t nextLinkAfter(const bt::BTGraph& graph, uint32_t floor)
        {
            uint32_t next = std::max<uint32_t>(floor, 1);
            for (const bt::BTLink& link : graph.links)
            {
                next = std::max(next, link.id + 1);
            }
            return next;
        }

        // The catalogue's strict findings (Errors) as problems; `what` heads the ArgError.
        void throwStrictErrors(const bt::BehaviorTreeData& data, std::optional<std::unordered_set<uint32_t>> onlyNodes,
                               std::string_view what)
        {
            SpecErrors errors;
            for (const bt::validation::Diagnostic& diagnostic : bt::catalog::validateStrict(data, strictContext(std::move(onlyNodes))))
            {
                if (diagnostic.severity == bt::validation::Severity::Error)
                {
                    errors.add("", diagnostic.message);
                }
            }
            errors.throwIfAny(what);
        }

        // ------------------------------------------------------------------
        // Emit
        // ------------------------------------------------------------------

        nlohmann::json paramJson(const ParamSpec& param, const bt::BlackboardValue& value)
        {
            if (param.kind == ParamKind::TreePath)
            {
                if (const auto* path = std::get_if<std::string>(&value))
                {
                    return reportTreePath(*path);
                }
            }
            return valueJson(value);
        }

        nlohmann::json emitNode(const GraphIndex& index, uint32_t id, std::size_t depth,
                                std::unordered_set<uint32_t>& visited, EmittedTree& out)
        {
            const bt::BTNode& node = *index.node(id);
            visited.insert(id);
            const std::string type = bt::nodeTypeToString(node.type);

            const std::string name = node.name.empty() ? type : node.name;
            nlohmann::json entry = nlohmann::json::object();
            entry["id"] = node.id;
            entry["type"] = type;
            entry["name"] = name;

            nlohmann::json params = nlohmann::json::object();
            const NodeSpec* spec = bt::catalog::find(node.type);
            const std::vector<const ParamSpec*> known = spec != nullptr ? paramsOf(*spec, node)
                                                                        : std::vector<const ParamSpec*>{};
            for (const auto& [param, value] : node.properties)
            {
                const ParamSpec* paramSpec = findParam(known, param);
                if (paramSpec == nullptr || paramSpec->kind == ParamKind::ScriptPath)
                {
                    out.legacyParams.push_back({{"id", node.id}, {"name", name}, {"type", type}, {"param", param},
                                                {"value", valueJson(value)}});
                    continue;
                }
                params[param] = paramJson(*paramSpec, value);
            }
            if (node.type == bt::BTNodeType::ScriptTask && !node.scriptPath.empty())
            {
                params["script"] = node.scriptPath;
            }
            if (!params.empty())
            {
                entry["params"] = std::move(params);
            }
            if (!node.blackboardMappings.empty())
            {
                nlohmann::json mappings = nlohmann::json::array();
                for (const bt::BlackboardMapping& mapping : node.blackboardMappings)
                {
                    mappings.push_back({{"parent", mapping.parentKey}, {"child", mapping.childKey},
                                        {"dir", bt::mappingDirectionToString(mapping.direction)}});
                }
                entry["mappings"] = std::move(mappings);
            }

            nlohmann::json children = nlohmann::json::array();
            for (const uint32_t child : index.childrenOf(id))
            {
                if (visited.contains(child))
                {
                    out.warnings.push_back(nodeLabel(node) + " links to node " + std::to_string(child) +
                                           " a second time (a cycle or a shared node); it is reported once");
                    continue;
                }
                if (depth >= maxWalkDepth)
                {
                    out.warnings.push_back("the tree is deeper than " + std::to_string(maxWalkDepth) +
                                           " levels; the nodes below " + nodeLabel(node) + " are not reported");
                    break;
                }
                children.push_back(emitNode(index, child, depth + 1, visited, out));
            }
            if (spec != nullptr && spec->maxChildren == 1 && children.size() == 1)
            {
                entry["child"] = std::move(children.front());
            }
            else if (!children.empty() || (spec != nullptr && spec->minChildren > 0))
            {
                entry["children"] = std::move(children);
            }
            return entry;
        }

        // ------------------------------------------------------------------
        // Edits
        // ------------------------------------------------------------------

        constexpr std::array<std::string_view, 8> opNames{
            "setParams", "rename", "addChild", "remove", "move", "wrap", "unwrap", "replace"};

        std::vector<std::string_view> opKeys(std::string_view op)
        {
            if (op == "setParams") return {"op", "node", "params", "mappings"};
            if (op == "rename") return {"op", "node", "name"};
            if (op == "addChild") return {"op", "parent", "index", "node"};
            if (op == "move") return {"op", "node", "parent", "index"};
            if (op == "wrap" || op == "replace") return {"op", "node", "with"};
            return {"op", "node"};  // remove, unwrap
        }

        bool givesChildren(const nlohmann::json& spec)
        {
            return member(spec, "child") != nullptr || member(spec, "children") != nullptr;
        }

        class TreeEditor
        {
        public:
            TreeEditor(const bt::BehaviorTreeData& current, const SpecOptions& options)
                : data(current), options(options)
            {
                normalizeKeys(data.graph, warnings);
            }

            void apply(const nlohmann::json& op, std::size_t index)
            {
                const std::string path = "ops[" + std::to_string(index) + "]";
                if (!op.is_object())
                {
                    throw ArgError(path + " must be an object {op, ...}");
                }
                const nlohmann::json* opName = member(op, "op");
                std::string name;
                if (opName != nullptr && opName->is_string())
                {
                    for (const std::string_view candidate : opNames)
                    {
                        if (equalsIgnoreCase(candidate, opName->get<std::string>()))
                        {
                            name = candidate;
                        }
                    }
                }
                if (name.empty())
                {
                    throw ArgError(path + ".op must be one of " + joined(opNames) + " (got " +
                                   (opName != nullptr ? text(*opName) : std::string("nothing")) + ")");
                }
                const std::vector<std::string_view> allowed = opKeys(name);
                for (auto it = op.begin(); it != op.end(); ++it)
                {
                    if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
                    {
                        throw ArgError(path + ": unknown key '" + it.key() + "' for " + name + "; it takes " +
                                       joined(allowed));
                    }
                }

                const std::string where = path + " (" + name + ")";
                if (name == "setParams") setParams(op, path, where);
                else if (name == "rename") rename(op, path, where);
                else if (name == "addChild") addChild(op, path, where);
                else if (name == "remove") remove(op, path, where);
                else if (name == "move") move(op, path, where);
                else if (name == "wrap") wrap(op, path, where);
                else if (name == "unwrap") unwrap(op, path, where);
                else replace(op, path, where);
            }

            EditOutcome finish()
            {
                layoutTree(data.graph, needsPosition, options.relayout);
                // Structure and parameters of every touched node, as the authoring service checks them on save.
                throwStrictErrors(data, touched, "after the edits, the tree");
                EditOutcome outcome;
                outcome.data = std::move(data);
                outcome.touched = std::move(touched);
                outcome.warnings = std::move(warnings);
                return outcome;
            }

        private:
            bt::BehaviorTreeData data;
            const SpecOptions& options;
            std::unordered_set<uint32_t> touched;
            std::unordered_set<uint32_t> created;
            std::unordered_set<uint32_t> needsPosition;
            std::vector<std::string> warnings;

            bt::BTNode& nodeAt(uint32_t id)
            {
                bt::BTNode* node = data.graph.findNodeById(id);
                if (node == nullptr)
                {
                    throw std::logic_error("behavior tree edit lost node " + std::to_string(id));
                }
                return *node;
            }

            static const nlohmann::json& required(const nlohmann::json& op, const char* key, const std::string& where)
            {
                const nlohmann::json* value = member(op, key);
                if (value == nullptr)
                {
                    throw ArgError(where + " needs '" + key + "'");
                }
                return *value;
            }

            std::string nodeHint() const
            {
                std::vector<std::string> names;
                for (const auto& entry : nodeList(data.graph))
                {
                    if (names.size() == 30)
                    {
                        names.emplace_back("...");
                        break;
                    }
                    names.push_back("'" + entry["name"].get<std::string>() + "' (id " +
                                    std::to_string(entry["id"].get<uint32_t>()) + ")");
                }
                return names.empty() ? std::string() : "; nodes: " + joined(names);
            }

            // `ref`: a node id, or a name only one node has. The Root is implicit and cannot be addressed.
            uint32_t resolve(const nlohmann::json& ref, const std::string& path) const
            {
                const bt::BTGraph& graph = data.graph;
                if (const std::optional<int64_t> id = integralValue(ref))
                {
                    const bt::BTNode* found = *id >= 1 && *id <= static_cast<int64_t>(UINT32_MAX)
                        ? graph.findNodeById(static_cast<uint32_t>(*id))
                        : nullptr;
                    if (found == nullptr)
                    {
                        throw ArgError(path + ": the tree has no node with id " + std::to_string(*id) + nodeHint());
                    }
                    if (found->id == graph.rootNodeId)
                    {
                        throw ArgError(path + ": the Root node is implicit; address the node below it" + nodeHint());
                    }
                    return found->id;
                }
                if (ref.is_string())
                {
                    const std::string name = ref.get<std::string>();
                    std::vector<uint32_t> matches;
                    for (const bt::BTNode& node : graph.nodes)
                    {
                        const std::string label = node.name.empty() ? bt::nodeTypeToString(node.type) : node.name;
                        if (node.id != graph.rootNodeId && label == name)
                        {
                            matches.push_back(node.id);
                        }
                    }
                    if (matches.empty())
                    {
                        throw ArgError(path + ": the tree has no node named '" + name + "'" + nodeHint());
                    }
                    if (matches.size() > 1)
                    {
                        std::sort(matches.begin(), matches.end());
                        std::vector<std::string> ids;
                        for (const uint32_t id : matches)
                        {
                            ids.push_back(std::to_string(id));
                        }
                        throw ArgError(path + ": the name '" + name + "' is ambiguous (ids " + joined(ids) +
                                       "); pass the id instead");
                    }
                    return matches.front();
                }
                throw ArgError(path + " must be a node id or a unique node name (got " + text(ref) + ")");
            }

            // A node the edit created or changed, or whose child list it changed: strict-checked on save. A
            // stored node is normalised once, the first time an edit touches it.
            void touch(uint32_t id)
            {
                if (!touched.insert(id).second || created.contains(id))
                {
                    return;
                }
                if (bt::BTNode* node = data.graph.findNodeById(id))
                {
                    normalizeNode(*node, warnings);
                }
            }

            std::vector<uint32_t> children(uint32_t id) const
            {
                return childIds(data.graph, id);
            }

            uint32_t newLinkId()
            {
                const uint32_t id = nextLinkAfter(data.graph, data.graph.nextLinkId);
                data.graph.nextLinkId = id + 1;
                return id;
            }

            // The parent's children become exactly `order` (sortOrder 0..n-1); unchanged edges keep their link id.
            void relink(uint32_t parentId, const std::vector<uint32_t>& order)
            {
                std::unordered_map<uint32_t, uint32_t> oldIds;
                auto& links = data.graph.links;
                for (auto it = links.begin(); it != links.end();)
                {
                    if (it->sourceNodeId == parentId)
                    {
                        oldIds.emplace(it->targetNodeId, it->id);
                        it = links.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                for (std::size_t i = 0; i < order.size(); ++i)
                {
                    const auto old = oldIds.find(order[i]);
                    const uint32_t linkId = old != oldIds.end() ? old->second : newLinkId();
                    links.push_back(bt::BTLink{linkId, parentId, order[i], static_cast<uint32_t>(i)});
                }
            }

            std::unordered_set<uint32_t> subtreeOf(uint32_t id) const
            {
                const GraphIndex index(data.graph);
                std::unordered_set<uint32_t> ids;
                std::vector<uint32_t> pending{id};
                while (!pending.empty())
                {
                    const uint32_t current = pending.back();
                    pending.pop_back();
                    if (!ids.insert(current).second)
                    {
                        continue;
                    }
                    const std::vector<uint32_t>& kids = index.childrenOf(current);
                    pending.insert(pending.end(), kids.begin(), kids.end());
                }
                return ids;
            }

            // Removes the nodes and every link that touches them.
            void eraseNodes(const std::unordered_set<uint32_t>& ids)
            {
                std::erase_if(data.graph.nodes, [&ids](const bt::BTNode& node) { return ids.contains(node.id); });
                std::erase_if(data.graph.links, [&ids](const bt::BTLink& link)
                {
                    return ids.contains(link.sourceNodeId) || ids.contains(link.targetNodeId);
                });
                for (const uint32_t id : ids)
                {
                    created.erase(id);
                    needsPosition.erase(id);
                }
            }

            SpecNode parseSpec(const nlohmann::json& spec, const std::string& path, bool checkChildCount,
                               uint32_t freedId = 0)
            {
                SpecErrors errors;
                std::unordered_set<uint32_t> reserved;
                for (const bt::BTNode& node : data.graph.nodes)
                {
                    if (node.id != freedId)
                    {
                        reserved.insert(node.id);
                    }
                }
                SpecParser parser(data.graph.blackboardKeys, options.selfEnginePath, std::move(reserved), errors, warnings);
                std::optional<SpecNode> parsed = parser.parse(spec, path, 1, checkChildCount);
                errors.throwIfAny(path);
                if (!parsed.has_value())
                {
                    throw ArgError(path + " is not a node");
                }
                return std::move(*parsed);
            }

            // Adds a parsed subtree (unlinked at its top): ids for nodes without one, positions to lay out.
            uint32_t addSpec(SpecNode& spec)
            {
                std::unordered_set<uint32_t> used;
                for (const bt::BTNode& node : data.graph.nodes)
                {
                    used.insert(node.id);
                }
                uint32_t maxGiven = 0;
                collectGivenIds(spec, used, maxGiven);
                IdAllocator ids(nextIdAfter(data.graph, std::max(data.graph.nextNodeId, maxGiven + 1)), std::move(used));
                assignIds(spec, ids);
                appendSpec(spec, 0, 0);
                data.graph.nextNodeId = nextIdAfter(data.graph, data.graph.nextNodeId);
                return spec.node.id;
            }

            void appendSpec(const SpecNode& spec, uint32_t parentId, uint32_t order)
            {
                const uint32_t id = spec.node.id;
                created.insert(id);
                touched.insert(id);
                if (!spec.hasPosition)
                {
                    needsPosition.insert(id);
                }
                data.graph.nodes.push_back(spec.node);
                if (parentId != 0)
                {
                    data.graph.links.push_back(bt::BTLink{newLinkId(), parentId, id, order});
                }
                for (std::size_t i = 0; i < spec.children.size(); ++i)
                {
                    appendSpec(spec.children[i], id, static_cast<uint32_t>(i));
                }
            }

            std::size_t optIndex(const nlohmann::json& op, const std::string& path, std::size_t size) const
            {
                const nlohmann::json* index = member(op, "index");
                if (index == nullptr)
                {
                    return size;
                }
                const std::optional<int64_t> value = integralValue(*index);
                if (!value.has_value() || *value < 0 || *value > static_cast<int64_t>(size))
                {
                    throw ArgError(path + ".index must be 0.." + std::to_string(size) + " (got " + text(*index) + ")");
                }
                return static_cast<std::size_t>(*value);
            }

            void requireRoomFor(uint32_t parentId, std::size_t currentChildren, const std::string& where)
            {
                const bt::BTNode& parent = nodeAt(parentId);
                const NodeSpec* spec = bt::catalog::find(parent.type);
                if (spec == nullptr || spec->maxChildren == 0)
                {
                    throw ArgError(where + ": " + nodeLabel(parent) + " is a task and cannot have children");
                }
                if (currentChildren >= spec->maxChildren)
                {
                    throw ArgError(where + ": " + nodeLabel(parent) + " " + childRule(*spec) +
                                   " and has it already; wrap or replace that child instead");
                }
            }

            void setParams(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const nlohmann::json* params = member(op, "params");
                const nlohmann::json* mappings = member(op, "mappings");
                if (params == nullptr && mappings == nullptr)
                {
                    throw ArgError(where + " needs 'params' (parameter -> value; null resets an optional one to its "
                                           "default)");
                }
                if (params != nullptr && !params->is_object())
                {
                    throw ArgError(path + ".params must be an object of parameter -> value");
                }
                touch(id);
                bt::BTNode& node = nodeAt(id);
                SpecErrors errors;
                if (params != nullptr)
                {
                    ParamWriter writer(data.graph.blackboardKeys, options.selfEnginePath, errors, warnings);
                    writer.write(node, params, path + ".params", false);
                }
                if (mappings != nullptr)
                {
                    if (node.type != bt::BTNodeType::DynamicSubTree)
                    {
                        errors.add(path + ".mappings", "only a DynamicSubTree has mappings");
                    }
                    else if (auto parsed = parseMappings(*mappings, path + ".mappings", data.graph.blackboardKeys, errors))
                    {
                        node.blackboardMappings = std::move(*parsed);
                    }
                }
                errors.throwIfAny(where);
            }

            void rename(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const nlohmann::json& name = required(op, "name", where);
                if (!name.is_string() || name.get<std::string>().empty())
                {
                    throw ArgError(path + ".name must be a non-empty string");
                }
                touch(id);
                nodeAt(id).name = name.get<std::string>();
            }

            void addChild(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t parent = resolve(required(op, "parent", where), path + ".parent");
                std::vector<uint32_t> kids = children(parent);
                requireRoomFor(parent, kids.size(), where);
                const std::size_t index = optIndex(op, path, kids.size());
                SpecNode spec = parseSpec(required(op, "node", where), path + ".node", true);
                const uint32_t top = addSpec(spec);
                kids.insert(kids.begin() + static_cast<std::ptrdiff_t>(index), top);
                relink(parent, kids);
                touch(parent);
            }

            void remove(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const std::optional<uint32_t> parent = parentOf(data.graph, id);
                if (parent.has_value() && *parent == data.graph.rootNodeId)
                {
                    throw ArgError(where + ": " + nodeLabel(nodeAt(id)) + " is the tree's top node and cannot be "
                                           "removed; replace it instead");
                }
                const std::unordered_set<uint32_t> doomed = subtreeOf(id);
                if (parent.has_value())
                {
                    std::vector<uint32_t> kids = children(*parent);
                    std::erase(kids, id);
                    relink(*parent, kids);
                }
                eraseNodes(doomed);
                if (parent.has_value())
                {
                    touch(*parent);
                }
            }

            void move(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const uint32_t newParent = resolve(required(op, "parent", where), path + ".parent");
                if (subtreeOf(id).contains(newParent))
                {
                    throw ArgError(where + ": cannot move " + nodeLabel(nodeAt(id)) + " below itself");
                }
                const std::optional<uint32_t> oldParent = parentOf(data.graph, id);
                if (oldParent.has_value() && *oldParent == data.graph.rootNodeId)
                {
                    throw ArgError(where + ": " + nodeLabel(nodeAt(id)) + " is the tree's top node; wrap it, or move "
                                           "the nodes below it instead");
                }
                std::vector<uint32_t> targetKids = children(newParent);
                std::erase(targetKids, id);
                requireRoomFor(newParent, targetKids.size(), where);
                if (oldParent.has_value())
                {
                    std::vector<uint32_t> kids = children(*oldParent);
                    std::erase(kids, id);
                    relink(*oldParent, kids);
                }
                const std::size_t index = optIndex(op, path, targetKids.size());
                targetKids.insert(targetKids.begin() + static_cast<std::ptrdiff_t>(index), id);
                relink(newParent, targetKids);
                if (oldParent.has_value())
                {
                    touch(*oldParent);
                }
                touch(newParent);
            }

            void wrap(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const nlohmann::json& with = required(op, "with", where);
                if (givesChildren(with))
                {
                    throw ArgError(path + ".with must not have children: the wrapped node becomes its child");
                }
                SpecNode spec = parseSpec(with, path + ".with", false);
                const NodeSpec* withSpec = bt::catalog::find(spec.node.type);
                if (withSpec == nullptr || withSpec->maxChildren == 0)
                {
                    throw ArgError(path + ".with: a " + bt::nodeTypeToString(spec.node.type) + " cannot have children; "
                                          "wrap with a decorator or a service (e.g. BlackboardCondition, Cooldown, "
                                          "Service) or a composite");
                }
                const std::optional<uint32_t> parent = parentOf(data.graph, id);
                std::vector<uint32_t> siblings;
                if (parent.has_value())
                {
                    siblings = children(*parent);
                }
                const uint32_t top = addSpec(spec);
                if (parent.has_value())
                {
                    std::replace(siblings.begin(), siblings.end(), id, top);
                    relink(*parent, siblings);
                    touch(*parent);
                }
                relink(top, {id});
            }

            void unwrap(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const std::vector<uint32_t> kids = children(id);
                if (kids.size() != 1)
                {
                    throw ArgError(where + ": " + nodeLabel(nodeAt(id)) + " has " + std::to_string(kids.size()) +
                                   " children; only a node with exactly one child can be unwrapped");
                }
                const std::optional<uint32_t> parent = parentOf(data.graph, id);
                if (parent.has_value())
                {
                    std::vector<uint32_t> siblings = children(*parent);
                    std::replace(siblings.begin(), siblings.end(), id, kids.front());
                    relink(*parent, siblings);
                }
                eraseNodes({id});
                if (parent.has_value())
                {
                    touch(*parent);
                }
            }

            void replace(const nlohmann::json& op, const std::string& path, const std::string& where)
            {
                const uint32_t id = resolve(required(op, "node", where), path + ".node");
                const nlohmann::json& with = required(op, "with", where);
                const bool newChildren = givesChildren(with);
                SpecNode spec = parseSpec(with, path + ".with", newChildren, id);

                // The replacement keeps the replaced node's id and editor position unless it gives its own.
                const bt::BTNode& old = nodeAt(id);
                std::unordered_set<uint32_t> givenIds;
                uint32_t maxGiven = 0;
                collectGivenIds(spec, givenIds, maxGiven);
                if (!spec.hasId && !givenIds.contains(id))
                {
                    spec.node.id = id;
                    spec.hasId = true;
                }
                if (!spec.hasPosition)
                {
                    spec.node.position = old.position;
                    spec.hasPosition = true;
                }

                std::vector<uint32_t> keptChildren;
                if (!newChildren)
                {
                    keptChildren = children(id);
                    const NodeSpec* newSpec = bt::catalog::find(spec.node.type);
                    if (newSpec == nullptr || keptChildren.size() < newSpec->minChildren ||
                        keptChildren.size() > newSpec->maxChildren)
                    {
                        throw ArgError(path + ".with: a " + bt::nodeTypeToString(spec.node.type) + " " +
                                       (newSpec != nullptr ? childRule(*newSpec) : std::string("is unknown")) + ", and " +
                                       nodeLabel(old) + " has " + std::to_string(keptChildren.size()) +
                                       "; give 'children' (the old subtree is then removed) or change them first");
                    }
                }

                const std::optional<uint32_t> parent = parentOf(data.graph, id);
                std::vector<uint32_t> siblings;
                if (parent.has_value())
                {
                    siblings = children(*parent);
                }
                eraseNodes(newChildren ? subtreeOf(id) : std::unordered_set<uint32_t>{id});
                const uint32_t top = addSpec(spec);
                if (!newChildren)
                {
                    relink(top, keptChildren);
                }
                if (parent.has_value())
                {
                    std::replace(siblings.begin(), siblings.end(), id, top);
                    relink(*parent, siblings);
                    touch(*parent);
                }
            }
        };

        std::string describeUses(const std::vector<KeyUse>& uses)
        {
            std::vector<std::string> parts;
            for (const KeyUse& use : uses)
            {
                parts.push_back("'" + use.nodeName + "' (" + use.nodeType + ", id " + std::to_string(use.nodeId) +
                                ") as " + use.param);
            }
            return joined(parts);
        }
    }

    // ------------------------------------------------------------------
    // Paths
    // ------------------------------------------------------------------

    TreeFile resolveTreeArgument(const std::string& text, const std::string& label, bool mustExist)
    {
        const fs::path root = terrainProjectRoot();
        std::string error;
        const std::optional<fs::path> file = locateTree(root, text, mustExist, error);
        if (!file.has_value())
        {
            throw ArgError(label + ": " + error);
        }
        return makeTreeFile(root, *file);
    }

    TreeFile treeFileFromEnginePath(const std::string& enginePath)
    {
        TreeFile out;
        out.enginePath = bt::BehaviorTreeAsset::canonicalPath(enginePath);
        out.file = fs::path(out.enginePath);
        out.relative = projectRelativeUtf8(tryTerrainProjectRoot(), out.file);
        return out;
    }

    TreeFile treeFromArguments(const ArgReader& reader, std::string_view tool)
    {
        const bool hasPath = reader.has("path");
        if (hasPath == reader.has("entity"))
        {
            throw ArgError(std::string(tool) + " takes either 'path' (a .vfBehaviorTree) or 'entity' (the tree of its "
                           "BehaviorTree component)");
        }
        if (hasPath)
        {
            return resolveTreeArgument(reader.requireString("path"), "argument 'path'", true);
        }
        const services::EntityData entity = requireEntity(reader, "entity");
        const std::optional<services::BehaviorTreeComponentData> data = componentData(entity.handle);
        const std::string label = "Entity " + std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        if (!data.has_value())
        {
            throw std::runtime_error(label + " has no BehaviorTree component (bt_assign gives it one)");
        }
        if (data->treePath.empty())
        {
            throw std::runtime_error(label + " has a BehaviorTree component without a tree (bt_assign sets one)");
        }
        return treeFileFromEnginePath(data->treePath);
    }

    std::string reportTreePath(const std::string& enginePath)
    {
        return enginePath.empty() ? std::string() : treeFileFromEnginePath(enginePath).relative;
    }

    std::string storedTreePath(const fs::path& file)
    {
        const std::string absolute = file.lexically_normal().generic_string();
        try
        {
            const std::optional<std::string> project =
                events::EventDispatcher::instance().query(events::project::GetProjectPathQuery{});
            if (project.has_value() && !project->empty())
            {
                std::error_code ec;
                const fs::path relative = fs::relative(file, fs::path(*project).parent_path(), ec);
                if (!ec && !relative.empty() && *relative.begin() != "..")
                {
                    const std::string candidate = relative.generic_string();
                    if (bt::BehaviorTreeAsset::exists(candidate) && sameTreePath(candidate, absolute))
                    {
                        return candidate;
                    }
                }
            }
        }
        catch (const std::runtime_error&)
        {
            // No project service: the absolute path is always found.
        }
        return absolute;
    }

    bool sameTreePath(const std::string& a, const std::string& b)
    {
        if (a.empty() || b.empty())
        {
            return false;
        }
        return tools::detail::componentEquals(fs::path(bt::BehaviorTreeAsset::canonicalPath(a)),
                                              fs::path(bt::BehaviorTreeAsset::canonicalPath(b)));
    }

    std::optional<ScriptTaskFile> resolveScriptTask(const std::string& text, std::string& error)
    {
        if (text.empty())
        {
            error = "must be a script path such as \"game/ai/MeleeAttack.mt\" (relative to the scripts folder)";
            return std::nullopt;
        }
        fs::path root;
        try
        {
            root = scriptsRootOnMain();
        }
        catch (const std::runtime_error& e)
        {
            error = e.what();
            return std::nullopt;
        }
        std::string pathError;
        const std::optional<fs::path> file = resolveInside(root, text, pathError, ".mt");
        if (!file.has_value())
        {
            error = pathError;
            return std::nullopt;
        }
        if (!resolveInside(root / "game", pathToUtf8(*file), pathError).has_value())
        {
            error = "'" + text + "' must be under game/ (a project script such as \"game/ai/MeleeAttack.mt\"; lib/ is "
                    "the engine API)";
            return std::nullopt;
        }
        ScriptReadResult read;
        try
        {
            read = readScript(root, pathToUtf8(*file));
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return std::nullopt;
        }
        switch (read.status)
        {
        case ScriptReadStatus::Ok:
            break;
        case ScriptReadStatus::NotFound:
            error = "no script at '" + text + "' (write it with script_write, then run scripts_build)";
            return std::nullopt;
        case ScriptReadStatus::Unreadable:
            error = "'" + text + "' is larger than 1 MB or cannot be read";
            return std::nullopt;
        case ScriptReadStatus::OpenFailed:
            error = "'" + text + "' cannot be opened";
            return std::nullopt;
        }
        std::optional<bt::ScriptTaskInfo> info = bt::scanScriptTask(read.source);
        if (!info.has_value())
        {
            error = "'" + read.path + "' is not a ScriptTask: its first @Script class must declare "
                    "tick(float deltaTime): string (section 12 of vf://docs/mtype-api)";
            return std::nullopt;
        }
        return ScriptTaskFile{read.path, std::move(*info)};
    }

    std::optional<std::string> scriptTaskClass(std::string_view scriptPath)
    {
        if (scriptPath.empty())
        {
            return std::nullopt;
        }
        const std::string given(scriptPath);
        fs::path root;
        try
        {
            root = scriptsRootOnMain();
        }
        catch (const std::runtime_error&)
        {
            // No project: the path is taken as given.
        }
        std::error_code ec;
        fs::path file = root.empty() ? fs::path(given) : fs::path(root.string() + "/" + given);
        if (!fs::exists(file, ec) && fs::exists(fs::path(given), ec))
        {
            file = fs::path(given);
        }
        const std::optional<std::string> source = readSmallFile(file);
        if (!source.has_value())
        {
            return std::nullopt;
        }
        std::optional<bt::ScriptTaskInfo> info = bt::scanScriptTask(*source);
        if (!info.has_value())
        {
            return std::nullopt;
        }
        return std::move(info->className);
    }

    std::optional<std::string> eqsQueryProblem(const std::string& name)
    {
        events::ai::GetEQSQueryInfoQuery query;
        query.queryName = name;
        services::EQSQueryInfo info;
        try
        {
            info = events::EventDispatcher::instance().query(query);
        }
        catch (const std::runtime_error&)
        {
            return std::nullopt;  // no EQS service: not checked
        }
        if (!info.exists)
        {
            return "EQS query '" + name + "' does not exist (templates are assets/eqs/<Name>.vfEQSQuery; "
                   "eqs_list_queries lists them)";
        }
        if (!info.valid)
        {
            std::string problem = "EQS query '" + name + "' is invalid";
            if (!info.errors.empty())
            {
                const eqs::EQSSpecError& first = info.errors.front();
                problem += ": " + (first.path.empty() ? first.message : first.path + ": " + first.message);
            }
            return problem;
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------
    // Hashes
    // ------------------------------------------------------------------

    std::string hashHex(uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    std::optional<uint64_t> optHash(const ArgReader& reader, const char* name)
    {
        if (!reader.has(name))
        {
            return std::nullopt;
        }
        const nlohmann::json& value = reader.raw(name);
        std::string digits = value.is_string() ? value.get<std::string>() : std::string();
        if (digits.starts_with("0x") || digits.starts_with("0X"))
        {
            digits.erase(0, 2);
        }
        uint64_t hash = 0;
        const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16);
        if (digits.empty() || digits.size() > 16 || ec != std::errc{} || end != digits.data() + digits.size())
        {
            throw ArgError(std::string("argument '") + name + "' must be the contentHash string a bt_* tool returned "
                           "(16 hex digits; got " + text(value) + ")");
        }
        return hash;
    }

    // ------------------------------------------------------------------
    // Blackboard values
    // ------------------------------------------------------------------

    const std::vector<std::string>& blackboardTypeNames()
    {
        static const std::vector<std::string> names{"Float", "Int", "Bool", "String", "Vec3", "Entity"};
        return names;
    }

    std::optional<bt::BlackboardValueType> blackboardTypeFromName(std::string_view name)
    {
        const std::vector<std::string>& names = blackboardTypeNames();
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (equalsIgnoreCase(names[i], name))
            {
                return static_cast<bt::BlackboardValueType>(i);
            }
        }
        return std::nullopt;
    }

    bt::BlackboardValueType valueType(const bt::BlackboardValue& value)
    {
        // BlackboardValue's alternatives follow BlackboardValueType (asserted in BehaviorTreeValidation.hpp).
        return static_cast<bt::BlackboardValueType>(value.index());
    }

    bt::BlackboardValue defaultValueOf(bt::BlackboardValueType type)
    {
        switch (type)
        {
        case Type::Float: return 0.0f;
        case Type::Int: return int32_t{0};
        case Type::Bool: return false;
        case Type::String: return std::string();
        case Type::Vec3: return glm::vec3(0.0f);
        case Type::Entity: return services::EntityHandle::invalid();
        }
        return 0.0f;
    }

    nlohmann::json entityJson(const services::EntityHandle& entity)
    {
        if (!entity.isValid())
        {
            return {{"entity", nullptr}, {"name", nullptr}};
        }
        const std::optional<services::EntityData> data = findEntity(entity);
        nlohmann::json out{{"entity", toId(entity)},
                           {"name", data.has_value() ? nlohmann::json(data->name) : nlohmann::json(nullptr)}};
        if (!data.has_value())
        {
            out["exists"] = false;
        }
        return out;
    }

    nlohmann::json valueJson(const bt::BlackboardValue& value)
    {
        return std::visit([](const auto& stored) -> nlohmann::json
        {
            using T = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return env::floatJson(stored);
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return env::vec3Json(stored);
            }
            else if constexpr (std::is_same_v<T, services::EntityHandle>)
            {
                return entityJson(stored);
            }
            else
            {
                return nlohmann::json(stored);  // int32_t, bool, std::string
            }
        }, value);
    }

    nlohmann::json startValueJson(const services::BehaviorTreeInitialValueData& value)
    {
        const auto* handle = std::get_if<services::EntityHandle>(&value.value);
        if (handle == nullptr)
        {
            return valueJson(value.value);
        }
        nlohmann::json out;
        if (handle->isValid())
        {
            const std::optional<services::EntityData> entity = findEntity(*handle);
            out = {{"entity", toId(*handle)}, {"name", entity.has_value() ? entity->name : value.entityRef.name}};
        }
        else
        {
            out = {{"entity", nullptr}, {"name", value.entityRef.name}};
        }
        if (value.unresolved)
        {
            out["unresolved"] = true;
        }
        return out;
    }

    std::optional<bt::BlackboardValue> parseValue(const nlohmann::json& value, bt::BlackboardValueType type,
                                                  const std::string& path, SpecErrors& errors)
    {
        switch (type)
        {
        case Type::Float:
        {
            if (!value.is_number())
            {
                errors.add(path, "must be a number (a Float value; got " + text(value) + ")");
                return std::nullopt;
            }
            const float number = static_cast<float>(value.get<double>());
            if (!std::isfinite(number))
            {
                errors.add(path, "must be a finite number");
                return std::nullopt;
            }
            return number;
        }
        case Type::Int:
        {
            const std::optional<int64_t> number = integralValue(value);
            if (!number.has_value() || *number < std::numeric_limits<int32_t>::min() ||
                *number > std::numeric_limits<int32_t>::max())
            {
                errors.add(path, "must be an integer (an Int value; got " + text(value) + ")");
                return std::nullopt;
            }
            return static_cast<int32_t>(*number);
        }
        case Type::Bool:
            if (!value.is_boolean())
            {
                errors.add(path, "must be true or false (a Bool value; got " + text(value) + ")");
                return std::nullopt;
            }
            return value.get<bool>();
        case Type::String:
            if (!value.is_string())
            {
                errors.add(path, "must be a string (a String value; got " + text(value) + ")");
                return std::nullopt;
            }
            return value.get<std::string>();
        case Type::Vec3:
        {
            const nlohmann::json* x = nullptr;
            const nlohmann::json* y = nullptr;
            const nlohmann::json* z = nullptr;
            if (value.is_array() && value.size() == 3)
            {
                x = &value[0];
                y = &value[1];
                z = &value[2];
            }
            else if (value.is_object() && value.size() == 3)
            {
                x = member(value, "x");
                y = member(value, "y");
                z = member(value, "z");
            }
            if (x == nullptr || y == nullptr || z == nullptr || !x->is_number() || !y->is_number() || !z->is_number())
            {
                errors.add(path, "must be [x, y, z] (a Vec3 value; got " + text(value) + ")");
                return std::nullopt;
            }
            const glm::vec3 vector{static_cast<float>(x->get<double>()), static_cast<float>(y->get<double>()),
                                   static_cast<float>(z->get<double>())};
            if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z))
            {
                errors.add(path, "must hold finite numbers");
                return std::nullopt;
            }
            return vector;
        }
        case Type::Entity:
        {
            const nlohmann::json* id = &value;
            if (value.is_object())
            {
                for (auto it = value.begin(); it != value.end(); ++it)
                {
                    if (it.key() != "entity" && it.key() != "name" && it.key() != "unresolved" && it.key() != "exists")
                    {
                        errors.add(path + "." + it.key(), "unknown key; an Entity value is an entity id or {entity: id}");
                        return std::nullopt;
                    }
                }
                id = member(value, "entity");
                if (id == nullptr)
                {
                    errors.add(path, "{entity: null} names no entity; pass null for the whole value to remove it");
                    return std::nullopt;
                }
            }
            const std::optional<int64_t> number = integralValue(*id);
            if (!number.has_value() || *number < 0 || *number > static_cast<int64_t>(UINT32_MAX))
            {
                errors.add(path, "must be an entity id or {entity: id} (an Entity value; got " + text(value) + ")");
                return std::nullopt;
            }
            const services::EntityHandle handle = toHandle(static_cast<uint32_t>(*number));
            if (!findEntity(handle).has_value())
            {
                errors.add(path, "entity " + std::to_string(*number) + " does not exist (scene_get_hierarchy and "
                                 "entity_find list the entities)");
                return std::nullopt;
            }
            return handle;
        }
        }
        return std::nullopt;
    }

    bt::BlackboardValue requireValue(const nlohmann::json& value, bt::BlackboardValueType type, const std::string& path)
    {
        SpecErrors errors;
        std::optional<bt::BlackboardValue> parsed = parseValue(value, type, path, errors);
        errors.throwIfAny("the value");
        return std::move(*parsed);
    }

    nlohmann::json keySpecJson(const bt::BlackboardKeyDef& key)
    {
        nlohmann::json out{{"name", key.name}, {"type", bt::blackboardValueTypeToString(key.type)}};
        if (key.type == Type::Entity)
        {
            const auto* handle = std::get_if<services::EntityHandle>(&key.defaultValue);
            out["default"] = handle != nullptr && handle->isValid() ? entityJson(*handle) : nlohmann::json(nullptr);
        }
        else
        {
            out["default"] = valueJson(valueType(key.defaultValue) == key.type ? key.defaultValue : defaultValueOf(key.type));
        }
        return out;
    }

    nlohmann::json keySpecsJson(const std::vector<bt::BlackboardKeyDef>& keys)
    {
        nlohmann::json out = nlohmann::json::array();
        for (const bt::BlackboardKeyDef& key : keys)
        {
            out.push_back(keySpecJson(key));
        }
        return out;
    }

    std::optional<bt::BlackboardKeyDef> parseKeySpec(const nlohmann::json& spec, const std::string& path,
                                                     SpecErrors& errors)
    {
        if (!spec.is_object())
        {
            errors.add(path, "must be a key {name, type, default?} (got " + text(spec) + ")");
            return std::nullopt;
        }
        bool ok = true;
        for (auto it = spec.begin(); it != spec.end(); ++it)
        {
            if (it.key() != "name" && it.key() != "type" && it.key() != "default")
            {
                errors.add(path + "." + it.key(), "unknown key; a blackboard key has name, type and default");
                ok = false;
            }
        }
        bt::BlackboardKeyDef key;
        const nlohmann::json* name = member(spec, "name");
        if (name == nullptr || !name->is_string() || name->get<std::string>().empty())
        {
            errors.add(path + ".name", "must be a non-empty string");
            ok = false;
        }
        else
        {
            key.name = name->get<std::string>();
        }
        const nlohmann::json* type = member(spec, "type");
        const std::optional<bt::BlackboardValueType> parsedType =
            type != nullptr && type->is_string() ? blackboardTypeFromName(type->get<std::string>()) : std::nullopt;
        if (!parsedType.has_value())
        {
            errors.add(path + ".type", "must be one of " + joined(blackboardTypeNames()) +
                                       (type != nullptr ? " (got " + text(*type) + ")" : std::string()));
            return std::nullopt;
        }
        key.type = *parsedType;
        const nlohmann::json* given = member(spec, "default");
        if (given == nullptr)
        {
            key.defaultValue = defaultValueOf(key.type);
        }
        else if (key.type == Type::Entity)
        {
            errors.add(path + ".default", "an Entity key has no default (entity ids change on every load): give the "
                                          "value per entity (bt_assign blackboard, bt_set_blackboard {entity, values})");
            ok = false;
        }
        else if (std::optional<bt::BlackboardValue> value = parseValue(*given, key.type, path + ".default", errors))
        {
            key.defaultValue = std::move(*value);
        }
        else
        {
            ok = false;
        }
        if (!ok)
        {
            return std::nullopt;
        }
        return key;
    }

    const bt::BlackboardKeyDef* findKey(const std::vector<bt::BlackboardKeyDef>& keys, const std::string& name)
    {
        for (const bt::BlackboardKeyDef& key : keys)
        {
            if (key.name == name)
            {
                return &key;
            }
        }
        return nullptr;
    }

    // ------------------------------------------------------------------
    // Graph
    // ------------------------------------------------------------------

    std::vector<uint32_t> childIds(const bt::BTGraph& graph, uint32_t parentId)
    {
        return GraphIndex(graph).childrenOf(parentId);
    }

    std::optional<uint32_t> parentOf(const bt::BTGraph& graph, uint32_t nodeId)
    {
        for (const bt::BTLink& link : graph.links)
        {
            if (link.targetNodeId == nodeId && graph.findNodeById(link.sourceNodeId) != nullptr)
            {
                return link.sourceNodeId;
            }
        }
        return std::nullopt;
    }

    std::string nodeLabel(const bt::BTNode& node)
    {
        const std::string type = bt::nodeTypeToString(node.type);
        return "'" + (node.name.empty() ? type : node.name) + "' (" + type + ", id " + std::to_string(node.id) + ")";
    }

    nlohmann::json nodeSummary(const bt::BTNode& node)
    {
        const std::string type = bt::nodeTypeToString(node.type);
        return {{"id", node.id}, {"name", node.name.empty() ? type : node.name}, {"type", type}};
    }

    nlohmann::json nodeList(const bt::BTGraph& graph)
    {
        const GraphIndex index(graph);
        nlohmann::json out = nlohmann::json::array();
        std::unordered_set<uint32_t> visited{graph.rootNodeId};
        std::vector<uint32_t> pending;
        const std::vector<uint32_t>& top = index.childrenOf(graph.rootNodeId);
        pending.assign(top.rbegin(), top.rend());
        while (!pending.empty())
        {
            const uint32_t id = pending.back();
            pending.pop_back();
            if (!visited.insert(id).second)
            {
                continue;
            }
            out.push_back(nodeSummary(*index.node(id)));
            const std::vector<uint32_t>& kids = index.childrenOf(id);
            pending.insert(pending.end(), kids.rbegin(), kids.rend());
        }
        return out;
    }

    void layoutTree(bt::BTGraph& graph, const std::unordered_set<uint32_t>& needsPosition, bool relayout)
    {
        if (!relayout && needsPosition.empty())
        {
            return;
        }
        const GraphIndex index(graph);
        if (index.node(graph.rootNodeId) == nullptr)
        {
            return;
        }
        std::unordered_map<uint32_t, glm::vec2> tidy;
        std::unordered_set<uint32_t> visited;
        int leaves = 0;
        std::function<float(uint32_t, std::size_t)> place = [&](uint32_t id, std::size_t depth) -> float
        {
            visited.insert(id);
            float first = 0.0f;
            float last = 0.0f;
            bool any = false;
            if (depth < maxWalkDepth)
            {
                for (const uint32_t child : index.childrenOf(id))
                {
                    if (visited.contains(child))
                    {
                        continue;
                    }
                    const float x = place(child, depth + 1);
                    first = any ? first : x;
                    last = x;
                    any = true;
                }
            }
            const float x = any ? (first + last) * 0.5f : static_cast<float>(leaves++) * layoutColumn;
            tidy[id] = glm::vec2(x, static_cast<float>(depth) * layoutRow);
            return x;
        };
        place(graph.rootNodeId, 0);
        for (bt::BTNode& node : graph.nodes)
        {
            if (!relayout && !needsPosition.contains(node.id))
            {
                continue;
            }
            if (const auto it = tidy.find(node.id); it != tidy.end())
            {
                node.position = it->second;
            }
        }
    }

    bool sameTreeContent(const bt::BehaviorTreeData& a, const bt::BehaviorTreeData& b)
    {
        const auto content = [](const bt::BehaviorTreeData& data)
        {
            const nlohmann::json document = nlohmann::json::parse(bt::BehaviorTreeAsset::serialize(data));
            std::vector<nlohmann::json> nodes(document["nodes"].begin(), document["nodes"].end());
            std::sort(nodes.begin(), nodes.end(), [](const nlohmann::json& x, const nlohmann::json& y)
            {
                return x.value("id", 0u) < y.value("id", 0u);
            });
            const GraphIndex index(data.graph);
            std::vector<std::pair<uint32_t, std::vector<uint32_t>>> order(index.children.begin(), index.children.end());
            std::sort(order.begin(), order.end());
            nlohmann::json edges = nlohmann::json::array();
            for (const auto& [parent, kids] : order)
            {
                edges.push_back(nlohmann::json::array({parent, kids}));
            }
            return nlohmann::json{{"name", document["name"]}, {"root", document["rootNodeId"]},
                                  {"keys", document["blackboardKeys"]}, {"nodes", nodes}, {"edges", edges}};
        };
        return content(a) == content(b);
    }

    // ------------------------------------------------------------------
    // The tree JSON
    // ------------------------------------------------------------------

    ParsedTree parseTreeSpec(const nlohmann::json& spec, const SpecOptions& options)
    {
        if (!spec.is_object())
        {
            throw ArgError("argument 'tree' must be an object {name, blackboard?, root} (vf://docs/bt section 3)");
        }
        ParsedTree out;
        SpecErrors errors;
        for (auto it = spec.begin(); it != spec.end(); ++it)
        {
            if (it.key() != "name" && it.key() != "blackboard" && it.key() != "root")
            {
                errors.add(it.key(), "unknown key; a tree has name, blackboard and root");
            }
        }

        std::string name;
        const nlohmann::json* nameValue = member(spec, "name");
        if (nameValue == nullptr || !nameValue->is_string() || nameValue->get<std::string>().empty())
        {
            errors.add("name", "must be a non-empty string (the tree's name)");
        }
        else
        {
            name = nameValue->get<std::string>();
        }

        std::vector<bt::BlackboardKeyDef> keys;
        if (const nlohmann::json* blackboard = member(spec, "blackboard"))
        {
            if (!blackboard->is_array())
            {
                errors.add("blackboard", "must be an array of {name, type, default?}");
            }
            else if (blackboard->size() > maxBlackboardKeys)
            {
                errors.add("blackboard", "holds more than " + std::to_string(maxBlackboardKeys) + " keys");
            }
            else
            {
                std::unordered_map<std::string, std::size_t> seen;
                for (std::size_t i = 0; i < blackboard->size(); ++i)
                {
                    const std::string keyPath = "blackboard[" + std::to_string(i) + "]";
                    std::optional<bt::BlackboardKeyDef> key = parseKeySpec((*blackboard)[i], keyPath, errors);
                    if (!key.has_value())
                    {
                        continue;
                    }
                    const auto [it, inserted] = seen.emplace(key->name, i);
                    if (!inserted)
                    {
                        errors.add(keyPath + ".name", "duplicate key '" + key->name + "' (also blackboard[" +
                                                      std::to_string(it->second) + "])");
                        continue;
                    }
                    keys.push_back(std::move(*key));
                }
            }
        }

        SpecParser parser(keys, options.selfEnginePath, {}, errors, out.warnings);
        const nlohmann::json* rootSpec = member(spec, "root");
        std::string rootPath = "root";
        std::optional<uint32_t> wrapperId;
        if (rootSpec == nullptr)
        {
            errors.add("root", "missing: the tree's top node (the Root above it is implicit)");
        }
        else if (const nlohmann::json* type = member(*rootSpec, "type");
                 type != nullptr && type->is_string() && equalsIgnoreCase(type->get<std::string>(), "Root"))
        {
            out.warnings.push_back("'root' was a Root node: the Root is implicit, so its child became the tree's top node");
            for (auto it = rootSpec->begin(); it != rootSpec->end(); ++it)
            {
                if (it.key() != "type" && it.key() != "name" && it.key() != "id" && it.key() != "position" &&
                    it.key() != "child" && it.key() != "children")
                {
                    errors.add("root." + it.key(), "a Root wrapper has only a type, an id and its one child");
                }
            }
            if (const nlohmann::json* id = member(*rootSpec, "id"))
            {
                uint32_t value = 0;
                if (parser.claimId(*id, "root.id", value))
                {
                    wrapperId = value;
                }
            }
            const nlohmann::json* child = member(*rootSpec, "child");
            const nlohmann::json* children = member(*rootSpec, "children");
            if (child != nullptr && children == nullptr)
            {
                rootSpec = child;
                rootPath = "root.child";
            }
            else if (child == nullptr && children != nullptr && children->is_array() && children->size() == 1)
            {
                rootSpec = &(*children)[0];
                rootPath = "root.children[0]";
            }
            else
            {
                errors.add("root", "a Root wrapper needs exactly one child: the tree's top node");
                rootSpec = nullptr;
            }
        }

        std::optional<SpecNode> top;
        if (rootSpec != nullptr)
        {
            top = parser.parse(*rootSpec, rootPath, 1);
        }
        errors.throwIfAny("argument 'tree'");
        if (!top.has_value())
        {
            throw ArgError("argument 'tree' has no top node");
        }

        // ---- The graph ----
        bt::BehaviorTreeData& data = out.data;
        data.version = bt::BT_FORMAT_VERSION;
        data.name = name;
        data.graph.blackboardKeys = std::move(keys);

        const bt::BTGraph* previous = options.previous;
        std::unordered_set<uint32_t> used;
        uint32_t maxGiven = 0;
        for (const auto& [id, path] : parser.givenIds())
        {
            used.insert(id);
            maxGiven = std::max(maxGiven, id);
        }
        uint32_t start = maxGiven + 1;
        if (previous != nullptr)
        {
            start = nextIdAfter(*previous, std::max(start, previous->nextNodeId));
        }
        IdAllocator ids(start, used);

        uint32_t rootId = 0;
        if (wrapperId.has_value())
        {
            rootId = *wrapperId;
        }
        else if (previous != nullptr && previous->rootNodeId != 0 && !used.contains(previous->rootNodeId))
        {
            rootId = previous->rootNodeId;
            ids.claim(rootId);
        }
        else
        {
            rootId = ids.take();
        }
        assignIds(*top, ids);

        bt::BTNode root;
        root.id = rootId;
        root.type = bt::BTNodeType::Root;
        root.name = "Root";
        data.graph.nodes.push_back(root);
        std::unordered_set<uint32_t> needsPosition{rootId};
        LinkAllocator links(previous);
        appendTree(data.graph, *top, rootId, 0, links, needsPosition);
        data.graph.rootNodeId = rootId;
        data.graph.nextNodeId = nextIdAfter(data.graph, previous != nullptr ? previous->nextNodeId : 1);
        data.graph.nextLinkId = nextLinkAfter(data.graph, previous != nullptr ? previous->nextLinkId : 1);

        // bt_set keeps the editor position of every node that survives by id.
        if (previous != nullptr && !options.relayout)
        {
            for (bt::BTNode& node : data.graph.nodes)
            {
                if (needsPosition.contains(node.id))
                {
                    if (const bt::BTNode* old = previous->findNodeById(node.id))
                    {
                        node.position = old->position;
                        needsPosition.erase(node.id);
                    }
                }
            }
        }
        layoutTree(data.graph, needsPosition, options.relayout);

        // The catalogue's own strict check, as the authoring service runs it on save.
        throwStrictErrors(data, std::nullopt, "argument 'tree'");
        return out;
    }

    EmittedTree emitTree(const bt::BehaviorTreeData& data)
    {
        EmittedTree out;
        const bt::BTGraph& graph = data.graph;
        const GraphIndex index(graph);
        std::unordered_set<uint32_t> visited;

        nlohmann::json top = nullptr;
        const bt::BTNode* root = index.node(graph.rootNodeId);
        if (root == nullptr)
        {
            out.warnings.push_back("the tree has no Root node (rootNodeId " + std::to_string(graph.rootNodeId) + ")");
        }
        else
        {
            visited.insert(root->id);
            if (root->type != bt::BTNodeType::Root)
            {
                out.warnings.push_back("rootNodeId names " + nodeLabel(*root) + ", not a Root node");
            }
            const std::vector<uint32_t>& children = index.childrenOf(root->id);
            if (children.empty())
            {
                out.warnings.push_back("the tree is empty: the Root has no child");
            }
            else
            {
                if (children.size() > 1)
                {
                    out.warnings.push_back("the Root has " + std::to_string(children.size()) +
                                           " children; the runtime runs only the first");
                }
                top = emitNode(index, children.front(), 1, visited, out);
            }
        }

        for (const bt::BTNode& node : graph.nodes)
        {
            if (!visited.contains(node.id))
            {
                out.unreachable.push_back(nodeSummary(node));
            }
        }
        if (!out.unreachable.empty())
        {
            out.warnings.push_back(std::to_string(out.unreachable.size()) + " node(s) are not under the tree's top "
                                   "node (see 'unreachable'): the runtime never runs them and bt_set drops them");
        }

        out.tree = {{"name", data.name}, {"blackboard", keySpecsJson(graph.blackboardKeys)}, {"root", std::move(top)}};
        return out;
    }

    // ------------------------------------------------------------------
    // Edits
    // ------------------------------------------------------------------

    EditOutcome applyEdits(const bt::BehaviorTreeData& current, const nlohmann::json& ops, const SpecOptions& options)
    {
        if (!ops.is_array() || ops.empty() || ops.size() > maxEditOps)
        {
            throw ArgError("argument 'ops' must be an array of 1-" + std::to_string(maxEditOps) + " operations");
        }
        TreeEditor editor(current, options);
        for (std::size_t i = 0; i < ops.size(); ++i)
        {
            editor.apply(ops[i], i);
        }
        return editor.finish();
    }

    KeyEditOutcome applyKeyEdits(const bt::BehaviorTreeData& current, const nlohmann::json* keys,
                                 const nlohmann::json* removeKeys)
    {
        KeyEditOutcome out;
        out.data = current;
        bt::BTGraph& graph = out.data.graph;
        normalizeKeys(graph, out.warnings);

        SpecErrors errors;
        std::vector<bt::BlackboardKeyDef> specs;
        if (keys != nullptr)
        {
            if (!keys->is_array() || keys->size() > maxBlackboardKeys)
            {
                throw ArgError("argument 'keys' must be an array of up to " + std::to_string(maxBlackboardKeys) +
                               " keys {name, type, default?}");
            }
            std::unordered_map<std::string, std::size_t> seen;
            for (std::size_t i = 0; i < keys->size(); ++i)
            {
                const std::string path = "keys[" + std::to_string(i) + "]";
                std::optional<bt::BlackboardKeyDef> key = parseKeySpec((*keys)[i], path, errors);
                if (!key.has_value())
                {
                    continue;
                }
                const auto [it, inserted] = seen.emplace(key->name, i);
                if (!inserted)
                {
                    errors.add(path + ".name", "duplicate key '" + key->name + "' (also keys[" +
                                               std::to_string(it->second) + "])");
                    continue;
                }
                specs.push_back(std::move(*key));
            }
        }
        std::vector<std::string> removals;
        if (removeKeys != nullptr)
        {
            if (!removeKeys->is_array())
            {
                throw ArgError("argument 'removeKeys' must be an array of key names");
            }
            for (std::size_t i = 0; i < removeKeys->size(); ++i)
            {
                const std::string path = "removeKeys[" + std::to_string(i) + "]";
                const nlohmann::json& name = (*removeKeys)[i];
                if (!name.is_string())
                {
                    errors.add(path, "must be a key name (got " + text(name) + ")");
                    continue;
                }
                const std::string key = name.get<std::string>();
                if (findKey(graph.blackboardKeys, key) == nullptr)
                {
                    errors.add(path, "the tree has no key '" + key + "' (" + keyListText(graph.blackboardKeys) + ")");
                }
                else if (findKey(specs, key) != nullptr)
                {
                    errors.add(path, "'" + key + "' is also in 'keys'");
                }
                else if (std::find(removals.begin(), removals.end(), key) == removals.end())
                {
                    removals.push_back(key);
                }
            }
        }
        errors.throwIfAny("the blackboard edit");

        const std::map<std::string, std::vector<KeyUse>> uses = keyUses(graph);
        SpecErrors inUse;
        for (const std::string& key : removals)
        {
            const auto it = uses.find(key);
            if (it != uses.end() && !it->second.empty())
            {
                inUse.add("removeKeys", "key '" + key + "' is still used by " + describeUses(it->second) +
                                        "; point those parameters at another key first (bt_edit setParams)");
            }
        }
        inUse.throwIfAny("the blackboard edit");

        std::erase_if(graph.blackboardKeys, [&removals](const bt::BlackboardKeyDef& key)
        {
            return std::find(removals.begin(), removals.end(), key.name) != removals.end();
        });
        for (bt::BlackboardKeyDef& spec : specs)
        {
            const auto it = std::find_if(graph.blackboardKeys.begin(), graph.blackboardKeys.end(),
                                         [&spec](const bt::BlackboardKeyDef& key) { return key.name == spec.name; });
            if (it == graph.blackboardKeys.end())
            {
                graph.blackboardKeys.push_back(std::move(spec));
                continue;
            }
            if (it->type != spec.type)
            {
                if (const auto used = uses.find(spec.name); used != uses.end())
                {
                    for (const KeyUse& use : used->second)
                    {
                        out.strictNodes.insert(use.nodeId);
                    }
                }
            }
            *it = std::move(spec);
        }
        if (graph.blackboardKeys.size() > maxBlackboardKeys)
        {
            throw ArgError("the tree would have more than " + std::to_string(maxBlackboardKeys) + " blackboard keys");
        }

        // A node reading a key whose type changed must still fit it.
        for (const uint32_t id : out.strictNodes)
        {
            if (bt::BTNode* node = graph.findNodeById(id))
            {
                normalizeNode(*node, out.warnings);
            }
        }
        if (!out.strictNodes.empty())
        {
            throwStrictErrors(out.data, out.strictNodes, "with the new key types, the nodes using them");
        }
        return out;
    }

    std::map<std::string, std::vector<KeyUse>> keyUses(const bt::BTGraph& graph)
    {
        std::map<std::string, std::vector<KeyUse>> uses;
        for (const bt::BTNode& node : graph.nodes)
        {
            const NodeSpec* spec = bt::catalog::find(node.type);
            if (node.id == graph.rootNodeId || spec == nullptr)
            {
                continue;
            }
            const std::string type = bt::nodeTypeToString(node.type);
            const std::string name = node.name.empty() ? type : node.name;
            for (const ParamSpec* param : bt::catalog::paramsFor(*spec, canonicalServiceType(storedServiceType(node))))
            {
                if (param->kind != ParamKind::KeyRef)
                {
                    continue;
                }
                // An unset parameter reads the key its seed names, like the runtime's default.
                std::string key;
                const auto it = node.properties.find(param->name);
                if (it != node.properties.end())
                {
                    if (const auto* value = std::get_if<std::string>(&it->second))
                    {
                        key = *value;
                    }
                }
                else if (const auto* seed = std::get_if<std::string>(&param->defaultValue))
                {
                    key = *seed;
                }
                if (!key.empty())
                {
                    uses[key].push_back({node.id, name, type, param->name});
                }
            }
            for (const bt::BlackboardMapping& mapping : node.blackboardMappings)
            {
                if (!mapping.parentKey.empty())
                {
                    uses[mapping.parentKey].push_back({node.id, name, type, "mappings"});
                }
            }
        }
        return uses;
    }

    nlohmann::json keyUsesJson(const std::vector<KeyUse>& uses)
    {
        nlohmann::json out = nlohmann::json::array();
        for (const KeyUse& use : uses)
        {
            out.push_back({{"id", use.nodeId}, {"name", use.nodeName}, {"type", use.nodeType}, {"param", use.param}});
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Engine
    // ------------------------------------------------------------------

    services::BehaviorTreeAssetLoadResult loadTree(const std::string& enginePath)
    {
        events::ai::LoadBehaviorTreeAssetQuery query;
        query.path = enginePath;
        try
        {
            return events::EventDispatcher::instance().query(query);
        }
        catch (const std::runtime_error&)
        {
            services::BehaviorTreeAssetLoadResult result;
            result.error = "behavior tree authoring is not available (it is an Editor service)";
            return result;
        }
    }

    services::BehaviorTreeAssetSaveResult saveTree(const TreeFile& file, const bt::BehaviorTreeData& data,
                                                   std::optional<uint64_t> expectedHash, bool createNew,
                                                   std::optional<std::unordered_set<uint32_t>> strictNodes)
    {
        events::ai::SaveBehaviorTreeAssetCommand command;
        command.path = file.enginePath;
        command.data = data;
        command.expectedContentHash = expectedHash;
        command.createNew = createNew;
        command.strictNodes = std::move(strictNodes);
        return events::EventDispatcher::instance().execute(command);
    }

    ToolResult saveFailure(const TreeFile& file, const services::BehaviorTreeAssetSaveResult& result)
    {
        std::string message = "Cannot save '" + file.relative + "': " + result.error;
        const std::vector<std::string> errors = diagnosticTexts(result.diagnostics, true);
        for (std::size_t i = 0; i < errors.size() && i < maxSpecErrors; ++i)
        {
            message += "\n- " + errors[i];
        }
        if (errors.size() > maxSpecErrors)
        {
            message += "\n- ... and " + std::to_string(errors.size() - maxSpecErrors) + " more";
        }
        return ToolResult::error(message);
    }

    std::vector<std::string> diagnosticTexts(const std::vector<bt::validation::Diagnostic>& diagnostics, bool errors)
    {
        std::vector<std::string> out;
        for (const bt::validation::Diagnostic& diagnostic : diagnostics)
        {
            if ((diagnostic.severity == bt::validation::Severity::Error) != errors)
            {
                continue;
            }
            // Catalogue messages name their node; the engine validator's only carry its id.
            out.push_back(diagnostic.nodeId != 0 && !diagnostic.message.starts_with("'")
                ? "node " + std::to_string(diagnostic.nodeId) + ": " + diagnostic.message
                : diagnostic.message);
        }
        return out;
    }

    bt::catalog::StrictContext strictContext(std::optional<std::unordered_set<uint32_t>> onlyNodes)
    {
        bt::catalog::StrictContext context;
        context.scriptClass = [](std::string_view path) { return scriptTaskClass(path); };
        context.treeExists = [](std::string_view path) { return bt::BehaviorTreeAsset::exists(path); };
        context.eqsQueryExists = [](std::string_view name) { return !eqsQueryProblem(std::string(name)).has_value(); };
        context.onlyNodes = std::move(onlyNodes);
        return context;
    }

    nlohmann::json validationJson(const bt::BehaviorTreeData& data)
    {
        constexpr std::size_t cap = 50;
        const auto capped = [](std::vector<std::string> items)
        {
            if (items.size() > cap)
            {
                const std::size_t more = items.size() - cap;
                items.resize(cap);
                items.push_back("... and " + std::to_string(more) + " more");
            }
            return items;
        };
        bt::validation::ValidationContext engineContext;
        engineContext.subtreeExists = [](std::string_view path) { return bt::BehaviorTreeAsset::exists(path); };
        const std::vector<bt::validation::Diagnostic> engine =
            bt::validation::validateBehaviorTree(data, engineContext).diagnostics;
        const std::vector<bt::validation::Diagnostic> strict = bt::catalog::validateStrict(data, strictContext());
        return {{"errors", capped(diagnosticTexts(engine, true))},
                {"warnings", capped(diagnosticTexts(engine, false))},
                {"strict", capped(diagnosticTexts(strict, true))}};
    }

    bool playMode()
    {
        try
        {
            return events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    int liveRuntimesUsing(const std::string& enginePath)
    {
        try
        {
            const std::vector<events::ai::BTAttachedTreeInfo> attached =
                events::EventDispatcher::instance().query(events::ai::GetAttachedBehaviorTreesQuery{});
            return static_cast<int>(std::count_if(attached.begin(), attached.end(), [&enginePath](const auto& info)
            {
                return sameTreePath(info.treePath, enginePath);
            }));
        }
        catch (const std::runtime_error&)
        {
            return 0;
        }
    }

    nlohmann::json attachedTo(const std::string& enginePath)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        std::map<uint64_t, nlohmann::json> rows;
        try
        {
            events::scene::GetEntitiesWithComponentQuery query;
            query.componentType = services::ComponentTypeId::BehaviorTree;
            for (const services::EntityHandle& entity : dispatcher.query(query))
            {
                const std::optional<services::BehaviorTreeComponentData> data = componentData(entity);
                if (!data.has_value() || !sameTreePath(data->treePath, enginePath))
                {
                    continue;
                }
                const std::optional<services::EntityData> entityData = findEntity(entity);
                rows[entity.id] = {{"entity", toId(entity)},
                                   {"name", entityData.has_value() ? entityData->name : std::string()},
                                   {"component", true},
                                   {"enabled", data->enabled},
                                   {"live", false}};
            }
        }
        catch (const std::runtime_error&)
        {
            // No entity query service: components are not listed.
        }
        try
        {
            for (const events::ai::BTAttachedTreeInfo& info : dispatcher.query(events::ai::GetAttachedBehaviorTreesQuery{}))
            {
                if (!sameTreePath(info.treePath, enginePath))
                {
                    continue;
                }
                auto [it, inserted] = rows.try_emplace(
                    info.entity.id, nlohmann::json{{"entity", toId(info.entity)}, {"name", info.name}, {"component", false}});
                it->second["live"] = true;
            }
        }
        catch (const std::runtime_error&)
        {
            // No behavior tree service: no live runtimes.
        }
        nlohmann::json out = nlohmann::json::array();
        for (auto& [id, row] : rows)
        {
            out.push_back(std::move(row));
        }
        return out;
    }

    TreeFacts treeFacts(const std::string& enginePath)
    {
        TreeFacts facts;
        std::vector<std::string> stack;
        std::function<bool(const std::string&, const std::string&, std::size_t)> visit =
            [&](const std::string& path, const std::string& source, std::size_t depth) -> bool
        {
            const std::string canonical = bt::BehaviorTreeAsset::canonicalPath(path);
            const services::BehaviorTreeAssetLoadResult loaded = loadTree(canonical);
            if (!loaded.success)
            {
                if (depth == 0)
                {
                    facts.error = loaded.error;
                }
                return false;
            }
            stack.push_back(canonical);
            const bt::BTGraph& graph = loaded.data.graph;
            for (const bt::BlackboardKeyDef& key : graph.blackboardKeys)
            {
                if (findKey(facts.keys, key.name) == nullptr)
                {
                    facts.keys.push_back(key);
                    facts.keySources.push_back(source);
                }
            }
            for (const auto& [key, uses] : keyUses(graph))
            {
                for (const KeyUse& use : uses)
                {
                    const bt::BTNode* node = graph.findNodeById(use.nodeId);
                    if (listed(targetParams, use.param))
                    {
                        facts.targetKeys.insert(key);
                    }
                    else if (listed(outputParams, use.param) ||
                             (node != nullptr && node->type == bt::BTNodeType::SetBlackboardValue && use.param == "key"))
                    {
                        facts.writtenKeys.insert(key);
                    }
                }
            }
            for (const bt::BTNode& node : graph.nodes)
            {
                if (node.type == bt::BTNodeType::MoveTo || node.type == bt::BTNodeType::FollowRoute)
                {
                    facts.moves = true;
                }
                if (node.type != bt::BTNodeType::SubTree || depth >= 8)
                {
                    continue;
                }
                const std::string child = stringProperty(node, "treePath");
                if (child.empty() || std::any_of(stack.begin(), stack.end(), [&child](const std::string& open)
                {
                    return sameTreePath(open, child);
                }))
                {
                    continue;  // a cycle: the runtime refuses it, so it adds nothing
                }
                visit(child, reportTreePath(child), depth + 1);
            }
            stack.pop_back();
            return true;
        };
        facts.readable = visit(enginePath, "", 0);
        return facts;
    }

    std::optional<services::BehaviorTreeComponentData> componentData(const services::EntityHandle& entity)
    {
        events::ai::GetBehaviorTreeComponentDataQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }
}
