#include "MaterialPropertyPanel.hpp"
#include "../../graph/ShaderGraphEditor.hpp"
#include <material/MaterialParameterSet.hpp>
#include <nfd/FileDialog.hpp>
#include "../../dragdrop/AssetDropTarget.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace editor::materialeditor
{
    namespace
    {
        bool isExposableNode(::material::NodeType type)
        {
            return type == ::material::NodeType::ConstantScalar ||
                   type == ::material::NodeType::ConstantVec2 ||
                   type == ::material::NodeType::ConstantVec3 ||
                   type == ::material::NodeType::ConstantColor ||
                   type == ::material::NodeType::TextureSample ||
                   type == ::material::NodeType::OrmSample;
        }

        bool isParameterMetaProperty(const std::string& name)
        {
            return name == ::material::PARAM_FLAG_PROPERTY ||
                   name == ::material::PARAM_NAME_PROPERTY ||
                   name == ::material::PARAM_MIN_PROPERTY ||
                   name == ::material::PARAM_MAX_PROPERTY;
        }
    }

    void MaterialPropertyPanel::notifyChanged()
    {
        if (onPropertyChanged) {
            onPropertyChanged();
        }
    }

    void MaterialPropertyPanel::notifyValueChanged()
    {
        if (onParameterValueChanged) {
            onParameterValueChanged();
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

        ::material::MaterialParameterSet paramSet = ::material::collectParameters(materialData->graph);

        // Edit the source node's "value" property directly — the node stays the single
        // source of truth for the default, and live tweaks reach the preview through the
        // parameter UBO without a recompile.
        for (const auto& desc : paramSet.values) {
            ::material::ShaderNode* node = materialData->graph.findNode(desc.sourceNodeId);
            if (!node) continue;

            auto valueIt = node->properties.find("value");
            if (valueIt == node->properties.end()) continue;

            ImGui::PushID(static_cast<int>(desc.sourceNodeId));

            switch (desc.type) {
                case ::material::ParameterType::Scalar: {
                    if (float* value = std::get_if<float>(&valueIt->second)) {
                        float v = *value;
                        if (ImGui::SliderFloat(desc.name.c_str(), &v, desc.min, desc.max)) {
                            valueIt->second = v;
                            notifyValueChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec2: {
                    if (glm::vec2* value = std::get_if<glm::vec2>(&valueIt->second)) {
                        glm::vec2 v = *value;
                        if (ImGui::DragFloat2(desc.name.c_str(), &v.x, 0.01f)) {
                            valueIt->second = v;
                            notifyValueChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec3: {
                    if (glm::vec3* value = std::get_if<glm::vec3>(&valueIt->second)) {
                        glm::vec3 v = *value;
                        if (ImGui::DragFloat3(desc.name.c_str(), &v.x, 0.01f)) {
                            valueIt->second = v;
                            notifyValueChanged();
                        }
                    }
                    break;
                }
                case ::material::ParameterType::Vec4:
                case ::material::ParameterType::Color: {
                    if (glm::vec4* value = std::get_if<glm::vec4>(&valueIt->second)) {
                        glm::vec4 v = *value;
                        if (ImGui::ColorEdit4(desc.name.c_str(), &v.x)) {
                            valueIt->second = v;
                            notifyValueChanged();
                        }
                    }
                    break;
                }
            }

            ImGui::PopID();
        }

        for (const auto& desc : paramSet.textures) {
            ImGui::PushID(static_cast<int>(desc.sourceNodeId));
            std::string display = desc.defaultTexturePath.empty() ? "(None)" :
                (desc.defaultTexturePath.length() > 24
                     ? "..." + desc.defaultTexturePath.substr(desc.defaultTexturePath.length() - 21)
                     : desc.defaultTexturePath);
            ImGui::Text("%s", desc.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("[tex] %s", display.c_str());
            ImGui::PopID();
        }

        if (paramSet.empty()) {
            ImGui::TextDisabled("No exposed parameters");
            ImGui::TextDisabled("Select a constant or texture node and");
            ImGui::TextDisabled("tick 'Expose as parameter'");
        }
    }

    void MaterialPropertyPanel::drawExposeSection(::material::ShaderNode& node, bool& structuralChange)
    {
        bool exposed = false;
        auto flagIt = node.properties.find(::material::PARAM_FLAG_PROPERTY);
        if (flagIt != node.properties.end()) {
            if (const float* flag = std::get_if<float>(&flagIt->second)) {
                exposed = *flag != 0.0f;
            }
        }

        if (ImGui::Checkbox("Expose as parameter", &exposed)) {
            if (exposed) {
                node.properties[::material::PARAM_FLAG_PROPERTY] = 1.0f;
                if (::material::parameterNameOf(node).empty()) {
                    node.properties[::material::PARAM_NAME_PROPERTY] =
                        node.name.empty() ? std::string("Parameter") : node.name;
                }
            } else {
                node.properties.erase(::material::PARAM_FLAG_PROPERTY);
                node.properties.erase(::material::PARAM_NAME_PROPERTY);
                node.properties.erase(::material::PARAM_MIN_PROPERTY);
                node.properties.erase(::material::PARAM_MAX_PROPERTY);
            }
            structuralChange = true;
        }

        if (exposed) {
            std::string paramName;
            auto nameIt = node.properties.find(::material::PARAM_NAME_PROPERTY);
            if (nameIt != node.properties.end()) {
                if (const std::string* name = std::get_if<std::string>(&nameIt->second)) {
                    paramName = *name;
                }
            }

            char buffer[128];
            std::strncpy(buffer, paramName.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText("Parameter Name", buffer, sizeof(buffer))) {
                node.properties[::material::PARAM_NAME_PROPERTY] = std::string(buffer);
                structuralChange = true;
            }

            if (node.type == ::material::NodeType::ConstantScalar) {
                float minValue = 0.0f;
                float maxValue = 1.0f;
                auto minIt = node.properties.find(::material::PARAM_MIN_PROPERTY);
                if (minIt != node.properties.end()) {
                    if (const float* v = std::get_if<float>(&minIt->second)) minValue = *v;
                }
                auto maxIt = node.properties.find(::material::PARAM_MAX_PROPERTY);
                if (maxIt != node.properties.end()) {
                    if (const float* v = std::get_if<float>(&maxIt->second)) maxValue = *v;
                }

                // Range hints affect only the parameter UI, not the generated shader
                if (ImGui::DragFloat("Min", &minValue, 0.1f)) {
                    node.properties[::material::PARAM_MIN_PROPERTY] = minValue;
                    notifyValueChanged();
                }
                if (ImGui::DragFloat("Max", &maxValue, 0.1f)) {
                    node.properties[::material::PARAM_MAX_PROPERTY] = maxValue;
                    notifyValueChanged();
                }
            }
        }

        ImGui::Separator();
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

        if (isExposableNode(selectedNode->type)) {
            drawExposeSection(*selectedNode, changed);
        }

        for (auto& [propName, propValue] : selectedNode->properties) {
            if (propName == "textureIndex" || isParameterMetaProperty(propName)) {
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
                    if (auto dropped = windows::acceptAssetDropOnLastItem("TextureSlotDrop", {".vfimage"})) {
                        propValue = *dropped;
                        changed = true;
                    }

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
