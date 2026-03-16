#include "IBLDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include <imgui.h>

namespace windows::details {

    void IBLDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasIBLComponentQuery hasIBLQuery;
        hasIBLQuery.entity = handle;
        bool hasIBL = dispatcher.query(hasIBLQuery);

        if (!hasIBL)
            return;

        events::scene::GetIBLDataQuery iblQuery;
        iblQuery.entity = handle;
        auto iblOpt = dispatcher.query(iblQuery);

        if (!iblOpt.has_value())
            return;

        ImGui::PushID("IBLComponent");

        bool removeIBL = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##IBLHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("IBL");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveIBL", ImVec2(18, 18)))
        {
            removeIBL = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);
            ImGui::Text("File: %s", iblOpt->hdrRef.resolve().c_str());
            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeIBL)
        {
            events::scene::RemoveIBLComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            events::render::RemoveIBLCommand removeRenderCmd;
            dispatcher.execute(removeRenderCmd);
        }
    }

}
