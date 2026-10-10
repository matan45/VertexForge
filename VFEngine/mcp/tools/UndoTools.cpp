#include "CoreTools.hpp"
#include "TerrainToolSupport.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/EntityIdRemap.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace mcp::tools
{
    namespace
    {
        // Undo/redo of agent edits may re-create entities with new ids; the agent
        // needs the old -> new pairs to keep addressing them.
        constexpr const char* historyNote =
            " Agent entity, component and material_assign edits are undoable (one step per tool call), and so is "
            "each terrain_sculpt, terrain_paint_layer and applied terrain_generate_heightmap (one terrain stroke "
            "each); asset, script, scene and play-mode operations, terrain_create / terrain_delete / terrain_save "
            "and terrain layer changes are not. Undo of a delete re-creates entities with NEW ids: "
            "follow remappedEntities [{from, to}].";

        // VK-1653: a terrain undo rewrites tile heights in place, which would tear a .vfTerrain that a
        // save (the editor's terrain panel saves on a worker thread) is writing at that moment.
        constexpr const char* terrainSaveRefusal =
            "A terrain save is in progress; undo / redo are refused until it finishes (retry in a moment)";

        // Undoing a large terrain stroke restores every tile it touched.
        constexpr std::chrono::milliseconds historyTimeout{60000};

        nlohmann::json historyState(bool performed, const std::map<uint32_t, uint32_t>& remapBefore)
        {
            const services::UndoHistoryStats stats =
                events::EventDispatcher::instance().query(events::undoredo::GetUndoHistoryStatsQuery{});

            nlohmann::json remapped = nlohmann::json::array();
            const auto& remap = undo::EntityIdRemap::instance();
            for (const auto& [from, to] : undo::EntityIdRemap::changes(remapBefore, remap.snapshot()))
            {
                remapped.push_back({{"from", from}, {"to", to}});
            }
            return {
                {"performed", performed},
                {"undoCount", stats.undoCount},
                {"redoCount", stats.redoCount},
                {"remappedEntities", std::move(remapped)}
            };
        }

        void registerUndo(ToolRegistry& registry, std::shared_ptr<events::ScopedSubscription> sceneCleared)
        {
            ToolDef tool;
            tool.name = "undo";
            tool.title = "Undo";
            tool.description =
                std::string("Undo the most recent undoable editor action (the editor undo stack, shared with Ctrl+Z). "
                            "Returns {performed, undoCount, redoCount, remappedEntities}; performed=false when "
                            "there was nothing to undo.") + historyNote;
            tool.timeout = historyTimeout;
            tool.handler = [sceneCleared](const nlohmann::json&) -> ToolResult
            {
                if (isTerrainSaveLocked())
                {
                    return ToolResult::error(terrainSaveRefusal);
                }
                const auto remapBefore = undo::EntityIdRemap::instance().snapshot();
                const bool performed = events::EventDispatcher::instance().execute(events::undoredo::UndoCommand{});
                return ToolResult::ok(historyState(performed, remapBefore));
            };
            registry.add(std::move(tool));
        }

        void registerRedo(ToolRegistry& registry, std::shared_ptr<events::ScopedSubscription> sceneCleared)
        {
            ToolDef tool;
            tool.name = "redo";
            tool.title = "Redo";
            tool.description =
                std::string("Redo the most recently undone editor action (Ctrl+Y). Returns "
                            "{performed, undoCount, redoCount, remappedEntities}; performed=false when there was "
                            "nothing to redo.") + historyNote;
            tool.timeout = historyTimeout;
            tool.handler = [sceneCleared](const nlohmann::json&) -> ToolResult
            {
                if (isTerrainSaveLocked())
                {
                    return ToolResult::error(terrainSaveRefusal);
                }
                const auto remapBefore = undo::EntityIdRemap::instance().snapshot();
                const bool performed = events::EventDispatcher::instance().execute(events::undoredo::RedoCommand{});
                return ToolResult::ok(historyState(performed, remapBefore));
            };
            registry.add(std::move(tool));
        }
    }

    void registerUndoTools(ToolRegistry& registry, const ToolContext&)
    {
        // The id remap dies with the undo history it serves (UndoRedoServiceImpl clears
        // on the same notification). The subscription lives as long as these tools:
        // a restarted MCP service re-registers and drops the old one instead of
        // stacking subscriptions, and unsubscribing a token a cleared dispatcher no
        // longer knows is a no-op.
        auto sceneCleared = std::make_shared<events::ScopedSubscription>(
            events::EventDispatcher::instance().subscribe<events::scene::SceneClearedNotification>(
                [](const events::scene::SceneClearedNotification&)
                {
                    undo::EntityIdRemap::instance().clear();
                }));

        registerUndo(registry, sceneCleared);
        registerRedo(registry, sceneCleared);
    }
}
