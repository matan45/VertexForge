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

            // Tree path
            char pathBuf[256];
            strncpy(pathBuf, bt.behaviorTreePath.c_str(), sizeof(pathBuf) - 1);
            pathBuf[sizeof(pathBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 35.0f);
            if (ImGui::InputText("##TreePath", pathBuf, sizeof(pathBuf)))
            {
                bt.behaviorTreePath = pathBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button("...##btBrowse"))
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

            // Enabled toggle
            if (ImGui::Checkbox("Enabled", &bt.enabled))
            {
                events::ai::SetBehaviorTreeEnabledCommand cmd;
                cmd.entity = handle;
                cmd.enabled = bt.enabled;
                events::EventDispatcher::instance().execute(cmd);
            }

            // Attach/Detach buttons
            if (!bt.behaviorTreePath.empty())
            {
                if (!bt.isInitialized)
                {
                    if (ImGui::Button("Attach"))
                    {
                        events::ai::AttachBehaviorTreeCommand cmd;
                        cmd.entity = handle;
                        cmd.treePath = bt.behaviorTreePath;
                        bool result = events::EventDispatcher::instance().execute(cmd);
                        if (result) bt.isInitialized = true;
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Attached");
                    ImGui::SameLine();
                    if (ImGui::Button("Detach"))
                    {
                        events::ai::DetachBehaviorTreeCommand cmd;
                        cmd.entity = handle;
                        events::EventDispatcher::instance().execute(cmd);
                        bt.isInitialized = false;
                    }
                }
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
