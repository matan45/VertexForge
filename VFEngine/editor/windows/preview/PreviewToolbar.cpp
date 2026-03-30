#include "PreviewToolbar.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <imgui.h>
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

        // Toggle Grid
        if (ImGui::SmallButton(env.showGrid ? (ICON_FA_BORDER_ALL " Grid") : (ICON_FA_BORDER_NONE " Grid")))
        {
            env.showGrid = !env.showGrid;
            changed = true;
        }

        ImGui::SameLine();

        // Toggle Lighting
        const char* lightLabel = (env.lightingMode == LightingMode::ThreePoint)
            ? ICON_FA_LIGHTBULB " 3-Point"
            : ICON_FA_SUN " Default";
        if (ImGui::SmallButton(lightLabel))
        {
            env.lightingMode = (env.lightingMode == LightingMode::Default)
                ? LightingMode::ThreePoint
                : LightingMode::Default;
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
