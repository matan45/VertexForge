#include "EnvironmentUndo.hpp"

#include <stdexcept>
#include <utility>

namespace mcp::undo
{
    // ------------------------------------------------------------------
    // RenderSettingsSectionUndo
    // ------------------------------------------------------------------

    RenderSettingsSectionUndo::RenderSettingsSectionUndo(tools::env::SectionValue before, tools::env::SectionValue after)
        : before(std::move(before)), after(std::move(after))
    {
        if (this->before.index() != this->after.index())
        {
            throw std::logic_error("RenderSettingsSectionUndo needs two values of the same section");
        }
    }

    void RenderSettingsSectionUndo::execute() { tools::env::writeSection(after); }
    void RenderSettingsSectionUndo::undo() { tools::env::writeSection(before); }

    std::string RenderSettingsSectionUndo::getDescription() const
    {
        return "MCP: Set " + std::string(tools::env::sectionName(after));
    }

    size_t RenderSettingsSectionUndo::getMemoryFootprint() const
    {
        return sizeof(*this); // the settings structs own no heap memory
    }

    // ------------------------------------------------------------------
    // IblEnvironmentUndo
    // ------------------------------------------------------------------

    IblEnvironmentUndo::IblEnvironmentUndo(std::optional<services::IBLData> before, std::optional<services::IBLData> after)
        : before(std::move(before)), after(std::move(after))
    {
    }

    bool IblEnvironmentUndo::hdrChanges() const
    {
        if (before.has_value() != after.has_value())
        {
            return true;
        }
        return before.has_value() && before->hdrRef != after->hdrRef;
    }

    void IblEnvironmentUndo::execute() { tools::env::applyIbl(after, hdrChanges()); }
    void IblEnvironmentUndo::undo() { tools::env::applyIbl(before, hdrChanges()); }

    std::string IblEnvironmentUndo::getDescription() const
    {
        return "MCP: Set IBL";
    }

    size_t IblEnvironmentUndo::getMemoryFootprint() const
    {
        // AssetRef's cached path is not counted: reading it would resolve the asset.
        return sizeof(*this);
    }

    // ------------------------------------------------------------------
    // WeatherUndo
    // ------------------------------------------------------------------

    WeatherUndo::WeatherUndo(tools::env::WeatherSnapshot before, tools::env::WeatherSnapshot after)
        : before(before), after(after)
    {
    }

    void WeatherUndo::execute() { tools::env::applyWeather(after); }
    void WeatherUndo::undo() { tools::env::applyWeather(before); }

    std::string WeatherUndo::getDescription() const
    {
        return "MCP: Set weather";
    }

    size_t WeatherUndo::getMemoryFootprint() const
    {
        return sizeof(*this);
    }
}
