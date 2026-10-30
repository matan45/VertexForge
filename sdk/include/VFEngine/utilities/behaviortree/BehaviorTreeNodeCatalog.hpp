#pragma once

#include "BehaviorTreeTypes.hpp"
#include "BehaviorTreeValidation.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// VK-1660: the one description of every behavior tree node type and its parameters. It drives the
// strict validator below (used before an MCP / authoring save), the MCP bt_list_node_types reply,
// the vf://docs/bt node table and the BT editor's createNode defaults - so they never disagree.
namespace behaviortree::catalog
{
    enum class NodeCategory : uint8_t
    {
        Root,
        Composite,
        Decorator,
        Service,
        Task
    };

    enum class ParamKind : uint8_t
    {
        Float,      // stored as float; any JSON number is accepted and converted
        Int,        // stored as int32; integral values only
        Bool,
        String,
        Enum,       // a string from enumValues (canonical spelling)
        KeyRef,     // the name of a declared blackboard key whose type is in keyTypes
        TypedValue, // a value whose type follows the key named by valueKeyParam
        TreePath,   // a project .vfBehaviorTree path (SubTree / DynamicSubTree)
        ScriptPath, // ScriptTask script, stored in BTNode::scriptPath (not in properties)
        EqsQuery    // the name of an EQS query template
    };

    struct ParamSpec
    {
        std::string name;
        ParamKind kind = ParamKind::Float;
        // The caller (an MCP agent) must give it explicitly. Optional params take `defaultValue`.
        bool required = false;
        // Seed value: what a new node gets (editor createNode) and what an omitted optional param
        // becomes. Required params still carry a placeholder seed (e.g. "" / 0.0f) for the editor.
        BlackboardValue defaultValue = 0.0f;
        bool hasRange = false;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        std::vector<std::string> enumValues;      // Enum
        std::vector<BlackboardValueType> keyTypes; // KeyRef: allowed key types
        bool allowEmptyKey = false;               // KeyRef: "" means "not used" (optional outputs)
        std::string valueKeyParam;                // TypedValue: the param naming the key
        std::string serviceVariant;               // Service only: applies to this serviceType ("" = all)
        std::string description;
    };

    struct NodeSpec
    {
        BTNodeType type = BTNodeType::Sequence;
        std::string name; // canonical type string (nodeTypeToString)
        NodeCategory category = NodeCategory::Task;
        uint32_t minChildren = 0;
        uint32_t maxChildren = 0; // kUnlimitedChildren for composites
        std::vector<ParamSpec> params;
        std::string description;
    };

    inline constexpr uint32_t kUnlimitedChildren = 0xFFFFFFFFu;

    const char* categoryToString(NodeCategory category);
    const char* paramKindToString(ParamKind kind);

    // Every node type, Root first, in BTNodeType order.
    const std::vector<NodeSpec>& nodes();
    const NodeSpec* find(BTNodeType type);
    // Case-insensitive match on the canonical type string; nullptr when unknown.
    const NodeSpec* findByName(std::string_view name);

    // Service types, canonical spelling: EQSRefresh, LineOfSightRefresh, FocusUpdate, TargetDistance.
    const std::vector<std::string>& serviceTypes();

    // The params that apply to `spec`: for a Service, the common params plus those of `serviceType`
    // (an unknown or empty serviceType yields only the common params).
    std::vector<const ParamSpec*> paramsFor(const NodeSpec& spec, std::string_view serviceType = {});

    // The property map a new node of `type` starts with (every non-ScriptPath param's seed). For a
    // Service, `serviceType` picks the variant ("" = EQSRefresh, the editor's historical default).
    std::unordered_map<std::string, BlackboardValue> defaultProperties(BTNodeType type,
                                                                       std::string_view serviceType = {});

    struct StrictContext
    {
        // Returns the @Script class name declared by a game script that has a `tick(float` method;
        // nullopt when the file is missing or is not a ScriptTask. Unset = the script is not checked.
        std::function<std::optional<std::string>(std::string_view scriptPath)> scriptClass;
        // Unset = tree paths are not checked.
        std::function<bool(std::string_view treePath)> treeExists;
        // Unset = EQS query names are not checked.
        std::function<bool(std::string_view queryName)> eqsQueryExists;
        // When non-empty, per-node checks (parameters and child counts) run only for these node ids:
        // the nodes a call created or changed, plus the parents whose child lists it changed. Untouched
        // nodes of an editor-made tree are left to validateBehaviorTree, so they never block an edit.
        // Blackboard-key checks always run.
        std::unordered_set<uint32_t> onlyNodes;
    };

    // Strict checks on top of validation::validateBehaviorTree (which stays permissive for editor
    // trees). Every finding is an Error; messages name the node ("'Chase' (MoveTo, id 7): ...").
    // Checks:
    //  - each node's type has a spec; its child count is within [minChildren, maxChildren]
    //  - no property outside the spec's params (paramsFor with the node's serviceType)
    //  - required params present; ScriptTask has a non-empty scriptPath
    //  - stored alternative matches the kind (Float -> float, Int -> int32, ...); Float/Int ranges
    //  - Enum values are canonical members of enumValues
    //  - KeyRef names a declared key of an allowed type ("" only when allowEmptyKey)
    //  - TypedValue matches the type of the key its valueKeyParam names; comparisons use a
    //    compareOp valid for that type (numeric operators only on Float/Int)
    //  - ScriptTask scripts / tree paths / EQS names exist when the context can check them, and
    //    scriptClassName equals the class scriptClass() reports
    //  - DynamicSubTree has at least one of selectionKey, injectionTag, defaultTreePath
    //  - EQSRefresh interval >= 0.1
    //  - blackboard keys: non-empty unique names, default matches the type, Entity defaults unset
    std::vector<validation::Diagnostic> validateStrict(const BehaviorTreeData& data, const StrictContext& context = {});
}
