#include "CoreTools.hpp"
#include "BehaviorTreeToolSupport.hpp"
#include "SettingsTable.hpp"

#include "behaviortree/BehaviorTreeNodeCatalog.hpp"
#include "eqs/EQSQuerySpec.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// VK-1660: vf://docs/bt - how behavior trees run, the tree JSON the bt_* tools exchange, the node catalogue and the
// EQS catalogue (both generated, so they never disagree with the strict checks), the reactivity and movement
// patterns, the worked patrol / chase / attack example and the limits. Static apart from the generated tables; Worker
// affinity (it dispatches nothing).
namespace mcp::tools
{
    namespace
    {
        namespace bt = behaviortree;
        using bt::catalog::NodeCategory;
        using bt::catalog::NodeSpec;
        using bt::catalog::ParamKind;
        using bt::catalog::ParamSpec;

        std::string joined(const std::vector<std::string>& items, const char* separator)
        {
            std::string out;
            for (const std::string& item : items)
            {
                out += (out.empty() ? "" : separator) + item;
            }
            return out;
        }

        std::string keyTypesText(const std::vector<bt::BlackboardValueType>& types)
        {
            std::string out;
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                out += i == 0 ? "" : (i + 1 == types.size() ? " or " : ", ");
                out += bt::blackboardValueTypeToString(types[i]);
            }
            return out;
        }

        std::string childText(const NodeSpec& spec)
        {
            if (spec.maxChildren == 0)
            {
                return "no children";
            }
            if (spec.minChildren == spec.maxChildren)
            {
                return std::to_string(spec.minChildren) + (spec.minChildren == 1 ? " child" : " children");
            }
            if (spec.maxChildren == bt::catalog::kUnlimitedChildren)
            {
                return std::to_string(spec.minChildren) + "+ children";
            }
            return std::to_string(spec.minChildren) + "-" + std::to_string(spec.maxChildren) + " children";
        }

        std::string paramLine(const ParamSpec& param)
        {
            std::vector<std::string> facts{bt::catalog::paramKindToString(param.kind)};
            if (param.required)
            {
                facts.emplace_back("required");
            }
            else if (param.kind != ParamKind::ScriptPath)
            {
                facts.push_back("default " + bttool::valueJson(param.defaultValue).dump());
            }
            if (param.hasRange)
            {
                facts.push_back(param.kind == ParamKind::Int
                    ? std::to_string(static_cast<int64_t>(param.minValue)) + ".." +
                      std::to_string(static_cast<int64_t>(param.maxValue))
                    : env::detail::floatText(param.minValue) + ".." + env::detail::floatText(param.maxValue));
            }
            if (!param.enumValues.empty())
            {
                facts.push_back(joined(param.enumValues, " | "));
            }
            if (!param.keyTypes.empty())
            {
                facts.push_back("a " + keyTypesText(param.keyTypes) + " key" + (param.allowEmptyKey ? ", \"\" = none" : ""));
            }
            if (!param.valueKeyParam.empty())
            {
                facts.push_back("typed like the key in `" + param.valueKeyParam + "`");
            }
            if (!param.serviceVariant.empty())
            {
                facts.push_back("serviceType " + param.serviceVariant);
            }
            return "- `" + param.name + "` (" + joined(facts, "; ") + "): " + param.description + "\n";
        }

        std::string nodeCatalogue()
        {
            std::string text;
            for (const NodeSpec& spec : bt::catalog::nodes())
            {
                text += "### " + spec.name + " - " + bt::catalog::categoryToString(spec.category) + ", " +
                        childText(spec) + (spec.category == NodeCategory::Root ? " (implicit)" : "") + "\n\n" +
                        spec.description + "\n\n";
                for (const ParamSpec& param : spec.params)
                {
                    text += paramLine(param);
                }
                if (!spec.params.empty())
                {
                    text += "\n";
                }
            }
            return text;
        }

        std::string eqsValueText(const eqs::EQSParamValue& value)
        {
            return std::visit([](const auto& stored) -> std::string
            {
                using T = std::decay_t<decltype(stored)>;
                if constexpr (std::is_same_v<T, float>)
                {
                    return env::detail::floatText(stored);
                }
                else if constexpr (std::is_same_v<T, int32_t>)
                {
                    return std::to_string(stored);
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    return stored ? "true" : "false";
                }
                else
                {
                    return "\"" + stored + "\"";
                }
            }, value);
        }

        std::string eqsParamsText(const std::vector<eqs::EQSParamSpec>& params)
        {
            std::vector<std::string> parts;
            for (const eqs::EQSParamSpec& param : params)
            {
                std::string part = "`" + param.name + "`";
                if (!param.enumValues.empty())
                {
                    part += " " + joined(param.enumValues, "|");
                }
                else if (param.hasRange)
                {
                    part += " " + (param.kind == eqs::EQSParamKind::Int
                        ? std::to_string(static_cast<int64_t>(param.minValue)) + ".." +
                          std::to_string(static_cast<int64_t>(param.maxValue))
                        : env::detail::floatText(param.minValue) + ".." + env::detail::floatText(param.maxValue));
                }
                part += param.required ? " (required)" : " (" + eqsValueText(param.defaultValue) + ")";
                parts.push_back(part + " " + param.description);
            }
            return parts.empty() ? std::string("no params") : joined(parts, "; ");
        }

        std::string eqsCatalogue()
        {
            std::string text = "Generators (where the candidates come from):\n\n";
            for (const eqs::EQSGeneratorTypeSpec& generator : eqs::generatorTypes())
            {
                text += "- **" + generator.type + "**: " + generator.description + " Params: " +
                        eqsParamsText(generator.params) + "\n";
            }
            text += "\nTests (each candidate gets a metric):\n\n";
            for (const eqs::EQSTestTypeSpec& test : eqs::testTypes())
            {
                text += "- **" + test.type + "**" + (test.boolean ? " (boolean)" : "") + ": " + test.description +
                        " Metric: " + test.metric + ". Params: " + eqsParamsText(test.params) + "\n";
            }
            return text;
        }

        constexpr const char* workedExample = R"json({"name": "EnemyAI",
 "blackboard": [{"name": "target", "type": "Entity"}, {"name": "route", "type": "Entity"},
                {"name": "targetDistance", "type": "Float", "default": 1000000.0}, {"name": "hits", "type": "Int", "default": 0}],
 "root": {"type": "Service", "name": "Sense Target",
          "params": {"serviceType": "TargetDistance", "interval": 0.2, "runOnActivation": true,
                     "targetKey": "target", "distanceKey": "targetDistance", "horizontal": true},
          "child": {"type": "Selector", "name": "Brain", "children": [
            {"type": "BlackboardCondition", "name": "In Attack Range",
             "params": {"key": "targetDistance", "compareOp": "<=", "compareValue": 2.0, "abortMode": "Both"},
             "child": {"type": "ScriptTask", "name": "Attack", "params": {"script": "game/ai/EnemyMeleeAttack.mt"}}},
            {"type": "BlackboardCondition", "name": "In Chase Range",
             "params": {"key": "targetDistance", "compareOp": "<=", "compareValue": 10.0, "abortMode": "Both"},
             "child": {"type": "MoveTo", "name": "Chase",
                       "params": {"targetKey": "target", "arrivalDistance": 1.5, "repathDistance": 1.0, "stopOnArrival": true}}},
            {"type": "FollowRoute", "name": "Patrol",
             "params": {"routeKey": "route", "mode": "loop", "arrivalDistance": 1.0, "dwell": 1.0,
                        "startAtNearest": true, "legTimeout": 30.0}}]}}})json";

        constexpr const char* introAndBasics = R"md(# Behavior trees

A behavior tree (.vfBehaviorTree) decides what an AI entity does every frame. The bt_* tools write and read trees as
the nested JSON of section 3, assign them to entities with per-entity blackboard values, and watch them run.
Movement uses the navmesh and patrol routes (vf://docs/navigation); custom actions are mType ScriptTasks (section 9,
and section 12 of vf://docs/mtype-api).

## 1. Ticking model

- Trees run only in Play mode (the editor's Play and an exported game). When Play starts, every enabled BehaviorTree
  component attaches its tree to its entity and applies the entity's blackboard start values.
- Every frame the tree ticks from the implicit Root, which ticks its one child (the tree's top node). When the top
  node finishes - Success or Failure - the tree starts over on the next tick.
- A node that returns Running is resumed on the next tick: a Sequence goes on with its running child, a Selector
  re-checks its guarded branches first (section 5).
- Edit mode never ticks a tree: bt_debug_state reports attached:false there.

## 2. Workflow

1. `bt_list_node_types`: the node types and parameters, the project's EQS templates (`eqsQueries`) and ScriptTask
   scripts (`scriptTasks`).
2. Custom behaviour: `script_write game/ai/<Name>.mt` (a ScriptTask, section 9), then `scripts_build` until it builds.
3. `bt_create {path: "ai/<Name>.vfBehaviorTree", tree}` (paths are relative to the project's asset folder).
4. Movement needs a baked navmesh and an agent: `navmesh_bake`, `component_add {type: "NavmeshAgent"}`, patrol routes
   with `route_create` (vf://docs/navigation).
5. `bt_assign {entity, tree, blackboard: {target: <entity id>, route: <route id>}}`: the tree and this entity's start
   values (Entity keys are always set per entity); check `bt_get_blackboard {entity}`.
6. `scene_save`, `play_start`, `bt_debug_state {entity, sampleSeconds: 10}`, `logs_read`, `play_stop`.
7. Change the tree with `bt_edit` (small changes) or `bt_set` (the whole tree); read it with `bt_get`.

## 3. The tree JSON

```
TreeSpec := {name*, blackboard?: [KeySpec], root*: NodeSpec}
KeySpec  := {name*, type*: Float|Int|Bool|String|Vec3|Entity, default?}
NodeSpec := {type*, name? (default = type), id?, position? [x, y],
             params?: {...},
             children?: [NodeSpec] | child?: NodeSpec,
             mappings?: [{parent, child, dir: In|Out|InOut}]}      (DynamicSubTree only)
```

- The Root is implicit: `root` is the tree's top node (a Root wrapper is unwrapped with a warning).
- Children: composites (Sequence, Selector, Parallel) take 1 or more, decorators and services exactly 1, tasks none.
- `default`: omitted = 0 / 0 / false / "" / [0, 0, 0]. An Entity key has no default (null): entity ids change on every
  load, so Entity values are set per entity (section 7).
- Ids: a given id (a positive integer, unique) is kept; the others are assigned in pre-order. Ids never change on
  later edits: the editor's debugger and breakpoints key on them. bt_get reports every id.
- Layout: editor positions are a tidy tree (x = leaf order x 220, y = depth x 140), applied to nodes without a position
  (bt_set and bt_edit keep the positions of the nodes they keep; `relayout: true` lays out every node).
- Strict, all or nothing (up to 20 problems per call, each with a JSON path such as
  `root.child.children[1].params.compareValue`): an unknown node type or parameter, a missing required parameter, a
  duplicate id, a wrong child count, a number outside its range, an unknown enum value, a key parameter naming an
  undeclared key or one of a type it cannot use, a value that does not match its key's type, a script that is not a
  ScriptTask, a SubTree file that does not exist (or the tree itself) and an EQS template that does not exist are all
  refused, and nothing is written.
- Floats accept any number and are stored as floats (2 and 2.0 alike); Ints accept integral numbers only; enums are
  case-insensitive on input ("loop" = "Loop") and canonical on output. Every optional parameter is written explicitly.
- Key parameters name a declared key: targets (`targetKey` of MoveTo, LineOfSight, the services and EQS) take a Vec3,
  Entity or Int (an entity id) key; `FollowRoute.routeKey` an Entity or Int key; `visibilityKey` a Bool key;
  `distanceKey` a Float key; EQS `resultKey` a Vec3 key and `resultEntityKey` an Entity or Int key. A parameter left out
  uses its default key (e.g. MoveTo `targetKey` = "target"), which must then be declared.
- `compareValue` (BlackboardCondition, CheckBlackboardValue) and `value` (SetBlackboardValue) take the type of their
  key; the numeric compareOps (> < >= <=) need a Float or Int key; Vec3 and Entity keys cannot be compared.
- ScriptTask: `params: {script: "game/ai/<Name>.mt"}`; the class name is read from the file.
- SubTree `treePath` / DynamicSubTree `defaultTreePath`: a .vfBehaviorTree inside the project.
- bt_edit applies the strict checks to the nodes it creates or changes and to the parents whose children change; the
  other nodes keep what they store, so trees made in the editor stay editable. Stored parameters the catalogue does
  not know are kept there and reported by bt_get as `legacyParams`.
- Save guards: `contentHash` (bt_get) as `expectedContentHash` refuses a write when the file changed since it was read.

## 4. Node catalogue

)md";

        constexpr const char* reactivityAndMore = R"md(## 5. Reactivity

- A BlackboardCondition guards a branch: it runs its child only while `blackboard[key] compareOp compareValue` holds.
  `abortMode`: None; Self (abort the running child when the condition turns false); LowerPriority (when it is a child of
  a Selector and turns true, the Selector aborts the lower-priority branch that runs and switches to it); Both.
- The priority selector: a Selector of guarded branches, highest priority first, with an unguarded fallback last
  (Attack, Chase, Patrol in section 10). With `Both` each branch drops out as soon as its condition fails, and a higher
  one takes over as soon as its condition holds.
- **A guarded branch must keep returning Running while its condition holds.** If its task fails at once after the
  guard pre-empted the branch below it, the Selector falls through to the lower branch, which the guard pre-empts again
  on the next tick: the tree thrashes every frame (bt_debug_state warns when the running leaf changes on most samples).
  So timing such as an attack's swing interval lives inside a task that stays Running (a ScriptTask with its own
  cooldown), not in a Cooldown decorator that fails between swings.
- A Service runs its job every `interval` seconds while its branch is active (`runOnActivation`: also once when the
  branch starts). TargetDistance writes the owner-to-target distance (XZ; 3D with `horizontal: false`; 1e9 without a
  target) into a Float key, so "within range" is one BlackboardCondition whose `compareValue` is the range.
  LineOfSightRefresh writes visibility into a Bool key, FocusUpdate the target's position into a Vec3 key, EQSRefresh
  re-runs an EQS template (section 8).

## 6. Movement

- MoveTo moves the entity's NavmeshAgent (or VolumetricAgent) to `targetKey`: an Entity it follows (the destination is
  re-issued when the target moved more than `repathDistance`), a Vec3 point, or an Int holding an entity id. It
  succeeds within `arrivalDistance` (measured on XZ; in 3D for a flying agent) and `stopOnArrival` stops the agent
  there. It fails at once when the key holds no entity (an unset Entity key) or is of another type; without an agent
  the entity never moves (bt_assign warns about it).
- FollowRoute patrols a route: the ordered child markers of the route entity in `routeKey` (a container entity with a
  NavRouteComponent; `route_create` makes one and `route_get` reads it). `mode` Loop / PingPong run until the branch
  is aborted, Once succeeds at the last marker; `dwell` waits at each marker; `startAtNearest` starts at the nearest
  one; a leg that takes longer than `legTimeout` fails. After an abort it resumes the leg it was on.
- NavmeshAgent rules (vf://docs/navigation): bake the navmesh first (`navmesh_bake`); keep agents at the scene root
  (the crowd writes the agent's LOCAL position); `baseOffset` lifts a pivot that is not at the feet (1 for a 2 m
  capsule with a centred pivot); an agent's radius and height should not exceed the bake's agentRadius and
  agentHeight. `navmesh_find_path` explains why a path fails.
- Flying agents: a VolumetricAgent inside a baked VolumetricNavVolume; MoveTo and FollowRoute route through the volume
  and arrive in 3D; use TargetDistance with `horizontal: false`.

## 7. Blackboard

- Key types: Float, Int, Bool, String, Vec3, Entity. A tree declares its keys with their defaults; the keys of its
  SubTrees merge in (this tree wins a name clash).
- Per-entity start values: `bt_assign {blackboard}` and `bt_set_blackboard {entity, values}` store values on the
  entity's BehaviorTree component (one undo step each); they are applied when Play starts. An Entity value is given as
  an entity id (or {entity: id}), reported as {entity, name}, and stored as the target's UUID plus its name: when Play
  starts it resolves by UUID first, then to the first entity with that name. A value whose key the tree lacks, or
  whose type differs, is skipped (the component's warnings and bt_get_blackboard's staleValues show it).
- Live values: `bt_set_blackboard {entity, values, live: true}` writes the running tree's blackboard in Play mode;
  `bt_debug_state` shows it. Scripts use `Blackboard::getFloat / getInt / getBool / getString / getVec3 / getEntity`
  (-1 when unset) and the matching `set*` (assets/scripts/lib/engine/Blackboard.mt).

## 8. EQS

- EQS (environment queries) picks a place or an entity out of many candidates - a cover spot, a flanking position, the
  nearest enemy. For a single known target use the TargetDistance or LineOfSightRefresh service instead: they are far
  cheaper.
- A template is `assets/eqs/<Name>.vfEQSQuery` (`eqs_list_queries` lists them, `eqs_set_query` writes one):
  `{"version": 1, "description": "...", "generator": {"type", "params"}, "tests": [{"type", "params", "filter":
  {"min"?, "max"?} | "score": {"mode"?, "weight"?, "combine"?}}], "maxResults": 1}`. Every test has exactly one of
  filter or score; a boolean test (OnNavMesh, Trace) with `filter: {}` must pass, `filter: {"max": 0}` must fail.
- Filters run first, in order, and drop candidates; then each scorer's metric is normalised over the surviving
  candidates (min-max, 0..1), shaped by `mode` (Linear: a high metric is good; InverseLinear: a low one is good, so
  Distance + InverseLinear = nearest first; Square; Constant), multiplied by `weight` and combined (Multiply or
  Additive) into a total that starts at 1. A metric equal for every candidate scores 1. The best total wins.
- EnvironmentQuery (a task) submits the query when it starts and stays Running until the result arrives; it writes the
  best position to `resultKey` (Vec3) and its entity to `resultEntityKey` (Entity or Int), and fails when nothing
  passes. The EQSRefresh service re-runs a template every `interval` (at least 0.1 s) while its branch is active.
  `targetKey` is the query's "target" (Entity, Vec3 or Int key).
- Queries run in the EQS frame task with a 2 ms budget per frame, so a result arrives one frame later at the earliest.
- `eqs_run` runs a template now with every candidate's metrics and draws them. In Edit mode physics has no entity
  colliders, so a Trace there sees only terrain.
- Examples: the shipped NearestEnemy (Entities named "Enemy" within 25 m, scored by Distance InverseLinear; give it a
  `resultEntityKey`), and a cover spot hidden from the target:

```json
{"version": 1, "description": "A spot near the querier that its target cannot see.",
 "generator": {"type": "Ring", "params": {"center": "querier", "radius": 4.0, "pointCount": 16, "ringCount": 3, "ringSpacing": 3.0}},
 "tests": [{"type": "OnNavMesh", "params": {"tolerance": 1.0}, "filter": {}},
           {"type": "Trace", "params": {"from": "target"}, "filter": {"max": 0}},
           {"type": "Distance", "params": {"to": "querier"}, "score": {"mode": "InverseLinear"}}],
 "maxResults": 1}
```

  used as `{"type": "EnvironmentQuery", "params": {"queryName": "CoverFromTarget", "targetKey": "target", "resultKey":
  "coverPoint"}}` followed by `{"type": "MoveTo", "params": {"targetKey": "coverPoint"}}` (coverPoint a Vec3 key).

)md";

        constexpr const char* scriptsExampleAndLimits = R"md(## 9. ScriptTasks

- A ScriptTask runs the first @Script class of a `game/**/*.mt` script through `tick(float deltaTime): string`:
  "running" keeps the task running, "success" succeeds, anything else fails. Optional hooks: `onAbort()` when a guard or
  a higher-priority branch interrupts it, `onEnd(bool success)` when tick finished it. `onStart` runs once, the first
  time the task runs for the entity; `onUpdate` is never called.
- There is one instance per entity and script for the whole Play session, so reset per-run state in onAbort and onEnd.
  tick, onAbort and onEnd take no @Override (Behaviour does not declare them). `this.entityId()` is the entity that
  runs the tree.
- `params: {script: "game/ai/<Name>.mt"}`; build the scripts (`scripts_build`) before Play. Section 12 of
  vf://docs/mtype-api has the full contract and the example assets/scripts/game/examples/MeleeAttack.mt.

## 10. Worked example: patrol, chase, attack

```json
)md";

        constexpr const char* exampleNotes = R"md(
```

- `bt_create {path: "ai/EnemyAI.vfBehaviorTree", tree: <the JSON above>}` after writing game/ai/EnemyMeleeAttack.mt
  (MeleeAttack.mt, renamed) and building it.
- `bt_assign {entity: <enemy>, tree: "ai/EnemyAI.vfBehaviorTree", blackboard: {target: <player id>, route: <route
  id>}}`; the enemy needs a NavmeshAgent on a baked navmesh, the route comes from `route_create`.
- How it plays: the Service measures the distance to the target 5 times a second. The attack branch takes over within
  2 m - before MoveTo's 1.5 m arrival, so the chaser never stops short of attacking; `Both` makes each branch drop out
  as soon as its range is left, and Patrol resumes its leg. The swing timing lives in the ScriptTask, which keeps
  returning "running" while it is in range (section 5).
- Verify: `play_start`, `bt_debug_state {entity, sampleSeconds: 10}` (expect Patrol -> Chase -> Attack in `switches`),
  `logs_read`, `play_stop`. Change the chase range with
  `bt_edit {path, ops: [{op: "setParams", node: "In Chase Range", params: {compareValue: 15}}]}`.

## 11. Debugging

- `bt_debug_state {entity}`: status, the active path from the top node to the running leaf, the running nodes with
  their elapsed time and the blackboard; `sampleSeconds` records the running leaf over time (`samples`, `switches`).
  It never takes over the editor's BT debugger.
- `logs_read`: ScriptTask load errors, skipped start values, EQS failures, trees that did not attach.
- `bt_get`: `validation` (errors, warnings, strict findings) and `attachedTo`; `bt_get_blackboard {entity}`: the start
  and live values.
- `navmesh_find_path` (why an agent does not move), `eqs_run` (what a query sees), `viewport_screenshot`.
- The editor's BT window shows the same tree: it reloads when a tool saves it, or shows a "changed on disk" banner
  when it has unsaved edits.

## 12. Limits

- Trees attach only when Play starts. Assign them in Edit mode: changing a BehaviorTree component in Play mode is
  refused (Stop restores the scene, and a running tree never sees it). A tree a script attaches
  (`Blackboard::attachTree`) has no component.
- Saving a tree (bt_create, bt_set, bt_edit, bt_set_blackboard {path}) is allowed in Play mode: every running tree that
  uses the file reloads before its next tick and keeps the blackboard values that still fit. Tree writes are not
  undoable; bt_assign, bt_unassign, bt_set_blackboard {entity} and component_* BehaviorTree are.
- A tree holds at most 1000 nodes per bt_create / bt_set call and 256 keys; bt_edit takes 1-64 ops.
- Ids are permanent; a replaced node keeps its id unless the replacement gives one.
)md";
    }

    namespace bttool
    {
        const std::string& workedExampleTree()
        {
            static const std::string text = workedExample;
            return text;
        }

        std::string buildBehaviorTreeDoc()
        {
            std::string text = introAndBasics;
            text += nodeCatalogue();
            text += reactivityAndMore;
            text += eqsCatalogue();
            text += "\n";
            text += scriptsExampleAndLimits;
            text += workedExample;
            text += exampleNotes;
            return text;
        }
    }

    void registerBehaviorTreeResources(ResourceRegistry& registry, const ToolContext& context)
    {
        (void)context;
        ResourceDef resource;
        resource.uri = "vf://docs/bt";
        resource.name = "bt";
        resource.title = "Behavior trees";
        resource.description =
            "Behavior trees for AI: how they tick, the tree JSON the bt_* tools take (ids, layout, strict checks), the "
            "node catalogue, reactivity (abort modes, the priority selector, the guarded-branch trap), movement "
            "(MoveTo, FollowRoute, routes, agents), the blackboard and per-entity values, EQS (templates, generators, "
            "tests, scoring), ScriptTasks, the worked patrol / chase / attack example, debugging and limits.";
        resource.mimeType = "text/markdown";
        resource.affinity = ThreadAffinity::Worker;
        resource.timeout = std::chrono::milliseconds(10000);
        resource.reader = []() -> ResourceContents
        {
            return {"vf://docs/bt", "text/markdown", bttool::buildBehaviorTreeDoc()};
        };
        registry.add(std::move(resource));
    }
}
