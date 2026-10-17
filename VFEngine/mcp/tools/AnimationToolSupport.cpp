#include "AnimationToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "string/FileNameSanitize.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace mcp::tools::anim
{
    namespace
    {
        namespace fs = std::filesystem;
        using animator::AnimatorData;
        using animator::AnimatorGraph;
        using animator::AnimatorParameter;
        using animator::AnimatorParameterType;
        using animator::AnimatorParameterValue;
        using animator::AnimatorState;
        using animator::AnimatorTransition;
        using animator::ComparisonOperator;

        // Indexed by the enumerators' values.
        constexpr std::array<std::string_view, 4> parameterTypeNames{"Float", "Int", "Bool", "Trigger"};
        constexpr std::array<std::string_view, 6> operatorNames{">", "<", ">=", "<=", "==", "!="};

        constexpr double maxFloatValue = static_cast<double>(std::numeric_limits<float>::max());
        constexpr double minInt32 = static_cast<double>(std::numeric_limits<int32_t>::min());
        constexpr double maxInt32 = static_cast<double>(std::numeric_limits<int32_t>::max());
        constexpr double maxNodePosition = 1.0e6;
        constexpr double maxPlaybackSpeed = 100.0;
        constexpr double maxBlendDuration = 60.0;
        constexpr double maxThreshold = 1.0e6;

        // ------------------------------------------------------------------
        // Argument parsing
        // ------------------------------------------------------------------

        // The member when it is present and not null (null counts as absent, like ArgReader).
        const nlohmann::json* member(const nlohmann::json& object, const char* key)
        {
            const auto it = object.find(key);
            return it == object.end() || it->is_null() ? nullptr : &*it;
        }

        const nlohmann::json& requiredMember(const nlohmann::json& object, const char* key, const std::string& path)
        {
            const nlohmann::json* value = member(object, key);
            if (value == nullptr)
            {
                throw ArgError("'" + path + "." + key + "' is required");
            }
            return *value;
        }

        std::string indexed(const std::string& path, std::size_t index)
        {
            return path + "[" + std::to_string(index) + "]";
        }

        void requireArray(const nlohmann::json& value, const std::string& path)
        {
            if (!value.is_array())
            {
                throw ArgError("'" + path + "' must be an array (got " + env::detail::valueText(value) + ")");
            }
        }

        std::string requireName(const nlohmann::json& value, const std::string& path)
        {
            std::string name = env::requireString(value, path);
            if (name.empty())
            {
                throw ArgError("'" + path + "' must not be empty");
            }
            return name;
        }

        AnimatorParameterType parameterTypeFrom(const nlohmann::json& value, const std::string& path)
        {
            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                for (std::size_t i = 0; i < parameterTypeNames.size(); ++i)
                {
                    if (env::detail::equalsIgnoreCase(text, parameterTypeNames[i]))
                    {
                        return static_cast<AnimatorParameterType>(i);
                    }
                }
            }
            throw ArgError("'" + path + "' must be one of Float, Int, Bool, Trigger (got " + env::detail::valueText(value) +
                           ")");
        }

        ComparisonOperator operatorFrom(const nlohmann::json& value, const std::string& path)
        {
            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                for (std::size_t i = 0; i < operatorNames.size(); ++i)
                {
                    if (text == operatorNames[i])
                    {
                        return static_cast<ComparisonOperator>(i);
                    }
                }
            }
            throw ArgError("'" + path + "' must be one of >, <, >=, <=, ==, != (got " + env::detail::valueText(value) + ")");
        }

        AnimatorParameterValue zeroValue(AnimatorParameterType type)
        {
            switch (type)
            {
            case AnimatorParameterType::Float:
                return AnimatorParameterValue(std::in_place_type<float>, 0.0f);
            case AnimatorParameterType::Int:
                return AnimatorParameterValue(std::in_place_type<int32_t>, 0);
            case AnimatorParameterType::Bool:
            case AnimatorParameterType::Trigger:
                return AnimatorParameterValue(std::in_place_type<bool>, false);
            }
            return AnimatorParameterValue(std::in_place_type<float>, 0.0f);
        }

        glm::vec2 vec2From(const nlohmann::json& value, const std::string& path)
        {
            if (!value.is_array() || value.size() != 2)
            {
                throw ArgError("'" + path + "' must be [x, y] (got " + env::detail::valueText(value) + ")");
            }
            return {env::requireFloat(value[0], -maxNodePosition, maxNodePosition, path + "[0]"),
                    env::requireFloat(value[1], -maxNodePosition, maxNodePosition, path + "[1]")};
        }

        nlohmann::json vec2Json(const glm::vec2& value)
        {
            return nlohmann::json::array({env::floatJson(value.x), env::floatJson(value.y)});
        }

        // ------------------------------------------------------------------
        // Reporting
        // ------------------------------------------------------------------

        nlohmann::json blendTreeJson(const animator::BlendTreeData& tree, const ClipNamer& clipName)
        {
            const bool twoD = tree.type == animator::BlendTreeType::BlendTree2D;
            nlohmann::json entries = nlohmann::json::array();
            for (const animator::BlendTreeEntry& entry : tree.entries)
            {
                nlohmann::json item{{"clip", clipName(entry.animationRef)}};
                if (twoD)
                {
                    item["position"] = vec2Json(entry.position);
                }
                else
                {
                    item["threshold"] = env::floatJson(entry.threshold);
                }
                entries.push_back(std::move(item));
            }
            nlohmann::json out{{"type", twoD ? "2D" : "1D"}, {"parameter", tree.parameterName}};
            if (twoD)
            {
                out["parameterY"] = tree.parameterNameY;
            }
            out["entries"] = std::move(entries);
            return out;
        }

        void collectParameterSummary(const AnimatorGraph& graph, std::unordered_set<std::string>& seen, nlohmann::json& out)
        {
            for (const AnimatorParameter& parameter : graph.parameters)
            {
                if (seen.insert(parameter.name).second)
                {
                    out.push_back(nlohmann::json{
                        {"name", parameter.name},
                        {"type", animator::parameterTypeToString(parameter.type)}
                    });
                }
            }
        }

        std::size_t graphBytes(const AnimatorGraph& graph)
        {
            std::size_t bytes = graph.states.capacity() * sizeof(AnimatorState) +
                                graph.transitions.capacity() * sizeof(AnimatorTransition) +
                                graph.parameters.capacity() * sizeof(AnimatorParameter);
            for (const AnimatorState& state : graph.states)
            {
                bytes += state.name.capacity() + state.events.capacity() * sizeof(animator::AnimationEvent);
                for (const animator::AnimationEvent& event : state.events)
                {
                    bytes += event.name.capacity() + event.payload.capacity();
                }
                if (state.blendTree.has_value())
                {
                    bytes += state.blendTree->parameterName.capacity() + state.blendTree->parameterNameY.capacity() +
                             state.blendTree->entries.capacity() * sizeof(animator::BlendTreeEntry);
                }
            }
            for (const AnimatorTransition& transition : graph.transitions)
            {
                bytes += transition.conditions.capacity() * sizeof(animator::TransitionCondition);
                for (const animator::TransitionCondition& condition : transition.conditions)
                {
                    bytes += condition.parameterName.capacity();
                }
            }
            for (const AnimatorParameter& parameter : graph.parameters)
            {
                bytes += parameter.name.capacity();
            }
            return bytes;
        }

        // ------------------------------------------------------------------
        // Editing
        // ------------------------------------------------------------------

        using ParameterTypes = std::unordered_map<std::string, AnimatorParameterType>;

        // One animation_set_state_machine edit of one state-machine layer. Steps run in a fixed order:
        // remove (current names) -> parameters -> states (renames apply in list order) -> defaultState ->
        // transitions (names after the renames).
        class GraphEdit
        {
        public:
            GraphEdit(AnimatorData& data, uint32_t layer, const ClipResolver& resolveClip)
                : data(data), layer(layer), graph(stateMachineLayer(data, layer)), resolveClip(resolveClip)
            {
            }

            EditSummary apply(const nlohmann::json& edit)
            {
                env::rejectUnknownKeys(edit, {"replace", "parameters", "states", "transitions", "defaultState", "remove"},
                                       "edit");
                const nlohmann::json* replaceValue = member(edit, "replace");
                const bool replace = replaceValue != nullptr && env::requireBool(*replaceValue, "replace");
                const nlohmann::json* remove = member(edit, "remove");
                if (replace && remove != nullptr)
                {
                    throw ArgError("'remove' cannot be combined with replace:true: the given lists already replace the "
                                   "layer's lists");
                }

                // States are matched by name but moved by id: a hand-edited file with a repeated id cannot be
                // edited safely (validation would refuse to save it anyway).
                std::unordered_set<uint32_t> stateIds;
                for (const AnimatorState& state : graph.states)
                {
                    if (!stateIds.insert(state.id).second)
                    {
                        throw std::runtime_error("The animator's " + layerText() + " has two states with id " +
                                                 std::to_string(state.id) + "; repair it in the Animator Editor first");
                    }
                }

                // A hand-edited file may hold ids past its counters; new ids must not collide with them.
                for (const AnimatorState& state : graph.states)
                {
                    graph.nextStateId = std::max(graph.nextStateId, state.id + 1);
                }
                for (const AnimatorTransition& transition : graph.transitions)
                {
                    graph.nextTransitionId = std::max(graph.nextTransitionId, transition.id + 1);
                }

                if (remove != nullptr)
                {
                    removeEntries(*remove);
                }
                if (const nlohmann::json* parameters = member(edit, "parameters"))
                {
                    editParameters(*parameters, replace);
                }
                if (const nlohmann::json* states = member(edit, "states"))
                {
                    editStates(*states, replace);
                }
                applyDefaultState(edit);
                if (const nlohmann::json* transitions = member(edit, "transitions"))
                {
                    editTransitions(*transitions, replace);
                }

                // The serializer writes only the layers of a layered file; the loader mirrors layer 0 into
                // data.graph, and so does this edit.
                if (!data.layers.empty())
                {
                    data.graph = data.layers.front().graph;
                }
                return summary;
            }

        private:
            std::string layerText() const
            {
                return data.layers.empty() ? std::string("the animator") : "layer " + std::to_string(layer);
            }

            std::string stateList() const
            {
                std::string names;
                for (const AnimatorState& state : graph.states)
                {
                    names += names.empty() ? state.name : ", " + state.name;
                }
                return names.empty() ? std::string("none") : names;
            }

            // The id of the state `name` names; "Any" is the Any State (0) where `allowAny`.
            uint32_t stateId(const std::string& name, bool allowAny, const std::string& path) const
            {
                if (name == anyStateName)
                {
                    if (allowAny)
                    {
                        return 0;
                    }
                    throw ArgError("'" + path + "': \"Any\" is the Any State, which can only be a transition's 'from'");
                }
                const AnimatorState* state = graph.findStateByName(name);
                if (state == nullptr)
                {
                    throw ArgError("'" + path + "': no state named \"" + name + "\" in " + layerText() + " (states: " +
                                   stateList() + ")");
                }
                return state->id;
            }

            // Drops a state with every transition into or out of it.
            void dropState(uint32_t id)
            {
                const std::size_t removed = std::erase_if(graph.transitions, [id](const AnimatorTransition& transition)
                {
                    return transition.sourceStateId == id || transition.targetStateId == id;
                });
                summary.transitionsRemoved += static_cast<uint32_t>(removed);
                if (graph.defaultStateId == id)
                {
                    graph.defaultStateId = 0;
                    defaultRemoved = true;
                }
                std::erase_if(graph.states, [id](const AnimatorState& state) { return state.id == id; });
            }

            void removeEntries(const nlohmann::json& remove)
            {
                env::rejectUnknownKeys(remove, {"parameters", "states", "transitions"}, "remove");

                if (const nlohmann::json* list = member(remove, "transitions"))
                {
                    requireArray(*list, "remove.transitions");
                    for (std::size_t i = 0; i < list->size(); ++i)
                    {
                        const std::string path = indexed("remove.transitions", i);
                        const nlohmann::json& item = (*list)[i];
                        env::rejectUnknownKeys(item, {"from", "to"}, path);
                        const std::string fromName = requireName(requiredMember(item, "from", path), path + ".from");
                        const std::string toName = requireName(requiredMember(item, "to", path), path + ".to");
                        const uint32_t from = stateId(fromName, true, path + ".from");
                        const uint32_t to = stateId(toName, false, path + ".to");
                        const std::size_t removed = std::erase_if(graph.transitions, [from, to](const AnimatorTransition& transition)
                        {
                            return transition.sourceStateId == from && transition.targetStateId == to;
                        });
                        if (removed == 0)
                        {
                            throw ArgError("'" + path + "': there is no transition from " + fromName + " to " + toName);
                        }
                        summary.transitionsRemoved += static_cast<uint32_t>(removed);
                    }
                }

                if (const nlohmann::json* list = member(remove, "states"))
                {
                    requireArray(*list, "remove.states");
                    for (std::size_t i = 0; i < list->size(); ++i)
                    {
                        const std::string path = indexed("remove.states", i);
                        const std::string name = requireName((*list)[i], path);
                        dropState(stateId(name, false, path));
                        summary.statesRemoved.push_back(name);
                    }
                }

                if (const nlohmann::json* list = member(remove, "parameters"))
                {
                    requireArray(*list, "remove.parameters");
                    for (std::size_t i = 0; i < list->size(); ++i)
                    {
                        const std::string path = indexed("remove.parameters", i);
                        const std::string name = requireName((*list)[i], path);
                        const std::size_t removed = std::erase_if(graph.parameters, [&name](const AnimatorParameter& parameter)
                        {
                            return parameter.name == name;
                        });
                        if (removed == 0)
                        {
                            throw ArgError("'" + path + "': no parameter named \"" + name + "\" in " + layerText());
                        }
                        summary.parametersRemoved.push_back(name);
                    }
                }
            }

            void editParameters(const nlohmann::json& list, bool replace)
            {
                requireArray(list, "parameters");
                std::vector<AnimatorParameter> listed;
                std::unordered_set<std::string> names;
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    const std::string path = indexed("parameters", i);
                    const nlohmann::json& item = list[i];
                    env::rejectUnknownKeys(item, {"name", "type", "default"}, path);
                    const std::string name = requireName(requiredMember(item, "name", path), path + ".name");
                    if (!names.insert(name).second)
                    {
                        throw ArgError("'" + path + "': parameter \"" + name + "\" is listed twice");
                    }

                    const auto existing = std::find_if(graph.parameters.begin(), graph.parameters.end(),
                                                       [&name](const AnimatorParameter& parameter) { return parameter.name == name; });
                    const bool isNew = existing == graph.parameters.end();
                    AnimatorParameter parameter = isNew ? AnimatorParameter{} : *existing;
                    parameter.name = name;

                    const nlohmann::json* type = member(item, "type");
                    if (isNew && type == nullptr)
                    {
                        throw ArgError("'" + path + "': the new parameter \"" + name +
                                       "\" needs a 'type' (Float, Int, Bool or Trigger)");
                    }
                    if (type != nullptr)
                    {
                        const AnimatorParameterType parsed = parameterTypeFrom(*type, path + ".type");
                        if (isNew || parsed != parameter.type)
                        {
                            parameter.type = parsed;
                            parameter.defaultValue = zeroValue(parsed);
                        }
                    }
                    if (const nlohmann::json* value = member(item, "default"))
                    {
                        parameter.defaultValue = parameterValueFrom(parameter.type, *value, path + ".default");
                    }

                    (isNew ? summary.parametersAdded : summary.parametersUpdated).push_back(name);
                    if (replace)
                    {
                        listed.push_back(std::move(parameter));
                    }
                    else if (isNew)
                    {
                        graph.parameters.push_back(std::move(parameter));
                    }
                    else
                    {
                        *existing = std::move(parameter);
                    }
                }

                if (replace)
                {
                    for (const AnimatorParameter& parameter : graph.parameters)
                    {
                        if (!names.contains(parameter.name))
                        {
                            summary.parametersRemoved.push_back(parameter.name);
                        }
                    }
                    graph.parameters = std::move(listed);
                }
            }

            asset::AssetRef clipFrom(const nlohmann::json& value, const std::string& path) const
            {
                const std::string text = env::requireString(value, path);
                return text.empty() ? asset::AssetRef::invalid() : resolveClip(text, path);
            }

            animator::BlendTreeData blendTreeFrom(const nlohmann::json& value, const std::string& path) const
            {
                env::rejectUnknownKeys(value, {"type", "parameter", "parameterY", "entries"}, path);
                const std::string type = env::requireString(requiredMember(value, "type", path), path + ".type");
                const bool twoD = env::detail::equalsIgnoreCase(type, "2D");
                if (!twoD && !env::detail::equalsIgnoreCase(type, "1D"))
                {
                    throw ArgError("'" + path + ".type' must be \"1D\" or \"2D\" (got \"" + type + "\")");
                }

                animator::BlendTreeData tree;
                tree.type = twoD ? animator::BlendTreeType::BlendTree2D : animator::BlendTreeType::BlendTree1D;
                tree.parameterName = requireName(requiredMember(value, "parameter", path), path + ".parameter");
                if (twoD)
                {
                    tree.parameterNameY = requireName(requiredMember(value, "parameterY", path), path + ".parameterY");
                }
                else if (member(value, "parameterY") != nullptr)
                {
                    throw ArgError("'" + path + ".parameterY' only applies to a 2D blend tree");
                }

                const nlohmann::json& entries = requiredMember(value, "entries", path);
                requireArray(entries, path + ".entries");
                for (std::size_t i = 0; i < entries.size(); ++i)
                {
                    const std::string entryPath = indexed(path + ".entries", i);
                    const nlohmann::json& entry = entries[i];
                    animator::BlendTreeEntry out;
                    if (twoD)
                    {
                        env::rejectUnknownKeys(entry, {"clip", "position"}, entryPath);
                        out.position = vec2From(requiredMember(entry, "position", entryPath), entryPath + ".position");
                    }
                    else
                    {
                        env::rejectUnknownKeys(entry, {"clip", "threshold"}, entryPath);
                        out.threshold = env::requireFloat(requiredMember(entry, "threshold", entryPath), -maxThreshold,
                                                          maxThreshold, entryPath + ".threshold");
                    }
                    const std::string clip = requireName(requiredMember(entry, "clip", entryPath), entryPath + ".clip");
                    out.animationRef = resolveClip(clip, entryPath + ".clip");
                    tree.entries.push_back(std::move(out));
                }
                return tree;
            }

            void editStates(const nlohmann::json& list, bool replace)
            {
                requireArray(list, "states");
                std::unordered_set<std::string> names;
                std::vector<uint32_t> order; // ids of the listed states, in list order
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    const std::string path = indexed("states", i);
                    const nlohmann::json& item = list[i];
                    env::rejectUnknownKeys(item, {"name", "rename", "clip", "playbackSpeed", "loop", "position", "blendTree"},
                                           path);
                    const std::string name = requireStateName(requiredMember(item, "name", path), path + ".name");
                    if (!names.insert(name).second)
                    {
                        throw ArgError("'" + path + "': state \"" + name + "\" is listed twice");
                    }

                    AnimatorState* state = graph.findStateByName(name);
                    const bool isNew = state == nullptr;
                    if (isNew)
                    {
                        AnimatorState created;
                        created.id = graph.nextStateId++;
                        created.name = name;
                        created.position = nextFreePosition(graph);
                        graph.states.push_back(std::move(created));
                        state = &graph.states.back();
                    }
                    else if (std::find(order.begin(), order.end(), state->id) != order.end())
                    {
                        throw ArgError("'" + path + "': state \"" + name +
                                       "\" was already edited earlier in this list (it was renamed to that name)");
                    }
                    order.push_back(state->id);

                    if (const nlohmann::json* rename = member(item, "rename"))
                    {
                        const std::string newName = requireStateName(*rename, path + ".rename");
                        if (newName != state->name)
                        {
                            if (graph.findStateByName(newName) != nullptr)
                            {
                                throw ArgError("'" + path + ".rename': a state named \"" + newName + "\" already exists");
                            }
                            summary.statesRenamed.emplace_back(state->name, newName);
                            state->name = newName;
                        }
                    }
                    if (const nlohmann::json* clip = member(item, "clip"))
                    {
                        state->animationRef = clipFrom(*clip, path + ".clip");
                    }
                    if (const nlohmann::json* speed = member(item, "playbackSpeed"))
                    {
                        state->playbackSpeed = requirePlaybackSpeed(*speed, path + ".playbackSpeed");
                    }
                    if (const nlohmann::json* loop = member(item, "loop"))
                    {
                        state->loop = env::requireBool(*loop, path + ".loop");
                    }
                    if (const nlohmann::json* position = member(item, "position"))
                    {
                        // Whole numbers: imgui-node-editor floors node positions (SetNodePosition) and the
                        // Animator Editor marks itself dirty when a node's position differs from the file's.
                        // Blend-tree entry positions are parameter values and stay exact.
                        state->position = glm::round(vec2From(*position, path + ".position"));
                    }
                    // An explicit null removes the blend tree (the state plays its clip again).
                    if (const auto tree = item.find("blendTree"); tree != item.end())
                    {
                        if (tree->is_null())
                        {
                            state->blendTree.reset();
                        }
                        else
                        {
                            state->blendTree = blendTreeFrom(*tree, path + ".blendTree");
                        }
                    }
                    (isNew ? summary.statesAdded : summary.statesUpdated).push_back(state->name);
                }

                if (replace)
                {
                    std::vector<uint32_t> unlisted;
                    for (const AnimatorState& state : graph.states)
                    {
                        if (std::find(order.begin(), order.end(), state.id) == order.end())
                        {
                            unlisted.push_back(state.id);
                        }
                    }
                    for (uint32_t id : unlisted)
                    {
                        summary.statesRemoved.push_back(graph.findStateById(id)->name);
                        dropState(id);
                    }
                    // State ids are unique (checked in apply), so each listed id names exactly one state.
                    std::vector<AnimatorState> ordered;
                    ordered.reserve(order.size());
                    for (uint32_t id : order)
                    {
                        ordered.push_back(std::move(*graph.findStateById(id)));
                    }
                    graph.states = std::move(ordered);
                }
            }

            void applyDefaultState(const nlohmann::json& edit)
            {
                if (const nlohmann::json* value = member(edit, "defaultState"))
                {
                    const std::string name = requireName(*value, "defaultState");
                    const AnimatorState* state = graph.findStateByName(name);
                    if (state == nullptr)
                    {
                        throw ArgError("'defaultState': no state named \"" + name + "\" in " + layerText() + " (states: " +
                                       stateList() + ")");
                    }
                    if (graph.defaultStateId != state->id)
                    {
                        summary.defaultState = name;
                    }
                    graph.defaultStateId = state->id;
                    return;
                }
                // With no state left there is nothing to name; validation reports the empty graph.
                if (defaultRemoved && !graph.states.empty())
                {
                    throw ArgError("the default state was removed: name the new default state in 'defaultState'");
                }
                if (graph.findStateById(graph.defaultStateId) == nullptr && !graph.states.empty())
                {
                    // A graph without a default state (a new file) starts in its first state.
                    graph.defaultStateId = graph.states.front().id;
                    summary.defaultState = graph.states.front().name;
                }
            }

            // The runtime's shared parameter map, which conditions of any layer may test: the single graph's
            // declarations, or every state-machine layer's in layer order with the first declaration of a name
            // winning (AnimationLayerStack::initializeSharedParameters, like validateAnimator).
            ParameterTypes parameterTypes() const
            {
                ParameterTypes types;
                if (data.layers.empty())
                {
                    for (const AnimatorParameter& parameter : graph.parameters)
                    {
                        types.try_emplace(parameter.name, parameter.type);
                    }
                    return types;
                }
                for (const animator::AnimationLayerData& layerData : data.layers)
                {
                    if (layerData.sourceMode != animator::LayerSourceMode::StateMachine)
                    {
                        continue;
                    }
                    for (const AnimatorParameter& parameter : layerData.graph.parameters)
                    {
                        types.try_emplace(parameter.name, parameter.type);
                    }
                }
                return types;
            }

            static std::string parameterList(const ParameterTypes& types)
            {
                std::vector<std::string> names;
                for (const auto& [name, type] : types)
                {
                    names.push_back(name);
                }
                std::sort(names.begin(), names.end());
                std::string out;
                for (const std::string& name : names)
                {
                    out += out.empty() ? name : ", " + name;
                }
                return out.empty() ? std::string("none") : out;
            }

            // A condition's value is converted by its parameter's type: Float a number, Int an integer, Bool a
            // boolean compared with == or !=, Trigger only == true (op and value may be omitted).
            animator::TransitionCondition conditionFrom(const nlohmann::json& value, const ParameterTypes& types,
                                                        const std::string& path) const
            {
                env::rejectUnknownKeys(value, {"parameter", "op", "value"}, path);
                animator::TransitionCondition condition;
                condition.parameterName = requireName(requiredMember(value, "parameter", path), path + ".parameter");
                const auto found = types.find(condition.parameterName);
                if (found == types.end())
                {
                    throw ArgError("'" + path + ".parameter': no parameter named \"" + condition.parameterName +
                                   "\"; declare it in 'parameters' (declared: " + parameterList(types) + ")");
                }
                const AnimatorParameterType type = found->second;
                const nlohmann::json* op = member(value, "op");
                const nlohmann::json* operand = member(value, "value");

                if (type == AnimatorParameterType::Trigger)
                {
                    condition.op = op != nullptr ? operatorFrom(*op, path + ".op") : ComparisonOperator::Equal;
                    const bool expected = operand != nullptr ? env::requireBool(*operand, path + ".value") : true;
                    if (condition.op != ComparisonOperator::Equal || !expected)
                    {
                        throw ArgError("'" + path + "': Trigger \"" + condition.parameterName +
                                       "\" is tested with op \"==\" and value true (or with op and value omitted)");
                    }
                    condition.value = AnimatorParameterValue(std::in_place_type<bool>, true);
                    return condition;
                }

                if (op == nullptr)
                {
                    throw ArgError("'" + path + ".op' is required: one of >, <, >=, <=, ==, !=");
                }
                if (operand == nullptr)
                {
                    throw ArgError("'" + path + ".value' is required (a " +
                                   std::string(animator::parameterTypeToString(type)) + " value)");
                }
                condition.op = operatorFrom(*op, path + ".op");
                if (type == AnimatorParameterType::Bool && condition.op != ComparisonOperator::Equal &&
                    condition.op != ComparisonOperator::NotEqual)
                {
                    throw ArgError("'" + path + ".op': Bool parameter \"" + condition.parameterName +
                                   "\" is compared with == or != only");
                }
                condition.value = parameterValueFrom(type, *operand, path + ".value");
                return condition;
            }

            void editTransitions(const nlohmann::json& list, bool replace)
            {
                requireArray(list, "transitions");
                const ParameterTypes types = parameterTypes();

                std::vector<AnimatorTransition> parsed;
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    const std::string path = indexed("transitions", i);
                    const nlohmann::json& item = list[i];
                    env::rejectUnknownKeys(item, {"from", "to", "conditions", "blendDuration", "hasExitTime", "exitTime",
                                                  "priority"}, path);
                    AnimatorTransition transition;
                    transition.sourceStateId =
                        stateId(requireName(requiredMember(item, "from", path), path + ".from"), true, path + ".from");
                    transition.targetStateId =
                        stateId(requireName(requiredMember(item, "to", path), path + ".to"), false, path + ".to");
                    if (const nlohmann::json* conditions = member(item, "conditions"))
                    {
                        requireArray(*conditions, path + ".conditions");
                        for (std::size_t c = 0; c < conditions->size(); ++c)
                        {
                            transition.conditions.push_back(
                                conditionFrom((*conditions)[c], types, indexed(path + ".conditions", c)));
                        }
                    }
                    if (const nlohmann::json* blend = member(item, "blendDuration"))
                    {
                        transition.blendDuration = env::requireFloat(*blend, 0.0, maxBlendDuration, path + ".blendDuration");
                    }
                    const nlohmann::json* exitTime = member(item, "exitTime");
                    if (exitTime != nullptr)
                    {
                        transition.exitTime = env::requireFloat(*exitTime, 0.0, 1.0, path + ".exitTime");
                    }
                    // Giving an exit time enables it unless hasExitTime says otherwise.
                    const nlohmann::json* hasExitTime = member(item, "hasExitTime");
                    transition.hasExitTime =
                        hasExitTime != nullptr ? env::requireBool(*hasExitTime, path + ".hasExitTime") : exitTime != nullptr;
                    if (const nlohmann::json* priority = member(item, "priority"))
                    {
                        transition.priority = static_cast<int32_t>(
                            env::detail::checkedInteger(*priority, minInt32, maxInt32, path + ".priority"));
                    }
                    parsed.push_back(std::move(transition));
                }

                if (replace)
                {
                    summary.transitionsRemoved += static_cast<uint32_t>(graph.transitions.size());
                    graph.transitions.clear();
                    for (AnimatorTransition& transition : parsed)
                    {
                        transition.id = graph.nextTransitionId++;
                        graph.transitions.push_back(std::move(transition));
                        ++summary.transitionsAdded;
                    }
                    return;
                }

                // Each listed (from, to) pair replaces all of that pair's transitions, in list order, where the
                // pair's first transition was (else at the end): equal priorities keep their evaluation order.
                std::vector<std::pair<uint32_t, uint32_t>> pairs;
                for (const AnimatorTransition& transition : parsed)
                {
                    const std::pair<uint32_t, uint32_t> key{transition.sourceStateId, transition.targetStateId};
                    if (std::find(pairs.begin(), pairs.end(), key) == pairs.end())
                    {
                        pairs.push_back(key);
                    }
                }
                for (const std::pair<uint32_t, uint32_t>& pair : pairs)
                {
                    const auto samePair = [from = pair.first, to = pair.second](const AnimatorTransition& transition)
                    {
                        return transition.sourceStateId == from && transition.targetStateId == to;
                    };
                    // Every transition of the pair sits at or after the first one, so erasing them leaves
                    // the index of the first one in place.
                    std::size_t insertAt = static_cast<std::size_t>(std::distance(
                        graph.transitions.begin(),
                        std::find_if(graph.transitions.begin(), graph.transitions.end(), samePair)));
                    summary.transitionsRemoved += static_cast<uint32_t>(std::erase_if(graph.transitions, samePair));
                    for (AnimatorTransition& transition : parsed)
                    {
                        if (!samePair(transition))
                        {
                            continue;
                        }
                        transition.id = graph.nextTransitionId++;
                        graph.transitions.insert(graph.transitions.begin() + static_cast<std::ptrdiff_t>(insertAt),
                                                 std::move(transition));
                        ++insertAt;
                        ++summary.transitionsAdded;
                    }
                }
            }

            AnimatorData& data;
            uint32_t layer;
            AnimatorGraph& graph;
            const ClipResolver& resolveClip;
            EditSummary summary;
            bool defaultRemoved = false; // the call removed the default state
        };

        template <typename Drives>
        ClipCoverage coverageOf(const services::MeshSkeletonInfo& skeleton, float minCoverage, const Drives& drives,
                                std::optional<std::size_t> root)
        {
            ClipCoverage out;
            out.skeletonBones = static_cast<uint32_t>(skeleton.boneNames.size());
            for (const std::string& bone : skeleton.boneNames)
            {
                if (drives(bone))
                {
                    ++out.matchedBones;
                }
            }
            out.coverage = out.skeletonBones == 0
                ? 0.0f
                : static_cast<float>(out.matchedBones) / static_cast<float>(out.skeletonBones);
            out.rootMatched = root.has_value() && *root < skeleton.boneNames.size() && drives(skeleton.boneNames[*root]);
            out.compatible = out.rootMatched && out.coverage >= minCoverage;
            return out;
        }
    }

    // ------------------------------------------------------------------
    // Agent JSON
    // ------------------------------------------------------------------

    nlohmann::json parameterValueJson(const animator::AnimatorParameterValue& value)
    {
        if (const float* number = std::get_if<float>(&value))
        {
            return env::floatJson(*number);
        }
        if (const int32_t* integer = std::get_if<int32_t>(&value))
        {
            return *integer;
        }
        return std::get<bool>(value);
    }

    nlohmann::json parameterJson(const animator::AnimatorParameter& parameter)
    {
        return {
            {"name", parameter.name},
            {"type", animator::parameterTypeToString(parameter.type)},
            {"default", parameterValueJson(parameter.defaultValue)}
        };
    }

    nlohmann::json stateRefJson(const animator::AnimatorGraph& graph, uint32_t stateId)
    {
        if (stateId == 0)
        {
            return std::string(anyStateName);
        }
        const animator::AnimatorState* state = graph.findStateById(stateId);
        return state != nullptr ? nlohmann::json(state->name) : nlohmann::json(nullptr);
    }

    nlohmann::json stateJson(const animator::AnimatorState& state, const ClipNamer& clipName)
    {
        nlohmann::json out{
            {"id", state.id},
            {"name", state.name},
            {"clip", clipName(state.animationRef)},
            {"playbackSpeed", env::floatJson(state.playbackSpeed)},
            {"loop", state.loop},
            {"position", vec2Json(state.position)},
            {"events", state.events.size()}
        };
        if (state.blendTree.has_value())
        {
            out["blendTree"] = blendTreeJson(*state.blendTree, clipName);
        }
        return out;
    }

    nlohmann::json transitionJson(const animator::AnimatorGraph& graph, const animator::AnimatorTransition& transition)
    {
        nlohmann::json conditions = nlohmann::json::array();
        for (const animator::TransitionCondition& condition : transition.conditions)
        {
            conditions.push_back(nlohmann::json{
                {"parameter", condition.parameterName},
                {"op", animator::comparisonOperatorToString(condition.op)},
                {"value", parameterValueJson(condition.value)}
            });
        }
        return {
            {"id", transition.id},
            {"from", stateRefJson(graph, transition.sourceStateId)},
            {"to", transition.targetStateId == 0 ? nlohmann::json(nullptr) : stateRefJson(graph, transition.targetStateId)},
            {"conditions", std::move(conditions)},
            {"blendDuration", env::floatJson(transition.blendDuration)},
            {"hasExitTime", transition.hasExitTime},
            {"exitTime", env::floatJson(transition.exitTime)},
            {"priority", transition.priority}
        };
    }

    nlohmann::json graphJson(const animator::AnimatorGraph& graph, const ClipNamer& clipName)
    {
        nlohmann::json parameters = nlohmann::json::array();
        for (const animator::AnimatorParameter& parameter : graph.parameters)
        {
            parameters.push_back(parameterJson(parameter));
        }
        nlohmann::json states = nlohmann::json::array();
        for (const animator::AnimatorState& state : graph.states)
        {
            states.push_back(stateJson(state, clipName));
        }
        nlohmann::json transitions = nlohmann::json::array();
        for (const animator::AnimatorTransition& transition : graph.transitions)
        {
            transitions.push_back(transitionJson(graph, transition));
        }
        const animator::AnimatorState* defaultState = graph.findStateById(graph.defaultStateId);
        return {
            {"defaultState", defaultState != nullptr ? nlohmann::json(defaultState->name) : nlohmann::json(nullptr)},
            {"parameters", std::move(parameters)},
            {"states", std::move(states)},
            {"transitions", std::move(transitions)}
        };
    }

    nlohmann::json layersJson(const animator::AnimatorData& data, const ClipNamer& clipName)
    {
        nlohmann::json layers = nlohmann::json::array();
        if (data.layers.empty())
        {
            const animator::AnimationLayerData base{}; // what the runtime plays a single graph as
            layers.push_back(nlohmann::json{
                {"index", 0},
                {"name", base.name},
                {"sourceMode", animator::layerSourceModeToString(base.sourceMode)},
                {"weight", env::floatJson(base.weight)},
                {"blendMode", animator::layerBlendModeToString(base.blendMode)},
                {"boneMask", nullptr},
                {"states", data.graph.states.size()}
            });
            return layers;
        }
        for (std::size_t i = 0; i < data.layers.size(); ++i)
        {
            const animator::AnimationLayerData& layer = data.layers[i];
            const bool directClip = layer.sourceMode == animator::LayerSourceMode::DirectClip;
            nlohmann::json item{
                {"index", i},
                {"name", layer.name},
                {"sourceMode", animator::layerSourceModeToString(layer.sourceMode)},
                {"weight", env::floatJson(layer.weight)},
                {"blendMode", animator::layerBlendModeToString(layer.blendMode)},
                {"boneMask", layer.boneMaskName.empty() ? nlohmann::json(nullptr) : nlohmann::json(layer.boneMaskName)},
                {"states", directClip ? nlohmann::json(nullptr) : nlohmann::json(layer.graph.states.size())}
            };
            if (directClip)
            {
                item["directClip"] = clipName(layer.directClipRef);
            }
            layers.push_back(std::move(item));
        }
        return layers;
    }

    nlohmann::json issuesJson(const std::vector<animator::GraphIssue>& issues)
    {
        nlohmann::json out = nlohmann::json::array();
        for (const animator::GraphIssue& issue : issues)
        {
            out.push_back(nlohmann::json{
                {"level", issue.level == animator::GraphIssue::Level::Error ? "error" : "warning"},
                {"layer", issue.layer < 0 ? nlohmann::json(nullptr) : nlohmann::json(issue.layer)},
                {"message", issue.message}
            });
        }
        return out;
    }

    std::string errorLines(const std::vector<animator::GraphIssue>& issues)
    {
        std::string out;
        for (const animator::GraphIssue& issue : issues)
        {
            if (issue.level != animator::GraphIssue::Level::Error)
            {
                continue;
            }
            out += out.empty() ? "- " : "\n- ";
            if (issue.layer >= 0)
            {
                out += "[layer " + std::to_string(issue.layer) + "] ";
            }
            out += issue.message;
        }
        return out;
    }

    nlohmann::json parameterSummaryJson(const animator::AnimatorData& data)
    {
        nlohmann::json out = nlohmann::json::array();
        std::unordered_set<std::string> seen;
        if (data.layers.empty())
        {
            collectParameterSummary(data.graph, seen, out);
            return out;
        }
        for (const animator::AnimationLayerData& layer : data.layers)
        {
            if (layer.sourceMode == animator::LayerSourceMode::StateMachine)
            {
                collectParameterSummary(layer.graph, seen, out);
            }
        }
        return out;
    }

    const animator::AnimatorGraph& stateMachineLayer(const animator::AnimatorData& data, uint32_t layer)
    {
        if (data.layers.empty())
        {
            if (layer != 0)
            {
                throw ArgError("layer " + std::to_string(layer) +
                               " does not exist: this animator has a single state machine (layer 0)");
            }
            return data.graph;
        }
        if (layer >= data.layers.size())
        {
            throw ArgError("layer " + std::to_string(layer) + " does not exist: this animator has " +
                           std::to_string(data.layers.size()) + " layers (0-" + std::to_string(data.layers.size() - 1) + ")");
        }
        const animator::AnimationLayerData& selected = data.layers[layer];
        if (selected.sourceMode != animator::LayerSourceMode::StateMachine)
        {
            throw ArgError("layer " + std::to_string(layer) + " ('" + selected.name +
                           "') plays a single clip (DirectClip) and has no state machine");
        }
        return selected.graph;
    }

    animator::AnimatorGraph& stateMachineLayer(animator::AnimatorData& data, uint32_t layer)
    {
        return const_cast<animator::AnimatorGraph&>(stateMachineLayer(std::as_const(data), layer));
    }

    const animator::AnimatorGraph& baseGraph(const animator::AnimatorData& data)
    {
        return data.layers.empty() ? data.graph : data.layers.front().graph;
    }

    // ------------------------------------------------------------------
    // Building and editing
    // ------------------------------------------------------------------

    std::string requireStateName(const nlohmann::json& value, const std::string& path)
    {
        std::string name = requireName(value, path);
        if (name == anyStateName)
        {
            throw ArgError("'" + path + "': \"Any\" is reserved for the Any State; pick another state name");
        }
        return name;
    }

    float requirePlaybackSpeed(const nlohmann::json& value, const std::string& path)
    {
        const float speed = env::requireFloat(value, 0.0, maxPlaybackSpeed, path);
        if (speed <= 0.0f)
        {
            throw ArgError("'" + path + "' must be greater than 0 (got " + env::detail::valueText(value) + ")");
        }
        return speed;
    }

    animator::AnimatorParameterValue parameterValueFrom(animator::AnimatorParameterType type, const nlohmann::json& value,
                                                        const std::string& path)
    {
        switch (type)
        {
        case animator::AnimatorParameterType::Float:
            return animator::AnimatorParameterValue(std::in_place_type<float>,
                                                    env::requireFloat(value, -maxFloatValue, maxFloatValue, path));
        case animator::AnimatorParameterType::Int:
            return animator::AnimatorParameterValue(
                std::in_place_type<int32_t>, static_cast<int32_t>(env::detail::checkedInteger(value, minInt32, maxInt32, path)));
        case animator::AnimatorParameterType::Bool:
        case animator::AnimatorParameterType::Trigger:
            return animator::AnimatorParameterValue(std::in_place_type<bool>, env::requireBool(value, path));
        }
        throw std::logic_error("unknown animator parameter type");
    }

    const animator::AnimatorParameter* findParameter(const animator::AnimatorData& data, const std::string& name)
    {
        if (data.layers.empty())
        {
            return data.graph.findParameter(name);
        }
        for (const animator::AnimationLayerData& layer : data.layers)
        {
            if (layer.sourceMode != animator::LayerSourceMode::StateMachine)
            {
                continue;
            }
            if (const animator::AnimatorParameter* parameter = layer.graph.findParameter(name))
            {
                return parameter;
            }
        }
        return nullptr;
    }

    glm::vec2 nextFreePosition(const animator::AnimatorGraph& graph)
    {
        const auto occupied = [&graph](const glm::vec2& cell)
        {
            const auto within = [&cell](const glm::vec2& node) { return glm::distance(cell, node) < layoutClearance; };
            return within(graph.entryPosition) || within(graph.anyStatePosition) ||
                   std::any_of(graph.states.begin(), graph.states.end(),
                               [&within](const animator::AnimatorState& state) { return within(state.position); });
        };
        // Cells are 250 apart across and 150 down, so a node can block at most two of them (one above the
        // other): one of the first 2 * nodes + 1 cells is free.
        const std::size_t cells = 2 * (graph.states.size() + 2) + 1;
        const auto columns = static_cast<std::size_t>(layoutColumns);
        glm::vec2 cell = layoutOrigin;
        for (std::size_t i = 0; i < cells; ++i)
        {
            cell = layoutOrigin + layoutStep * glm::vec2(static_cast<float>(i % columns), static_cast<float>(i / columns));
            if (!occupied(cell))
            {
                return cell;
            }
        }
        return cell;
    }

    animator::AnimatorData emptyAnimator(std::string name)
    {
        animator::AnimatorData data;
        data.name = std::move(name);
        // The node layout of the Animator Editor's new file (AnimatorAsset::createDefault).
        data.graph.entryPosition = glm::vec2(50.0f, 100.0f);
        data.graph.anyStatePosition = glm::vec2(50.0f, 250.0f);
        return data;
    }

    animator::AnimatorData newAnimator(std::string name, std::string stateName, const asset::AssetRef& clip,
                                       float playbackSpeed, bool loop)
    {
        animator::AnimatorData data = emptyAnimator(std::move(name));
        animator::AnimatorState state;
        state.id = data.graph.nextStateId++;
        state.name = std::move(stateName);
        state.animationRef = clip;
        state.playbackSpeed = playbackSpeed;
        state.loop = loop;
        state.position = nextFreePosition(data.graph);
        data.graph.defaultStateId = state.id;
        data.graph.states.push_back(std::move(state));
        return data;
    }

    nlohmann::json EditSummary::toJson() const
    {
        nlohmann::json renamed = nlohmann::json::array();
        for (const auto& [from, to] : statesRenamed)
        {
            renamed.push_back(nlohmann::json{{"from", from}, {"to", to}});
        }
        return {
            {"parametersAdded", parametersAdded},
            {"parametersUpdated", parametersUpdated},
            {"parametersRemoved", parametersRemoved},
            {"statesAdded", statesAdded},
            {"statesUpdated", statesUpdated},
            {"statesRemoved", statesRemoved},
            {"statesRenamed", std::move(renamed)},
            {"transitionsAdded", transitionsAdded},
            {"transitionsRemoved", transitionsRemoved},
            {"defaultState", defaultState.has_value() ? nlohmann::json(*defaultState) : nlohmann::json(nullptr)}
        };
    }

    EditSummary applyAnimatorEdit(animator::AnimatorData& data, const nlohmann::json& edit, uint32_t layer,
                                  const ClipResolver& resolveClip)
    {
        return GraphEdit(data, layer, resolveClip).apply(edit);
    }

    std::size_t animatorDataBytes(const animator::AnimatorData& data)
    {
        std::size_t bytes = sizeof(animator::AnimatorData) + data.version.capacity() + data.name.capacity() +
                            graphBytes(data.graph) + data.layers.capacity() * sizeof(animator::AnimationLayerData) +
                            data.boneMasks.capacity() * sizeof(animator::BoneMaskDefinition);
        for (const animator::AnimationLayerData& layer : data.layers)
        {
            bytes += layer.name.capacity() + layer.boneMaskName.capacity() + graphBytes(layer.graph);
        }
        for (const animator::BoneMaskDefinition& mask : data.boneMasks)
        {
            bytes += mask.name.capacity() + mask.includedBoneNames.capacity() * sizeof(std::string);
            for (const std::string& bone : mask.includedBoneNames)
            {
                bytes += bone.capacity();
            }
        }
        return bytes;
    }

    // ------------------------------------------------------------------
    // Clip compatibility
    // ------------------------------------------------------------------

    std::optional<std::size_t> firstRootBone(const services::MeshSkeletonInfo& skeleton)
    {
        const std::size_t count = std::min(skeleton.boneNames.size(), skeleton.parents.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (skeleton.parents[i] < 0)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> retargetRootBone(const services::MeshSkeletonInfo& skeleton,
                                                const retargeting::RetargetBoneMapping& mapping)
    {
        for (std::size_t i = 0; i < skeleton.boneNames.size(); ++i)
        {
            const std::optional<retargeting::RetargetBoneMatch> match = mapping.match(skeleton.boneNames[i]);
            if (match.has_value() && match->target->role == retargeting::HumanoidBoneRole::Hips)
            {
                return i;
            }
        }
        return firstRootBone(skeleton);
    }

    ClipCoverage nativeCoverage(const services::MeshSkeletonInfo& skeleton, const std::vector<std::string>& channels,
                                float minCoverage)
    {
        const std::unordered_set<std::string> driven(channels.begin(), channels.end());
        return coverageOf(skeleton, minCoverage, [&driven](const std::string& bone) { return driven.contains(bone); },
                          firstRootBone(skeleton));
    }

    ClipCoverage retargetCoverage(const services::MeshSkeletonInfo& skeleton,
                                  const retargeting::RetargetBoneMapping& mapping,
                                  const std::vector<std::string>& channels, float minCoverage)
    {
        const std::unordered_set<std::string> driven(channels.begin(), channels.end());
        return coverageOf(skeleton, minCoverage, [&driven, &mapping](const std::string& bone)
        {
            const std::optional<retargeting::RetargetBoneMatch> match = mapping.match(bone);
            return match.has_value() && match->source != nullptr && driven.contains(match->source->boneName);
        }, retargetRootBone(skeleton, mapping));
    }

    // ------------------------------------------------------------------
    // Engine access
    // ------------------------------------------------------------------

    std::optional<services::MeshData> meshData(const services::EntityHandle& entity)
    {
        events::scene::GetMeshDataQuery query;
        query.entity = entity;
        return events::EventDispatcher::instance().query(query);
    }

    services::MeshSkeletonInfo meshSkeleton(const std::string& meshPath)
    {
        events::animationAuthoring::GetMeshSkeletonQuery query;
        query.meshPath = meshPath;
        return events::EventDispatcher::instance().query(query);
    }

    services::AnimatorAssetLoadResult loadAnimator(const std::string& path)
    {
        events::animationAuthoring::LoadAnimatorAssetQuery query;
        query.path = path;
        return events::EventDispatcher::instance().query(query);
    }

    std::optional<std::string> animatorHostProblem(const services::EntityHandle& entity)
    {
        const std::string id = std::to_string(static_cast<uint32_t>(entity.id));
        const std::optional<services::MeshData> mesh = meshData(entity);
        if (!mesh.has_value())
        {
            return "entity " + id + " has no Mesh component (add one with component_add {type:\"Mesh\", data:{meshRef}})";
        }
        if (!mesh->meshRef.isValid())
        {
            return "the Mesh of entity " + id + " has no meshRef";
        }
        const std::string path = mesh->meshRef.resolve();
        if (path.empty())
        {
            throw std::runtime_error("The mesh asset of entity " + id + " (guid:" + mesh->meshRef.toHexString() +
                                     ") cannot be found");
        }
        const services::MeshSkeletonInfo skeleton = meshSkeleton(path);
        if (!skeleton.success)
        {
            throw std::runtime_error("The skeleton of '" + path + "' cannot be read: " + skeleton.error);
        }
        if (!skeleton.skinned)
        {
            return "the mesh of entity " + id + " ('" + path + "') is static (it has no skeleton); an Animator needs a "
                   "skinned mesh";
        }
        return std::nullopt;
    }

    asset::AssetRef resolveClip(const fs::path& root, const std::string& path, const std::string& argument)
    {
        std::string error;
        const std::optional<fs::path> file = resolveInside(root, path, error, ".vfAnim");
        if (!file.has_value())
        {
            throw ArgError("'" + argument + "': " + error);
        }
        std::error_code ec;
        if (!fs::is_regular_file(*file, ec))
        {
            throw ArgError("'" + argument + "': no clip at '" + path +
                           "' (animation_list_clips lists the clips that fit a mesh)");
        }
        asset::AssetRef ref = asset::AssetRef::fromPath(file->string());
        if (!ref.isValid())
        {
            throw ArgError("'" + argument + "': '" + path + "' is not a registered animation clip");
        }
        return ref;
    }

    nlohmann::json assetPathJson(const std::optional<fs::path>& root, const asset::AssetRef& ref)
    {
        if (!ref.isValid())
        {
            return nullptr;
        }
        const std::string& path = ref.resolve();
        if (path.empty())
        {
            return "guid:" + ref.toHexString();
        }
        return projectRelativeUtf8(root, fs::path(path));
    }

    ClipNamer projectNamer(std::optional<fs::path> root)
    {
        return [root = std::move(root)](const asset::AssetRef& ref) { return assetPathJson(root, ref); };
    }

    fs::path defaultAnimatorPath(const services::EntityHandle& entity, const fs::path& root, std::string_view explicitHint)
    {
        const std::optional<services::MeshData> mesh = meshData(entity);
        const std::string meshPath = mesh.has_value() && mesh->meshRef.isValid() ? mesh->meshRef.resolve() : std::string();
        if (meshPath.empty())
        {
            throw std::runtime_error("Entity " + std::to_string(static_cast<uint32_t>(entity.id)) +
                                     " has no resolvable mesh asset to create an animator next to; " +
                                     std::string(explicitHint));
        }
        const fs::path meshFile(meshPath);
        std::string error;
        const std::optional<fs::path> folder = resolveInside(root, pathToUtf8(meshFile.parent_path()), error);
        if (!folder.has_value())
        {
            throw ArgError("The mesh '" + genericPathToUtf8(meshFile) +
                           "' lies outside the project, so no animator can be created next to it; " +
                           std::string(explicitHint));
        }
        const std::optional<services::EntityData> data = findEntity(entity);
        const std::string stem = strutil::toSafeFileStem(data.has_value() ? data->name : std::string(), "Animator");
        return uniqueFilePath(*folder, stem, ".vfAnimator");
    }

    services::AnimatorAssetSaveResult createAnimatorFile(const fs::path& file, const animator::AnimatorData& data)
    {
        events::animationAuthoring::SaveAnimatorAssetCommand command;
        command.path = file.string();
        command.data = data;
        command.createNew = true;
        services::AnimatorAssetSaveResult result = events::EventDispatcher::instance().execute(command);
        if (!result.success)
        {
            throw std::runtime_error("Creating the animator '" + pathToUtf8(file) + "' failed: " + result.error);
        }
        return result;
    }

    std::vector<services::EntityHandle> animatorUsers(const asset::AssetRef& animator)
    {
        std::vector<services::EntityHandle> users;
        if (!animator.isValid())
        {
            return users;
        }
        events::scene::GetEntitiesWithComponentQuery query;
        query.componentType = services::ComponentTypeId::Mesh;
        for (const services::EntityHandle& entity : events::EventDispatcher::instance().query(query))
        {
            const std::optional<services::MeshData> mesh = meshData(entity);
            if (mesh.has_value() && mesh->animatorRef == animator)
            {
                users.push_back(entity);
            }
        }
        return users;
    }

    nlohmann::json usersJson(const std::vector<services::EntityHandle>& users)
    {
        nlohmann::json out = nlohmann::json::array();
        for (const services::EntityHandle& entity : users)
        {
            const std::optional<services::EntityData> data = findEntity(entity);
            out.push_back(nlohmann::json{
                {"entity", toId(entity)},
                {"name", data.has_value() ? nlohmann::json(data->name) : nlohmann::json(nullptr)}
            });
        }
        return out;
    }
}
