#pragma once

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/SceneGraphSystem.hpp"

#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include "impl/components/UIComponentService.hpp"
#include "impl/scene/EntityQueryService.hpp"
#include "impl/scene/EntityStateService.hpp"
#include "impl/scene/HierarchyService.hpp"
#include "impl/scene/ScenePersistenceService.hpp"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// VK-1659: the fixture of the MCP UI tool tests (test_mcp_ui_tools.cpp). The UI tools run against the REAL engine
// services over a private scene graph - hierarchy, entity queries, entity state, scene persistence (prefab JSON:
// creation undo, prefabs) and UIComponentService (every UI component event, list-view reconcile, click
// simulation) - plus fakes for the editor state the tools consult (Play mode, a pending scene load, World mode,
// the project folder) and an undo stack that records what the tools push without executing it.
namespace mcp_ui_test
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

    inline services::EntityHandle handleOf(uint32_t id)
    {
        services::EntityHandle handle;
        handle.id = static_cast<uint64_t>(id);
        return handle;
    }

    inline entt::entity enttOf(uint32_t id)
    {
        return static_cast<entt::entity>(id);
    }

    inline entt::registry& reg()
    {
        return scene::EntityRegistry::getRegistry();
    }

    inline bool alive(uint32_t id)
    {
        return reg().valid(enttOf(id));
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

    struct UIToolFixture
    {
        DispatcherScope dispatcherScope;
        fs::path project;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        services::EntityStateService entityState;
        services::HierarchyService hierarchy;
        services::EntityQueryService entityQuery;
        services::ScenePersistenceService persistence;
        services::UIComponentService ui;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        bool playMode = false;
        bool loadPending = false;
        bool worldMode = false;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        std::vector<std::string> savedAssets;

        explicit UIToolFixture(const char* directory)
            : project(makeTestDirectory(directory))
            , sceneGraph(std::make_shared<scene::SceneGraphSystem>())
            , entityState(sceneGraph)
            , hierarchy(sceneGraph)
            , entityQuery(sceneGraph)
            , persistence(sceneGraph, &entityState)
            , ui(sceneGraph)
        {
            auto& d = events::EventDispatcher::instance();
            entityState.registerEventHandlers(d);
            hierarchy.registerEventHandlers(d);
            entityQuery.registerEventHandlers(d);
            persistence.registerEventHandlers(d);
            ui.registerEventHandlers(d);

            // Editor state. IsSceneLoadPendingQuery replaces the persistence service's own handler.
            d.unregisterQueryHandler<events::scene::IsSceneLoadPendingQuery>();
            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::world::IsWorldModeQuery>(
                [this](const events::world::IsWorldModeQuery&) { return worldMode; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpUi";
                    config.workingDirectory = project.string();
                    return config;
                });
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
            // Cleared with the dispatcher in the destructor, so the token is not kept.
            (void)d.subscribe<events::resource::AssetSavedNotification>(
                [this](const events::resource::AssetSavedNotification& notification)
                { savedAssets.push_back(notification.filePath); });

            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerUITools(registry, context);
        }

        ~UIToolFixture()
        {
            // Handlers first (they capture the services), then the entities this scene graph made.
            events::EventDispatcher::instance().clear();
            sceneGraph->clearScene();
            reg().destroy(sceneGraph->GetRoot().getHandle());
            std::error_code ec;
            fs::remove_all(project, ec);
        }

        UIToolFixture(const UIToolFixture&) = delete;
        UIToolFixture& operator=(const UIToolFixture&) = delete;

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

        uint32_t rootId() const
        {
            return static_cast<uint32_t>(sceneGraph->GetRoot().getHandle());
        }

        // A screen-space canvas at the scene root (1920 x 1080, scaleWithScreenSize).
        uint32_t makeCanvas(const char* name = "McpUiCanvas")
        {
            return callOk("ui_create_canvas", {{"name", name}})["entity"].get<uint32_t>();
        }

        uint32_t addWidget(nlohmann::json args)
        {
            return callOk("ui_add_widget", args)["entity"].get<uint32_t>();
        }

        // The live id of an id recorded before an undo / redo re-created the entity.
        static uint32_t live(uint32_t id)
        {
            return mcp::undo::EntityIdRemap::instance().resolve(id);
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

        std::vector<entt::entity> childrenOf(uint32_t id) const
        {
            const auto* children = reg().try_get<components::ChildrenComponent>(enttOf(id));
            return children != nullptr ? children->children : std::vector<entt::entity>{};
        }

        std::size_t listItemsUnder(uint32_t id) const
        {
            std::size_t count = 0;
            for (const entt::entity child : childrenOf(id))
            {
                if (reg().valid(child) && reg().all_of<components::UIListItemComponent>(child))
                {
                    ++count;
                }
            }
            return count;
        }

        fs::path file(const std::string& relative) const
        {
            return (project / fs::path(relative)).lexically_normal();
        }
    };
}
