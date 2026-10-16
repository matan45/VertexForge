#include "EnvironmentUndo.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace mcp::undo
{
    namespace
    {
        // Heap memory a section owns: only colour grading's LUT paths.
        size_t sectionHeapBytes(const std::vector<tools::env::SectionValue>& sections)
        {
            size_t bytes = sections.capacity() * sizeof(tools::env::SectionValue);
            for (const tools::env::SectionValue& section : sections)
            {
                if (const auto* grading = std::get_if<tools::env::ColorGradingSettings>(&section))
                {
                    bytes += grading->primaryLutPath.capacity() + grading->secondaryLutPath.capacity();
                }
            }
            return bytes;
        }
    }

    // ------------------------------------------------------------------
    // RenderSettingsSectionUndo
    // ------------------------------------------------------------------

    RenderSettingsSectionUndo::RenderSettingsSectionUndo(std::vector<tools::env::SectionValue> before,
                                                         std::vector<tools::env::SectionValue> after)
        : before(std::move(before)), after(std::move(after))
    {
        if (this->before.empty() || this->before.size() != this->after.size())
        {
            throw std::logic_error("RenderSettingsSectionUndo needs the same non-empty list of sections twice");
        }
        for (size_t i = 0; i < this->before.size(); ++i)
        {
            if (this->before[i].index() != this->after[i].index())
            {
                throw std::logic_error("RenderSettingsSectionUndo needs two values of the same section");
            }
        }
    }

    void RenderSettingsSectionUndo::execute() { tools::env::writeSections(after); }
    void RenderSettingsSectionUndo::undo() { tools::env::writeSections(before); }

    std::string RenderSettingsSectionUndo::getDescription() const
    {
        std::string names;
        for (const tools::env::SectionValue& section : after)
        {
            names += names.empty() ? "" : ", ";
            names += tools::env::sectionName(section);
        }
        return "MCP: Set " + names;
    }

    size_t RenderSettingsSectionUndo::getMemoryFootprint() const
    {
        return sizeof(*this) + sectionHeapBytes(before) + sectionHeapBytes(after);
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
