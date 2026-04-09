#include "MetaComponentDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "core/PluginContextImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <unordered_map>

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
        // Enums (registered via entt::meta_factory<E>().data<E::Value>("Name"))
        else if (type.is_enum())
        {
            // Find current value name
            const char* currentName = nullptr;
            int currentIndex = 0;
            int idx = 0;

            std::vector<std::pair<const char*, entt::meta_any>> enumEntries;
            for (auto [id, member] : type.data())
            {
                const char* entryName = member.name();
                if (!entryName) continue;
                auto entryVal = member.get({});
                if (entryVal == value)
                {
                    currentName = entryName;
                    currentIndex = idx;
                }
                enumEntries.emplace_back(entryName, entryVal);
                ++idx;
            }

            if (!enumEntries.empty())
            {
                if (ImGui::BeginCombo(name, currentName ? currentName : "???"))
                {
                    for (int i = 0; i < static_cast<int>(enumEntries.size()); ++i)
                    {
                        bool selected = (i == currentIndex);
                        if (ImGui::Selectable(enumEntries[i].first, selected))
                        {
                            data.set(instance, enumEntries[i].second);
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextDisabled("%s (enum, no values registered)", name);
            }
        }
        // Sequence containers (std::vector<T>, etc.)
        else if (type.is_sequence_container())
        {
            // Work on the copy, then write back via data.set() if modified
            auto view = value.as_sequence_container();
            auto elemType = view.value_type();
            std::size_t count = view.size();
            bool containerChanged = false;

            if (ImGui::TreeNode(name, "%s [%zu]", name, count))
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    auto elem = view[i];
                    std::string label = "[" + std::to_string(i) + "]";

                    if (elemType.info() == entt::type_id<int>()) {
                        int val = elem.cast<int>();
                        if (ImGui::DragInt(label.c_str(), &val)) {
                            view[i].assign(entt::meta_any{val});
                            containerChanged = true;
                        }
                    }
                    else if (elemType.info() == entt::type_id<float>()) {
                        float val = elem.cast<float>();
                        if (ImGui::DragFloat(label.c_str(), &val, 0.1f)) {
                            view[i].assign(entt::meta_any{val});
                            containerChanged = true;
                        }
                    }
                    else if (elemType.info() == entt::type_id<bool>()) {
                        bool val = elem.cast<bool>();
                        if (ImGui::Checkbox(label.c_str(), &val)) {
                            view[i].assign(entt::meta_any{val});
                            containerChanged = true;
                        }
                    }
                    else if (elemType.info() == entt::type_id<std::string>()) {
                        auto str = elem.cast<std::string>();
                        char buffer[256];
                        std::strncpy(buffer, str.c_str(), sizeof(buffer));
                        buffer[sizeof(buffer) - 1] = '\0';
                        if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer))) {
                            view[i].assign(entt::meta_any{std::string(buffer)});
                            containerChanged = true;
                        }
                    }
                    else if (elemType.info() == entt::type_id<glm::vec3>()) {
                        auto val = elem.cast<glm::vec3>();
                        if (ImGui::DragFloat3(label.c_str(), &val.x, 0.1f)) {
                            view[i].assign(entt::meta_any{val});
                            containerChanged = true;
                        }
                    }
                    else {
                        ImGui::TextDisabled("%s (unsupported element)", label.c_str());
                    }

                    ImGui::PopID();
                }

                // Add / Remove buttons
                if (ImGui::SmallButton("+ Add"))
                {
                    if (elemType.info() == entt::type_id<int>()) view.insert(view.end(), entt::meta_any{0});
                    else if (elemType.info() == entt::type_id<float>()) view.insert(view.end(), entt::meta_any{0.0f});
                    else if (elemType.info() == entt::type_id<bool>()) view.insert(view.end(), entt::meta_any{false});
                    else if (elemType.info() == entt::type_id<std::string>()) view.insert(view.end(), entt::meta_any{std::string("")});
                    else if (elemType.info() == entt::type_id<glm::vec3>()) view.insert(view.end(), entt::meta_any{glm::vec3(0.0f)});
                    containerChanged = true;
                }
                if (count > 0)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("- Remove Last"))
                    {
                        auto it = view.begin();
                        for (std::size_t i = 0; i + 1 < count; ++i) ++it;
                        view.erase(it);
                        containerChanged = true;
                    }
                }

                ImGui::TreePop();
            }

            // Write modified copy back to the actual component
            if (containerChanged)
            {
                data.set(instance, value);
                changed = true;
            }
        }
        // Associative containers (std::map<K,V>, etc.)
        else if (type.is_associative_container())
        {
            // Work on the copy, then write back via data.set() if modified
            auto view = value.as_associative_container();
            auto valType = view.mapped_type();
            std::size_t count = view.size();
            bool containerChanged = false;

            if (ImGui::TreeNode(name, "%s {%zu}", name, count))
            {
                // Collect entries for stable iteration (erase invalidates iterators)
                std::vector<std::pair<std::string, entt::meta_any>> entries;
                for (auto it = view.begin(), last = view.end(); it != last; ++it)
                {
                    auto [key, val] = *it;
                    std::string keyStr;
                    auto keyCopy = key;
                    if (view.key_type().info() == entt::type_id<std::string>() && keyCopy.allow_cast<std::string>())
                        keyStr = keyCopy.cast<std::string>();
                    else if (view.key_type().info() == entt::type_id<int>() && keyCopy.allow_cast<int>())
                        keyStr = std::to_string(keyCopy.cast<int>());
                    else if (view.key_type().info() == entt::type_id<float>() && keyCopy.allow_cast<float>())
                        keyStr = std::to_string(keyCopy.cast<float>());
                    else
                        keyStr = "?";
                    entries.emplace_back(std::move(keyStr), val);
                }

                std::string keyToRemove;
                int entryIdx = 0;
                for (auto& [keyStr, val] : entries)
                {
                    ImGui::PushID(entryIdx++);
                    std::string valueLabel = "##val";

                    auto makeKeyAny = [&]() -> entt::meta_any {
                        return (view.key_type().info() == entt::type_id<int>())
                            ? entt::meta_any{std::stoi(keyStr)} : entt::meta_any{keyStr};
                    };

                    // Helper: erase then re-insert to update existing key (insert alone won't overwrite)
                    auto updateEntry = [&](entt::meta_any newVal) {
                        auto k = makeKeyAny();
                        view.erase(k);
                        view.insert(makeKeyAny(), std::move(newVal));
                        containerChanged = true;
                    };

                    if (valType.info() == entt::type_id<int>()) {
                        int v = val.cast<int>();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                        if (ImGui::DragInt(valueLabel.c_str(), &v))
                            updateEntry(entt::meta_any{v});
                    }
                    else if (valType.info() == entt::type_id<float>()) {
                        float v = val.cast<float>();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                        if (ImGui::DragFloat(valueLabel.c_str(), &v, 0.1f))
                            updateEntry(entt::meta_any{v});
                    }
                    else if (valType.info() == entt::type_id<bool>()) {
                        bool v = val.cast<bool>();
                        if (ImGui::Checkbox(valueLabel.c_str(), &v))
                            updateEntry(entt::meta_any{v});
                    }
                    else if (valType.info() == entt::type_id<std::string>()) {
                        auto str = val.cast<std::string>();
                        char buffer[256];
                        std::strncpy(buffer, str.c_str(), sizeof(buffer));
                        buffer[sizeof(buffer) - 1] = '\0';
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                        if (ImGui::InputText(valueLabel.c_str(), buffer, sizeof(buffer)))
                            updateEntry(entt::meta_any{std::string(buffer)});
                    }
                    else if (valType.info() == entt::type_id<glm::vec3>()) {
                        auto v = val.cast<glm::vec3>();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                        if (ImGui::DragFloat3(valueLabel.c_str(), &v.x, 0.1f))
                            updateEntry(entt::meta_any{v});
                    }
                    else {
                        ImGui::TextDisabled("(unsupported value)");
                    }

                    ImGui::SameLine();
                    ImGui::TextUnformatted(keyStr.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        keyToRemove = keyStr;
                    }

                    ImGui::PopID();
                }

                // Deferred removal
                if (!keyToRemove.empty())
                {
                    entt::meta_any keyAny = (view.key_type().info() == entt::type_id<int>())
                        ? entt::meta_any{std::stoi(keyToRemove)} : entt::meta_any{keyToRemove};
                    view.erase(keyAny);
                    containerChanged = true;
                }

                // Add new entry
                static std::unordered_map<std::string, std::string> newKeyBuffers;
                auto& newKeyStr = newKeyBuffers[name];
                char newKeyBuf[128];
                std::strncpy(newKeyBuf, newKeyStr.c_str(), sizeof(newKeyBuf));
                newKeyBuf[sizeof(newKeyBuf) - 1] = '\0';
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("##newkey", newKeyBuf, sizeof(newKeyBuf)))
                    newKeyStr = newKeyBuf;
                ImGui::SameLine();
                if (ImGui::SmallButton("+ Add Entry") && newKeyBuf[0] != '\0')
                {
                    entt::meta_any keyAny = (view.key_type().info() == entt::type_id<int>())
                        ? entt::meta_any{std::stoi(std::string(newKeyBuf))} : entt::meta_any{std::string(newKeyBuf)};
                    entt::meta_any defVal;
                    if (valType.info() == entt::type_id<int>()) defVal = 0;
                    else if (valType.info() == entt::type_id<float>()) defVal = 0.0f;
                    else if (valType.info() == entt::type_id<bool>()) defVal = false;
                    else if (valType.info() == entt::type_id<std::string>()) defVal = std::string("");
                    else if (valType.info() == entt::type_id<glm::vec3>()) defVal = glm::vec3(0.0f);
                    if (keyAny && defVal)
                    {
                        view.insert(keyAny, defVal);
                        newKeyStr.clear();
                        containerChanged = true;
                    }
                }

                ImGui::TreePop();
            }

            // Write modified copy back to the actual component
            if (containerChanged)
            {
                data.set(instance, value);
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
