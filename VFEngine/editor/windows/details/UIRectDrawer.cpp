#include "UIRectDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    namespace
    {
        // True if `handle`'s parent carries a UILayoutGroupComponent. That layout overwrites this
        // element's anchoredPosition every frame, so manual position edits don't stick — the
        // Anchored Position field is disabled to make that clear.
        bool isLayoutControlled(services::EntityHandle handle)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = handle;
            auto data = dispatcher.query(entityQuery);
            if (!data.has_value() || !data->parent.has_value() || !data->parent->isValid())
                return false;
            events::ui::HasUILayoutGroupComponentQuery layoutQuery;
            layoutQuery.entity = *data->parent;
            return dispatcher.query(layoutQuery);
        }
    }

    bool UIRectDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIRectComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasRect = dispatcher.query(hasQuery);

        if (!hasRect)
        {
            return false;
        }

        events::ui::GetUIRectDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIRectComponent");

        bool removeRect = false;
        bool isOpen = drawHeader(removeRect);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIRectData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Rect transform anchoring and layout");
            ImGui::Spacing();

            changed |= drawAnchors(data);
            ImGui::Spacing();
            changed |= drawPivot(data);
            ImGui::Spacing();
            changed |= drawSizeDelta(data);
            ImGui::Spacing();

            // A layout-group child's position is driven by the parent's layout every frame, so
            // disable the field (manual edits would be overwritten) and say why.
            const bool layoutControlled = isLayoutControlled(handle);
            if (layoutControlled) ImGui::BeginDisabled();
            changed |= drawAnchoredPosition(data);
            if (layoutControlled)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Position set by parent Layout Group");
            }
            ImGui::Spacing();
            changed |= drawBlocksRaycast(data);

            if (changed)
            {
                events::ui::SetUIRectDataCommand cmd;
                cmd.entity = handle;
                cmd.rectData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeRect)
        {
            events::ui::RemoveUIRectComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIRectDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIRectHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Rect");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIRect", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIRectDrawer::drawAnchors(services::UIRectData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Anchor Min##UIRect", &data.anchorMin.x, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }

        if (ImGui::DragFloat2("Anchor Max##UIRect", &data.anchorMax.x, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UIRectDrawer::drawPivot(services::UIRectData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Pivot##UIRect", &data.pivot.x, 0.01f, 0.0f, 1.0f, "%.2f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UIRectDrawer::drawSizeDelta(services::UIRectData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Size Delta##UIRect", &data.sizeDelta.x, 1.0f, 0.0f, 0.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UIRectDrawer::drawAnchoredPosition(services::UIRectData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat2("Anchored Position##UIRect", &data.anchoredPosition.x, 1.0f, 0.0f, 0.0f, "%.1f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UIRectDrawer::drawBlocksRaycast(services::UIRectData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Blocks Raycast##UIRect", &data.blocksRaycast))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When checked, the cursor over this element makes UI::isPointerOverUI()\nreturn true so game scripts skip world raycasts/picking behind it.");
        }

        return changed;
    }
}
