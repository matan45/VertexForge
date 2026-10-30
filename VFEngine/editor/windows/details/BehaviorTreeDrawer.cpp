#include "BehaviorTreeDrawer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>
#include <variant>

namespace windows::details
{
    namespace
    {
        // One per-entity start value as text; Entity values name the entity their reference resolves to.
        std::string describeInitialValue(entt::registry& registry, const services::BehaviorTreeInitialValueData& entry)
        {
            using behaviortree::BlackboardValueType;
            char buffer[96];
            switch (static_cast<BlackboardValueType>(entry.value.index()))
            {
            case BlackboardValueType::Float:
                std::snprintf(buffer, sizeof(buffer), "%g", std::get<float>(entry.value));
                return buffer;
            case BlackboardValueType::Int:
                return std::to_string(std::get<int32_t>(entry.value));
            case BlackboardValueType::Bool:
                return std::get<bool>(entry.value) ? "true" : "false";
            case BlackboardValueType::String:
                return "\"" + std::get<std::string>(entry.value) + "\"";
            case BlackboardValueType::Vec3:
            {
                const glm::vec3& v = std::get<glm::vec3>(entry.value);
                std::snprintf(buffer, sizeof(buffer), "(%g, %g, %g)", v.x, v.y, v.z);
                return buffer;
            }
            case BlackboardValueType::Entity:
            {
                if (entry.unresolved)
                {
                    const std::string& name = entry.entityRef.name;
                    return "(unresolved: " + (name.empty() ? "UUID " + std::to_string(entry.entityRef.uuid) : name) + ")";
                }
                const auto target = std::get<services::EntityHandle>(entry.value);
                if (!target.isValid()) return "(none)";
                const auto targetEntity = services::internal::fromHandle(target);
                const auto* name = registry.valid(targetEntity)
                                       ? registry.try_get<components::NameComponent>(targetEntity)
                                       : nullptr;
                return name && !name->name.empty() ? name->name : "entity " + std::to_string(target.id);
            }
            default:
                return {};
            }
        }
    }

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
            std::string resolvedPath = bt.behaviorTreeRef.resolve();
            std::string display = !bt.behaviorTreeRef.isValid() ? "(None)" : resolvedPath;
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
                    bt.behaviorTreeRef = asset::AssetRef::fromPath(path);
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
                bt.behaviorTreeRef = asset::AssetRef::invalid();
            }

            if (ImGui::Checkbox("Enabled", &bt.enabled))
            {
                events::ai::SetBehaviorTreeEnabledCommand cmd;
                cmd.entity = handle;
                cmd.enabled = bt.enabled;
                events::EventDispatcher::instance().execute(cmd);
            }

            // VK-1660: per-entity blackboard start values, read-only here (the query resolves Entity
            // references UUID first, then by name).
            if (!bt.initialValues.empty())
            {
                ImGui::Spacing();
                ImGui::Text("Start Values");
                ImGui::TextDisabled("Set by the MCP behavior-tree tools; applied when Play starts");

                events::ai::GetBehaviorTreeComponentDataQuery query;
                query.entity = handle;
                const auto data = events::EventDispatcher::instance().query(query);
                if (data)
                {
                    for (const auto& entry : data->initialValues)
                    {
                        const std::string text = describeInitialValue(registry, entry);
                        ImGui::Bullet();
                        ImGui::SameLine();
                        if (entry.unresolved)
                            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s = %s", entry.key.c_str(), text.c_str());
                        else
                            ImGui::Text("%s = %s", entry.key.c_str(), text.c_str());
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
