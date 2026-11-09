#pragma once

#include "../tools/EnvironmentToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// VK-1654: undo entries for the environment tools (and, VK-1655, the render-settings tools). Each
// replays the same helpers the tools use (EnvironmentToolSupport), so undo and redo keep the scene copy,
// the live renderer copy and the editor windows in step exactly like the original edit. Failures throw;
// the McpBatchUndoCommand (McpUndo.hpp) the tools push logs them and lets the entry be consumed.
namespace mcp::undo
{
    // Render-settings sections (atmosphere, clouds, volumetric fog, GI, the post-process effects, shadows,
    // MSAA), written together by one writeSections call. writeSections re-reads the scene's settings every
    // time, so undoing this entry never reverts another section edited since; the tools pass only the
    // sections they changed. Both lists must name the same sections in the same order.
    class RenderSettingsSectionUndo : public services::IUndoableCommand
    {
    public:
        RenderSettingsSectionUndo(std::vector<tools::env::SectionValue> before, std::vector<tools::env::SectionValue> after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        std::vector<tools::env::SectionValue> before;
        std::vector<tools::env::SectionValue> after;
    };

    // The scene root's IBL (nullopt = no IBLComponent). The environment is re-baked only when the HDR
    // reference differs between the two states; knob-only changes just re-push the knobs.
    class IblEnvironmentUndo : public services::IUndoableCommand
    {
    public:
        IblEnvironmentUndo(std::optional<services::IBLData> before, std::optional<services::IBLData> after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        bool hdrChanges() const;

        std::optional<services::IBLData> before;
        std::optional<services::IBLData> after;
    };

    // The session weather (enabled + state). Redo jumps to the end state of a timed transition.
    class WeatherUndo : public services::IUndoableCommand
    {
    public:
        WeatherUndo(tools::env::WeatherSnapshot before, tools::env::WeatherSnapshot after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        tools::env::WeatherSnapshot before;
        tools::env::WeatherSnapshot after;
    };
}
