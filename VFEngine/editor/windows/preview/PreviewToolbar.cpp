#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

namespace editor::preview
{
    bool PreviewToolbar::draw(PreviewEnvironment& env, OrbitCamera* camera, const math::AABB* bounds)
    {
        bool changed = false;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

        // Reset Camera
        if (ImGui::SmallButton(ICON_FA_EXPAND " Reset"))
        {
            if (bounds)
            {
                camera->fitToBounds(*bounds);
            }
            else
            {
                camera->target = glm::vec3(0.0f);
                camera->distance = 5.0f;
                camera->yaw = 45.0f;
                camera->pitch = 30.0f;
                camera->updateMatrices();
            }
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Vertical nudge: move the camera target up/down (effectively translating the
        // previewed model in the viewport). Step is proportional to camera distance so
        // the visual nudge feels consistent regardless of zoom level.
        const float nudgeStep = 0.1f * camera->distance;

        if (ImGui::SmallButton(ICON_FA_ARROW_UP "##NudgeUp"))
        {
            camera->target.y += nudgeStep;
            camera->updateMatrices();
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");

        ImGui::SameLine();

        if (ImGui::SmallButton(ICON_FA_ARROW_DOWN "##NudgeDown"))
        {
            camera->target.y -= nudgeStep;
            camera->updateMatrices();
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Toggle Grid
        if (ImGui::SmallButton(env.showGrid ? (ICON_FA_BORDER_ALL " Grid") : (ICON_FA_BORDER_NONE " Grid")))
        {
            env.showGrid = !env.showGrid;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Background mode dropdown
        ImGui::SetNextItemWidth(100.0f);
        const char* bgItems[] = { "Solid Color", "Gradient" };
        int bgIndex = static_cast<int>(env.backgroundMode);
        if (ImGui::Combo("##BgMode", &bgIndex, bgItems, 2))
        {
            env.backgroundMode = static_cast<BackgroundMode>(bgIndex);
            changed = true;
        }

        ImGui::PopStyleVar();

        return changed;
    }
}
