#include "MetaComponentDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "core/PluginContextImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

namespace windows::details
{
    static bool drawMetaData(entt::meta_data data, entt::meta_any& instance)
    {
        bool changed = false;
        const char* name = data.name();
        if (!name) return false;

        auto value = data.get(instance);
        if (!value) return false;

        auto type = data.type();

        // int
        if (type.info() == entt::type_id<int>())
        {
            int val = value.cast<int>();
            if (ImGui::DragInt(name, &val))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // float
        else if (type.info() == entt::type_id<float>())
        {
            float val = value.cast<float>();
            if (ImGui::DragFloat(name, &val, 0.1f))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // bool
        else if (type.info() == entt::type_id<bool>())
        {
            bool val = value.cast<bool>();
            if (ImGui::Checkbox(name, &val))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // std::string
        else if (type.info() == entt::type_id<std::string>())
        {
            auto& str = value.cast<std::string&>();
            char buffer[256];
            std::strncpy(buffer, str.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText(name, buffer, sizeof(buffer)))
            {
                data.set(instance, std::string(buffer));
                changed = true;
            }
        }
        // glm::vec2
        else if (type.info() == entt::type_id<glm::vec2>())
        {
            auto val = value.cast<glm::vec2>();
            if (ImGui::DragFloat2(name, &val.x, 0.1f))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // glm::vec3
        else if (type.info() == entt::type_id<glm::vec3>())
        {
            auto val = value.cast<glm::vec3>();
            if (ImGui::DragFloat3(name, &val.x, 0.1f))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // glm::vec4
        else if (type.info() == entt::type_id<glm::vec4>())
        {
            auto val = value.cast<glm::vec4>();
            if (ImGui::DragFloat4(name, &val.x, 0.1f))
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // glm::quat
        else if (type.info() == entt::type_id<glm::quat>())
        {
            auto q = value.cast<glm::quat>();
            glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
            if (ImGui::DragFloat3(name, &euler.x, 0.5f))
            {
                data.set(instance, glm::quat(glm::radians(euler)));
                changed = true;
            }
        }
        else
        {
            ImGui::TextDisabled("%s (unsupported type)", name);
        }

        return changed;
    }

    void MetaComponentDrawer::draw(services::EntityHandle handle)
    {
        auto& reg = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(handle);

        if (!reg.valid(entity))
            return;

        auto& bridges = plugin::PluginContextImpl::getAllBridges();

        for (const auto& bridge : bridges)
        {
            if (!bridge.has(reg, entity))
                continue;

            void* rawPtr = bridge.tryGet(reg, entity);
            if (!rawPtr)
                continue;

            auto metaType = bridge.metaType;
            if (!metaType)
                continue;

            // Wrap the raw component pointer in a meta_any (non-owning)
            auto instance = metaType.from_void(rawPtr);
            if (!instance)
                continue;

            std::string displayName = bridge.name ? bridge.name : "Unknown";

            EntityDetailsPanel::pushComponentHeaderStyle();
            std::string headerId = "##meta_" + std::to_string(bridge.typeId);
            bool open = ImGui::CollapsingHeader((displayName + headerId).c_str(),
                                                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            EntityDetailsPanel::popComponentHeaderStyle();

            // Remove button
            EntityDetailsPanel::pushRemoveButtonStyle();
            std::string removeId = "X##remove_meta_" + std::to_string(bridge.typeId);
            if (ImGui::Button(removeId.c_str(), ImVec2(18, 18)))
            {
                bridge.remove(reg, entity);
                EntityDetailsPanel::popRemoveButtonStyle();
                continue;
            }
            EntityDetailsPanel::popRemoveButtonStyle();

            if (open)
            {
                ImGui::Indent(8.0f);
                ImGui::PushID(static_cast<int>(bridge.typeId));

                for (auto&& [id, member] : metaType.data())
                {
                    drawMetaData(member, instance);
                }

                ImGui::PopID();
                ImGui::Unindent(8.0f);
                ImGui::Spacing();
            }
        }
    }
}
