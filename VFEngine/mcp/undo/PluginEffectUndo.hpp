#pragma once

#include "data/UndoTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

// VK-1655: undo entry for postprocess_effect_set. Replays the same SetPluginPostProcessEffectEnabledCommand
// the tool sends (RenderSettingsToolSupport::setPluginEffectEnabled), so the plugin's requested state and
// its editor checkbox follow undo and redo. Throws when the effect is gone; the McpBatchUndoCommand
// (McpUndo.hpp) the tool pushes logs it and lets the entry be consumed.
namespace mcp::undo
{
    class PluginPostProcessEffectUndo : public services::IUndoableCommand
    {
    public:
        PluginPostProcessEffectUndo(uint64_t id, std::string label, bool before, bool after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        void apply(bool enabled) const;

        uint64_t id = 0;
        std::string label; // "<plugin>/<name>" for the description and errors
        bool before = false;
        bool after = false;
    };
}
