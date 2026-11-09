#pragma once

#include "../protocol/ArgReader.hpp"

#include "animator/AnimatorTypes.hpp"
#include "animator/AnimatorValidation.hpp"
#include "asset/AssetRef.hpp"
#include "data/AnimationAuthoringData.hpp"
#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "retargeting/RetargetBoneMapping.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// VK-1656 helpers shared by the Animator component binding (AnimatorBinding.cpp), the animation tools
// (AnimationTools.cpp) and their undo entry (mcp/undo/AnimatorAssetUndo). The agent JSON of an animator
// uses the engine's names: parameter {name, type, default}, state {name, clip, playbackSpeed, loop,
// position, blendTree, events}, transition {from, to, conditions, blendDuration, hasExitTime, exitTime,
// priority}. States are addressed by name; "Any" is the Any State.
//
// The first section is pure (any thread; the tests drive it directly). The second dispatches and runs on
// the main thread (inside a Main-affinity handler or an undo command).
namespace mcp::tools::anim
{
    // The name transitions use for the Any State (sourceStateId 0). Not a valid state name.
    inline constexpr std::string_view anyStateName = "Any";

    // Turns an agent clip path into a clip reference; throws ArgError naming `argument`.
    using ClipResolver = std::function<asset::AssetRef(const std::string& path, const std::string& argument)>;
    // How a clip (or other asset) reference is reported: a path string, "guid:<hex>", or null for none.
    using ClipNamer = std::function<nlohmann::json(const asset::AssetRef& clip)>;

    // ------------------------------------------------------------------
    // Pure: agent JSON of an animator (any thread)
    // ------------------------------------------------------------------

    nlohmann::json parameterValueJson(const animator::AnimatorParameterValue& value);
    nlohmann::json parameterJson(const animator::AnimatorParameter& parameter);
    // A state of `graph` by id: its name, "Any" for 0, null when no state has the id.
    nlohmann::json stateRefJson(const animator::AnimatorGraph& graph, uint32_t stateId);
    nlohmann::json stateJson(const animator::AnimatorState& state, const ClipNamer& clipName);
    nlohmann::json transitionJson(const animator::AnimatorGraph& graph, const animator::AnimatorTransition& transition);

    // {defaultState, parameters, states, transitions} of one state machine.
    nlohmann::json graphJson(const animator::AnimatorGraph& graph, const ClipNamer& clipName);

    // [{index, name, sourceMode, weight, blendMode, boneMask, states}], plus directClip on a DirectClip
    // layer. A single-graph file reports its graph as one StateMachine base layer.
    nlohmann::json layersJson(const animator::AnimatorData& data, const ClipNamer& clipName);

    // [{level: "error" | "warning", layer, message}].
    nlohmann::json issuesJson(const std::vector<animator::GraphIssue>& issues);

    // The errors as "- [layer N] message" lines ("" when there are none).
    std::string errorLines(const std::vector<animator::GraphIssue>& issues);

    // Every parameter of the animator's state-machine layers, first declaration first (the runtime's
    // shared parameter map), as [{name, type}].
    nlohmann::json parameterSummaryJson(const animator::AnimatorData& data);

    // The state machine `layer` addresses: a single-graph file has only layer 0 (data.graph); a layered
    // file's layer must exist and be a StateMachine layer. Throws ArgError naming the valid layers.
    const animator::AnimatorGraph& stateMachineLayer(const animator::AnimatorData& data, uint32_t layer);
    animator::AnimatorGraph& stateMachineLayer(animator::AnimatorData& data, uint32_t layer);

    // The base layer's graph: data.graph for a single-graph file, else layers[0].graph.
    const animator::AnimatorGraph& baseGraph(const animator::AnimatorData& data);

    // ------------------------------------------------------------------
    // Pure: building and editing (any thread)
    // ------------------------------------------------------------------

    // A state name: a non-empty string other than "Any". ArgError naming `path`.
    std::string requireStateName(const nlohmann::json& value, const std::string& path);

    // A state's playback speed: a number in (0, 100]. ArgError naming `path`.
    float requirePlaybackSpeed(const nlohmann::json& value, const std::string& path);

    // A value of a parameter's type: Float a number, Int an integer, Bool / Trigger a boolean. ArgError
    // naming `path`.
    animator::AnimatorParameterValue parameterValueFrom(animator::AnimatorParameterType type,
                                                        const nlohmann::json& value, const std::string& path);

    // The first declaration of `name` among the state-machine layers (the runtime's shared map), or null.
    const animator::AnimatorParameter* findParameter(const animator::AnimatorData& data, const std::string& name);

    // Auto-layout of new state nodes: a grid from layoutOrigin, layoutStep apart, layoutColumns per row,
    // skipping every cell within layoutClearance of an existing node (states, Entry, Any State).
    inline constexpr glm::vec2 layoutOrigin{300.0f, 100.0f};
    inline constexpr glm::vec2 layoutStep{250.0f, 150.0f};
    inline constexpr int layoutColumns = 4;
    inline constexpr float layoutClearance = 100.0f;

    glm::vec2 nextFreePosition(const animator::AnimatorGraph& graph);

    // A single-graph animator without states, laid out like the Animator Editor's new file.
    animator::AnimatorData emptyAnimator(std::string name);

    // A single-graph animator with one state (id 1, the default state) playing `clip`.
    animator::AnimatorData newAnimator(std::string name, std::string stateName, const asset::AssetRef& clip,
                                       float playbackSpeed, bool loop);

    struct EditSummary
    {
        std::vector<std::string> parametersAdded;
        std::vector<std::string> parametersUpdated;
        std::vector<std::string> parametersRemoved;
        std::vector<std::string> statesAdded;
        std::vector<std::string> statesUpdated;
        std::vector<std::string> statesRemoved;
        std::vector<std::pair<std::string, std::string>> statesRenamed; // old name -> new name
        uint32_t transitionsAdded = 0;
        uint32_t transitionsRemoved = 0;
        std::optional<std::string> defaultState; // set when the call changed the default state

        nlohmann::json toJson() const;
    };

    // Applies an animation_set_state_machine edit {replace?, parameters?, states?, transitions?,
    // defaultState?, remove?} to `layer` of `data` (see the tool description for the semantics). Throws
    // ArgError for invalid input; `data` is then partly edited and must be discarded. A layered file
    // edits layers[layer].graph and mirrors data.graph = layers[0].graph afterwards. Does not validate
    // the result (animator::validateAnimator).
    EditSummary applyAnimatorEdit(animator::AnimatorData& data, const nlohmann::json& edit, uint32_t layer,
                                  const ClipResolver& resolveClip);

    // Approximate heap bytes `data` holds (undo byte budget).
    std::size_t animatorDataBytes(const animator::AnimatorData& data);

    // ------------------------------------------------------------------
    // Pure: clip compatibility (any thread)
    // ------------------------------------------------------------------

    struct ClipCoverage
    {
        uint32_t matchedBones = 0;  // skeleton bones the clip drives
        uint32_t skeletonBones = 0;
        float coverage = 0.0f;      // matchedBones / skeletonBones
        bool rootMatched = false;   // the root bone (firstRootBone / retargetRootBone) is driven
        bool compatible = false;    // coverage >= minCoverage && rootMatched
    };

    // Index of the first root bone (parent -1), nullopt without one.
    std::optional<std::size_t> firstRootBone(const services::MeshSkeletonInfo& skeleton);

    // The bone a retargeted pose is rooted at: the skeleton bone the retarget map drives through the Hips
    // role (RetargetContext retargets the root there), else the first root bone.
    std::optional<std::size_t> retargetRootBone(const services::MeshSkeletonInfo& skeleton,
                                                const retargeting::RetargetBoneMapping& mapping);

    // Played natively, a bone is driven by the channel of the same name (AnimationEvaluator).
    ClipCoverage nativeCoverage(const services::MeshSkeletonInfo& skeleton, const std::vector<std::string>& channels,
                                float minCoverage);

    // Played through a retarget map, a target bone is driven when its role's source bone is a channel
    // (RetargetBoneMapping, the runtime's own mapping; no fallback by name).
    ClipCoverage retargetCoverage(const services::MeshSkeletonInfo& skeleton,
                                  const retargeting::RetargetBoneMapping& mapping,
                                  const std::vector<std::string>& channels, float minCoverage);

    // ------------------------------------------------------------------
    // Engine access (main thread)
    // ------------------------------------------------------------------

    // GetMeshDataQuery.
    std::optional<services::MeshData> meshData(const services::EntityHandle& entity);

    // GetMeshSkeletonQuery / LoadAnimatorAssetQuery for an absolute engine path.
    services::MeshSkeletonInfo meshSkeleton(const std::string& meshPath);
    services::AnimatorAssetLoadResult loadAnimator(const std::string& path);

    // nullopt when the entity can carry an Animator (a Mesh whose asset is skinned). A missing Mesh,
    // meshRef or skeleton gives the reason; an unresolvable mesh asset or an unreadable file throws
    // std::runtime_error.
    std::optional<std::string> animatorHostProblem(const services::EntityHandle& entity);

    // The sandboxed .vfAnim at `path` (relative to `root`) as a registered clip; ArgError naming `argument`.
    asset::AssetRef resolveClip(const std::filesystem::path& root, const std::string& path, const std::string& argument);

    // Reports a reference as a project-relative path ("guid:<hex>" when unresolvable, null when invalid).
    nlohmann::json assetPathJson(const std::optional<std::filesystem::path>& root, const asset::AssetRef& ref);
    ClipNamer projectNamer(std::optional<std::filesystem::path> root);

    // `<mesh folder>/<entity name>.vfAnimator`, made unique. The mesh folder must lie inside `root`
    // (ArgError otherwise; `explicitHint` says how to pass a path instead).
    std::filesystem::path defaultAnimatorPath(const services::EntityHandle& entity, const std::filesystem::path& root,
                                              std::string_view explicitHint);

    // SaveAnimatorAssetCommand{createNew}; throws std::runtime_error when the file is not written.
    services::AnimatorAssetSaveResult createAnimatorFile(const std::filesystem::path& file,
                                                         const animator::AnimatorData& data);

    // Entities whose Mesh plays `animator` (GetEntitiesWithComponentQuery{Mesh} + GetMeshDataQuery), and
    // the same as [{entity, name}].
    std::vector<services::EntityHandle> animatorUsers(const asset::AssetRef& animator);
    nlohmann::json usersJson(const std::vector<services::EntityHandle>& users);
}
