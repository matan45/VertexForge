#include "NavInvokerDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details
{
    bool NavInvokerDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::NavInvokerComponent>(entity))
        {
            return false;
        }

        auto& invoker = registry.get<components::NavInvokerComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Nav Invoker", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##NavInvokerGenRadius", &invoker.generationRadius, 1.0f, 16.0f, 2048.0f, "Gen Radius: %.0f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##NavInvokerUnloadMult", &invoker.unloadRadiusMultiplier, 0.01f, 1.0f, 2.0f, "Unload Mult: %.2f");
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Checkbox("Save Generated To Cache", &invoker.saveGeneratedToCache);

            if (invoker.isActive)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveNavInvokerComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
