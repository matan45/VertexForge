#include "MaterialPropertyPanel.hpp"
#include "../../graph/ShaderGraphEditor.hpp"
#include <nfd/FileDialog.hpp>
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace editor::materialeditor
{
    void MaterialPropertyPanel::notifyChanged()
    {
        if (onPropertyChanged) {
            onPropertyChanged();
        }
    }

    void MaterialPropertyPanel::drawParameterPanel(std::shared_ptr<::material::MaterialData> materialData)
    {
        ImGui::Text("Parameters");
        ImGui::Separator();

        if (!materialData) {
            ImGui::TextDisabled("No material");
            return;
        }

        for (auto& [name, param] : materialData->parameters) {
            ImGui::PushID(name.c_str());

            switch (param.type) {
                case ::material::ParameterType::Scalar: {
                    if (std::holds_alternative<float>(param.value)) {
                        float value = std::get<float>(param.value);
                        if (ImGui::SliderFloat(name.c_str(), &value, param.min, param.max)) {
                            param.value = value;
                            notifyChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec2: {
                    if (std::holds_alternative<glm::vec2>(param.value)) {
                        glm::vec2 value = std::get<glm::vec2>(param.value);
                        if (ImGui::DragFloat2(name.c_str(), &value.x, 0.01f)) {
                            param.value = value;
                            notifyChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec3: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        glm::vec3 v3(value);
                        if (ImGui::DragFloat3(name.c_str(), &v3.x, 0.01f)) {
                            param.value = glm::vec4(v3, value.w);
                            notifyChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec4: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        if (ImGui::DragFloat4(name.c_str(), &value.x, 0.01f)) {
                            param.value = value;
                            notifyChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Color: {
                    if (std::holds_alternative<glm::vec4>(param.value)) {
                        glm::vec4 value = std::get<glm::vec4>(param.value);
                        if (ImGui::ColorEdit4(name.c_str(), &value.x)) {
                            param.value = value;
                            notifyChanged();
                        }
                    }
                    break;
                }
            }

            ImGui::PopID();
        }

        if (materialData->parameters.empty()) {
            ImGui::TextDisabled("No exposed parameters");
        }
    }

    void MaterialPropertyPanel::drawPropertiesPanel(
        std::shared_ptr<::material::MaterialData> materialData,
        editor::graph::ShaderGraphEditor* graphEditor)
    {
        ImGui::Text("Node Properties");
        ImGui::Separator();

        if (!graphEditor || !materialData) {
            ImGui::TextDisabled("No material");
            return;
        }

        uint32_t selectedId = graphEditor->getSelectedNodeId();
        if (selectedId == 0) {
            ImGui::TextDisabled("Select a node to edit properties");
            return;
        }

        ::material::ShaderNode* selectedNode = materialData->graph.findNode(selectedId);
        if (!selectedNode) {
            ImGui::TextDisabled("Node not found");
            return;
        }

        ImGui::Text("Node: %s", selectedNode->name.c_str());
        ImGui::Separator();

        bool changed = false;

        for (auto& [propName, propValue] : selectedNode->properties) {
            if (propName == "textureIndex") {
                continue;
            }

            ImGui::PushID(propName.c_str());

            if (std::holds_alternative<float>(propValue)) {
                float value = std::get<float>(propValue);
                if (ImGui::DragFloat(propName.c_str(), &value, 0.01f, 0.0f, 1.0f)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec2>(propValue)) {
                glm::vec2 value = std::get<glm::vec2>(propValue);
                if (ImGui::DragFloat2(propName.c_str(), &value.x, 0.01f)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<glm::vec3>(propValue)) {
                glm::vec3 value = std::get<glm::vec3>(propValue);
                if (selectedNode->type == ::material::NodeType::ConstantColor) {
                    if (ImGui::ColorEdit3(propName.c_str(), &value.x)) {
                        propValue = value;
                        changed = true;
                    }
                } else {
                    if (ImGui::DragFloat3(propName.c_str(), &value.x, 0.01f)) {
                        propValue = value;
                        changed = true;
                    }
                }
            }
            else if (std::holds_alternative<glm::vec4>(propValue)) {
                glm::vec4 value = std::get<glm::vec4>(propValue);
                if (ImGui::ColorEdit4(propName.c_str(), &value.x)) {
                    propValue = value;
                    changed = true;
                }
            }
            else if (std::holds_alternative<std::string>(propValue)) {
                std::string value = std::get<std::string>(propValue);

                if (propName == "texturePath" &&
                    (selectedNode->type == ::material::NodeType::TextureSample ||
                     selectedNode->type == ::material::NodeType::OrmSample)) {
                    ImGui::Text("Texture:");
                    ImGui::SameLine();

                    std::string displayPath = value.empty() ? "(None)" :
                        (value.length() > 30 ? "..." + value.substr(value.length() - 27) : value);
                    ImGui::TextDisabled("%s", displayPath.c_str());

                    if (ImGui::Button("Browse...")) {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Image", L"*.vfImage"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty()) {
                            selectedPath.erase(std::remove(selectedPath.begin(), selectedPath.end(), '\0'), selectedPath.end());
                            propValue = selectedPath;
                            changed = true;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        propValue = std::string("");
                        changed = true;
                    }
                } else {
                    char buffer[256];
                    std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
                    buffer[sizeof(buffer) - 1] = '\0';
                    if (ImGui::InputText(propName.c_str(), buffer, sizeof(buffer))) {
                        propValue = std::string(buffer);
                        changed = true;
                    }
                }
            }

            ImGui::PopID();
        }

        if (changed) {
            notifyChanged();
        }

        if (selectedNode->properties.empty()) {
            ImGui::TextDisabled("No editable properties");
        }
    }
}
