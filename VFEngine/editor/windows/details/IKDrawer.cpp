#include "IKDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/IKEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "animator/IKTypes.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace windows::details
{
    static void drawConstraintEditor(animator::ik::JointConstraint& constraint, int boneIdx)
    {
        ImGui::PushID(boneIdx);

        const char* typeLabels[] = {"None", "Hinge", "Cone", "Ball & Socket"};
        int currentType = static_cast<int>(constraint.type);

        ImGui::PushItemWidth(120.0f);
        if (ImGui::Combo("##ConstraintType", &currentType, typeLabels, IM_ARRAYSIZE(typeLabels)))
        {
            constraint.type = static_cast<animator::ik::JointConstraintType>(currentType);
        }
        ImGui::PopItemWidth();

        switch (constraint.type)
        {
        case animator::ik::JointConstraintType::Hinge:
            ImGui::DragFloat3("##HingeAxis", glm::value_ptr(constraint.hingeAxis), 0.01f, -1.0f, 1.0f,
                              "Axis: %.2f");
            break;
        case animator::ik::JointConstraintType::Cone:
        {
            float coneDeg = glm::degrees(constraint.coneAngle);
            if (ImGui::DragFloat("##ConeAngle", &coneDeg, 0.5f, 0.0f, 180.0f, "Cone: %.1f deg"))
                constraint.coneAngle = glm::radians(coneDeg);
            break;
        }
        case animator::ik::JointConstraintType::BallAndSocket:
        {
            float swingDeg = glm::degrees(constraint.swingAngle);
            float twistMinDeg = glm::degrees(constraint.twistMin);
            float twistMaxDeg = glm::degrees(constraint.twistMax);

            if (ImGui::DragFloat("##SwingAngle", &swingDeg, 0.5f, 0.0f, 180.0f, "Swing: %.1f deg"))
                constraint.swingAngle = glm::radians(swingDeg);
            if (ImGui::DragFloat("##TwistMin", &twistMinDeg, 0.5f, -180.0f, 0.0f, "Twist Min: %.1f deg"))
                constraint.twistMin = glm::radians(twistMinDeg);
            if (ImGui::DragFloat("##TwistMax", &twistMaxDeg, 0.5f, 0.0f, 180.0f, "Twist Max: %.1f deg"))
                constraint.twistMax = glm::radians(twistMaxDeg);
            break;
        }
        default:
            break;
        }

        ImGui::PopID();
    }

    bool IKDrawer::draw(services::EntityHandle handle)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!registry.valid(entity) || !registry.all_of<components::IKTargetComponent>(entity))
            return false;

        auto& ikComp = registry.get<components::IKTargetComponent>(entity);

        bool open = true;
        if (ImGui::CollapsingHeader("Inverse Kinematics", &open, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            // Draw existing chains
            int chainToRemove = -1;
            for (int i = 0; i < static_cast<int>(ikComp.chains.size()); ++i)
            {
                auto& chain = ikComp.chains[i];
                ImGui::PushID(i);

                bool chainOpen = ImGui::TreeNodeEx(chain.chainName.c_str(),
                                                   ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                // Remove chain button
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
                if (ImGui::SmallButton("x"))
                    chainToRemove = i;
                ImGui::PopStyleColor(2);

                if (chainOpen)
                {
                    // Chain name
                    char nameBuffer[64];
                    std::strncpy(nameBuffer, chain.chainName.c_str(), sizeof(nameBuffer));
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        chain.chainName = nameBuffer;
                        ikComp.isInitialized = false;
                    }

                    // Tip bone name
                    char tipBuffer[128];
                    std::strncpy(tipBuffer, chain.tipBoneName.c_str(), sizeof(tipBuffer));
                    tipBuffer[sizeof(tipBuffer) - 1] = '\0';
                    if (ImGui::InputText("Tip Bone", tipBuffer, sizeof(tipBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        chain.tipBoneName = tipBuffer;
                        ikComp.isInitialized = false;
                    }

                    // Weight slider
                    ImGui::SliderFloat("Weight", &chain.weight, 0.0f, 1.0f, "%.2f");

                    // Enabled toggle
                    ImGui::Checkbox("Enabled", &chain.enabled);

                    // Chain bone names
                    if (ImGui::TreeNode("Chain Bones"))
                    {
                        int boneToRemove = -1;
                        for (int b = 0; b < static_cast<int>(chain.chainBoneNames.size()); ++b)
                        {
                            ImGui::PushID(b);
                            char boneBuffer[128];
                            std::strncpy(boneBuffer, chain.chainBoneNames[b].c_str(), sizeof(boneBuffer));
                            boneBuffer[sizeof(boneBuffer) - 1] = '\0';

                            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                            if (ImGui::InputText("##BoneName", boneBuffer, sizeof(boneBuffer),
                                                 ImGuiInputTextFlags_EnterReturnsTrue))
                            {
                                chain.chainBoneNames[b] = boneBuffer;
                                ikComp.isInitialized = false;
                            }
                            ImGui::PopItemWidth();

                            ImGui::SameLine();
                            if (ImGui::SmallButton("-"))
                                boneToRemove = b;

                            ImGui::PopID();
                        }

                        if (boneToRemove >= 0)
                        {
                            chain.chainBoneNames.erase(chain.chainBoneNames.begin() + boneToRemove);
                            ikComp.isInitialized = false;
                        }

                        if (ImGui::SmallButton("+ Add Bone"))
                        {
                            chain.chainBoneNames.emplace_back("");
                        }

                        ImGui::TreePop();
                    }

                    // Constraints
                    if (ImGui::TreeNode("Constraints"))
                    {
                        // Ensure constraints vector matches bone count
                        if (chain.constraints.size() != chain.chainBoneNames.size())
                            chain.constraints.resize(chain.chainBoneNames.size());

                        for (int b = 0; b < static_cast<int>(chain.constraints.size()); ++b)
                        {
                            std::string label = (b < static_cast<int>(chain.chainBoneNames.size()) &&
                                                 !chain.chainBoneNames[b].empty())
                                                    ? chain.chainBoneNames[b]
                                                    : "Bone " + std::to_string(b);

                            if (ImGui::TreeNode(label.c_str()))
                            {
                                drawConstraintEditor(chain.constraints[b], b);
                                ImGui::TreePop();
                            }
                        }

                        ImGui::TreePop();
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }

            // Remove chain if requested
            if (chainToRemove >= 0)
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::ik::RemoveIKChainCommand cmd;
                cmd.entity = handle;
                cmd.chainName = ikComp.chains[chainToRemove].chainName;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Add new chain
            ImGui::PushItemWidth(100.0f);
            ImGui::InputText("##NewChainName", newChainName, sizeof(newChainName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Add Chain"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::ik::AddIKChainCommand cmd;
                cmd.entity = handle;
                cmd.chainName = newChainName;
                cmd.tipBoneName = "";
                dispatcher.execute(cmd);
            }

            ImGui::Unindent();
        }

        if (!open)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::ik::RemoveIKComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
            return false;
        }

        return true;
    }
}
