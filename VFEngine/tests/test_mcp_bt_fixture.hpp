#pragma once

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/BehaviorTreeToolSupport.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "behaviortree/BehaviorTreeAsset.hpp"
#include "behaviortree/BehaviorTreeTypes.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/SceneGraphSystem.hpp"

#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/EQSEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"

#include "impl/ai/BehaviorTreeAuthoringService.hpp"
#include "impl/ai/BehaviorTreeServiceImpl.hpp"
#include "impl/scene/EntityQueryService.hpp"
#include "impl/scene/EntityStateService.hpp"
#include "impl/scene/HierarchyService.hpp"
#include "providers/ai/IBehaviorTreeProvider.hpp"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// VK-1660: the fixture of the MCP behavior-tree tool tests (test_mcp_bt_tools.cpp). The bt_* tools and the BehaviorTree
// binding run against the REAL engine services over a private scene graph - hierarchy, entity queries and state,
// BehaviorTreeServiceImpl (component data, runtime-state and blackboard events) and BehaviorTreeAuthoringService (the
// strict, hash-guarded .vfBehaviorTree writes into a temporary project) - plus a fake behavior tree provider (the live
// runtimes Play mode would have), a fake EQS registry and fakes for the editor state the tools consult (Play mode, a
// pending scene load, the project folder) and an undo stack that records what the tools push without executing it.
namespace mcp_bt_test
{
    namespace fs = std::filesystem;
    namespace bt = behaviortree;

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

    inline void writeFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
    }

    inline services::EntityHandle handleOf(uint32_t id)
    {
        services::EntityHandle handle;
        handle.id = static_cast<uint64_t>(id);
        return handle;
    }

    inline entt::registry& reg()
    {
        return scene::EntityRegistry::getRegistry();
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

    // A ScriptTask: its first @Script class declares tick(float).
    inline std::string scriptTaskSource(const std::string& className)
    {
        return "// " + className + " - a test ScriptTask.\n"
               "import * from \"../../lib/engine/oop/Behaviour.mt\";\n\n"
               "@Script\n"
               "public class " + className + " extends Behaviour {\n"
               "    public constructor() : super() {\n"
               "    }\n\n"
               "    public function tick(float deltaTime): string {\n"
               "        return \"running\";\n"
               "    }\n\n"
               "    public function onAbort(): void {\n"
               "    }\n"
               "}\n";
    }

    // The live runtimes Play mode would have. getRuntimeState returns the entity's states one per call (the last one
    // repeats), so a sampling test sees the running leaf change.
    struct FakeBehaviorTreeProvider final : services::IBehaviorTreeProvider
    {
        struct Write
        {
            uint64_t entity = 0;
            std::string key;
            bt::BlackboardValue value;
        };

        std::map<uint64_t, std::vector<bt::BTRuntimeState>> runtime;
        mutable std::map<uint64_t, std::size_t> reads;
        std::vector<std::string> reloads;
        std::vector<Write> writes;
        std::vector<uint64_t> detached;
        std::vector<uint64_t> debugTargets;

        const bt::BTRuntimeState* latest(uint64_t id) const
        {
            const auto it = runtime.find(id);
            return it == runtime.end() || it->second.empty() ? nullptr : &it->second.back();
        }

        bool attachTree(services::EntityHandle, const std::string&) override { return true; }
        void detachTree(services::EntityHandle entity) override { detached.push_back(entity.id); }
        void setEnabled(services::EntityHandle, bool) override {}
        bool hasTree(services::EntityHandle entity) const override { return latest(entity.id) != nullptr; }

        std::string getTreePath(services::EntityHandle entity) const override
        {
            const bt::BTRuntimeState* state = latest(entity.id);
            return state != nullptr ? state->treePath : std::string();
        }

        std::vector<services::EntityHandle> getAttachedEntities() const override
        {
            std::vector<services::EntityHandle> entities;
            for (const auto& [id, states] : runtime)
            {
                if (!states.empty() && states.back().attached)
                {
                    services::EntityHandle handle;
                    handle.id = id;
                    entities.push_back(handle);
                }
            }
            return entities;
        }

        bool isEnabled(services::EntityHandle entity) const override
        {
            const bt::BTRuntimeState* state = latest(entity.id);
            return state != nullptr && state->enabled;
        }

        std::string getStatus(services::EntityHandle entity) const override
        {
            const bt::BTRuntimeState* state = latest(entity.id);
            return state != nullptr ? state->status : std::string("stopped");
        }

        void updateAll(float) override {}
        void stopAll() override {}
        void reloadAsset(const std::string& treePath) override { reloads.push_back(treePath); }

        void setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                const bt::BlackboardValue& value) override
        {
            writes.push_back(Write{entity.id, key, value});
        }

        bt::BlackboardValue getBlackboardValue(services::EntityHandle, const std::string&) override { return 0.0f; }
        bool hasBlackboardKey(services::EntityHandle, const std::string&) const override { return false; }
        void setDynamicSubtree(services::EntityHandle, const std::string&, const std::string&) override {}
        void setDebugTarget(services::EntityHandle entity) override { debugTargets.push_back(entity.id); }
        bt::BTRuntimeSnapshot getRuntimeSnapshot(services::EntityHandle) const override { return {}; }
        void setDebugPaused(bool) override {}
        void stepDebug() override {}
        void setBreakpoints(services::EntityHandle, const std::vector<uint32_t>&) override {}

        bt::BTRuntimeState getRuntimeState(services::EntityHandle entity) const override
        {
            const auto it = runtime.find(entity.id);
            if (it == runtime.end() || it->second.empty())
            {
                return {};
            }
            std::size_t& count = reads[entity.id];
            const std::size_t index = std::min(count, it->second.size() - 1);
            ++count;
            return it->second[index];
        }
    };

    struct BTToolFixture
    {
        DispatcherScope dispatcherScope;
        fs::path project;  // the asset folder (ProjectConfig::workingDirectory)
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        services::EntityStateService entityState;
        services::HierarchyService hierarchy;
        services::EntityQueryService entityQuery;
        FakeBehaviorTreeProvider provider;
        services::BehaviorTreeServiceImpl behaviorTrees;
        services::BehaviorTreeAuthoringService authoring;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        bool playMode = false;
        bool loadPending = false;
        std::map<std::string, bool> eqsQueries;  // name -> valid
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        std::vector<std::string> savedAssets;

        explicit BTToolFixture(const char* directory)
            : project(makeTestDirectory(directory))
            , sceneGraph(std::make_shared<scene::SceneGraphSystem>())
            , entityState(sceneGraph)
            , hierarchy(sceneGraph)
            , entityQuery(sceneGraph)
            , behaviorTrees(&provider)
        {
            scene::EntityRegistry::init();  // UUID lookups (idempotent)

            auto& d = events::EventDispatcher::instance();
            entityState.registerEventHandlers(d);
            hierarchy.registerEventHandlers(d);
            entityQuery.registerEventHandlers(d);
            behaviorTrees.registerEventHandlers();
            authoring.registerEventHandlers();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpBehaviorTrees";
                    config.workingDirectory = project.string();
                    return config;
                });
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
            d.registerQueryHandler<events::ai::GetEQSQueryInfoQuery>(
                [this](const events::ai::GetEQSQueryInfoQuery& query)
                {
                    services::EQSQueryInfo info;
                    info.name = query.queryName;
                    const auto it = eqsQueries.find(query.queryName);
                    info.exists = it != eqsQueries.end();
                    info.valid = info.exists && it->second;
                    if (info.exists && !info.valid)
                    {
                        info.errors.push_back(eqs::EQSSpecError{"tests[0]", "unknown test type"});
                    }
                    return info;
                });
            d.registerQueryHandler<events::ai::ListEQSQueriesQuery>(
                [this](const events::ai::ListEQSQueriesQuery&)
                {
                    std::vector<services::EQSQueryInfo> infos;
                    for (const auto& [name, valid] : eqsQueries)
                    {
                        services::EQSQueryInfo info;
                        info.name = name;
                        info.exists = true;
                        info.valid = valid;
                        infos.push_back(std::move(info));
                    }
                    return infos;
                });
            // Cleared with the dispatcher in the destructor, so the token is not kept.
            (void)d.subscribe<events::resource::AssetSavedNotification>(
                [this](const events::resource::AssetSavedNotification& notification)
                { savedAssets.push_back(notification.filePath); });

            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerBehaviorTreeTools(registry, context);
            mcp::tools::registerComponentTools(registry, context);
        }

        ~BTToolFixture()
        {
            // Handlers first (they capture the services), then the entities this scene graph made.
            events::EventDispatcher::instance().clear();
            sceneGraph->clearScene();
            reg().destroy(sceneGraph->GetRoot().getHandle());
            std::error_code ec;
            fs::remove_all(project, ec);
        }

        BTToolFixture(const BTToolFixture&) = delete;
        BTToolFixture& operator=(const BTToolFixture&) = delete;

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

        // Worker-affinity tools block on runOnMain; play the editor main thread here.
        mcp::ToolResult callWorker(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Worker);
            const mcp::ToolHandler handler = def.handler;
            auto future = std::async(std::launch::async, [handler, args]() { return handler(args); });
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                queue.drain(std::chrono::milliseconds(8));
            }
            return future.get();
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

        uint32_t makeEntity(const std::string& name, std::optional<uint32_t> parent = std::nullopt)
        {
            events::scene::CreateEntityCommand command;
            command.name = name;
            if (parent.has_value())
            {
                command.parent = handleOf(*parent);
            }
            return static_cast<uint32_t>(events::EventDispatcher::instance().execute(command).id);
        }

        void deleteEntity(uint32_t id)
        {
            events::scene::DeleteEntityCommand command;
            command.entity = handleOf(id);
            events::EventDispatcher::instance().execute(command);
        }

        bool alive(uint32_t id) const
        {
            return reg().valid(static_cast<entt::entity>(id));
        }

        fs::path file(const std::string& relative) const
        {
            return (project / fs::path(relative)).lexically_normal();
        }

        // <asset folder>/scripts/<relative>
        void writeScript(const std::string& relative, const std::string& source)
        {
            writeFile(project / "scripts" / fs::path(relative), source);
        }

        bt::BehaviorTreeData load(const std::string& relative) const
        {
            const std::optional<bt::BehaviorTreeData> data = bt::BehaviorTreeAsset::load(file(relative).string());
            REQUIRE(data.has_value());
            return *data;
        }

        void undoLast()
        {
            REQUIRE_FALSE(undoStack.empty());
            undoStack.back()->undo();
        }

        void redoLast()
        {
            REQUIRE_FALSE(undoStack.empty());
            undoStack.back()->execute();
        }
    };

    // The node of `data` named `name` (it must be unique).
    inline bt::BTNode nodeNamed(const bt::BehaviorTreeData& data, const std::string& name)
    {
        const bt::BTNode* found = nullptr;
        for (const bt::BTNode& node : data.graph.nodes)
        {
            if (node.name == name)
            {
                REQUIRE(found == nullptr);
                found = &node;
            }
        }
        REQUIRE(found != nullptr);
        return *found;
    }

    inline std::vector<uint32_t> childrenOf(const bt::BehaviorTreeData& data, uint32_t id)
    {
        return mcp::tools::bttool::childIds(data.graph, id);
    }

    template <typename T>
    T property(const bt::BTNode& node, const std::string& name)
    {
        const auto it = node.properties.find(name);
        REQUIRE(it != node.properties.end());
        REQUIRE(std::holds_alternative<T>(it->second));
        return std::get<T>(it->second);
    }
}
