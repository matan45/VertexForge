#include "UIUndo.hpp"
#include "McpUndoCommands.hpp"
#include "../tools/ToolHelpers.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace mcp::undo
{
    namespace
    {
        namespace uitool = tools::uitool;

        std::string idText(uint32_t recordedId)
        {
            return std::to_string(recordedId);
        }

        std::string componentName(uitool::Section section)
        {
            return std::string(uitool::sectionComponents[static_cast<std::size_t>(section)]);
        }

        // The entity the change was recorded on, as it is now (a re-created entity resolves through
        // EntityIdRemap).
        services::EntityHandle liveHandle(uint32_t recordedId, const std::string& name)
        {
            const services::EntityHandle handle = currentHandle(recordedId);
            if (!tools::findEntity(handle).has_value())
            {
                throw std::runtime_error("entity " + idText(recordedId) + " ('" + name + "') no longer exists");
            }
            return handle;
        }

        // Writes the authored members of `value` over the live component; its runtime members stay the engine's.
        void applySection(const services::EntityHandle& handle, uint32_t recordedId, uitool::Section section,
                          const uitool::SectionData& value)
        {
            const std::optional<uitool::SectionData> live = uitool::readSection(handle, section);
            if (!live.has_value())
            {
                throw std::runtime_error("entity " + idText(recordedId) + " has no " + componentName(section) +
                                         " any more");
            }
            uitool::writeSection(handle, uitool::copyAuthored(*live, value));
        }

        // The heap bytes a section value holds (its strings and lists).
        size_t dynamicBytes(const uitool::SectionData& data)
        {
            return std::visit(
                [](const auto& value) -> size_t
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, uitool::CanvasValues>)
                    {
                        return value.theme.size();
                    }
                    else if constexpr (std::is_same_v<T, services::UIImageData>)
                    {
                        return value.renderTextureSourceName.size() + value.externalTextureKey.size();
                    }
                    else if constexpr (std::is_same_v<T, services::UILabelData>)
                    {
                        return value.text.size();
                    }
                    else if constexpr (std::is_same_v<T, services::UICheckboxData>)
                    {
                        return value.groupName.size();
                    }
                    else if constexpr (std::is_same_v<T, services::UIDropdownData>)
                    {
                        size_t total = value.placeholderText.size() +
                                       value.options.capacity() * sizeof(services::DropdownOptionData);
                        for (const services::DropdownOptionData& option : value.options)
                        {
                            total += option.text.size();
                        }
                        return total;
                    }
                    else if constexpr (std::is_same_v<T, services::UITextInputData>)
                    {
                        return value.text.size() + value.placeholderText.size();
                    }
                    else
                    {
                        return 0;
                    }
                },
                data);
        }
    }

    UISectionUndo::UISectionUndo(uint32_t entity, std::string entityName, std::vector<UISectionChange> changes)
        : entity(entity), entityName(std::move(entityName)), changes(std::move(changes))
    {
        footprint = sizeof(*this) + this->entityName.size() + this->changes.capacity() * sizeof(UISectionChange);
        for (const UISectionChange& change : this->changes)
        {
            if (change.before.has_value())
            {
                footprint += dynamicBytes(*change.before);
            }
            footprint += dynamicBytes(change.after);
        }
    }

    void UISectionUndo::execute()
    {
        const services::EntityHandle handle = liveHandle(entity, entityName);
        for (const UISectionChange& change : changes)
        {
            // An attached section comes back first (an Add*Component may already have re-added it as a sibling).
            if (!change.before.has_value() && !uitool::hasSection(handle, change.section))
            {
                uitool::addSection(handle, change.section);
            }
            applySection(handle, entity, change.section, change.after);
        }
    }

    void UISectionUndo::undo()
    {
        const services::EntityHandle handle = liveHandle(entity, entityName);
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        {
            if (!it->before.has_value())
            {
                if (uitool::hasSection(handle, it->section))
                {
                    uitool::removeSection(handle, it->section);
                }
                continue;
            }
            applySection(handle, entity, it->section, *it->before);
        }
    }

    std::string UISectionUndo::getDescription() const
    {
        return "MCP: Set UI (" + entityName + ")";
    }

    size_t UISectionUndo::getMemoryFootprint() const
    {
        return footprint;
    }
}
