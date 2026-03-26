#include "NavmeshObstacleDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
    static const char* modeNames[] = {"Carve", "Avoidance Only"};
    static const char* shapeNames[] = {"Box", "Cylinder"};

    bool NavmeshObstacleDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::NavmeshObstacleComponent>(entity))
        {
            return false;
        }

        auto& obstacle = registry.get<components::NavmeshObstacleComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Navmesh Obstacle", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Mode");
            ImGui::PushItemWidth(-1);
            int modeIdx = static_cast<int>(obstacle.mode);
            if (ImGui::Combo("##NavObstacleMode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames)))
            {
                obstacle.mode = static_cast<components::NavmeshObstacleMode>(modeIdx);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Shape");
            ImGui::PushItemWidth(-1);
            int shapeIdx = static_cast<int>(obstacle.shape);
            if (ImGui::Combo("##NavObstacleShape", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames)))
            {
                obstacle.shape = static_cast<components::NavmeshObstacleShape>(shapeIdx);
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Size");
            ImGui::PushItemWidth(-1);
            if (obstacle.shape == components::NavmeshObstacleShape::Box)
            {
                ImGui::DragFloat3("##NavObstacleSize", &obstacle.size.x, 0.1f, 0.1f, 100.0f, "%.1f");
            }
            else
            {
                ImGui::DragFloat("##NavObstacleRadius", &obstacle.size.x, 0.01f, 0.1f, 50.0f, "Radius: %.2f");
                ImGui::DragFloat("##NavObstacleHeight", &obstacle.size.y, 0.1f, 0.1f, 50.0f, "Height: %.1f");
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Text("Offset");
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##NavObstacleOffset", &obstacle.offset.x, 0.1f, -100.0f, 100.0f, "%.1f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##NavObstacleThreshold", &obstacle.movementThreshold, 0.01f, 0.01f, 10.0f, "Move Threshold: %.2f");
            ImGui::PopItemWidth();

            if (obstacle.isRegistered)
            {
                ImGui::Spacing();
                if (obstacle.mode == components::NavmeshObstacleMode::AvoidanceOnly && obstacle.phantomAgentIndex >= 0)
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Phantom Agent #%d", obstacle.phantomAgentIndex);
                else
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Registered");
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveNavmeshObstacleComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
