#include "BehaviorTreeDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>

namespace windows::details
{
    bool BehaviorTreeDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::BehaviorTreeComponent>(entity))
        {
            return false;
        }

        auto& bt = registry.get<components::BehaviorTreeComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Behavior Tree", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            // Tree path display (read-only text, like material drawer)
            ImGui::Text("Tree:");
            std::string display = bt.behaviorTreePath.empty() ? "(None)" : bt.behaviorTreePath;
            if (display.length() > 35)
            {
                display = "..." + display.substr(display.length() - 32);
            }
            ImGui::TextDisabled("%s", display.c_str());

            ImGui::SameLine();
            if (ImGui::Button("Browse##BT"))
            {
                static const std::vector<std::pair<std::wstring, std::wstring>> BT_FILE_TYPES = {
                    {L"Behavior Tree", L"*.vfBehaviorTree"}
                };
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(BT_FILE_TYPES);
                if (!path.empty())
                {
                    bt.behaviorTreePath = path;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear##BT"))
            {
                if (bt.isInitialized)
                {
                    events::ai::DetachBehaviorTreeCommand cmd;
                    cmd.entity = handle;
                    events::EventDispatcher::instance().execute(cmd);
                    bt.isInitialized = false;
                }
                bt.behaviorTreePath.clear();
            }

            if (ImGui::Checkbox("Enabled", &bt.enabled))
            {
                events::ai::SetBehaviorTreeEnabledCommand cmd;
                cmd.entity = handle;
                cmd.enabled = bt.enabled;
                events::EventDispatcher::instance().execute(cmd);
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            events::scene::RemoveBehaviorTreeComponentCommand cmd;
            cmd.entity = handle;
            events::EventDispatcher::instance().execute(cmd);
            return false;
        }

        return true;
    }
}
