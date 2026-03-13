#include "WorldSectorDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <imgui.h>

namespace windows::details {

    void WorldSectorDrawer::draw(services::EntityHandle handle)
    {
        auto enttEntity = services::internal::fromHandle(handle);
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(enttEntity)) return;

        scene::Entity entity(enttEntity);
        if (!entity.hasComponent<components::WorldSectorComponent>())
            return;

        const auto& wsComp = entity.getComponent<components::WorldSectorComponent>();

        ImGui::PushID("WorldSectorComponent");

        bool removeComponent = false;

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WorldSectorHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("World Sector");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveWorldSector", ImVec2(18, 18)))
        {
            removeComponent = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);
            ImGui::Text("World File: %s", wsComp.worldFilePath.c_str());

            auto& dispatcher = events::EventDispatcher::instance();
            bool isWorld = dispatcher.query(events::world::IsWorldModeQuery{});

            if (isWorld)
            {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "World Active");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "World Not Loaded");
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            entity.removeComponent<components::WorldSectorComponent>();

            auto& dispatcher = events::EventDispatcher::instance();
            events::world::ClearWorldCommand cmd;
            dispatcher.execute(cmd);
        }
    }

}
