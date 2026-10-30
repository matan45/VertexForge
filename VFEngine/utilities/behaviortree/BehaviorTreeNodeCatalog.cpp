#include "BehaviorTreeNodeCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace behaviortree::catalog
{
    namespace
    {
        using Type = BlackboardValueType;
        using validation::Diagnostic;
        using validation::Severity;

        const std::vector<std::string>& compareOps()
        {
            static const std::vector<std::string> ops{"==", "!=", ">", "<", ">=", "<="};
            return ops;
        }

        // Keys the runtime resolves to a position: an entity, a point, or an Int holding an entity id.
        std::vector<Type> targetKeyTypes()
        {
            return {Type::Entity, Type::Vec3, Type::Int};
        }

        // Keys a BlackboardCondition / CheckBlackboardValue can compare (the runtime cannot compare
        // Vec3 or Entity values).
        std::vector<Type> comparableKeyTypes()
        {
            return {Type::Float, Type::Int, Type::Bool, Type::String};
        }

        // ---- ParamSpec builders ----

        ParamSpec floatParam(std::string name, float seed, float minValue, float maxValue, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::Float;
            param.defaultValue = seed;
            param.hasRange = true;
            param.minValue = minValue;
            param.maxValue = maxValue;
            param.description = std::move(description);
            return param;
        }

        ParamSpec intParam(std::string name, int32_t seed, int32_t minValue, int32_t maxValue, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::Int;
            param.defaultValue = seed;
            param.hasRange = true;
            param.minValue = static_cast<float>(minValue);
            param.maxValue = static_cast<float>(maxValue);
            param.description = std::move(description);
            return param;
        }

        ParamSpec boolParam(std::string name, bool seed, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::Bool;
            param.defaultValue = seed;
            param.description = std::move(description);
            return param;
        }

        ParamSpec stringParam(std::string name, std::string seed, bool required, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::String;
            param.required = required;
            param.defaultValue = std::move(seed);
            param.description = std::move(description);
            return param;
        }

        ParamSpec enumParam(std::string name, std::vector<std::string> values, std::string seed, bool required,
                            std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::Enum;
            param.required = required;
            param.defaultValue = std::move(seed);
            param.enumValues = std::move(values);
            param.description = std::move(description);
            return param;
        }

        ParamSpec keyParam(std::string name, std::vector<Type> types, std::string seed, bool required, bool allowEmpty,
                           std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::KeyRef;
            param.required = required;
            param.defaultValue = std::move(seed);
            param.keyTypes = std::move(types);
            param.allowEmptyKey = allowEmpty;
            param.description = std::move(description);
            return param;
        }

        ParamSpec valueParam(std::string name, std::string keyParamName, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::TypedValue;
            param.required = true;
            param.defaultValue = 0.0f;
            param.valueKeyParam = std::move(keyParamName);
            param.description = std::move(description);
            return param;
        }

        ParamSpec treePathParam(std::string name, bool required, std::string description)
        {
            ParamSpec param;
            param.name = std::move(name);
            param.kind = ParamKind::TreePath;
            param.required = required;
            param.defaultValue = std::string();
            param.description = std::move(description);
            return param;
        }

        ParamSpec scriptParam(std::string description)
        {
            ParamSpec param;
            param.name = "script";
            param.kind = ParamKind::ScriptPath;
            param.required = true;
            param.defaultValue = std::string();
            param.description = std::move(description);
            return param;
        }

        ParamSpec eqsQueryParam(std::string description)
        {
            ParamSpec param;
            param.name = "queryName";
            param.kind = ParamKind::EqsQuery;
            param.required = true;
            param.defaultValue = std::string();
            param.description = std::move(description);
            return param;
        }

        ParamSpec forService(ParamSpec param, std::string serviceType)
        {
            param.serviceVariant = std::move(serviceType);
            return param;
        }

        NodeSpec makeSpec(BTNodeType type, NodeCategory category, uint32_t minChildren, uint32_t maxChildren,
                          std::string description, std::vector<ParamSpec> params = {})
        {
            NodeSpec spec;
            spec.type = type;
            spec.name = nodeTypeToString(type);
            spec.category = category;
            spec.minChildren = minChildren;
            spec.maxChildren = maxChildren;
            spec.params = std::move(params);
            spec.description = std::move(description);
            return spec;
        }

        // ---- Shared parameter texts ----

        constexpr const char* kTargetKeyHelp =
            "Target: an Entity key, a Vec3 key, or an Int key holding an entity id.";
        constexpr const char* kCompareOpHelp =
            "Comparison: == != > < >= <= (the numeric operators need a Float or Int key).";

        std::vector<NodeSpec> buildCatalog()
        {
            std::vector<NodeSpec> specs;

            // Order follows BTNodeType so nodes()[index] == the spec of BTNodeType(index).

            specs.push_back(makeSpec(BTNodeType::Root, NodeCategory::Root, 1, 1,
                "Entry point: ticks its single child every frame and restarts it when it finishes. Implicit in "
                "the MCP tree format."));

            specs.push_back(makeSpec(BTNodeType::Sequence, NodeCategory::Composite, 1, kUnlimitedChildren,
                "Runs its children in order: fails as soon as one fails, succeeds when all succeed. A running "
                "child is resumed on the next tick."));

            specs.push_back(makeSpec(BTNodeType::Selector, NodeCategory::Composite, 1, kUnlimitedChildren,
                "Runs its children in order until one succeeds; fails only when all fail. Every tick it "
                "re-checks earlier BlackboardCondition children with abortMode LowerPriority or Both and "
                "switches to the first one that has become true (observer abort)."));

            specs.push_back(makeSpec(BTNodeType::Parallel, NodeCategory::Composite, 1, kUnlimitedChildren,
                "Ticks every child each tick and finishes according to policy.",
                {
                    enumParam("policy", {"RequireAll", "RequireOne"}, "RequireAll", false,
                        "RequireAll: fail on the first failure, succeed when all children succeed. "
                        "RequireOne: succeed on the first success, fail when all children fail.")
                }));

            specs.push_back(makeSpec(BTNodeType::Inverter, NodeCategory::Decorator, 1, 1,
                "Inverts its child's result (Success <-> Failure); Running passes through."));

            specs.push_back(makeSpec(BTNodeType::Repeater, NodeCategory::Decorator, 1, 1,
                "Runs its child repeatCount times, whatever each run returns, then reports the last result.",
                {
                    intParam("repeatCount", 3, 0, 1000, "How many times to run the child; 0 = forever.")
                }));

            specs.push_back(makeSpec(BTNodeType::Succeeder, NodeCategory::Decorator, 1, 1,
                "Runs its child and reports Success once it finishes, whatever the child returned."));

            specs.push_back(makeSpec(BTNodeType::RepeatUntilFail, NodeCategory::Decorator, 1, 1,
                "Re-runs its child until the child fails, then reports Success (Running until then)."));

            specs.push_back(makeSpec(BTNodeType::Cooldown, NodeCategory::Decorator, 1, 1,
                "Runs its child; after the child finishes, this node fails for cooldownTime seconds.",
                {
                    floatParam("cooldownTime", 1.0f, 0.0f, 60.0f,
                        "Seconds after the child finishes during which this node fails without running it.")
                }));

            specs.push_back(makeSpec(BTNodeType::TimeLimit, NodeCategory::Decorator, 1, 1,
                "Runs its child and fails once the child has been running for timeLimit seconds.",
                {
                    floatParam("timeLimit", 5.0f, 0.0f, 300.0f, "Seconds the child may run before this node fails.")
                }));

            specs.push_back(makeSpec(BTNodeType::BlackboardCondition, NodeCategory::Decorator, 1, 1,
                "Runs its child only while blackboard[key] compareOp compareValue holds, else fails. abortMode "
                "Self aborts the running child when the condition turns false; LowerPriority lets a parent "
                "Selector switch to this branch when it turns true; Both does both.",
                {
                    keyParam("key", comparableKeyTypes(), std::string(), true, false,
                        "Blackboard key to test (Float, Int, Bool or String)."),
                    enumParam("compareOp", compareOps(), "==", false, kCompareOpHelp),
                    valueParam("compareValue", "key", "Value to compare against; the same type as the key."),
                    enumParam("abortMode", {"None", "Self", "LowerPriority", "Both"}, "None", false,
                        "None, Self (abort the running child when the condition turns false), LowerPriority (a "
                        "parent Selector preempts lower branches when it turns true) or Both.")
                }));

            specs.push_back(makeSpec(BTNodeType::Wait, NodeCategory::Task, 0, 0,
                "Waits duration seconds, then succeeds.",
                {
                    floatParam("duration", 1.0f, 0.0f, 60.0f, "Seconds to wait.")
                }));

            specs.push_back(makeSpec(BTNodeType::Log, NodeCategory::Task, 0, 0,
                "Writes message to the engine log at level, then succeeds.",
                {
                    stringParam("message", "Hello", false, "Text to log."),
                    enumParam("level", {"Info", "Warn", "Error"}, "Info", false, "Info, Warn or Error.")
                }));

            specs.push_back(makeSpec(BTNodeType::MoveTo, NodeCategory::Task, 0, 0,
                "Moves the entity's NavmeshAgent (or VolumetricAgent) to the target - a point, or an entity it "
                "follows, re-pathing when the target moves more than repathDistance - and succeeds within "
                "arrivalDistance.",
                {
                    keyParam("targetKey", {Type::Vec3, Type::Entity, Type::Int}, "target", false, false,
                        kTargetKeyHelp),
                    floatParam("arrivalDistance", 0.5f, 0.0f, 100.0f,
                        "Succeeds within this distance of the target (m; XZ, or 3D for volumetric agents)."),
                    floatParam("repathDistance", 1.0f, 0.0f, 100.0f,
                        "Re-issue the destination when the target has moved farther than this (m); 0 = never."),
                    boolParam("stopOnArrival", true,
                        "Stop the agent on arrival instead of letting it walk on to the exact point.")
                }));

            specs.push_back(makeSpec(BTNodeType::PlayAnimation, NodeCategory::Task, 0, 0,
                "Forces the entity's animator into stateName; succeeds at once, or when the state finishes with "
                "waitForCompletion.",
                {
                    stringParam("stateName", std::string(), true, "Animator state to transition to."),
                    boolParam("waitForCompletion", false,
                        "Keep running until the state's normalized time reaches 1.")
                }));

            specs.push_back(makeSpec(BTNodeType::SetBlackboardValue, NodeCategory::Task, 0, 0,
                "Writes value to blackboard[key], then succeeds.",
                {
                    keyParam("key", {Type::Float, Type::Int, Type::Bool, Type::String, Type::Vec3}, std::string(),
                        true, false, "Blackboard key to write (Entity keys are set per entity, not by a node)."),
                    valueParam("value", "key", "Value to write; the same type as the key.")
                }));

            specs.push_back(makeSpec(BTNodeType::CheckBlackboardValue, NodeCategory::Task, 0, 0,
                "Succeeds when blackboard[key] compareOp compareValue holds, else fails.",
                {
                    keyParam("key", comparableKeyTypes(), std::string(), true, false,
                        "Blackboard key to test (Float, Int, Bool or String)."),
                    enumParam("compareOp", compareOps(), "==", false, kCompareOpHelp),
                    valueParam("compareValue", "key", "Value to compare against; the same type as the key.")
                }));

            specs.push_back(makeSpec(BTNodeType::ScriptTask, NodeCategory::Task, 0, 0,
                "Runs an @Script mType class through tick(float deltaTime): string - \"success\", \"running\", "
                "anything else = failure - plus optional onAbort() and onEnd(bool success). onStart runs once; "
                "onUpdate is never called. One instance per entity and script.",
                {
                    scriptParam("Script path relative to the scripts root, e.g. game/ai/MeleeAttack.mt.")
                }));

            specs.push_back(makeSpec(BTNodeType::EnvironmentQuery, NodeCategory::Task, 0, 0,
                "Runs the EQS query template queryName (around targetKey when set) and writes the best position "
                "to resultKey and its entity to resultEntityKey; fails when the query finds nothing.",
                {
                    eqsQueryParam("EQS query template name (assets/eqs/<Name>.vfEQSQuery)."),
                    keyParam("resultKey", {Type::Vec3}, "eqsResult", false, false,
                        "Vec3 key that receives the best position."),
                    keyParam("targetKey", targetKeyTypes(), std::string(), false, true,
                        "Optional target the query is built around (Entity, Vec3 or Int entity-id key); \"\" = none."),
                    keyParam("resultEntityKey", {Type::Entity, Type::Int}, std::string(), false, true,
                        "Optional Entity or Int key that receives the best result's entity (cleared when there is none).")
                }));

            specs.push_back(makeSpec(BTNodeType::LineOfSight, NodeCategory::Task, 0, 0,
                "Succeeds when the target is within maxDistance and an eye-height ray to it is not blocked.",
                {
                    keyParam("targetKey", targetKeyTypes(), "target", false, false, kTargetKeyHelp),
                    floatParam("maxDistance", 50.0f, 0.0f, 500.0f, "Farther than this (m) counts as not visible."),
                    floatParam("eyeOffset", 1.6f, 0.0f, 10.0f, "Eye height (m) added at both ends of the ray.")
                }));

            specs.push_back(makeSpec(BTNodeType::SubTree, NodeCategory::Task, 0, 0,
                "Inlines the tree at treePath when the asset loads; its blackboard keys merge into this tree's "
                "(this tree wins on a name clash).",
                {
                    treePathParam("treePath", true, "Project path of the .vfBehaviorTree to inline.")
                }));

            specs.push_back(makeSpec(BTNodeType::Service, NodeCategory::Service, 1, 1,
                "Runs its single child and, every interval seconds while that branch is active, a perception job "
                "that writes blackboard keys: EQSRefresh (EQS query), LineOfSightRefresh (visibility), "
                "FocusUpdate (target position) or TargetDistance (owner-to-target distance).",
                {
                    enumParam("serviceType", serviceTypes(), "EQSRefresh", true,
                        "EQSRefresh, LineOfSightRefresh, FocusUpdate or TargetDistance."),
                    floatParam("interval", 0.5f, 0.01f, 60.0f, "Seconds between runs while the branch is active."),
                    floatParam("randomDeviation", 0.0f, 0.0f, 60.0f,
                        "Random +/- seconds added to each interval (keep it below interval)."),
                    boolParam("runOnActivation", false, "Also run once as soon as the branch becomes active."),

                    forService(eqsQueryParam("EQS query template name (assets/eqs/<Name>.vfEQSQuery)."),
                        "EQSRefresh"),
                    forService(keyParam("resultKey", {Type::Vec3}, "eqsResult", false, false,
                        "Vec3 key that receives the best position."), "EQSRefresh"),
                    forService(keyParam("targetKey", targetKeyTypes(), std::string(), false, true,
                        "Optional target the query is built around (Entity, Vec3 or Int entity-id key); \"\" = none."),
                        "EQSRefresh"),
                    forService(keyParam("resultEntityKey", {Type::Entity, Type::Int}, std::string(), false, true,
                        "Optional Entity or Int key that receives the best result's entity (cleared when there is none)."),
                        "EQSRefresh"),

                    forService(keyParam("targetKey", targetKeyTypes(), "target", false, false, kTargetKeyHelp),
                        "LineOfSightRefresh"),
                    forService(keyParam("visibilityKey", {Type::Bool}, "targetVisible", false, false,
                        "Bool key set to whether the target is visible."), "LineOfSightRefresh"),
                    forService(floatParam("maxDistance", 50.0f, 0.0f, 500.0f,
                        "Farther than this (m) counts as not visible."), "LineOfSightRefresh"),
                    forService(floatParam("eyeOffset", 1.6f, 0.0f, 10.0f,
                        "Eye height (m) added at both ends of the ray."), "LineOfSightRefresh"),

                    forService(keyParam("targetKey", targetKeyTypes(), "target", false, false, kTargetKeyHelp),
                        "FocusUpdate"),
                    forService(keyParam("focusKey", {Type::Vec3}, "focusPoint", false, false,
                        "Vec3 key that receives the target's position."), "FocusUpdate"),

                    forService(keyParam("targetKey", targetKeyTypes(), "target", false, false, kTargetKeyHelp),
                        "TargetDistance"),
                    forService(keyParam("distanceKey", {Type::Float}, "targetDistance", false, false,
                        "Float key that receives the owner-to-target distance (1e9 when the target is missing)."),
                        "TargetDistance"),
                    forService(boolParam("horizontal", true,
                        "Measure on the XZ plane, ignoring height; false = 3D (flying agents)."), "TargetDistance")
                }));

            specs.push_back(makeSpec(BTNodeType::DynamicSubTree, NodeCategory::Task, 0, 0,
                "Runs another tree chosen at runtime - blackboard[selectionKey], else the tree bound to "
                "injectionTag, else defaultTreePath - with its own blackboard; mappings copy keys in and out.",
                {
                    keyParam("selectionKey", {Type::String}, std::string(), false, true,
                        "Optional String key holding the tree path to run (wins over injectionTag and defaultTreePath)."),
                    stringParam("injectionTag", std::string(), false,
                        "Optional tag a script binds a tree to (Blackboard::setDynamicSubtree)."),
                    treePathParam("defaultTreePath", false, "Tree to run when neither the key nor the tag names one.")
                }));

            specs.push_back(makeSpec(BTNodeType::FollowRoute, NodeCategory::Task, 0, 0,
                "Patrols a route: walks the ordered child markers of the route entity in routeKey with the "
                "entity's NavmeshAgent (or VolumetricAgent), dwelling at each marker. Loop and PingPong run "
                "until aborted; Once succeeds at the last marker. Fails when a leg exceeds legTimeout.",
                {
                    keyParam("routeKey", {Type::Entity, Type::Int}, "route", false, false,
                        "Entity (or Int entity-id) key holding the route entity (NavRouteComponent) whose ordered "
                        "children are the markers."),
                    enumParam("mode", {"Loop", "PingPong", "Once"}, "Loop", false,
                        "Loop (back to the first marker), PingPong (reverse at either end) or Once (succeed at the "
                        "last marker)."),
                    floatParam("arrivalDistance", 1.0f, 0.1f, 100.0f, "A marker counts as reached within this distance (m)."),
                    floatParam("dwell", 0.0f, 0.0f, 600.0f, "Seconds to wait at each marker."),
                    boolParam("startAtNearest", true, "Start at the nearest marker instead of the first."),
                    floatParam("legTimeout", 30.0f, 0.0f, 600.0f,
                        "Fail when one leg takes longer than this (seconds); 0 = never.")
                }));

            return specs;
        }

        std::string lowerAscii(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        std::string describeNode(const BTNode& node)
        {
            const std::string typeString = nodeTypeToString(node.type);
            const std::string& label = node.name.empty() ? typeString : node.name;
            return "'" + label + "' (" + typeString + ", id " + std::to_string(node.id) + ")";
        }

        std::string typeName(Type type)
        {
            return blackboardValueTypeToString(type);
        }

        Type storedType(const BlackboardValue& value)
        {
            // BlackboardValue alternatives follow BlackboardValueType (static_asserts in BehaviorTreeValidation.hpp).
            return static_cast<Type>(value.index());
        }

        std::string joinTypes(const std::vector<Type>& types)
        {
            std::string out;
            for (size_t i = 0; i < types.size(); ++i)
            {
                if (i > 0) out += types.size() == 2 ? " or " : (i + 1 == types.size() ? " or " : ", ");
                out += typeName(types[i]);
            }
            return out;
        }

        std::string joinValues(const std::vector<std::string>& values)
        {
            std::string out;
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0) out += ", ";
                out += values[i];
            }
            return out;
        }

        std::string formatNumber(float value)
        {
            std::string text = std::to_string(value);
            // std::to_string(float) prints 6 decimals; trim the trailing zeros for readable messages.
            if (text.find('.') != std::string::npos)
            {
                while (!text.empty() && text.back() == '0') text.pop_back();
                if (!text.empty() && text.back() == '.') text.pop_back();
            }
            return text;
        }

        const BlackboardKeyDef* findKey(const BTGraph& graph, const std::string& name)
        {
            for (const auto& key : graph.blackboardKeys)
            {
                if (key.name == name) return &key;
            }
            return nullptr;
        }

        uint32_t countChildren(const BTGraph& graph, uint32_t nodeId)
        {
            uint32_t count = 0;
            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId == nodeId && graph.findNodeById(link.targetNodeId) != nullptr) ++count;
            }
            return count;
        }

        std::string childRuleText(const NodeSpec& spec)
        {
            if (spec.maxChildren == 0) return "cannot have children";
            if (spec.minChildren == spec.maxChildren)
            {
                return "needs exactly " + std::to_string(spec.minChildren) +
                       (spec.minChildren == 1 ? " child" : " children");
            }
            if (spec.maxChildren == kUnlimitedChildren)
            {
                return "needs at least " + std::to_string(spec.minChildren) +
                       (spec.minChildren == 1 ? " child" : " children");
            }
            return "needs " + std::to_string(spec.minChildren) + " to " + std::to_string(spec.maxChildren) + " children";
        }

        class StrictChecker
        {
        public:
            StrictChecker(const BehaviorTreeData& data, const StrictContext& strictContext,
                          std::vector<Diagnostic>& diagnostics)
                : graph(data.graph), context(strictContext), out(diagnostics)
            {
            }

            void checkBlackboardKeys()
            {
                std::unordered_set<std::string> names;
                for (const auto& key : graph.blackboardKeys)
                {
                    if (key.name.empty())
                    {
                        graphError("Blackboard key with an empty name.");
                    }
                    else if (!names.insert(key.name).second)
                    {
                        graphError("Duplicate blackboard key '" + key.name + "'.");
                    }

                    const Type stored = storedType(key.defaultValue);
                    if (stored != key.type)
                    {
                        graphError("Blackboard key '" + key.name + "': its default is " + typeName(stored) +
                                   " but the key is " + typeName(key.type) + ".");
                    }
                    else if (key.type == Type::Entity && std::get<services::EntityHandle>(key.defaultValue).isValid())
                    {
                        graphError("Blackboard key '" + key.name + "': an Entity key cannot have a default (entity "
                                   "ids change on every load); set it per entity.");
                    }
                }
            }

            void checkStructure(const BTNode& node)
            {
                const NodeSpec* spec = find(node.type);
                if (!spec)
                {
                    nodeError(node, "unknown node type.");
                    return;
                }

                const uint32_t children = countChildren(graph, node.id);
                if (children < spec->minChildren || children > spec->maxChildren)
                {
                    nodeError(node, childRuleText(*spec) + " (has " + std::to_string(children) + ").");
                }
            }

            void checkNode(const BTNode& node)
            {
                const NodeSpec* spec = find(node.type);
                if (!spec) return; // reported by checkStructure

                std::string serviceType;
                if (node.type == BTNodeType::Service)
                {
                    auto it = node.properties.find("serviceType");
                    if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                    {
                        serviceType = std::get<std::string>(it->second);
                    }
                }
                const std::vector<const ParamSpec*> params = paramsFor(*spec, serviceType);

                for (const auto& property : node.properties)
                {
                    const std::string& name = property.first;
                    const bool known = std::any_of(params.begin(), params.end(), [&name](const ParamSpec* param)
                    {
                        return param->name == name && param->kind != ParamKind::ScriptPath;
                    });
                    if (!known)
                    {
                        std::string problem = "unknown parameter '" + name + "'";
                        if (node.type == BTNodeType::Service && !serviceType.empty())
                        {
                            problem += " for serviceType " + serviceType;
                        }
                        nodeError(node, problem + ".");
                    }
                }

                for (const ParamSpec* param : params)
                {
                    if (param->kind == ParamKind::ScriptPath)
                    {
                        checkScript(node);
                        continue;
                    }

                    auto it = node.properties.find(param->name);
                    if (it == node.properties.end())
                    {
                        if (param->required)
                        {
                            nodeError(node, "missing required parameter '" + param->name + "'.");
                        }
                        continue;
                    }
                    checkValue(node, *param, it->second);
                }

                if (node.type == BTNodeType::DynamicSubTree)
                {
                    const bool hasSelection = !stringProperty(node, "selectionKey").empty();
                    const bool hasTag = !stringProperty(node, "injectionTag").empty();
                    const bool hasDefault = !stringProperty(node, "defaultTreePath").empty();
                    if (!hasSelection && !hasTag && !hasDefault)
                    {
                        nodeError(node, "needs at least one of selectionKey, injectionTag or defaultTreePath.");
                    }
                }

                if (node.type == BTNodeType::Service && serviceType == "EQSRefresh")
                {
                    auto it = node.properties.find("interval");
                    if (it != node.properties.end() && std::holds_alternative<float>(it->second) &&
                        std::get<float>(it->second) < 0.1f)
                    {
                        nodeError(node, "an EQSRefresh service needs interval >= 0.1 s (each run is a full query).");
                    }
                }
            }

        private:
            const BTGraph& graph;
            const StrictContext& context;
            std::vector<Diagnostic>& out;

            void graphError(std::string message)
            {
                out.push_back(Diagnostic{Severity::Error, 0, std::move(message)});
            }

            void nodeError(const BTNode& node, const std::string& problem)
            {
                out.push_back(Diagnostic{Severity::Error, node.id, describeNode(node) + ": " + problem});
            }

            static std::string stringProperty(const BTNode& node, const std::string& name)
            {
                auto it = node.properties.find(name);
                if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                {
                    return std::get<std::string>(it->second);
                }
                return {};
            }

            void checkScript(const BTNode& node)
            {
                if (node.scriptPath.empty())
                {
                    nodeError(node, "missing required parameter 'script'.");
                    return;
                }
                // The adapter fails a ScriptTask whose scriptClassName is empty (its loader ignores the name).
                if (!context.scriptClass)
                {
                    if (node.scriptClassName.empty())
                        nodeError(node, "missing scriptClassName: it must name the script's @Script class.");
                    return;
                }

                const std::optional<std::string> scriptClass = context.scriptClass(node.scriptPath);
                if (!scriptClass)
                {
                    nodeError(node, "script '" + node.scriptPath +
                                    "' is not a ScriptTask: needs an @Script class with tick(float): string.");
                    return;
                }
                if (node.scriptClassName.empty())
                {
                    nodeError(node, "missing scriptClassName: it must be '" + *scriptClass +
                                    "', the class declared in '" + node.scriptPath + "'.");
                }
                else if (node.scriptClassName != *scriptClass)
                {
                    nodeError(node, "scriptClassName '" + node.scriptClassName + "' does not match the class '" +
                                    *scriptClass + "' declared in '" + node.scriptPath + "'.");
                }
            }

            bool requireAlternative(const BTNode& node, const ParamSpec& param, const BlackboardValue& value,
                                    Type expected)
            {
                const Type stored = storedType(value);
                if (stored == expected) return true;
                nodeError(node, "parameter '" + param.name + "' must be " + typeName(expected) + " (it is " +
                                typeName(stored) + ").");
                return false;
            }

            void checkRange(const BTNode& node, const ParamSpec& param, float value)
            {
                if (!std::isfinite(value))
                {
                    nodeError(node, "parameter '" + param.name + "' must be a finite number.");
                    return;
                }
                if (param.hasRange && (value < param.minValue || value > param.maxValue))
                {
                    nodeError(node, "parameter '" + param.name + "' = " + formatNumber(value) + " is outside " +
                                    formatNumber(param.minValue) + ".." + formatNumber(param.maxValue) + ".");
                }
            }

            void checkValue(const BTNode& node, const ParamSpec& param, const BlackboardValue& value)
            {
                switch (param.kind)
                {
                case ParamKind::Float:
                    if (requireAlternative(node, param, value, Type::Float))
                    {
                        checkRange(node, param, std::get<float>(value));
                    }
                    break;

                case ParamKind::Int:
                    if (requireAlternative(node, param, value, Type::Int))
                    {
                        checkRange(node, param, static_cast<float>(std::get<int32_t>(value)));
                    }
                    break;

                case ParamKind::Bool:
                    requireAlternative(node, param, value, Type::Bool);
                    break;

                case ParamKind::String:
                    if (requireAlternative(node, param, value, Type::String) && param.required &&
                        std::get<std::string>(value).empty())
                    {
                        nodeError(node, "parameter '" + param.name + "' must not be empty.");
                    }
                    break;

                case ParamKind::Enum:
                    if (requireAlternative(node, param, value, Type::String))
                    {
                        const std::string& text = std::get<std::string>(value);
                        if (std::find(param.enumValues.begin(), param.enumValues.end(), text) == param.enumValues.end())
                        {
                            nodeError(node, "parameter '" + param.name + "' = '" + text + "' must be one of " +
                                            joinValues(param.enumValues) + ".");
                        }
                    }
                    break;

                case ParamKind::KeyRef:
                    if (requireAlternative(node, param, value, Type::String))
                    {
                        checkKeyRef(node, param, std::get<std::string>(value));
                    }
                    break;

                case ParamKind::TypedValue:
                    checkTypedValue(node, param, value);
                    break;

                case ParamKind::TreePath:
                    if (requireAlternative(node, param, value, Type::String))
                    {
                        const std::string& path = std::get<std::string>(value);
                        if (path.empty())
                        {
                            if (param.required)
                            {
                                nodeError(node, "parameter '" + param.name + "' must not be empty.");
                            }
                        }
                        else if (context.treeExists && !context.treeExists(path))
                        {
                            nodeError(node, "tree '" + path + "' (" + param.name + ") does not exist.");
                        }
                    }
                    break;

                case ParamKind::EqsQuery:
                    if (requireAlternative(node, param, value, Type::String))
                    {
                        const std::string& queryName = std::get<std::string>(value);
                        if (queryName.empty())
                        {
                            if (param.required)
                            {
                                nodeError(node, "parameter '" + param.name + "' must name an EQS query.");
                            }
                        }
                        else if (context.eqsQueryExists && !context.eqsQueryExists(queryName))
                        {
                            nodeError(node, "EQS query '" + queryName + "' does not exist (assets/eqs/" + queryName +
                                            ".vfEQSQuery).");
                        }
                    }
                    break;

                case ParamKind::ScriptPath:
                    break; // handled by checkScript
                }
            }

            void checkKeyRef(const BTNode& node, const ParamSpec& param, const std::string& keyName)
            {
                if (keyName.empty())
                {
                    if (!param.allowEmptyKey)
                    {
                        nodeError(node, "parameter '" + param.name + "' must name a blackboard key (" +
                                        joinTypes(param.keyTypes) + ").");
                    }
                    return;
                }

                const BlackboardKeyDef* key = findKey(graph, keyName);
                if (!key)
                {
                    nodeError(node, "blackboard key '" + keyName + "' (" + param.name + ") is not declared.");
                    return;
                }
                if (std::find(param.keyTypes.begin(), param.keyTypes.end(), key->type) == param.keyTypes.end())
                {
                    nodeError(node, "blackboard key '" + keyName + "' is " + typeName(key->type) + "; " + param.name +
                                    " needs " + joinTypes(param.keyTypes) + ".");
                }
            }

            void checkTypedValue(const BTNode& node, const ParamSpec& param, const BlackboardValue& value)
            {
                const std::string keyName = stringProperty(node, param.valueKeyParam);
                const BlackboardKeyDef* key = keyName.empty() ? nullptr : findKey(graph, keyName);
                if (!key) return; // the KeyRef check reports the missing / undeclared key

                const Type stored = storedType(value);
                if (stored != key->type)
                {
                    nodeError(node, "parameter '" + param.name + "' is " + typeName(stored) +
                                    " but does not match key '" + keyName + "' (" + typeName(key->type) + ").");
                    return;
                }

                if (param.name == "compareValue")
                {
                    std::string op = stringProperty(node, "compareOp");
                    if (op.empty()) op = "==";
                    const bool numericOp = op == ">" || op == "<" || op == ">=" || op == "<=";
                    if (numericOp && key->type != Type::Float && key->type != Type::Int)
                    {
                        nodeError(node, "compareOp '" + op + "' is numeric and needs a Float or Int key (key '" +
                                        keyName + "' is " + typeName(key->type) + ").");
                    }
                }
            }
        };
    }

    const char* categoryToString(NodeCategory category)
    {
        switch (category)
        {
        case NodeCategory::Root: return "root";
        case NodeCategory::Composite: return "composite";
        case NodeCategory::Decorator: return "decorator";
        case NodeCategory::Service: return "service";
        case NodeCategory::Task: return "task";
        default: return "task";
        }
    }

    const char* paramKindToString(ParamKind kind)
    {
        switch (kind)
        {
        case ParamKind::Float: return "float";
        case ParamKind::Int: return "int";
        case ParamKind::Bool: return "bool";
        case ParamKind::String: return "string";
        case ParamKind::Enum: return "enum";
        case ParamKind::KeyRef: return "key";
        case ParamKind::TypedValue: return "value";
        case ParamKind::TreePath: return "treePath";
        case ParamKind::ScriptPath: return "script";
        case ParamKind::EqsQuery: return "eqsQuery";
        default: return "string";
        }
    }

    const std::vector<NodeSpec>& nodes()
    {
        static const std::vector<NodeSpec> specs = buildCatalog();
        return specs;
    }

    const NodeSpec* find(BTNodeType type)
    {
        const auto& specs = nodes();
        const auto index = static_cast<size_t>(type);
        if (index < specs.size() && specs[index].type == type)
        {
            return &specs[index];
        }
        for (const auto& spec : specs)
        {
            if (spec.type == type) return &spec;
        }
        return nullptr;
    }

    const NodeSpec* findByName(std::string_view name)
    {
        const std::string wanted = lowerAscii(name);
        for (const auto& spec : nodes())
        {
            if (lowerAscii(spec.name) == wanted) return &spec;
        }
        return nullptr;
    }

    const std::vector<std::string>& serviceTypes()
    {
        static const std::vector<std::string> types{"EQSRefresh", "LineOfSightRefresh", "FocusUpdate", "TargetDistance"};
        return types;
    }

    std::vector<const ParamSpec*> paramsFor(const NodeSpec& spec, std::string_view serviceType)
    {
        std::vector<const ParamSpec*> result;
        result.reserve(spec.params.size());
        for (const auto& param : spec.params)
        {
            if (param.serviceVariant.empty() || param.serviceVariant == serviceType)
            {
                result.push_back(&param);
            }
        }
        return result;
    }

    std::unordered_map<std::string, BlackboardValue> defaultProperties(BTNodeType type, std::string_view serviceType)
    {
        std::unordered_map<std::string, BlackboardValue> properties;
        const NodeSpec* spec = find(type);
        if (!spec) return properties;

        std::string variant;
        if (type == BTNodeType::Service)
        {
            variant = serviceType.empty() ? std::string("EQSRefresh") : std::string(serviceType);
        }

        for (const ParamSpec* param : paramsFor(*spec, variant))
        {
            if (param->kind == ParamKind::ScriptPath) continue;
            properties[param->name] = param->defaultValue;
        }
        if (type == BTNodeType::Service)
        {
            properties["serviceType"] = variant;
        }
        return properties;
    }

    std::vector<validation::Diagnostic> validateStrict(const BehaviorTreeData& data, const StrictContext& context)
    {
        std::vector<Diagnostic> diagnostics;
        StrictChecker checker(data, context, diagnostics);

        // Blackboard-key checks always run; structure and parameters only for the nodes in onlyNodes
        // (every node when it is unset), so untouched editor nodes never block an edit.
        checker.checkBlackboardKeys();
        for (const auto& node : data.graph.nodes)
        {
            if (context.onlyNodes && !context.onlyNodes->contains(node.id)) continue;
            checker.checkStructure(node);
            checker.checkNode(node);
        }
        return diagnostics;
    }
}
