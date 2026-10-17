#pragma once

#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"

#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationAuthoringEvents.hpp"
#include "events/animation/AnimatorEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// VK-1656: a fake engine for the MCP animation tests (test_mcp_animator_binding.cpp,
// test_mcp_animation_tools.cpp). Scene entities carry Mesh data; the animation-authoring handlers work
// on an in-memory .vfAnimator store whose files also exist on disk (the tools check paths before
// dispatching); clip summaries, retarget bindings, edit previews and the live animator state are canned.
// The log records the mutating events in dispatch order.
namespace mcpanim
{
    namespace fs = std::filesystem;

    struct DispatcherScope
    {
        DispatcherScope() { clearAll(); }
        ~DispatcherScope() { clearAll(); }

        static void clearAll()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
        }
    };

    inline fs::path makeTestDirectory(const char* name)
    {
        const fs::path directory = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(directory, ec);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    inline void touch(const fs::path& path, const std::string& text = "test")
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
    }

    // Engine paths arrive with either separator and, on Windows, any case.
    inline std::string pathKey(const std::string& path)
    {
        std::string key = fs::path(path).lexically_normal().generic_string();
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    inline services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    inline bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    inline std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    struct StoredAnimator
    {
        animator::AnimatorData data;
        uint64_t hash = 0;
    };

    struct FakeEntity
    {
        std::string name;
        std::optional<services::MeshData> mesh;
    };

    struct FakeRuntime
    {
        services::AnimatorRuntimeDebugData debug;
        std::string currentState;
        float normalizedTime = 0.0f;
        bool playing = true;
    };

    struct FakeAnimationEngine
    {
        fs::path assets;
        bool playMode = false;
        bool loadPending = false;
        bool previewFails = false;

        std::map<uint64_t, FakeEntity> entities;
        std::map<std::string, services::MeshSkeletonInfo> skeletons;   // pathKey -> skeleton
        std::map<std::string, StoredAnimator> animators;                 // pathKey -> file content
        std::map<std::string, services::AnimationClipSummary> clips;     // pathKey -> summary
        std::map<std::string, services::RetargetBindingInfo> retargets;  // pathKey -> binding
        std::map<uint64_t, services::AnimationEditPreviewStatus> previews;
        std::vector<std::pair<uint64_t, services::AnimationEditPreviewRequest>> startedPreviews;
        std::map<uint64_t, FakeRuntime> runtime;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        std::vector<std::string> log;
        uint64_t nextHash = 1000;
        int summaryQueries = 0;
        uint32_t lastDebugLayer = 0;

        fs::path file(const std::string& relative) const
        {
            return (assets / fs::path(relative)).lexically_normal();
        }

        // A .vfMesh on disk and in the asset database, with this skeleton (no bones = a static mesh).
        asset::AssetRef addMeshAt(const fs::path& path, std::vector<std::string> bones, std::vector<int32_t> parents)
        {
            touch(path);
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            services::MeshSkeletonInfo info;
            info.success = true;
            info.skinned = !bones.empty();
            info.boneNames = std::move(bones);
            info.parents = std::move(parents);
            skeletons[pathKey(ref.resolve())] = std::move(info);
            return ref;
        }

        asset::AssetRef addMesh(const std::string& relative, std::vector<std::string> bones, std::vector<int32_t> parents)
        {
            return addMeshAt(file(relative), std::move(bones), std::move(parents));
        }

        // A .vfAnim on disk and in the asset database whose channels animate `channels`.
        asset::AssetRef addClip(const std::string& relative, std::vector<std::string> channels)
        {
            const fs::path path = file(relative);
            touch(path);
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            services::AnimationClipSummary summary;
            summary.path = ref.resolve();
            summary.success = true;
            summary.name = path.stem().string();
            summary.durationSeconds = 1.5f;
            summary.ticksPerSecond = 30.0f;
            summary.channelNames = std::move(channels);
            clips[pathKey(ref.resolve())] = std::move(summary);
            return ref;
        }

        // An existing .vfAnimator: on disk, in the store and in the asset database.
        asset::AssetRef addAnimator(const std::string& relative, animator::AnimatorData data)
        {
            const fs::path path = file(relative);
            touch(path);
            animators[pathKey(path.string())] = StoredAnimator{std::move(data), nextHash++};
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            return ref;
        }

        // A registered file of any asset type (a .vfretarget, an unreadable .vfAnimator, ...).
        asset::AssetRef addFile(const std::string& relative)
        {
            const fs::path path = file(relative);
            touch(path);
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            return ref;
        }

        StoredAnimator* animatorAt(const std::string& relative)
        {
            const auto it = animators.find(pathKey(file(relative).string()));
            return it != animators.end() ? &it->second : nullptr;
        }

        void addEntity(uint64_t id, std::string name, std::optional<asset::AssetRef> mesh = std::nullopt)
        {
            FakeEntity entity;
            entity.name = std::move(name);
            if (mesh.has_value())
            {
                services::MeshData data;
                data.meshRef = *mesh;
                entity.mesh = data;
            }
            entities[id] = std::move(entity);
        }

        services::MeshData& meshOf(uint64_t id)
        {
            REQUIRE(entities.contains(id));
            REQUIRE(entities.at(id).mesh.has_value());
            return *entities.at(id).mesh;
        }

        bool logged(const std::string& prefix) const
        {
            return std::any_of(log.begin(), log.end(), [&prefix](const std::string& entry) { return entry.starts_with(prefix); });
        }

        void registerHandlers()
        {
            namespace authoring = events::animationAuthoring;
            namespace runtimeEvents = services::events::animator;
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpAnimation";
                    config.workingDirectory = assets.string();
                    return config;
                });

            // Scene.
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end())
                    {
                        return std::nullopt;
                    }
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = it->second.name;
                    if (it->second.mesh.has_value())
                    {
                        data.components.push_back(services::ComponentTypeId::Mesh);
                    }
                    return data;
                });
            d.registerQueryHandler<events::scene::GetMeshDataQuery>(
                [this](const events::scene::GetMeshDataQuery& query) -> std::optional<services::MeshData>
                {
                    const auto it = entities.find(query.entity.id);
                    return it != entities.end() ? it->second.mesh : std::nullopt;
                });
            d.registerCommandHandler<events::scene::SetMeshDataCommand>(
                [this](const events::scene::SetMeshDataCommand& command)
                {
                    log.push_back("SetMeshData:" + std::to_string(command.entity.id));
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end())
                    {
                        return false;
                    }
                    it->second.mesh = command.meshData;
                    return true;
                });
            d.registerQueryHandler<events::scene::GetEntitiesWithComponentQuery>(
                [this](const events::scene::GetEntitiesWithComponentQuery& query)
                {
                    std::vector<services::EntityHandle> found;
                    for (const auto& entry : entities)
                    {
                        if (query.componentType == services::ComponentTypeId::Mesh && entry.second.mesh.has_value())
                        {
                            found.push_back(handleOf(entry.first));
                        }
                    }
                    return found;
                });

            // Asset database: the real one, filled by AssetRef::fromPath.
            d.registerQueryHandler<events::assetdb::GetAssetsByTypeQuery>(
                [](const events::assetdb::GetAssetsByTypeQuery& query)
                {
                    std::vector<events::assetdb::AssetEntryData> entries;
                    for (const asset::AssetDatabaseEntry& entry : asset::AssetDatabase::instance().getAssetsByType(query.type))
                    {
                        entries.push_back(events::assetdb::AssetEntryData{entry.guid, entry.path, entry.type});
                    }
                    return entries;
                });

            // Animation authoring.
            d.registerQueryHandler<authoring::LoadAnimatorAssetQuery>(
                [this](const authoring::LoadAnimatorAssetQuery& query)
                {
                    services::AnimatorAssetLoadResult result;
                    const auto it = animators.find(pathKey(query.path));
                    if (it == animators.end())
                    {
                        result.error = "cannot open " + query.path;
                        return result;
                    }
                    result.success = true;
                    result.data = it->second.data;
                    result.contentHash = it->second.hash;
                    return result;
                });
            d.registerCommandHandler<authoring::SaveAnimatorAssetCommand>(
                [this](const authoring::SaveAnimatorAssetCommand& command)
                {
                    services::AnimatorAssetSaveResult result;
                    const fs::path path(command.path);
                    log.push_back("SaveAnimator:" + path.filename().string());
                    const std::string key = pathKey(command.path);
                    const auto it = animators.find(key);
                    std::error_code ec;
                    const bool exists = it != animators.end() || fs::exists(path, ec);
                    if (command.createNew && exists)
                    {
                        result.error = "the file already exists";
                        return result;
                    }
                    if (command.expectedContentHash.has_value() &&
                        (it == animators.end() || it->second.hash != *command.expectedContentHash))
                    {
                        result.error = "the file changed since it was read";
                        return result;
                    }
                    touch(path);
                    animators[key] = StoredAnimator{command.data, nextHash++};
                    result.success = true;
                    result.created = !exists;
                    result.contentHash = animators[key].hash;
                    return result;
                });
            d.registerQueryHandler<authoring::GetMeshSkeletonQuery>(
                [this](const authoring::GetMeshSkeletonQuery& query)
                {
                    const auto it = skeletons.find(pathKey(query.meshPath));
                    if (it != skeletons.end())
                    {
                        return it->second;
                    }
                    services::MeshSkeletonInfo info;
                    info.error = "cannot open " + query.meshPath;
                    return info;
                });
            d.registerQueryHandler<authoring::GetAnimationClipSummariesQuery>(
                [this](const authoring::GetAnimationClipSummariesQuery& query)
                {
                    ++summaryQueries;
                    std::vector<services::AnimationClipSummary> summaries;
                    for (const std::string& path : query.paths)
                    {
                        const auto it = clips.find(pathKey(path));
                        if (it != clips.end())
                        {
                            summaries.push_back(it->second);
                            continue;
                        }
                        services::AnimationClipSummary unreadable;
                        unreadable.path = path;
                        unreadable.error = "not a clip";
                        summaries.push_back(unreadable);
                    }
                    return summaries;
                });
            d.registerQueryHandler<authoring::GetRetargetBindingQuery>(
                [this](const authoring::GetRetargetBindingQuery& query)
                {
                    const auto it = retargets.find(pathKey(query.retargetPath));
                    if (it != retargets.end())
                    {
                        return it->second;
                    }
                    services::RetargetBindingInfo info;
                    info.error = "cannot open " + query.retargetPath;
                    return info;
                });
            d.registerCommandHandler<authoring::StartAnimationEditPreviewCommand>(
                [this](const authoring::StartAnimationEditPreviewCommand& command)
                {
                    log.push_back("StartPreview:" + std::to_string(command.entity.id));
                    startedPreviews.emplace_back(command.entity.id, command.request);
                    services::AnimationEditPreviewResult result;
                    if (previewFails)
                    {
                        result.error = "the animator is not initialized";
                        return result;
                    }
                    services::AnimationEditPreviewStatus status;
                    status.source = command.request.source;
                    status.state = command.request.state;
                    status.clipPath = command.request.clipPath;
                    status.normalizedTime = command.request.normalizedTime;
                    status.durationSeconds = 1.5f;
                    status.playing = command.request.playing;
                    previews[command.entity.id] = status;
                    result.success = true;
                    result.durationSeconds = 1.5f;
                    return result;
                });
            d.registerCommandHandler<authoring::StopAnimationEditPreviewCommand>(
                [this](const authoring::StopAnimationEditPreviewCommand& command)
                {
                    if (!command.entity.isValid())
                    {
                        log.push_back("StopPreview:all");
                        const bool any = !previews.empty();
                        previews.clear();
                        return any;
                    }
                    log.push_back("StopPreview:" + std::to_string(command.entity.id));
                    return previews.erase(command.entity.id) > 0;
                });
            d.registerQueryHandler<authoring::GetAnimationEditPreviewQuery>(
                [this](const authoring::GetAnimationEditPreviewQuery& query)
                    -> std::optional<services::AnimationEditPreviewStatus>
                {
                    const auto it = previews.find(query.entity.id);
                    return it != previews.end() ? std::optional(it->second) : std::nullopt;
                });

            // The live animators.
            d.registerQueryHandler<runtimeEvents::HasEntityAnimatorQuery>(
                [this](const runtimeEvents::HasEntityAnimatorQuery& query) { return runtime.contains(query.entity.id); });
            d.registerQueryHandler<runtimeEvents::GetAnimatorRuntimeDebugDataQuery>(
                [this](const runtimeEvents::GetAnimatorRuntimeDebugDataQuery& query)
                {
                    lastDebugLayer = query.layerIndex;
                    const auto it = runtime.find(query.entity.id);
                    return it != runtime.end() ? it->second.debug : services::AnimatorRuntimeDebugData{};
                });
            d.registerQueryHandler<runtimeEvents::GetEntityAnimatorCurrentStateQuery>(
                [this](const runtimeEvents::GetEntityAnimatorCurrentStateQuery& query)
                {
                    const auto it = runtime.find(query.entity.id);
                    return it != runtime.end() ? it->second.currentState : std::string();
                });
            d.registerQueryHandler<runtimeEvents::GetEntityAnimatorNormalizedTimeQuery>(
                [this](const runtimeEvents::GetEntityAnimatorNormalizedTimeQuery& query)
                {
                    const auto it = runtime.find(query.entity.id);
                    return it != runtime.end() ? it->second.normalizedTime : 0.0f;
                });
            d.registerQueryHandler<runtimeEvents::IsEntityAnimatorPlayingQuery>(
                [this](const runtimeEvents::IsEntityAnimatorPlayingQuery& query)
                {
                    const auto it = runtime.find(query.entity.id);
                    return it != runtime.end() && it->second.playing;
                });

            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    log.push_back("PushUndo");
                    undoStack.push_back(command.command);
                });
        }
    };

    struct AnimationFixture
    {
        DispatcherScope dispatcherScope;
        FakeAnimationEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        explicit AnimationFixture(const char* directory)
        {
            engine.assets = makeTestDirectory(directory);
            engine.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerComponentTools(registry, context);
            mcp::tools::registerAnimationTools(registry, context);
        }

        ~AnimationFixture()
        {
            std::error_code ec;
            fs::remove_all(engine.assets, ec);
        }

        AnimationFixture(const AnimationFixture&) = delete;
        AnimationFixture& operator=(const AnimationFixture&) = delete;

        const mcp::ToolDef& tool(const char* name) const
        {
            const auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        // The message of an ArgError the call throws.
        std::string argError(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        // The failure of a call, whether reported in-band (isError) or thrown.
        std::string failure(const char* name, const nlohmann::json& args)
        {
            try
            {
                const mcp::ToolResult result = call(name, args);
                return result.isError ? result.text : std::string("<no error>");
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
        }
    };
}
