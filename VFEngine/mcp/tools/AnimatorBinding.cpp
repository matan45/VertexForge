#include "ComponentBinding.hpp"
#include "AnimationToolSupport.hpp"
#include "EnvironmentToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentMediaEvents.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

// VK-1656: the "Animator" component binding. The engine keeps an entity's animator in its Mesh component
// (MeshData animatorRef / retargetRef / applyRootMotion); this binding presents those as a component of
// their own (present while animatorRef is set) and adds read-only facts about the assigned .vfAnimator.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view animatorExtension = ".vfAnimator";
        constexpr std::string_view retargetExtension = ".vfretarget";

        constexpr std::array<std::string_view, 3> writableFields{"animatorRef", "retargetRef", "applyRootMotion"};
        constexpr std::array<std::string_view, 8> readOnlyFields{"skeleton", "defaultState", "defaultClip", "playbackSpeed",
                                                                 "loop", "states", "parameters", "layers"};
        // Read-only fields component_add consumes when it creates the .vfAnimator.
        constexpr std::array<std::string_view, 4> seedFields{"defaultClip", "defaultState", "playbackSpeed", "loop"};

        constexpr const char* readOnlyHint =
            "Writable fields: animatorRef, retargetRef, applyRootMotion. Edit the animator's states, clips, "
            "parameters and transitions with animation_set_state_machine; defaultClip, defaultState, playbackSpeed "
            "and loop are accepted only by component_add when it creates a new .vfAnimator";

        template <std::size_t N>
        bool listed(const std::array<std::string_view, N>& fields, std::string_view field)
        {
            return std::find(fields.begin(), fields.end(), field) != fields.end();
        }

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }

        // A file the agent names: absolute, or relative to the project asset root.
        fs::path agentFile(const std::string& text)
        {
            fs::path path = pathFromUtf8(text);
            if (path.is_relative())
            {
                if (const std::optional<fs::path> root = tryTerrainProjectRoot())
                {
                    path = *root / path;
                }
            }
            return path.lexically_normal();
        }

        bool hasExtension(const fs::path& path, std::string_view extension)
        {
            return path.has_filename() && detail::componentEquals(path.extension(), pathFromUtf8(extension));
        }

        // An asset field: "" (or null) clears, "guid:<hex>" names a registered asset, anything else is the path
        // of an existing file with `extension`, absolute or project-relative. The file is checked before
        // AssetRef::fromPath, which would register a missing path.
        asset::AssetRef assetFieldFromJson(const nlohmann::json& value, const char* key, std::string_view extension)
        {
            const std::string type(extension);
            if (value.is_null())
            {
                return asset::AssetRef::invalid();
            }
            if (!value.is_string())
            {
                throw ArgError(std::string("field '") + key + "' must be a " + type + " path string");
            }
            const std::string text = value.get<std::string>();
            if (text.empty())
            {
                return asset::AssetRef::invalid();
            }

            static constexpr std::string_view guidPrefix = "guid:";
            if (text.starts_with(guidPrefix))
            {
                asset::AssetRef ref = asset::AssetRef::fromHexString(text.substr(guidPrefix.size()));
                if (!ref.isValid())
                {
                    throw ArgError(std::string("field '") + key + "': '" + text + "' is not a valid asset GUID");
                }
                // A GUID whose file cannot be found stays assignable, as component_get reports it.
                const std::string& path = ref.resolve();
                if (!path.empty() && !hasExtension(fs::path(path), extension))
                {
                    throw ArgError(std::string("field '") + key + "': '" + text + "' is not a " + type + " asset");
                }
                return ref;
            }

            const fs::path file = agentFile(text);
            if (!hasExtension(file, extension))
            {
                throw ArgError(std::string("field '") + key + "' must be a " + type + " file (got '" + text + "')");
            }
            std::error_code ec;
            if (!fs::is_regular_file(file, ec))
            {
                throw ArgError(std::string("field '") + key + "': '" + text + "' does not exist");
            }
            asset::AssetRef ref = asset::AssetRef::fromPath(file.string());
            if (!ref.isValid())
            {
                throw ArgError(std::string("field '") + key + "': '" + text + "' is not an engine asset");
            }
            return ref;
        }

        // Unknown and read-only keys are refused before anything is read or written.
        void checkFields(const nlohmann::json& patch)
        {
            if (!patch.is_object())
            {
                throw ArgError("the Animator data must be an object of field -> value");
            }
            for (auto it = patch.begin(); it != patch.end(); ++it)
            {
                if (listed(writableFields, it.key()))
                {
                    continue;
                }
                if (listed(readOnlyFields, it.key()))
                {
                    throw ArgError("field '" + it.key() + "' of Animator is read-only. " + readOnlyHint);
                }
                throw ArgError("unknown Animator field '" + it.key() +
                               "'; valid fields: animatorRef, retargetRef, applyRootMotion");
            }
        }

        // Asset fields are reported as UTF-8 paths, the encoding agentFile reads back (engine paths are narrow
        // strings in the process code page), so a replayed get() result (undo) names the same file.
        nlohmann::json assetJson(const asset::AssetRef& ref)
        {
            if (!ref.isValid())
            {
                return "";
            }
            const std::string& path = ref.resolve();
            return path.empty() ? "guid:" + ref.toHexString() : genericPathToUtf8(fs::path(path));
        }

        nlohmann::json skeletonJson(const services::MeshData& mesh)
        {
            nlohmann::json out{{"mesh", assetJson(mesh.meshRef)}, {"bones", nullptr}, {"root", nullptr}};
            const std::string path = mesh.meshRef.isValid() ? mesh.meshRef.resolve() : std::string();
            if (path.empty())
            {
                return out;
            }
            try
            {
                const services::MeshSkeletonInfo skeleton = anim::meshSkeleton(path);
                if (skeleton.success)
                {
                    out["bones"] = skeleton.boneNames.size();
                    if (const std::optional<std::size_t> root = anim::firstRootBone(skeleton))
                    {
                        out["root"] = skeleton.boneNames[*root];
                    }
                }
            }
            catch (const std::runtime_error&)
            {
                // No authoring handler: the facts stay null, the assignment itself is still reported.
            }
            return out;
        }

        // The read-only facts about the assigned .vfAnimator; they stay null when it cannot be read
        // (animation_get_state_machine reports why).
        void addAnimatorFacts(const services::MeshData& mesh, nlohmann::json& out)
        {
            const std::string path = mesh.animatorRef.resolve();
            if (path.empty())
            {
                return;
            }
            services::AnimatorAssetLoadResult loaded;
            try
            {
                loaded = anim::loadAnimator(path);
            }
            catch (const std::runtime_error&)
            {
                return;
            }
            if (!loaded.success)
            {
                return;
            }

            const animator::AnimatorGraph& base = anim::baseGraph(loaded.data);
            nlohmann::json states = nlohmann::json::array();
            for (const animator::AnimatorState& state : base.states)
            {
                states.push_back(state.name);
            }
            out["states"] = std::move(states);
            out["parameters"] = anim::parameterSummaryJson(loaded.data);
            out["layers"] = loaded.data.layers.empty() ? std::size_t{1} : loaded.data.layers.size();
            if (const animator::AnimatorState* state = base.findStateById(base.defaultStateId))
            {
                out["defaultState"] = state->name;
                out["defaultClip"] = state->animationRef.isValid() ? assetJson(state->animationRef) : nlohmann::json(nullptr);
                out["playbackSpeed"] = env::floatJson(state->playbackSpeed);
                out["loop"] = state->loop;
            }
        }

        std::optional<nlohmann::json> getAnimator(const services::EntityHandle& entity)
        {
            const std::optional<services::MeshData> mesh = anim::meshData(entity);
            if (!mesh.has_value() || !mesh->animatorRef.isValid())
            {
                return std::nullopt;
            }
            // The key set stays the same whatever can be read.
            nlohmann::json out{
                {"animatorRef", assetJson(mesh->animatorRef)},
                {"retargetRef", assetJson(mesh->retargetRef)},
                {"applyRootMotion", mesh->applyRootMotion},
                {"skeleton", skeletonJson(*mesh)},
                {"defaultState", nullptr},
                {"defaultClip", nullptr},
                {"playbackSpeed", nullptr},
                {"loop", nullptr},
                {"states", nullptr},
                {"parameters", nullptr},
                {"layers", nullptr}
            };
            addAnimatorFacts(*mesh, out);
            return out;
        }

        // Pure validation: an Animator needs a skinned Mesh. Nothing is added; patch() assigns the animator.
        bool addAnimator(const services::EntityHandle& entity)
        {
            return !anim::animatorHostProblem(entity).has_value();
        }

        bool removeAnimator(const services::EntityHandle& entity)
        {
            const std::optional<services::MeshData> mesh = anim::meshData(entity);
            // Nothing assigned, nothing to clear: component_add's rollback must keep a retargetRef the entity
            // had before.
            if (!mesh.has_value() || !mesh->animatorRef.isValid())
            {
                return true;
            }
            events::scene::SetMeshDataCommand command;
            command.entity = entity;
            command.meshData = *mesh;
            command.meshData.animatorRef = asset::AssetRef::invalid();
            command.meshData.retargetRef = asset::AssetRef::invalid();
            command.meshData.applyRootMotion = false;
            return events::EventDispatcher::instance().execute(command);
        }

        // Also applies while no animator is assigned (redo of a removal, undo of an add's removal): an
        // animatorRef in the patch makes the Animator present again.
        std::optional<nlohmann::json> patchAnimator(const services::EntityHandle& entity, const nlohmann::json& patch)
        {
            checkFields(patch);
            const std::optional<services::MeshData> mesh = anim::meshData(entity);
            if (!mesh.has_value())
            {
                return std::nullopt;
            }

            services::MeshData data = *mesh;
            if (const auto it = patch.find("animatorRef"); it != patch.end())
            {
                data.animatorRef = assetFieldFromJson(*it, "animatorRef", animatorExtension);
            }
            if (const auto it = patch.find("retargetRef"); it != patch.end())
            {
                data.retargetRef = assetFieldFromJson(*it, "retargetRef", retargetExtension);
            }
            readField(patch, "applyRootMotion", data.applyRootMotion);
            if (!data.animatorRef.isValid())
            {
                throw ArgError(mesh->animatorRef.isValid()
                    ? "field 'animatorRef' cannot be cleared with component_set: remove the Animator with "
                      "component_remove {type:\"Animator\"}"
                    : "entity " + idText(entity) + " has no animator assigned: pass animatorRef (a .vfAnimator path)");
            }

            events::scene::SetMeshDataCommand command;
            command.entity = entity;
            command.meshData = data;
            if (!events::EventDispatcher::instance().execute(command))
            {
                throw std::runtime_error("The engine rejected the Animator data of entity " + idText(entity));
            }
            std::optional<nlohmann::json> written = getAnimator(entity);
            if (!written.has_value())
            {
                throw std::runtime_error("Entity " + idText(entity) + " did not keep its animator assignment");
            }
            return written;
        }

        // True when the agent's animatorRef names an asset that already exists (a GUID, or a file on disk).
        bool animatorExists(const std::string& text)
        {
            if (text.starts_with("guid:"))
            {
                return true;
            }
            std::error_code ec;
            return fs::exists(agentFile(text), ec);
        }

        // component_add: consumes the creation seeds and, unless data.animatorRef names an existing animator,
        // creates a one-state .vfAnimator (next to the mesh, or at data.animatorRef) and patches it in.
        nlohmann::json prepareAnimatorAdd(const services::EntityHandle& entity, const nlohmann::json& data)
        {
            if (!data.is_object())
            {
                throw ArgError("argument 'data' must be an object of field -> value");
            }
            nlohmann::json patch = data;
            nlohmann::json seeds = nlohmann::json::object();
            for (std::string_view key : seedFields)
            {
                const std::string name(key);
                if (const auto it = patch.find(name); it != patch.end())
                {
                    if (!it->is_null())
                    {
                        seeds[name] = *it;
                    }
                    patch.erase(it);
                }
            }
            // A new Animator has nothing to clear: an empty animatorRef means "none given".
            if (const auto it = patch.find("animatorRef");
                it != patch.end() && (it->is_null() || (it->is_string() && it->get_ref<const std::string&>().empty())))
            {
                patch.erase(it);
            }

            // Everything else is validated before a file is created.
            checkFields(patch);
            if (const auto it = patch.find("retargetRef"); it != patch.end())
            {
                assetFieldFromJson(*it, "retargetRef", retargetExtension);
            }
            if (const auto it = patch.find("applyRootMotion"); it != patch.end() && !it->is_boolean())
            {
                throw ArgError("field 'applyRootMotion' must be a boolean");
            }

            std::optional<std::string> requested;
            if (const auto it = patch.find("animatorRef"); it != patch.end())
            {
                if (!it->is_string())
                {
                    throw ArgError("field 'animatorRef' must be a .vfAnimator path string");
                }
                requested = it->get<std::string>();
            }

            if (requested.has_value() && animatorExists(*requested))
            {
                if (!seeds.empty())
                {
                    throw ArgError("defaultClip, defaultState, playbackSpeed and loop apply only when component_add "
                                   "creates the animator, and '" + *requested + "' already exists; edit it with "
                                   "animation_set_state_machine");
                }
                return patch;
            }

            if (const std::optional<std::string> blocked = env::editBlocker("create an animator"))
            {
                throw std::runtime_error(*blocked);
            }
            const fs::path root = terrainProjectRoot();
            const auto clip = seeds.find("defaultClip");
            if (clip == seeds.end())
            {
                throw ArgError("component_add creates a new .vfAnimator here and needs data.defaultClip: the .vfAnim "
                               "its first state plays (animation_list_clips {entity} lists the clips that fit this mesh). "
                               "To assign an existing animator, pass data.animatorRef instead");
            }
            const asset::AssetRef clipRef =
                anim::resolveClip(root, env::requireString(*clip, "data.defaultClip"), "data.defaultClip");
            const auto state = seeds.find("defaultState");
            const std::string stateName =
                state != seeds.end() ? anim::requireStateName(*state, "data.defaultState") : std::string("Idle");
            const auto speed = seeds.find("playbackSpeed");
            const float playbackSpeed = speed != seeds.end() ? anim::requirePlaybackSpeed(*speed, "data.playbackSpeed") : 1.0f;
            const auto loop = seeds.find("loop");
            const bool looping = loop != seeds.end() ? env::requireBool(*loop, "data.loop") : true;

            fs::path target;
            if (requested.has_value())
            {
                std::string error;
                const std::optional<fs::path> file = resolveInside(root, *requested, error, animatorExtension);
                if (!file.has_value())
                {
                    throw ArgError("field 'animatorRef': " + error);
                }
                target = *file;
            }
            else
            {
                target = anim::defaultAnimatorPath(entity, root,
                                                   "pass data.animatorRef with a project-relative .vfAnimator path to "
                                                   "create it there");
            }

            anim::createAnimatorFile(target,
                                     anim::newAnimator(pathToUtf8(target.stem()), stateName, clipRef, playbackSpeed, looping));
            patch["animatorRef"] = projectRelativeUtf8(root, target);
            return patch;
        }
    }

    ComponentBinding makeAnimatorBinding()
    {
        ComponentBinding binding;
        binding.name = "Animator";
        binding.fieldHelp =
            "skeletal animation of a skinned Mesh entity (kept in the Mesh's animator fields: the entity has an "
            "Animator while animatorRef is set). Writable: animatorRef (.vfAnimator path), retargetRef (.vfretarget "
            "path or \"\" - plays clips authored for another rig), applyRootMotion (bool). Read-only, null when a file "
            "cannot be read: skeleton {mesh, bones, root}, defaultState, defaultClip, playbackSpeed, loop (of the "
            "default state), states (base-layer state names), parameters [{name, type}] (the names scripts set with "
            "Animator::setFloat / setBool / setTrigger), layers (count). component_add with data {defaultClip (.vfAnim "
            "path, see animation_list_clips), defaultState (default \"Idle\"), playbackSpeed (default 1), loop "
            "(default true)} creates a one-state .vfAnimator next to the mesh as <entity name>.vfAnimator (or at "
            "data.animatorRef) and assigns it - creating the file is not undoable; with data.animatorRef of an existing "
            ".vfAnimator it just assigns it. Edit states, parameters and transitions with animation_set_state_machine. "
            "component_remove clears animatorRef, retargetRef and applyRootMotion (the file stays).";
        binding.add = &addAnimator;
        binding.remove = &removeAnimator;
        binding.get = &getAnimator;
        binding.patch = &patchAnimator;
        for (std::string_view field : readOnlyFields)
        {
            binding.readOnlyFields.emplace_back(field);
        }
        binding.readOnlyHint = readOnlyHint;
        binding.addRequirement =
            "an Animator needs a Mesh component whose meshRef is a skinned .vfMesh (one with a skeleton); add the "
            "Mesh first with component_add {type:\"Mesh\", data:{meshRef}}";
        binding.prepareAdd = &prepareAnimatorAdd;
        // Undoing an add restores the Mesh's previous values of these fields (a retargetRef it already had
        // stays) rather than clearing them as component_remove does.
        binding.hostType = "Mesh";
        for (std::string_view field : writableFields)
        {
            binding.hostFields.emplace_back(field);
        }
        return binding;
    }
}
