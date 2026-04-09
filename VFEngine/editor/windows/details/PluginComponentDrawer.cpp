#include "PluginComponentDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/plugin/PluginComponentEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/CoreComponents.hpp"
#include "data/EntityConversion.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace windows::details
{
    void PluginComponentDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::plugin::GetRegisteredPluginComponentsQuery listQuery;
        auto registeredNames = dispatcher.query(listQuery);

        for (const auto& qualifiedName : registeredNames)
        {
            events::plugin::GetPluginComponentDataQuery dataQuery;
            dataQuery.entity = handle;
            dataQuery.qualifiedName = qualifiedName;
            auto dataOpt = dispatcher.query(dataQuery);

            if (dataOpt.has_value())
            {
                drawSingleComponent(handle, qualifiedName);
            }
        }
    }

    void PluginComponentDrawer::drawSingleComponent(services::EntityHandle handle,
                                                     const std::string& qualifiedName)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Extract display name: "pluginName::componentName" -> "componentName (pluginName)"
        std::string displayName = qualifiedName;
        auto separatorPos = qualifiedName.find("::");
        if (separatorPos != std::string::npos)
        {
            std::string plugin = qualifiedName.substr(0, separatorPos);
            std::string component = qualifiedName.substr(separatorPos + 2);
            displayName = component + " (" + plugin + ")";
        }

        EntityDetailsPanel::pushComponentHeaderStyle();

        std::string headerId = "##plugin_" + qualifiedName;
        bool open = ImGui::CollapsingHeader((displayName + headerId).c_str(),
                                             ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        EntityDetailsPanel::popComponentHeaderStyle();

        // Remove button
        EntityDetailsPanel::pushRemoveButtonStyle();
        std::string removeId = "X##remove_plugin_" + qualifiedName;
        if (ImGui::Button(removeId.c_str(), ImVec2(18, 18)))
        {
            events::plugin::RemovePluginComponentCommand cmd;
            cmd.entity = handle;
            cmd.qualifiedName = qualifiedName;
            dispatcher.execute(cmd);
            EntityDetailsPanel::popRemoveButtonStyle();
            return;
        }
        EntityDetailsPanel::popRemoveButtonStyle();

        if (open)
        {
            ImGui::Indent(8.0f);

            // Check for custom inspector override first
            events::plugin::HasPluginInspectorQuery inspQuery;
            inspQuery.qualifiedName = qualifiedName;
            bool hasCustomInspector = dispatcher.query(inspQuery);

            if (hasCustomInspector)
            {
                events::plugin::InvokePluginInspectorCommand inspCmd;
                inspCmd.entity = handle;
                inspCmd.qualifiedName = qualifiedName;
                dispatcher.execute(inspCmd);
            }
            else
            {
                // Auto-generate inspector from property descriptors
                events::plugin::GetPluginComponentDescriptorsQuery descQuery;
                descQuery.qualifiedName = qualifiedName;
                auto descriptors = dispatcher.query(descQuery);

                events::plugin::GetPluginComponentDataQuery dataQuery;
                dataQuery.entity = handle;
                dataQuery.qualifiedName = qualifiedName;
                auto dataOpt = dispatcher.query(dataQuery);

                if (dataOpt.has_value() && !descriptors.empty())
                {
                    nlohmann::json data = *dataOpt;
                    if (drawAutoInspector(descriptors, data))
                    {
                        events::plugin::UpdatePluginComponentDataCommand updateCmd;
                        updateCmd.entity = handle;
                        updateCmd.qualifiedName = qualifiedName;
                        updateCmd.data = std::move(data);
                        dispatcher.execute(updateCmd);
                    }
                }
            }

            ImGui::Unindent(8.0f);
            ImGui::Spacing();
        }
    }

    bool PluginComponentDrawer::drawAutoInspector(
        const std::vector<components::plugin::PropertyDescriptor>& properties,
        nlohmann::json& data)
    {
        using components::plugin::PropertyType;
        bool changed = false;

        for (const auto& prop : properties)
        {
            switch (prop.type)
            {
                case PropertyType::Int:
                {
                    int val = data.value(prop.name, prop.defaultValue.get<int>());
                    int minVal = static_cast<int>(prop.min);
                    int maxVal = static_cast<int>(prop.max);
                    if (minVal == 0 && maxVal == 0)
                    {
                        if (ImGui::DragInt(prop.name.c_str(), &val))
                        {
                            data[prop.name] = val;
                            changed = true;
                        }
                    }
                    else
                    {
                        if (ImGui::DragInt(prop.name.c_str(), &val, 1, minVal, maxVal))
                        {
                            data[prop.name] = val;
                            changed = true;
                        }
                    }
                    break;
                }
                case PropertyType::Float:
                {
                    float val = data.value(prop.name, prop.defaultValue.get<float>());
                    if (prop.min == 0 && prop.max == 0)
                    {
                        if (ImGui::DragFloat(prop.name.c_str(), &val, 0.1f))
                        {
                            data[prop.name] = val;
                            changed = true;
                        }
                    }
                    else
                    {
                        if (ImGui::DragFloat(prop.name.c_str(), &val, 0.1f, prop.min, prop.max))
                        {
                            data[prop.name] = val;
                            changed = true;
                        }
                    }
                    break;
                }
                case PropertyType::Bool:
                {
                    bool val = data.value(prop.name, prop.defaultValue.get<bool>());
                    if (ImGui::Checkbox(prop.name.c_str(), &val))
                    {
                        data[prop.name] = val;
                        changed = true;
                    }
                    break;
                }
                case PropertyType::String:
                {
                    std::string val = data.value(prop.name, prop.defaultValue.get<std::string>());
                    char buffer[256];
                    std::strncpy(buffer, val.c_str(), sizeof(buffer));
                    buffer[sizeof(buffer) - 1] = '\0';
                    if (ImGui::InputText(prop.name.c_str(), buffer, sizeof(buffer)))
                    {
                        data[prop.name] = std::string(buffer);
                        changed = true;
                    }
                    break;
                }
                case PropertyType::Vec2:
                {
                    glm::vec2 val{0.0f};
                    if (auto it = data.find(prop.name); it != data.end() && it->is_array() && it->size() >= 2)
                        val = {(*it)[0].get<float>(), (*it)[1].get<float>()};
                    if (ImGui::DragFloat2(prop.name.c_str(), &val.x, 0.1f))
                    {
                        data[prop.name] = nlohmann::json::array({val.x, val.y});
                        changed = true;
                    }
                    break;
                }
                case PropertyType::Vec3:
                {
                    glm::vec3 val{0.0f};
                    if (auto it = data.find(prop.name); it != data.end() && it->is_array() && it->size() >= 3)
                        val = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
                    if (ImGui::DragFloat3(prop.name.c_str(), &val.x, 0.1f))
                    {
                        data[prop.name] = nlohmann::json::array({val.x, val.y, val.z});
                        changed = true;
                    }
                    break;
                }
                case PropertyType::Vec4:
                {
                    glm::vec4 val{0.0f};
                    if (auto it = data.find(prop.name); it != data.end() && it->is_array() && it->size() >= 4)
                        val = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
                    if (ImGui::DragFloat4(prop.name.c_str(), &val.x, 0.1f))
                    {
                        data[prop.name] = nlohmann::json::array({val.x, val.y, val.z, val.w});
                        changed = true;
                    }
                    break;
                }
                case PropertyType::Color:
                {
                    glm::vec4 val{1.0f};
                    if (auto it = data.find(prop.name); it != data.end() && it->is_array() && it->size() >= 4)
                        val = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
                    if (ImGui::ColorEdit4(prop.name.c_str(), &val.x))
                    {
                        data[prop.name] = nlohmann::json::array({val.x, val.y, val.z, val.w});
                        changed = true;
                    }
                    break;
                }
                case PropertyType::Array:
                {
                    if (!data.contains(prop.name) || !data[prop.name].is_array())
                        data[prop.name] = nlohmann::json::array();

                    auto& arr = data[prop.name];
                    std::string headerLabel = prop.name + " (" + std::to_string(arr.size()) + ")";
                    std::string headerId = "##arr_" + prop.name;

                    if (ImGui::TreeNode((headerLabel + headerId).c_str()))
                    {
                        int removeIdx = -1;
                        for (size_t i = 0; i < arr.size(); ++i)
                        {
                            ImGui::PushID(static_cast<int>(i));

                            std::string elemLabel = "[" + std::to_string(i) + "]";
                            if (ImGui::TreeNode(elemLabel.c_str()))
                            {
                                if (!prop.children.empty() && arr[i].is_object())
                                {
                                    if (drawAutoInspector(prop.children, arr[i]))
                                        changed = true;
                                }

                                ImGui::TreePop();
                            }

                            ImGui::SameLine();
                            if (ImGui::SmallButton("X"))
                            {
                                removeIdx = static_cast<int>(i);
                            }

                            ImGui::PopID();
                        }

                        if (removeIdx >= 0)
                        {
                            arr.erase(arr.begin() + removeIdx);
                            changed = true;
                        }

                        if (ImGui::SmallButton(("+ Add##" + prop.name).c_str()))
                        {
                            // Build default element from children schema
                            nlohmann::json newElem = nlohmann::json::object();
                            for (const auto& child : prop.children)
                            {
                                newElem[child.name] = child.defaultValue;
                            }
                            arr.push_back(std::move(newElem));
                            changed = true;
                        }

                        ImGui::TreePop();
                    }
                    break;
                }
                case PropertyType::Object:
                {
                    if (!data.contains(prop.name) || !data[prop.name].is_object())
                    {
                        // Initialize from default
                        data[prop.name] = prop.defaultValue;
                    }

                    std::string headerId = "##obj_" + prop.name;
                    if (ImGui::TreeNode((prop.name + headerId).c_str()))
                    {
                        if (!prop.children.empty())
                        {
                            if (drawAutoInspector(prop.children, data[prop.name]))
                                changed = true;
                        }
                        ImGui::TreePop();
                    }
                    break;
                }
                case PropertyType::Enum:
                {
                    int val = data.value(prop.name, prop.defaultValue.get<int>());
                    auto getter = [](void* userData, int idx) -> const char* {
                        auto* opts = static_cast<const std::vector<std::string>*>(userData);
                        if (idx < 0 || idx >= static_cast<int>(opts->size())) return "";
                        return (*opts)[idx].c_str();
                    };
                    if (ImGui::Combo(prop.name.c_str(), &val, getter,
                                     const_cast<void*>(static_cast<const void*>(&prop.enumOptions)),
                                     static_cast<int>(prop.enumOptions.size())))
                    {
                        data[prop.name] = val;
                        changed = true;
                    }
                    break;
                }
                case PropertyType::AssetRef:
                {
                    std::string val = data.value(prop.name, std::string(""));
                    // Show filename only for display
                    std::string displayName = val;
                    auto lastSlash = val.find_last_of("/\\");
                    if (lastSlash != std::string::npos)
                        displayName = val.substr(lastSlash + 1);

                    ImGui::Text("%s", prop.name.c_str());
                    ImGui::SameLine();
                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float buttonWidth = 26.0f;
                    ImGui::SetNextItemWidth(availWidth - buttonWidth * 2 - 8.0f);
                    char buffer[512];
                    std::strncpy(buffer, displayName.c_str(), sizeof(buffer));
                    buffer[sizeof(buffer) - 1] = '\0';
                    ImGui::InputText(("##" + prop.name).c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::IsItemHovered() && !val.empty())
                        ImGui::SetTooltip("%s", val.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button(("...##browse_" + prop.name).c_str(), ImVec2(buttonWidth, 0)))
                    {
                        nfd::FileDialog dialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters;
                        if (!prop.assetTypeFilter.empty())
                        {
                            // Parse "Label|*.ext" format
                            auto sep = prop.assetTypeFilter.find('|');
                            if (sep != std::string::npos)
                            {
                                std::wstring label(prop.assetTypeFilter.begin(), prop.assetTypeFilter.begin() + sep);
                                std::wstring pattern(prop.assetTypeFilter.begin() + sep + 1, prop.assetTypeFilter.end());
                                filters.push_back({label, pattern});
                            }
                        }
                        auto path = dialog.openFileDialog(filters);
                        if (!path.empty())
                        {
                            data[prop.name] = path;
                            changed = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(("X##clear_" + prop.name).c_str(), ImVec2(buttonWidth, 0)))
                    {
                        data[prop.name] = "";
                        changed = true;
                    }
                    break;
                }
                case PropertyType::EntityRef:
                {
                    uint64_t entityId = data.value(prop.name, static_cast<uint64_t>(0));
                    auto& reg = scene::EntityRegistry::getRegistry();

                    // Find current entity name for preview
                    std::string preview = "(None)";
                    if (entityId != 0)
                    {
                        auto current = static_cast<entt::entity>(static_cast<uint32_t>(entityId));
                        if (reg.valid(current) && reg.all_of<components::NameComponent>(current))
                            preview = reg.get<components::NameComponent>(current).name;
                        else
                            preview = "(Invalid)";
                    }

                    ImGui::Text("%s", prop.name.c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::BeginCombo(("##" + prop.name).c_str(), preview.c_str()))
                    {
                        // None option
                        if (ImGui::Selectable("(None)", entityId == 0))
                        {
                            data[prop.name] = static_cast<uint64_t>(0);
                            changed = true;
                        }

                        // List all named entities
                        auto view = reg.view<components::NameComponent>();
                        for (auto entity : view)
                        {
                            auto& nameComp = view.get<components::NameComponent>(entity);
                            uint64_t id = static_cast<uint64_t>(static_cast<uint32_t>(entity));
                            bool isSelected = (id == entityId);
                            if (ImGui::Selectable(nameComp.name.c_str(), isSelected))
                            {
                                data[prop.name] = id;
                                changed = true;
                            }
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }
                    break;
                }
                case PropertyType::Quaternion:
                {
                    glm::quat q(1.0f, 0.0f, 0.0f, 0.0f); // identity: w,x,y,z
                    if (auto it = data.find(prop.name); it != data.end() && it->is_array() && it->size() >= 4)
                        q = glm::quat((*it)[3].get<float>(), (*it)[0].get<float>(),
                                      (*it)[1].get<float>(), (*it)[2].get<float>());
                    glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
                    if (ImGui::DragFloat3(prop.name.c_str(), &euler.x, 0.5f))
                    {
                        q = glm::quat(glm::radians(euler));
                        data[prop.name] = nlohmann::json::array({q.x, q.y, q.z, q.w});
                        changed = true;
                    }
                    break;
                }
            }
        }

        return changed;
    }
}
