#include "AddComponentPopup.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "core/PluginContextImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include <imgui.h>

namespace windows::details
{
    void AddComponentPopup::drawUISection(const ComponentPresence& c, const char* filter)
    {
        auto handle = c.handle;
        auto& dispatcher = events::EventDispatcher::instance();

        if (!c.hasUICanvas && matchesFilter("UI Canvas", filter))
        {
            if (ImGui::Selectable("  UI Canvas"))
            {
                events::ui::AddUICanvasComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("UI Canvas with reference resolution and auto-scaling");
        }

        if (!c.hasUIRect && matchesFilter("UI Rect", filter))
        {
            if (ImGui::Selectable("  UI Rect"))
            {
                events::ui::AddUIRectComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rect transform for UI anchoring and layout");
        }

        if (!c.hasUIImage && matchesFilter("UI Image", filter))
        {
            if (ImGui::Selectable("  UI Image"))
            {
                events::ui::AddUIImageComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Screen-space image with texture and color tint");
        }

        if (!c.hasUILabel && matchesFilter("UI Label", filter))
        {
            if (ImGui::Selectable("  UI Label"))
            {
                events::ui::AddUILabelComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Text label with font, alignment, and overflow settings");
        }

        if (!c.hasUIScroll && matchesFilter("UI Scroll", filter))
        {
            if (ImGui::Selectable("  UI Scroll"))
            {
                events::ui::AddUIScrollComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scrollable container with clipping and scrollbars");
        }

        if (!c.hasUILayoutGroup && matchesFilter("UI Layout Group", filter))
        {
            if (ImGui::Selectable("  UI Layout Group"))
            {
                events::ui::AddUILayoutGroupComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Auto-stack children vertically or horizontally");
        }

        if (!c.hasUIButton && matchesFilter("UI Button", filter))
        {
            if (ImGui::Selectable("  UI Button"))
            {
                events::ui::AddUIButtonComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Interactive button with state colors and click events");
        }

        if (!c.hasUITextInput && matchesFilter("UI Text Input", filter))
        {
            if (ImGui::Selectable("  UI Text Input"))
            {
                events::ui::AddUITextInputComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Editable text input field with focus and selection");
        }

        if (!c.hasUICheckbox && matchesFilter("UI Checkbox", filter))
        {
            if (ImGui::Selectable("  UI Checkbox"))
            {
                events::ui::AddUICheckboxComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Toggleable checkbox with radio group support");
        }

        if (!c.hasUIDropdown && matchesFilter("UI Dropdown", filter))
        {
            if (ImGui::Selectable("  UI Dropdown"))
            {
                events::ui::AddUIDropdownComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Dropdown / combo box with selectable options");
        }

        if (!c.hasUITabs && matchesFilter("UI Tabs", filter))
        {
            if (ImGui::Selectable("  UI Tabs"))
            {
                events::ui::AddUITabsComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tabbed panel container with switchable content views");
        }

        if (!c.hasUISlider && matchesFilter("UI Slider", filter))
        {
            if (ImGui::Selectable("  UI Slider"))
            {
                events::ui::AddUISliderComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draggable slider for numeric value input");
        }

        if (!c.hasUIProgressBar && matchesFilter("UI Progress Bar", filter))
        {
            if (ImGui::Selectable("  UI Progress Bar"))
            {
                events::ui::AddUIProgressBarComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Non-interactive bar displaying progress");
        }

        if (!c.hasUIAnimation && matchesFilter("UI Animation", filter))
        {
            if (ImGui::Selectable("  UI Animation"))
            {
                events::ui::AddUIAnimationComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tween animation system for UI elements");
        }

        if (!c.hasUIMask && matchesFilter("UI Mask", filter))
        {
            if (ImGui::Selectable("  UI Mask"))
            {
                events::ui::AddUIMaskComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stencil mask for clipping children to arbitrary shapes");
        }

        if (!c.hasUIDraggable && matchesFilter("UI Draggable", filter))
        {
            if (ImGui::Selectable("  UI Draggable"))
            {
                events::ui::AddUIDraggableComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Marks this element as a drag source for drag-and-drop");
        }

        if (!c.hasUIDropTarget && matchesFilter("UI Drop Target", filter))
        {
            if (ImGui::Selectable("  UI Drop Target"))
            {
                events::ui::AddUIDropTargetComponentCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Marks this element as a drop receiver for drag-and-drop");
        }
    }

    void AddComponentPopup::drawPluginSection(const ComponentPresence& c, const char* filter)
    {
        auto& bridges = plugin::PluginContextImpl::getAllBridges();
        if (bridges.empty())
            return;

        auto& reg = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(c.handle);

        for (const auto& bridge : bridges)
        {
            if (bridge.has(reg, entity))
                continue;

            const char* name = bridge.name ? bridge.name : "Unknown";
            if (filter && !matchesFilter(name, filter))
                continue;

            std::string label = std::string("  ") + name;
            if (ImGui::Selectable(label.c_str()))
            {
                bridge.emplace(reg, entity);
            }
        }
    }

}
