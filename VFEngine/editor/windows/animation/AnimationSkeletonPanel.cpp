#include "AnimationSkeletonPanel.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "imgui.h"
#include <glm/gtc/quaternion.hpp>

namespace windows::animation
{
    void AnimationSkeletonPanel::draw(const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                       const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                                       int& selectedChannel,
                                       bool& showBoneVisualization,
                                       bool meshLoadedInPreview,
                                       editor::OrbitCamera* camera,
                                       const std::unordered_set<std::string>* mappedBoneNames)
    {
        ImGui::Text("Skeleton Hierarchy");
        ImGui::Separator();

        if (evaluatedBones.empty())
        {
            ImGui::TextDisabled("No bones evaluated");
            return;
        }

        ImGui::Checkbox("Show Bones", &showBoneVisualization);
        ImGui::Separator();

        if (meshLoadedInPreview && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float itemWidth = ImGui::GetContentRegionAvail().x - 50.0f;

            float dist = camera->getDistance();
            float minDist = camera->getMinDistance();
            float maxDist = camera->getMaxDistance();

            ImGui::Text("Zoom");
            ImGui::SameLine(50.0f);
            ImGui::SetNextItemWidth(itemWidth);
            if (ImGui::SliderFloat("##Zoom", &dist, minDist, maxDist, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
                camera->setDistance(dist);
            }

            ImGui::Separator();
        }

        ImGui::Text("Bones");
        ImGui::Separator();

        auto rootIt = boneChildrenMap.find(-1);
        if (rootIt != boneChildrenMap.end())
        {
            for (size_t idx : rootIt->second)
            {
                drawBoneNode(idx, evaluatedBones, boneChildrenMap, selectedChannel, mappedBoneNames);
            }
        }
    }

    void AnimationSkeletonPanel::drawBoneNode(size_t index,
                                               const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                               const std::unordered_map<int32_t, std::vector<size_t>>& boneChildrenMap,
                                               int& selectedChannel,
                                               const std::unordered_set<std::string>* mappedBoneNames)
    {
        const auto& bone = evaluatedBones[index];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedChannel == static_cast<int>(index))
            flags |= ImGuiTreeNodeFlags_Selected;

        auto childIt = boneChildrenMap.find(static_cast<int32_t>(index));
        bool hasChildren = (childIt != boneChildrenMap.end() && !childIt->second.empty());
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

        bool hasMappedBody = mappedBoneNames && mappedBoneNames->count(bone.name) > 0;

        std::string label = hasMappedBody
            ? bone.name + " [P]"
            : bone.name;

        bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

        if (ImGui::IsItemClicked())
            selectedChannel = static_cast<int>(index);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Position: %.2f, %.2f, %.2f",
                        bone.position.x, bone.position.y, bone.position.z);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(bone.rotation));
            ImGui::Text("Rotation: %.1f, %.1f, %.1f", euler.x, euler.y, euler.z);
            ImGui::Text("Scale: %.2f, %.2f, %.2f",
                        bone.scale.x, bone.scale.y, bone.scale.z);
            ImGui::EndTooltip();
        }

        if (nodeOpen)
        {
            if (hasChildren)
            {
                for (size_t childIdx : childIt->second)
                {
                    drawBoneNode(childIdx, evaluatedBones, boneChildrenMap, selectedChannel, mappedBoneNames);
                }
            }
            ImGui::TreePop();
        }
    }
}
