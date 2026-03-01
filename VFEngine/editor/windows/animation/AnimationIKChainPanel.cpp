#include "AnimationIKChainPanel.hpp"
#include "MeshIKChainWriter.hpp"
#include "../../../services/events/IKEvents.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>

namespace windows::animation
{
    bool AnimationIKChainPanel::draw(std::vector<animator::ik::IKChainConfig>& chains,
                                      int& selectedChannel,
                                      const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                      const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                      const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                                      bool& showIKVisualization,
                                      const std::string& meshPath)
    {
        ImGui::Text("IK Chains");
        ImGui::Separator();

        ImGui::Checkbox("Show IK Chains", &showIKVisualization);
        ImGui::Separator();

        bool changed = false;

        if (evaluatedBones.empty())
        {
            ImGui::TextDisabled("No skeleton loaded");
            return false;
        }

        changed |= drawNewChainCreation(chains, evaluatedBones, boneNameToIndex, selectedChannel);

        ImGui::Spacing();
        ImGui::Text("Chain List (%zu)", chains.size());
        ImGui::Separator();

        if (chains.empty())
        {
            ImGui::TextDisabled("No IK chains defined");
            return changed;
        }

        drawChainList(chains);

        if (selectedChainIndex >= 0 && selectedChainIndex < static_cast<int>(chains.size()))
        {
            ImGui::Spacing();
            ImGui::Separator();
            changed |= drawChainEditor(chains[selectedChainIndex], evaluatedBones, boneNameToIndex);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Delete Chain"))
            {
                chains.erase(chains.begin() + selectedChainIndex);
                selectedChainIndex = -1;
                changed = true;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Separator();
        drawSaveButton(chains, meshPath);

        return changed;
    }

    void AnimationIKChainPanel::drawChainList(std::vector<animator::ik::IKChainConfig>& chains)
    {
        for (int i = 0; i < static_cast<int>(chains.size()); ++i)
        {
            const auto& chain = chains[i];
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedChainIndex == i)
                flags |= ImGuiTreeNodeFlags_Selected;

            std::string label = chain.chainName + " -> " + chain.tipBoneName;
            bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

            if (ImGui::IsItemClicked())
                selectedChainIndex = i;

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Tip Bone: %s", chain.tipBoneName.c_str());
                ImGui::Text("Bones: %zu", chain.chainBoneNames.size());
                ImGui::Text("Weight: %.2f", chain.weight);
                ImGui::Text("Enabled: %s", chain.enabled ? "Yes" : "No");
                ImGui::EndTooltip();
            }

            if (nodeOpen)
                ImGui::TreePop();
        }
    }

    bool AnimationIKChainPanel::drawChainEditor(animator::ik::IKChainConfig& chain,
                                                  const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                                  const std::unordered_map<std::string, size_t>& boneNameToIndex)
    {
        bool changed = false;

        if (ImGui::CollapsingHeader("Chain Properties", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            ImGui::Text("Name: %s", chain.chainName.c_str());

            if (ImGui::BeginCombo("Tip Bone", chain.tipBoneName.c_str()))
            {
                for (size_t i = 0; i < evaluatedBones.size(); ++i)
                {
                    bool isSelected = (evaluatedBones[i].name == chain.tipBoneName);
                    if (ImGui::Selectable(evaluatedBones[i].name.c_str(), isSelected))
                    {
                        chain.tipBoneName = evaluatedBones[i].name;
                        chain.chainBoneNames = walkHierarchyUp(
                            static_cast<int>(i),
                            static_cast<int>(chain.chainBoneNames.size()),
                            evaluatedBones);
                        chain.constraints.resize(chain.chainBoneNames.size());
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::SliderFloat("Weight", &chain.weight, 0.0f, 1.0f, "%.2f"))
                changed = true;

            if (ImGui::Checkbox("Enabled", &chain.enabled))
                changed = true;

            if (ImGui::TreeNode("Chain Bones"))
            {
                for (int b = 0; b < static_cast<int>(chain.chainBoneNames.size()); ++b)
                {
                    ImGui::PushID(b);

                    const char* currentBone = chain.chainBoneNames[b].c_str();
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::BeginCombo("##BoneName", currentBone))
                    {
                        for (size_t j = 0; j < evaluatedBones.size(); ++j)
                        {
                            bool isSelected = (evaluatedBones[j].name == chain.chainBoneNames[b]);
                            if (ImGui::Selectable(evaluatedBones[j].name.c_str(), isSelected))
                            {
                                chain.chainBoneNames[b] = evaluatedBones[j].name;
                                changed = true;
                            }
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    if (ImGui::SmallButton("-"))
                    {
                        chain.chainBoneNames.erase(chain.chainBoneNames.begin() + b);
                        if (b < static_cast<int>(chain.constraints.size()))
                            chain.constraints.erase(chain.constraints.begin() + b);
                        changed = true;
                        ImGui::PopID();
                        break;
                    }

                    ImGui::PopID();
                }

                if (ImGui::SmallButton("+ Add Bone"))
                {
                    chain.chainBoneNames.emplace_back("");
                    chain.constraints.emplace_back();
                    changed = true;
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Constraints"))
            {
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

            ImGui::Unindent(10.0f);
        }

        return changed;
    }

    bool AnimationIKChainPanel::drawNewChainCreation(std::vector<animator::ik::IKChainConfig>& chains,
                                                       const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                                       const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                                       int selectedChannel)
    {
        if (ImGui::CollapsingHeader("Create IK Chain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            ImGui::InputText("Name", newChainName, sizeof(newChainName));

            std::string tipBone = "None";
            if (selectedChannel >= 0 && selectedChannel < static_cast<int>(evaluatedBones.size()))
            {
                tipBone = evaluatedBones[selectedChannel].name;
            }
            ImGui::Text("Tip Bone: %s", tipBone.c_str());
            ImGui::TextDisabled("(Select a bone in the Skeleton panel)");

            ImGui::SliderInt("Chain Length", &chainLength, 1, 10);

            // Preview the chain that would be created
            if (selectedChannel >= 0 && selectedChannel < static_cast<int>(evaluatedBones.size()))
            {
                auto previewBones = walkHierarchyUp(selectedChannel, chainLength, evaluatedBones);
                if (!previewBones.empty())
                {
                    ImGui::TextDisabled("Preview:");
                    for (size_t i = 0; i < previewBones.size(); ++i)
                    {
                        ImGui::TextDisabled("  %zu: %s%s", i, previewBones[i].c_str(),
                                            (i == previewBones.size() - 1) ? " (tip)" : "");
                    }
                }
            }

            bool canCreate = std::strlen(newChainName) > 0 &&
                             selectedChannel >= 0 &&
                             selectedChannel < static_cast<int>(evaluatedBones.size());

            if (canCreate)
            {
                for (const auto& existing : chains)
                {
                    if (existing.chainName == newChainName)
                    {
                        canCreate = false;
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Name already exists");
                        break;
                    }
                }
            }

            if (!canCreate) ImGui::BeginDisabled();
            bool addClicked = ImGui::Button("Add Chain");
            if (!canCreate) ImGui::EndDisabled();

            if (addClicked)
            {
                animator::ik::IKChainConfig newChain;
                newChain.chainName = newChainName;
                newChain.tipBoneName = evaluatedBones[selectedChannel].name;
                newChain.chainBoneNames = walkHierarchyUp(selectedChannel, chainLength, evaluatedBones);
                newChain.constraints.resize(newChain.chainBoneNames.size());
                chains.push_back(std::move(newChain));

                newChainName[0] = '\0';
                selectedChainIndex = static_cast<int>(chains.size()) - 1;

                ImGui::Unindent(10.0f);
                return true;
            }

            ImGui::Unindent(10.0f);
        }

        return false;
    }

    void AnimationIKChainPanel::drawConstraintEditor(animator::ik::JointConstraint& constraint, int boneIdx)
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

    void AnimationIKChainPanel::drawSaveButton(const std::vector<animator::ik::IKChainConfig>& chains,
                                                const std::string& meshPath)
    {
        if (saveMessageTimer > 0.0f)
        {
            saveMessageTimer -= ImGui::GetIO().DeltaTime;
        }

        bool canSave = !meshPath.empty() && !chains.empty();

        if (!canSave) ImGui::BeginDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
        if (ImGui::Button("Save IK Chains to Mesh"))
        {
            saveSuccess = types::MeshIKChainWriter::saveIKChainsToMesh(meshPath, chains);
            saveMessageTimer = 3.0f;
            if (saveSuccess)
            {
                events::ik::IKChainDataSavedNotification notif;
                notif.meshPath = meshPath;
                events::EventDispatcher::instance().publish(notif);
            }
        }
        ImGui::PopStyleColor(2);

        if (!canSave) ImGui::EndDisabled();

        if (meshPath.empty())
        {
            ImGui::TextDisabled("Load a mesh first to save IK chains");
        }
        else if (chains.empty())
        {
            ImGui::TextDisabled("No IK chains to save");
        }

        if (saveMessageTimer > 0.0f)
        {
            if (saveSuccess)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "IK chains saved!");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Save failed! Check log.");
            }
        }
    }

    std::vector<std::string> AnimationIKChainPanel::walkHierarchyUp(
        int tipBoneIndex, int length,
        const std::vector<services::EvaluatedBoneInfo>& evaluatedBones)
    {
        std::vector<std::string> result;

        if (tipBoneIndex < 0 || tipBoneIndex >= static_cast<int>(evaluatedBones.size()))
            return result;

        // Walk from tip up toward root, collecting bone names
        int currentIdx = tipBoneIndex;
        for (int i = 0; i < length && currentIdx >= 0; ++i)
        {
            result.push_back(evaluatedBones[currentIdx].name);
            currentIdx = evaluatedBones[currentIdx].parentIndex;
        }

        // Reverse to get root-to-tip order (matching IKChainConfig::chainBoneNames convention)
        std::reverse(result.begin(), result.end());
        return result;
    }
}
