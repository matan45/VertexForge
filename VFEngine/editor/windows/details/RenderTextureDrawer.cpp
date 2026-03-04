#include "RenderTextureDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool RenderTextureDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasRenderTextureComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetRenderTextureDataQuery getQuery;
        getQuery.entity = handle;
        auto dataOpt = dispatcher.query(getQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("RenderTextureComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::RenderTextureData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Renders camera view to a texture");
            ImGui::Spacing();

            changed |= drawResolution(data);
            ImGui::Spacing();
            changed |= drawSettings(data);

            if (changed)
            {
                events::scene::SetRenderTextureDataCommand cmd;
                cmd.entity = handle;
                cmd.renderTextureData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemoveRenderTextureComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool RenderTextureDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##RenderTextureHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Render Texture");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveRenderTexture", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool RenderTextureDrawer::drawResolution(services::RenderTextureData& data)
    {
        bool changed = false;

        int w = static_cast<int>(data.width);
        int h = static_cast<int>(data.height);

        if (ImGui::DragInt("Width##RT", &w, 1.0f, 64, 4096))
        {
            data.width = static_cast<uint32_t>(std::max(64, w));
            changed = true;
        }

        if (ImGui::DragInt("Height##RT", &h, 1.0f, 64, 4096))
        {
            data.height = static_cast<uint32_t>(std::max(64, h));
            changed = true;
        }

        return changed;
    }

    bool RenderTextureDrawer::drawSettings(services::RenderTextureData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Enabled##RT", &data.enabled))
        {
            changed = true;
        }

        const char* updateModes[] = {"Every Frame", "On Demand", "Fixed Interval"};
        int currentMode = static_cast<int>(data.updateMode);
        if (ImGui::Combo("Update Mode##RT", &currentMode, updateModes, IM_ARRAYSIZE(updateModes)))
        {
            data.updateMode = static_cast<uint8_t>(currentMode);
            changed = true;
        }

        if (data.updateMode == 2) // FixedInterval
        {
            if (ImGui::DragFloat("Interval (s)##RT", &data.fixedIntervalSeconds, 0.001f, 0.001f, 10.0f, "%.3f"))
            {
                changed = true;
            }
        }

        if (ImGui::ColorEdit4("Clear Color##RT", &data.clearColor.x))
        {
            changed = true;
        }

        int priority = static_cast<int>(data.priority);
        if (ImGui::DragInt("Priority##RT", &priority, 1.0f, 0, 100))
        {
            data.priority = static_cast<uint32_t>(std::max(0, priority));
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Lower priority renders first");
        }

        return changed;
    }
}
