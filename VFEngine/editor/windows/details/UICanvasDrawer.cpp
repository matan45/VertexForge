#include "UICanvasDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/UIEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool UICanvasDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUICanvasComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasCanvas = dispatcher.query(hasQuery);

        if (!hasCanvas)
        {
            return false;
        }

        events::ui::GetUICanvasDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UICanvasComponent");

        bool removeCanvas = false;
        bool isOpen = drawHeader(removeCanvas);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UICanvasData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Canvas reference resolution and scaling");
            ImGui::Spacing();

            changed |= drawReferenceResolution(data);
            ImGui::Spacing();
            changed |= drawScaleMode(data);
            ImGui::Spacing();
            changed |= drawPixelsPerUnit(data);

            if (changed)
            {
                events::ui::SetUICanvasDataCommand cmd;
                cmd.entity = handle;
                cmd.canvasData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeCanvas)
        {
            events::ui::RemoveUICanvasComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UICanvasDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UICanvasHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Canvas");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUICanvas", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UICanvasDrawer::drawReferenceResolution(services::UICanvasData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Reference Width##UICanvas", &data.referenceWidth, 1.0f, 1.0f, 7680.0f, "%.0f"))
        {
            changed = true;
        }

        if (ImGui::DragFloat("Reference Height##UICanvas", &data.referenceHeight, 1.0f, 1.0f, 4320.0f, "%.0f"))
        {
            changed = true;
        }

        return changed;
    }

    bool UICanvasDrawer::drawScaleMode(services::UICanvasData& data)
    {
        bool changed = false;

        const char* modes[] = {"Constant Pixel Size", "Scale With Screen Size"};
        int currentMode = static_cast<int>(data.scaleMode);

        if (ImGui::Combo("Scale Mode##UICanvas", &currentMode, modes, 2))
        {
            data.scaleMode = static_cast<uint8_t>(currentMode);
            changed = true;
        }

        return changed;
    }

    bool UICanvasDrawer::drawPixelsPerUnit(services::UICanvasData& data)
    {
        bool changed = false;

        if (ImGui::DragFloat("Pixels Per Unit##UICanvas", &data.pixelsPerUnit, 1.0f, 1.0f, 1000.0f, "%.0f"))
        {
            changed = true;
        }

        return changed;
    }
}
