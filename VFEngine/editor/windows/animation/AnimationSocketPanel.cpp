#include "AnimationSocketPanel.hpp"
#include "MeshSocketWriter.hpp"
#include "../../../services/events/physics/SocketEvents.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "imgui.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

namespace windows::animation
{
    bool AnimationSocketPanel::draw(std::vector<animator::SocketDefinition>& sockets,
                                     int& selectedChannel,
                                     const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                     const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                     bool& showSocketVisualization,
                                     const std::string& meshPath)
    {
        ImGui::Text("Sockets");
        ImGui::Separator();

        ImGui::Checkbox("Show Sockets", &showSocketVisualization);
        ImGui::Separator();

        bool changed = false;

        if (evaluatedBones.empty())
        {
            ImGui::TextDisabled("No skeleton loaded");
            return false;
        }

        changed |= drawNewSocketCreation(sockets, evaluatedBones, boneNameToIndex, selectedChannel);

        ImGui::Spacing();
        ImGui::Text("Socket List (%zu)", sockets.size());
        ImGui::Separator();

        if (sockets.empty())
        {
            ImGui::TextDisabled("No sockets defined");
            return changed;
        }

        drawSocketList(sockets, selectedSocketIndex);

        if (selectedSocketIndex >= 0 && selectedSocketIndex < static_cast<int>(sockets.size()))
        {
            ImGui::Spacing();
            ImGui::Separator();
            changed |= drawSocketEditor(sockets[selectedSocketIndex], evaluatedBones, boneNameToIndex);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Delete Socket"))
            {
                sockets.erase(sockets.begin() + selectedSocketIndex);
                selectedSocketIndex = -1;
                changed = true;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Separator();
        drawSaveButton(sockets, meshPath);

        return changed;
    }

    void AnimationSocketPanel::drawSocketList(std::vector<animator::SocketDefinition>& sockets, int& selectedSocket)
    {
        for (int i = 0; i < static_cast<int>(sockets.size()); ++i)
        {
            const auto& socket = sockets[i];
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selectedSocket == i)
                flags |= ImGuiTreeNodeFlags_Selected;

            std::string label = socket.name + " -> " + socket.targetBoneName;
            bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);

            if (ImGui::IsItemClicked())
                selectedSocket = i;

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Bone: %s", socket.targetBoneName.c_str());
                ImGui::Text("Position: %.2f, %.2f, %.2f",
                            socket.localPosition.x, socket.localPosition.y, socket.localPosition.z);
                ImGui::EndTooltip();
            }

            if (nodeOpen)
                ImGui::TreePop();
        }
    }

    bool AnimationSocketPanel::drawSocketEditor(animator::SocketDefinition& socket,
                                                 const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                                 const std::unordered_map<std::string, size_t>& boneNameToIndex)
    {
        bool changed = false;

        if (ImGui::CollapsingHeader("Socket Properties", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            ImGui::Text("Name: %s", socket.name.c_str());

            if (ImGui::BeginCombo("Target Bone", socket.targetBoneName.c_str()))
            {
                for (size_t i = 0; i < evaluatedBones.size(); ++i)
                {
                    bool isSelected = (evaluatedBones[i].name == socket.targetBoneName);
                    if (ImGui::Selectable(evaluatedBones[i].name.c_str(), isSelected))
                    {
                        socket.targetBoneName = evaluatedBones[i].name;
                        auto it = boneNameToIndex.find(evaluatedBones[i].name);
                        socket.boneIndex = (it != boneNameToIndex.end())
                            ? static_cast<int32_t>(it->second)
                            : -1;
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            float pos[3] = {socket.localPosition.x, socket.localPosition.y, socket.localPosition.z};
            if (ImGui::DragFloat3("Position", pos, 0.01f))
            {
                socket.localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                changed = true;
            }

            glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(socket.localRotation));
            float rot[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
            if (ImGui::DragFloat3("Rotation", rot, 0.5f))
            {
                socket.localRotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                changed = true;
            }

            ImGui::Unindent(10.0f);
        }

        return changed;
    }

    bool AnimationSocketPanel::drawNewSocketCreation(std::vector<animator::SocketDefinition>& sockets,
                                                      const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                                      const std::unordered_map<std::string, size_t>& boneNameToIndex,
                                                      int selectedChannel)
    {
        if (ImGui::CollapsingHeader("Create Socket", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            ImGui::InputText("Name", newSocketName, sizeof(newSocketName));

            std::string targetBone = "None";
            if (selectedChannel >= 0 && selectedChannel < static_cast<int>(evaluatedBones.size()))
            {
                targetBone = evaluatedBones[selectedChannel].name;
            }
            ImGui::Text("Target Bone: %s", targetBone.c_str());
            ImGui::TextDisabled("(Select a bone in the Skeleton panel)");

            float rot[3] = {newSocketEulerDeg.x, newSocketEulerDeg.y, newSocketEulerDeg.z};
            if (ImGui::DragFloat3("Rotation", rot, 0.5f))
            {
                newSocketEulerDeg = glm::vec3(rot[0], rot[1], rot[2]);
            }

            bool canCreate = std::strlen(newSocketName) > 0 &&
                             selectedChannel >= 0 &&
                             selectedChannel < static_cast<int>(evaluatedBones.size());

            if (canCreate)
            {
                for (const auto& existing : sockets)
                {
                    if (existing.name == newSocketName)
                    {
                        canCreate = false;
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Name already exists");
                        break;
                    }
                }
            }

            if (!canCreate) ImGui::BeginDisabled();
            bool addClicked = ImGui::Button("Add Socket");
            if (!canCreate) ImGui::EndDisabled();

            if (addClicked)
            {
                animator::SocketDefinition newSocket;
                newSocket.name = newSocketName;
                newSocket.targetBoneName = evaluatedBones[selectedChannel].name;
                auto it = boneNameToIndex.find(newSocket.targetBoneName);
                newSocket.boneIndex = (it != boneNameToIndex.end())
                    ? static_cast<int32_t>(it->second)
                    : -1;
                newSocket.localRotation = glm::quat(glm::radians(newSocketEulerDeg));
                sockets.push_back(newSocket);

                newSocketName[0] = '\0';
                newSocketEulerDeg = glm::vec3(0.0f);
                selectedSocketIndex = static_cast<int>(sockets.size()) - 1;

                ImGui::Unindent(10.0f);
                return true;
            }

            ImGui::Unindent(10.0f);
        }

        return false;
    }

    void AnimationSocketPanel::drawSaveButton(const std::vector<animator::SocketDefinition>& sockets,
                                               const std::string& meshPath)
    {
        if (saveMessageTimer > 0.0f)
        {
            saveMessageTimer -= ImGui::GetIO().DeltaTime;
        }

        bool canSave = !meshPath.empty() && !sockets.empty();

        if (!canSave) ImGui::BeginDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
        if (ImGui::Button("Save Sockets to Mesh"))
        {
            saveSuccess = types::MeshSocketWriter::saveSocketsToMesh(meshPath, sockets);
            saveMessageTimer = 3.0f;
            if (saveSuccess)
            {
                events::socket::SocketDataSavedNotification notif;
                notif.meshPath = meshPath;
                events::EventDispatcher::instance().publish(notif);
            }
        }
        ImGui::PopStyleColor(2);

        if (!canSave) ImGui::EndDisabled();

        if (meshPath.empty())
        {
            ImGui::TextDisabled("Load a mesh first to save sockets");
        }
        else if (sockets.empty())
        {
            ImGui::TextDisabled("No sockets to save");
        }

        if (saveMessageTimer > 0.0f)
        {
            if (saveSuccess)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Sockets saved!");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Save failed! Check log.");
            }
        }
    }
}
