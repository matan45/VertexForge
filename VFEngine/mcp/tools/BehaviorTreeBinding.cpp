#include "ComponentBinding.hpp"
#include "BehaviorTreeToolSupport.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"
#include "../undo/EntityIdRemap.hpp"

#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

// VK-1660: "BehaviorTree" of component_* - the entity's tree, its enabled flag and its per-entity blackboard start
// values (BehaviorTreeComponent::initialValues, applied when the tree attaches at Edit->Play). Built on the whole-
// component events GetBehaviorTreeComponentDataQuery / SetBehaviorTreeComponentDataCommand. Entity values are stored
// by the engine as {UUID, name}; the tools exchange them as {entity, name}. patch() takes agent input (the value must
// fit the tree's key type, `blackboard` merges); restore() replays a get() snapshot for undo (`blackboard` replaces,
// types follow the JSON, a recorded entity id that no longer names the entity is looked up by name again). Every
// write is refused in Play mode and while a scene load is pending: Play->Stop would discard it, and a running tree
// never sees it. bt_assign / bt_unassign / bt_set_blackboard {entity} are thin wrappers over this binding.
namespace mcp::tools
{
    namespace
    {
        namespace bt = behaviortree;
        namespace fs = std::filesystem;

        enum class Source
        {
            Agent,    // component_set / bt_* input
            Snapshot  // an undo replay of a get() result
        };

        void refuseWhenBlocked()
        {
            if (const std::optional<std::string> blocked = env::editBlocker("change a BehaviorTree component"))
            {
                throw std::runtime_error(*blocked);
            }
        }

        std::string typeName(bt::BlackboardValueType type)
        {
            return bt::blackboardValueTypeToString(type);
        }

        using bttool::startValueJson;

        // What the component does not do as one might expect: no tree, a disabled tree, start values that do not fit
        // the tree (skipped when Play starts) or name no entity, Entity keys the tree reads that nothing sets.
        nlohmann::json warningsFor(const services::BehaviorTreeComponentData& data)
        {
            nlohmann::json warnings = nlohmann::json::array();
            if (data.treePath.empty())
            {
                warnings.push_back("no tree is assigned: the component does nothing (set 'tree')");
                return warnings;
            }
            const bttool::TreeFacts facts = bttool::treeFacts(data.treePath);
            if (!facts.readable)
            {
                warnings.push_back("the tree cannot be read: " + facts.error);
                return warnings;
            }
            if (!data.enabled)
            {
                warnings.push_back("disabled: the tree does not run when Play starts");
            }
            for (const services::BehaviorTreeInitialValueData& value : data.initialValues)
            {
                const bt::BlackboardKeyDef* key = bttool::findKey(facts.keys, value.key);
                const bt::BlackboardValueType type = bttool::valueType(value.value);
                if (key == nullptr)
                {
                    warnings.push_back("start value '" + value.key + "' is not a key of the tree: it is skipped when "
                                       "Play starts");
                }
                else if (key->type != type)
                {
                    warnings.push_back("start value '" + value.key + "' is " + typeName(type) + " but the tree's key is " +
                                       typeName(key->type) + ": it is skipped when Play starts");
                }
                else if (value.unresolved)
                {
                    warnings.push_back("start value '" + value.key + "' names entity '" + value.entityRef.name +
                                       "', which does not exist now (it is looked up by UUID, then by name, when Play "
                                       "starts)");
                }
            }
            for (const bt::BlackboardKeyDef& key : facts.keys)
            {
                const bool hasValue = std::any_of(data.initialValues.begin(), data.initialValues.end(),
                                                  [&key](const services::BehaviorTreeInitialValueData& value)
                                                  {
                                                      return value.key == key.name;
                                                  });
                if (key.type == bt::BlackboardValueType::Entity && !hasValue && facts.targetKeys.contains(key.name) &&
                    !facts.writtenKeys.contains(key.name))
                {
                    warnings.push_back("Entity key '" + key.name + "' has no start value, so the nodes that read it "
                                       "find no entity unless a script sets it (bt_set_blackboard {entity, values})");
                }
            }
            return warnings;
        }

        std::optional<nlohmann::json> getBehaviorTree(const services::EntityHandle& entity)
        {
            const std::optional<services::BehaviorTreeComponentData> data = bttool::componentData(entity);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            nlohmann::json blackboard = nlohmann::json::object();
            for (const services::BehaviorTreeInitialValueData& value : data->initialValues)
            {
                blackboard[value.key] = startValueJson(value);
            }
            return nlohmann::json{
                {"tree", bttool::reportTreePath(data->treePath)},
                {"enabled", data->enabled},
                {"blackboard", std::move(blackboard)},
                {"warnings", warningsFor(*data)}
            };
        }

        std::string agentTreePath(const nlohmann::json& value)
        {
            if (value.is_null())
            {
                return {};
            }
            if (!value.is_string())
            {
                throw ArgError("field 'tree' must be a .vfBehaviorTree path relative to the project's asset folder "
                               "(\"\" = none)");
            }
            const std::string text = value.get<std::string>();
            return text.empty() ? std::string() : bttool::resolveTreeArgument(text, "field 'tree'", true).enginePath;
        }

        // A tree path a get() reported: relative to the asset folder, or the full path of a tree outside it (the
        // editor can assign any tree; the engine's rules, not the agent's, decide what is restored).
        std::string snapshotTreePath(const nlohmann::json& value)
        {
            if (value.is_null())
            {
                return {};
            }
            if (!value.is_string())
            {
                throw ArgError("field 'tree' must be a path string");
            }
            const std::string text = value.get<std::string>();
            if (text.empty())
            {
                return {};
            }
            std::error_code ec;
            const fs::path given = pathFromUtf8(text);
            if (const std::optional<fs::path> root = tryTerrainProjectRoot())
            {
                const fs::path joined = (*root / given).lexically_normal();
                if (fs::is_regular_file(joined, ec))
                {
                    return joined.string();
                }
            }
            if (given.is_absolute() && fs::is_regular_file(given, ec))
            {
                return given.string();
            }
            throw std::runtime_error("the behavior tree '" + text + "' no longer exists");
        }

        // component_set / bt_* input: merged into the stored values; null removes one. A value reported back unchanged
        // stays as stored (an unresolved reference included); any other must fit the type of the tree's key.
        void mergeStartValues(services::BehaviorTreeComponentData& staged, const nlohmann::json& values)
        {
            if (!values.is_object())
            {
                throw ArgError("field 'blackboard' must be an object of key -> value (null removes a key's start value)");
            }
            std::optional<bttool::TreeFacts> facts;
            bttool::SpecErrors errors;
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                const std::string& key = it.key();
                const std::string path = "blackboard." + key;
                auto existing = std::find_if(staged.initialValues.begin(), staged.initialValues.end(),
                                             [&key](const services::BehaviorTreeInitialValueData& value)
                                             {
                                                 return value.key == key;
                                             });
                if (it->is_null())
                {
                    if (existing != staged.initialValues.end())
                    {
                        staged.initialValues.erase(existing);
                    }
                    continue;
                }
                if (existing != staged.initialValues.end() && *it == startValueJson(*existing))
                {
                    continue;
                }
                if (staged.treePath.empty())
                {
                    errors.add(path, "assign a tree first ('tree'): a start value must fit one of its keys");
                    continue;
                }
                if (!facts.has_value())
                {
                    facts = bttool::treeFacts(staged.treePath);
                }
                if (!facts->readable)
                {
                    errors.add(path, "the tree cannot be read: " + facts->error);
                    continue;
                }
                const bt::BlackboardKeyDef* definition = bttool::findKey(facts->keys, key);
                if (definition == nullptr)
                {
                    std::string names;
                    for (const bt::BlackboardKeyDef& candidate : facts->keys)
                    {
                        names += (names.empty() ? "" : ", ") + candidate.name + " (" + typeName(candidate.type) + ")";
                    }
                    errors.add(path, "the tree has no key '" + key + "' (keys: " + (names.empty() ? "none" : names) + ")");
                    continue;
                }
                std::optional<bt::BlackboardValue> value = bttool::parseValue(*it, definition->type, path, errors);
                if (!value.has_value())
                {
                    continue;
                }
                services::BehaviorTreeInitialValueData entry;
                entry.key = key;
                entry.value = std::move(*value);
                if (existing != staged.initialValues.end())
                {
                    *existing = std::move(entry);
                }
                else
                {
                    staged.initialValues.push_back(std::move(entry));
                }
            }
            errors.throwIfAny("field 'blackboard'");
        }

        // An Entity start value from a snapshot {entity, name}. The recorded id is used while it still names that
        // entity (an undo-recreated entity is found through EntityIdRemap); otherwise the value is stored by name
        // and resolved like every start value (UUID, then name) when it is read or Play starts.
        void restoreEntityValue(const nlohmann::json& value, services::BehaviorTreeInitialValueData& entry)
        {
            std::string name;
            if (const auto it = value.find("name"); it != value.end() && it->is_string())
            {
                name = it->get<std::string>();
            }
            entry.value = services::EntityHandle::invalid();
            entry.entityRef = bt::BlackboardEntityRef{0, name};

            const nlohmann::json& id = value.at("entity");
            if (!id.is_number_integer())
            {
                return;
            }
            const int64_t raw = id.get<int64_t>();
            if (raw < 0 || raw > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
            {
                return;
            }
            const services::EntityHandle live = toHandle(undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(raw)));
            const std::optional<services::EntityData> found = findEntity(live);
            if (found.has_value() && (name.empty() || found->name == name))
            {
                entry.value = live;
            }
        }

        // Undo: the snapshot's values replace the stored ones; each value's type is its JSON shape (get() reports
        // Int as an integer and Float as a decimal number).
        std::vector<services::BehaviorTreeInitialValueData> snapshotStartValues(const nlohmann::json& values)
        {
            if (!values.is_object())
            {
                throw ArgError("field 'blackboard' must be an object of key -> value");
            }
            std::vector<services::BehaviorTreeInitialValueData> out;
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                const nlohmann::json& value = *it;
                services::BehaviorTreeInitialValueData entry;
                entry.key = it.key();
                if (value.is_null())
                {
                    continue;  // a non-finite Float reports as null; nothing to restore
                }
                if (value.is_boolean())
                {
                    entry.value = value.get<bool>();
                }
                else if (value.is_number_integer())
                {
                    const int64_t number = value.is_number_unsigned()
                        ? static_cast<int64_t>(std::min<uint64_t>(value.get<uint64_t>(), std::numeric_limits<int64_t>::max()))
                        : value.get<int64_t>();
                    if (number < std::numeric_limits<int32_t>::min() || number > std::numeric_limits<int32_t>::max())
                    {
                        throw ArgError("start value '" + entry.key + "' is outside the Int range");
                    }
                    entry.value = static_cast<int32_t>(number);
                }
                else if (value.is_number_float())
                {
                    entry.value = static_cast<float>(value.get<double>());
                }
                else if (value.is_string())
                {
                    entry.value = value.get<std::string>();
                }
                else if (value.is_array())
                {
                    entry.value = ArgReader::toVec3(value, entry.key.c_str());
                }
                else if (value.is_object() && value.contains("entity"))
                {
                    restoreEntityValue(value, entry);
                }
                else
                {
                    throw ArgError("start value '" + entry.key + "' is not a blackboard value");
                }
                out.push_back(std::move(entry));
            }
            return out;
        }

        std::optional<nlohmann::json> writeBehaviorTree(const services::EntityHandle& entity, const nlohmann::json& data,
                                                        Source source)
        {
            refuseWhenBlocked();
            const std::optional<services::BehaviorTreeComponentData> current = bttool::componentData(entity);
            if (!current.has_value())
            {
                return std::nullopt;
            }
            if (!data.is_object())
            {
                throw ArgError("BehaviorTree data must be an object of field -> value");
            }
            for (auto it = data.begin(); it != data.end(); ++it)
            {
                if (it.key() != "tree" && it.key() != "enabled" && it.key() != "blackboard")
                {
                    throw ArgError("unknown BehaviorTree field '" + it.key() + "'; fields: tree, enabled, blackboard "
                                   "(warnings is read-only)");
                }
            }

            services::BehaviorTreeComponentData staged = *current;
            if (const auto it = data.find("tree"); it != data.end())
            {
                staged.treePath = source == Source::Agent ? agentTreePath(*it) : snapshotTreePath(*it);
            }
            if (const auto it = data.find("enabled"); it != data.end())
            {
                if (!it->is_boolean())
                {
                    throw ArgError("field 'enabled' must be a boolean");
                }
                staged.enabled = it->get<bool>();
            }
            if (const auto it = data.find("blackboard"); it != data.end())
            {
                if (source == Source::Agent)
                {
                    mergeStartValues(staged, *it);
                }
                else
                {
                    staged.initialValues = snapshotStartValues(*it);
                }
            }

            events::ai::SetBehaviorTreeComponentDataCommand command;
            command.entity = entity;
            command.data = std::move(staged);
            if (!events::EventDispatcher::instance().execute(command))
            {
                throw std::runtime_error("the engine refused the BehaviorTree data of entity " +
                                         std::to_string(toId(entity)) + " (logs_read shows why)");
            }
            return getBehaviorTree(entity);
        }

        bool addBehaviorTree(const services::EntityHandle& entity)
        {
            refuseWhenBlocked();
            events::scene::AddBehaviorTreeComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        bool removeBehaviorTree(const services::EntityHandle& entity)
        {
            refuseWhenBlocked();
            events::scene::RemoveBehaviorTreeComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }
    }

    ComponentBinding makeBehaviorTreeBinding()
    {
        ComponentBinding binding;
        binding.name = "BehaviorTree";
        binding.fieldHelp =
            "tree (.vfBehaviorTree path relative to the asset folder, \"\" = none), enabled, blackboard {key: value} = "
            "this entity's start values (merged; null removes one; each must fit the type of the tree's key; Entity "
            "values as an entity id or {entity}, reported {entity, name}), warnings (read-only: values that do not fit, "
            "entities that are gone, Entity keys nothing sets). Applied when Play starts; refused in Play mode. "
            "bt_assign and bt_set_blackboard wrap it (vf://docs/bt).";
        binding.add = addBehaviorTree;
        binding.remove = removeBehaviorTree;
        binding.get = getBehaviorTree;
        binding.patch = [](const services::EntityHandle& entity, const nlohmann::json& data)
        {
            return writeBehaviorTree(entity, data, Source::Agent);
        };
        binding.restore = [](const services::EntityHandle& entity, const nlohmann::json& data)
        {
            return writeBehaviorTree(entity, data, Source::Snapshot);
        };
        binding.readOnlyFields = {"warnings"};
        binding.readOnlyHint = "Writable fields: tree, enabled, blackboard.";
        return binding;
    }
}
