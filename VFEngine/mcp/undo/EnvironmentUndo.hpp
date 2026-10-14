#pragma once

#include "../tools/EnvironmentToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>

// VK-1654: undo entries for the environment tools. Each replays the same helpers the tools use
// (EnvironmentToolSupport), so undo and redo keep the scene copy, the live renderer copy and the
// editor windows in step exactly like the original edit. Failures throw; the McpBatchUndoCommand
// (McpUndo.hpp) the tools push logs them and lets the entry be consumed.
namespace mcp::undo
{
    // One render-settings section (atmosphere, clouds, volumetric fog or GI). writeSection re-reads
    // the scene's settings every time, so undoing this entry never reverts another section edited
    // since. Both values must hold the same section.
    class RenderSettingsSectionUndo : public services::IUndoableCommand
    {
    public:
        RenderSettingsSectionUndo(tools::env::SectionValue before, tools::env::SectionValue after);

        void execute() override;
        void undo() override;
        std::string getDescription() const override;
        size_t getMemoryFootprint() const override;

    private:
        tools::env::SectionValue before;
        tools::env::SectionValue after;
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
