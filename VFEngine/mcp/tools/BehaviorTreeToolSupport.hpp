#pragma once

#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"
#include "../protocol/ToolRegistry.hpp"

#include "behaviortree/BehaviorTreeNodeCatalog.hpp"
#include "behaviortree/BehaviorTreeTypes.hpp"
#include "behaviortree/BehaviorTreeValidation.hpp"
#include "behaviortree/ScriptTaskScan.hpp"
#include "data/BehaviorTreeAuthoringData.hpp"
#include "data/BehaviorTreeComponentData.hpp"
#include "data/EntityHandle.hpp"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// VK-1660 behavior-tree tools (BehaviorTreeTools.cpp, BehaviorTreeBlackboardTools.cpp) and the BehaviorTree
// component binding (BehaviorTreeBinding.cpp): the nested tree JSON an agent writes and reads, its strict parse onto
// the engine's node / link graph (ids, tidy layout, catalogue checks with JSON paths), the bt_edit operations, the
// blackboard value JSON and the path forms. Trees are written only through SaveBehaviorTreeAssetCommand (hash guard,
// strict + engine validation, atomic write, hot reload). Every function that dispatches runs on the main thread.
namespace mcp::tools::bttool
{
    namespace bt = behaviortree;

    inline constexpr std::size_t maxSpecErrors = 20;
    inline constexpr std::size_t maxTreeNodes = 1000;
    inline constexpr std::size_t maxTreeDepth = 64;
    inline constexpr std::size_t maxBlackboardKeys = 256;
    inline constexpr std::size_t maxEditOps = 64;
    inline constexpr std::size_t maxMappings = 64;
    inline constexpr float layoutColumn = 220.0f;  // tidy layout: x = leaf order x 220
    inline constexpr float layoutRow = 140.0f;     // y = depth x 140
    inline constexpr const char* treeExtension = ".vfBehaviorTree";

    // ------------------------------------------------------------------
    // Problems
    // ------------------------------------------------------------------

    // Collects up to maxSpecErrors "<path>: <message>" entries (paths like "root.child.children[1].params.key"),
    // so one call reports every problem of a tree at once. Any thread.
    class SpecErrors
    {
    public:
        void add(const std::string& path, const std::string& message)
        {
            if (entries.size() >= maxSpecErrors)
            {
                ++dropped;
                return;
            }
            entries.push_back(path.empty() ? message : path + ": " + message);
        }

        bool empty() const { return entries.empty(); }
        bool full() const { return entries.size() >= maxSpecErrors; }
        const std::vector<std::string>& messages() const { return entries; }

        // ArgError "<what> has N problem(s):\n- ..." when there is any; nothing is written then.
        void throwIfAny(std::string_view what) const
        {
            if (entries.empty())
            {
                return;
            }
            std::string text = std::string(what) + (entries.size() == 1 && dropped == 0
                ? std::string(" has a problem:")
                : " has " + std::to_string(entries.size() + dropped) + " problems:");
            for (const std::string& entry : entries)
            {
                text += "\n- " + entry;
            }
            if (dropped > 0)
            {
                text += "\n- ... and " + std::to_string(dropped) + " more";
            }
            throw ArgError(text);
        }

    private:
        std::vector<std::string> entries;
        std::size_t dropped = 0;
    };

    // ------------------------------------------------------------------
    // Paths
    // ------------------------------------------------------------------

    // A .vfBehaviorTree file the tools read or write.
    struct TreeFile
    {
        std::filesystem::path file;
        std::string enginePath;  // what the authoring events take (absolute, narrow)
        std::string relative;    // the agent's form: relative to the project asset folder ('/'), else the full path
    };

    // A path argument: relative to the project asset folder (ProjectConfig::workingDirectory, like scene_save's
    // "scenes/...") or absolute, inside that folder, with the .vfBehaviorTree extension. An existing file may also
    // be named as the engine spells it (relative to the project folder, "assets/behaviortrees/Guard.vfBehaviorTree").
    // Throws ArgError starting with `label` ("argument 'path'"); std::runtime_error without a project.
    TreeFile resolveTreeArgument(const std::string& text, const std::string& label, bool mustExist);

    // A tree path the engine stored (absolute or project-folder-relative); it may lie outside the asset folder.
    TreeFile treeFileFromEnginePath(const std::string& enginePath);

    // The read tools' {path} | {entity} (the tree of its BehaviorTree component); exactly one must be given.
    TreeFile treeFromArguments(const ArgReader& reader, std::string_view tool);

    // An engine tree path in the agent's form ("" for "").
    std::string reportTreePath(const std::string& enginePath);

    // How a SubTree / DynamicSubTree path is stored: relative to the project folder when the engine resolves that
    // spelling back to `file` (the form the shipped trees use), else the absolute path.
    std::string storedTreePath(const std::filesystem::path& file);

    // Both name the same tree file (BehaviorTreeAsset::canonicalPath, case-insensitive on Windows).
    bool sameTreePath(const std::string& a, const std::string& b);

    struct ScriptTaskFile
    {
        std::string path;  // relative to the scripts root, '/' ("game/ai/MeleeAttack.mt")
        bt::ScriptTaskInfo info;
    };

    // A ScriptTask script argument: a game/**/*.mt file under <asset folder>/scripts whose first @Script class
    // declares tick(float). nullopt with `error` set otherwise.
    std::optional<ScriptTaskFile> resolveScriptTask(const std::string& text, std::string& error);

    // catalog::StrictContext::scriptClass, resolved like the authoring service ("<scripts root>/<path>", else the
    // path as given).
    std::optional<std::string> scriptTaskClass(std::string_view scriptPath);

    // Why the EQS template `name` cannot be used (missing or invalid), or nullopt. Not checked (nullopt) when no
    // EQS service is registered.
    std::optional<std::string> eqsQueryProblem(const std::string& name);

    // ------------------------------------------------------------------
    // Content hashes (hex strings: the client is JavaScript)
    // ------------------------------------------------------------------

    std::string hashHex(uint64_t hash);
    // The optional hash argument `name` (1-16 hex digits, "0x" allowed). ArgError when malformed.
    std::optional<uint64_t> optHash(const ArgReader& reader, const char* name);

    // ------------------------------------------------------------------
    // Blackboard values
    // ------------------------------------------------------------------

    const std::vector<std::string>& blackboardTypeNames();  // Float, Int, Bool, String, Vec3, Entity
    std::optional<bt::BlackboardValueType> blackboardTypeFromName(std::string_view name);  // case-insensitive
    bt::BlackboardValueType valueType(const bt::BlackboardValue& value);
    bt::BlackboardValue defaultValueOf(bt::BlackboardValueType type);

    // {entity: id | null, name} (+ exists:false for an id that names no entity).
    nlohmann::json entityJson(const services::EntityHandle& entity);
    // Float as the shortest round-trip number, Vec3 as [x, y, z], Entity as entityJson.
    nlohmann::json valueJson(const bt::BlackboardValue& value);
    // A per-entity start value as the BehaviorTree binding reports it: an Entity value as {entity: id | null, name}
    // (+ unresolved:true when its stored reference names no entity now), any other as valueJson.
    nlohmann::json startValueJson(const services::BehaviorTreeInitialValueData& value);

    // Agent input for a value of `type`: numbers for Float (any) and Int (integral), [x, y, z] for Vec3, an entity
    // id or {entity: id} naming an existing entity for Entity. nullopt with the problem added to `errors`.
    std::optional<bt::BlackboardValue> parseValue(const nlohmann::json& value, bt::BlackboardValueType type,
                                                  const std::string& path, SpecErrors& errors);
    // parseValue that throws ArgError.
    bt::BlackboardValue requireValue(const nlohmann::json& value, bt::BlackboardValueType type, const std::string& path);

    // KeySpec {name, type, default} (an Entity key's default is null).
    nlohmann::json keySpecJson(const bt::BlackboardKeyDef& key);
    nlohmann::json keySpecsJson(const std::vector<bt::BlackboardKeyDef>& keys);
    std::optional<bt::BlackboardKeyDef> parseKeySpec(const nlohmann::json& spec, const std::string& path,
                                                     SpecErrors& errors);
    const bt::BlackboardKeyDef* findKey(const std::vector<bt::BlackboardKeyDef>& keys, const std::string& name);

    // ------------------------------------------------------------------
    // Graph
    // ------------------------------------------------------------------

    // Children in tick order (sortOrder, then link id); only links to existing nodes.
    std::vector<uint32_t> childIds(const bt::BTGraph& graph, uint32_t parentId);
    std::optional<uint32_t> parentOf(const bt::BTGraph& graph, uint32_t nodeId);
    // 'Chase' (MoveTo, id 7)
    std::string nodeLabel(const bt::BTNode& node);
    // {id, name, type}
    nlohmann::json nodeSummary(const bt::BTNode& node);
    // Every node under the Root in pre-order, as nodeSummary (the Root itself is implicit).
    nlohmann::json nodeList(const bt::BTGraph& graph);
    // Tidy tree from the root: x = leaf order x layoutColumn, y = depth x layoutRow, a parent centred over its
    // children. Applied to the nodes in `needsPosition`, or to every reachable node with `relayout`.
    void layoutTree(bt::BTGraph& graph, const std::unordered_set<uint32_t>& needsPosition, bool relayout);
    // Same tree content: name, keys, root, every node by id and the parent -> child order (link ids, node order
    // and the id counters do not count).
    bool sameTreeContent(const bt::BehaviorTreeData& a, const bt::BehaviorTreeData& b);

    // ------------------------------------------------------------------
    // The tree JSON (vf://docs/bt section 3)
    // ------------------------------------------------------------------

    struct SpecOptions
    {
        std::string selfEnginePath;            // the file being written: a SubTree may not include it
        const bt::BTGraph* previous = nullptr; // bt_set: the file's graph (root id, id counters, positions)
        bool relayout = false;
    };

    struct ParsedTree
    {
        bt::BehaviorTreeData data;
        std::vector<std::string> warnings;
    };

    // TreeSpec {name, blackboard?, root} -> a complete graph: the implicit Root, ids (a given id is kept, the rest
    // assigned in pre-order), links with sortOrder 0..n-1, every optional parameter written, tidy positions for
    // nodes without one. Strict: throws ArgError listing up to maxSpecErrors problems with their JSON paths.
    ParsedTree parseTreeSpec(const nlohmann::json& spec, const SpecOptions& options);

    struct EmittedTree
    {
        nlohmann::json tree;                                       // TreeSpec with ids, no positions
        nlohmann::json legacyParams = nlohmann::json::array();     // stored properties the catalogue does not know
        nlohmann::json unreachable = nlohmann::json::array();      // nodes not under the root (bt_set drops them)
        std::vector<std::string> warnings;
    };

    // Graph -> TreeSpec. parseTreeSpec(emitTree(x).tree) gives back the same tree, minus legacy params.
    EmittedTree emitTree(const bt::BehaviorTreeData& data);

    // ------------------------------------------------------------------
    // Edits
    // ------------------------------------------------------------------

    struct EditOutcome
    {
        bt::BehaviorTreeData data;
        // The nodes the ops created or changed, plus the parents whose child lists they changed: the strict scope
        // (SaveBehaviorTreeAssetCommand::strictNodes). May hold ids the ops removed again.
        std::unordered_set<uint32_t> touched;
        std::vector<std::string> warnings;
    };

    // bt_edit: applies `ops` in order on a copy of `current`, then checks the touched nodes against the catalogue.
    // Throws ArgError naming the op ("ops[2] (move): ...").
    EditOutcome applyEdits(const bt::BehaviorTreeData& current, const nlohmann::json& ops, const SpecOptions& options);

    struct KeyEditOutcome
    {
        bt::BehaviorTreeData data;
        std::unordered_set<uint32_t> strictNodes;  // nodes using a key whose type changed
        std::vector<std::string> warnings;
    };

    // bt_set_blackboard {path}: adds or replaces `keys` (KeySpecs, by name) and removes `removeKeys` (names).
    // Removing a key a node still uses is an error. Either pointer may be null.
    KeyEditOutcome applyKeyEdits(const bt::BehaviorTreeData& current, const nlohmann::json* keys,
                                 const nlohmann::json* removeKeys);

    struct KeyUse
    {
        uint32_t nodeId = 0;
        std::string nodeName;
        std::string nodeType;
        std::string param;  // the KeyRef parameter, or "mappings"
    };

    // Which nodes read or write each key (KeyRef parameters, an unset one at its seed, and DynamicSubTree mappings).
    std::map<std::string, std::vector<KeyUse>> keyUses(const bt::BTGraph& graph);
    nlohmann::json keyUsesJson(const std::vector<KeyUse>& uses);

    // ------------------------------------------------------------------
    // Engine
    // ------------------------------------------------------------------

    // LoadBehaviorTreeAssetQuery (an error result when no authoring service is registered).
    services::BehaviorTreeAssetLoadResult loadTree(const std::string& enginePath);
    // SaveBehaviorTreeAssetCommand. Throws std::runtime_error when no authoring service is registered.
    services::BehaviorTreeAssetSaveResult saveTree(const TreeFile& file, const bt::BehaviorTreeData& data,
                                                   std::optional<uint64_t> expectedHash, bool createNew,
                                                   std::optional<std::unordered_set<uint32_t>> strictNodes);
    // The in-band error for a refused save, with its blocking diagnostics.
    ToolResult saveFailure(const TreeFile& file, const services::BehaviorTreeAssetSaveResult& result);
    // Messages of the Errors (errors = true) or of the Warnings and Infos.
    std::vector<std::string> diagnosticTexts(const std::vector<bt::validation::Diagnostic>& diagnostics, bool errors);

    // StrictContext with scripts, tree paths and EQS names checked against the project.
    bt::catalog::StrictContext strictContext(std::optional<std::unordered_set<uint32_t>> onlyNodes = std::nullopt);
    // {errors, warnings, strict}: validateBehaviorTree findings and the catalogue's strict findings (which would
    // block an edit touching those nodes).
    nlohmann::json validationJson(const bt::BehaviorTreeData& data);

    // IsPlayModeQuery; false when no editor mode service is registered.
    bool playMode();
    // Live runtimes whose tree is `enginePath` (GetAttachedBehaviorTreesQuery; 0 outside Play).
    int liveRuntimesUsing(const std::string& enginePath);
    // [{entity, name, component, enabled?, live}]: BehaviorTree components using the tree, and live runtimes.
    nlohmann::json attachedTo(const std::string& enginePath);

    // The keys a runtime of the tree has (its own, then its SubTrees' - the tree wins a name clash, like the
    // runtime's expansion) and whether any of those trees moves the entity (MoveTo, FollowRoute).
    struct TreeFacts
    {
        bool readable = false;
        std::string error;
        std::vector<bt::BlackboardKeyDef> keys;
        std::vector<std::string> keySources;  // per key: "" = the tree itself, else the SubTree file (agent form)
        bool moves = false;
        std::set<std::string> targetKeys;     // keys a node reads as its target or route (targetKey, routeKey)
        std::set<std::string> writtenKeys;    // keys a node writes (result, distance, visibility, focus, SetBlackboardValue)
    };
    TreeFacts treeFacts(const std::string& enginePath);

    // GetBehaviorTreeComponentDataQuery.
    std::optional<services::BehaviorTreeComponentData> componentData(const services::EntityHandle& entity);

    // ------------------------------------------------------------------
    // Docs (BehaviorTreeDocs.cpp)
    // ------------------------------------------------------------------

    // The worked patrol / chase / attack tree of vf://docs/bt section 10, as the JSON text the docs quote (its
    // ScriptTask runs game/ai/EnemyMeleeAttack.mt). Pure.
    const std::string& workedExampleTree();

    // vf://docs/bt as markdown. Pure (the node and EQS tables are generated from the catalogues).
    std::string buildBehaviorTreeDoc();

    // ------------------------------------------------------------------
    // Registration (BehaviorTreeBlackboardTools.cpp; called by registerBehaviorTreeTools)
    // ------------------------------------------------------------------

    // bt_get_blackboard, bt_set_blackboard, bt_assign, bt_unassign.
    void registerBehaviorTreeBlackboardTools(ToolRegistry& registry, const ToolContext& context);
}
