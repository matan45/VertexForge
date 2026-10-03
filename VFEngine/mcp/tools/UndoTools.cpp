#include "CoreTools.hpp"
#include "../protocol/ArgReader.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"

namespace mcp::tools
{
    namespace
    {
        nlohmann::json historyState(bool performed)
        {
            const services::UndoHistoryStats stats =
                events::EventDispatcher::instance().query(events::undoredo::GetUndoHistoryStatsQuery{});
            return {
                {"performed", performed},
                {"undoCount", stats.undoCount},
                {"redoCount", stats.redoCount}
            };
        }

        void registerUndo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "undo";
            tool.title = "Undo";
            tool.description =
                "Undo the most recent undoable editor action (the editor undo stack). Not every MCP edit is "
                "recorded on the undo stack. Returns {performed, undoCount, redoCount}; performed=false when "
                "there was nothing to undo.";
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                const bool performed = events::EventDispatcher::instance().execute(events::undoredo::UndoCommand{});
                return ToolResult::ok(historyState(performed));
            };
            registry.add(std::move(tool));
        }

        void registerRedo(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "redo";
            tool.title = "Redo";
            tool.description =
                "Redo the most recently undone editor action. Returns "
                "{performed, undoCount, redoCount}; performed=false when there was nothing to redo.";
            tool.handler = [](const nlohmann::json&) -> ToolResult
            {
                const bool performed = events::EventDispatcher::instance().execute(events::undoredo::RedoCommand{});
                return ToolResult::ok(historyState(performed));
            };
            registry.add(std::move(tool));
        }
    }

    void registerUndoTools(ToolRegistry& registry, const ToolContext&)
    {
        registerUndo(registry);
        registerRedo(registry);
    }
}
