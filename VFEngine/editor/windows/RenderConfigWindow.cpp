#include "RenderConfigWindow.hpp"
#include <imgui.h>

namespace windows
{
    void RenderConfigWindow::show()
    {
        visible = true;
    }

    void RenderConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Render Configuration", &visible))
        {
            ImGui::TextDisabled("Render settings coming soon...");
            ImGui::Spacing();
            ImGui::TextDisabled("This window will contain scene-level rendering options.");
        }
        ImGui::End();
    }
}
