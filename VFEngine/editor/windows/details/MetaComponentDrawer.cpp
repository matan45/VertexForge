#include "MetaComponentDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "core/PluginContextImpl.hpp"
#include "api/FieldAttributes.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <unordered_map>
#include <optional>
#include <vector>
#include <string>
#include <algorithm>

namespace windows::details
{
    // Draw a scalar/struct value with the given label. Returns the new meta_any if changed, empty otherwise.
    static std::optional<entt::meta_any> drawElementValue(const char* label, entt::meta_any& elem, const entt::meta_type& elemType)
    {
        if (elemType.info() == entt::type_id<int>()) {
            int val = elem.cast<int>();
            if (ImGui::DragInt(label, &val))
                return entt::meta_any{val};
        }
        else if (elemType.info() == entt::type_id<float>()) {
            float val = elem.cast<float>();
            if (ImGui::DragFloat(label, &val, 0.1f))
                return entt::meta_any{val};
        }
        else if (elemType.info() == entt::type_id<bool>()) {
            bool val = elem.cast<bool>();
            if (ImGui::Checkbox(label, &val))
                return entt::meta_any{val};
        }
        else if (elemType.info() == entt::type_id<std::string>()) {
            auto str = elem.cast<std::string>();
            char buffer[256];
            std::strncpy(buffer, str.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
                return entt::meta_any{std::string(buffer)};
        }
        else if (elemType.info() == entt::type_id<glm::vec3>()) {
            auto val = elem.cast<glm::vec3>();
            if (ImGui::DragFloat3(label, &val.x, 0.1f))
                return entt::meta_any{val};
        }
        else if (elemType.info() == entt::type_id<glm::vec4>()) {
            auto val = elem.cast<glm::vec4>();
            if (ImGui::DragFloat4(label, &val.x, 0.1f))
                return entt::meta_any{val};
        }
        else if (elemType.info() == entt::type_id<glm::vec2>()) {
            auto val = elem.cast<glm::vec2>();
            if (ImGui::DragFloat2(label, &val.x, 0.1f))
                return entt::meta_any{val};
        }
        else if (elemType.is_enum()) {
            const char* currentName = nullptr;
            int currentIndex = 0;
            int idx = 0;
            std::vector<std::pair<const char*, entt::meta_any>> enumEntries;
            for (auto [id, member] : elemType.data())
            {
                const char* entryName = member.name();
                if (!entryName) continue;
                auto entryVal = member.get({});
                if (entryVal == elem) { currentName = entryName; currentIndex = idx; }
                enumEntries.emplace_back(entryName, entryVal);
                ++idx;
            }
            if (!enumEntries.empty() && ImGui::BeginCombo(label, currentName ? currentName : "???"))
            {
                for (int i = 0; i < static_cast<int>(enumEntries.size()); ++i)
                {
                    bool selected = (i == currentIndex);
                    if (ImGui::Selectable(enumEntries[i].first, selected))
                    {
                        ImGui::EndCombo();
                        return enumEntries[i].second;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        // AssetRef — drag-drop asset picker (engine resolves the GUID).
        else if (elemType.info() == entt::type_id<asset::AssetRef>()) {
            auto ref = elem.cast<asset::AssetRef>();
            if (ref.isValid()) {
                std::string filename = ref.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("%s: %s", label, filename.empty() ? "(unresolved)" : filename.c_str());
            }
            else {
                ImGui::Text("%s: ", label);
                ImGui::SameLine();
                ImGui::TextDisabled("No asset");
            }
            if (auto dropped = windows::acceptAssetDropOnLastItem(label, {}))
                return entt::meta_any{asset::AssetRef::fromPath(*dropped)};
            ImGui::SameLine();
            ImGui::PushID(label);
            bool wasEmpty = !ref.isValid();
            if (wasEmpty) ImGui::BeginDisabled();
            bool cleared = ImGui::SmallButton("Clear");
            if (wasEmpty) ImGui::EndDisabled();
            ImGui::PopID();
            if (cleared)
                return entt::meta_any{asset::AssetRef::invalid()};
        }
        // Struct/class — draw each member as a sub-field
        else if (elemType.is_class())
        {
            bool structChanged = false;
            if (ImGui::TreeNode(label))
            {
                for (auto&& [id, member] : elemType.data())
                {
                    const char* fieldName = member.name();
                    if (!fieldName) continue;
                    auto fieldVal = member.get(elem);
                    if (!fieldVal) continue;

                    ImGui::PushID(fieldName);
                    auto result = drawElementValue(fieldName, fieldVal, member.type());
                    if (result.has_value())
                    {
                        member.set(elem, *result);
                        structChanged = true;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            if (structChanged)
                return elem;
        }
        else {
            ImGui::TextDisabled("%s (unsupported)", label);
        }
        return std::nullopt;
    }

    // Create a default meta_any for a given type (for Add buttons)
    static entt::meta_any makeDefault(const entt::meta_type& elemType)
    {
        if (elemType.info() == entt::type_id<int>()) return 0;
        if (elemType.info() == entt::type_id<float>()) return 0.0f;
        if (elemType.info() == entt::type_id<bool>()) return false;
        if (elemType.info() == entt::type_id<std::string>()) return std::string("");
        if (elemType.info() == entt::type_id<glm::vec3>()) return glm::vec3(0.0f);
        if (elemType.info() == entt::type_id<glm::vec4>()) return glm::vec4(0.0f);
        if (elemType.info() == entt::type_id<glm::vec2>()) return glm::vec2(0.0f);
        // For structs: try default-constructing via meta
        if (auto ctor = elemType.construct(); ctor) return ctor;
        return {};
    }

    static bool drawMetaData(entt::meta_data data, entt::meta_any& instance, const char* componentName)
    {
        bool changed = false;
        const char* name = data.name();
        if (!name) return false;

        // Per-field inspector attributes (nullptr when the plugin registered none —
        // in that case every branch below behaves exactly as it did before).
        const plugin::inspector::FieldAttributes* attr =
            plugin::PluginContextImpl::getFieldAttributes(componentName, name);

        using plugin::inspector::Widget;
        const Widget widget = attr ? attr->widget : Widget::Auto;

        // Hidden fields are skipped entirely.
        if (attr && widget == Widget::Hidden)
            return false;

        auto value = data.get(instance);
        if (!value) return false;

        auto type = data.type();

        // Display label: attribute label (or raw name), optionally suffixed with
        // units. A stable "##<rawName>" id keeps ImGui IDs collision-free even when
        // two fields share a display label.
        const char* baseLabel = (attr && attr->label[0]) ? attr->label : name;
        std::string labelStr;
        if (attr && attr->units[0])
            labelStr = std::string(baseLabel) + " (" + attr->units + ")##" + name;
        else
            labelStr = std::string(baseLabel) + "##" + name;
        const char* label = labelStr.c_str();

        // ReadOnly fields render disabled and never write back to the component.
        const bool readOnly = (attr && widget == Widget::ReadOnly);
        if (readOnly)
            ImGui::BeginDisabled();

        // Apply the tooltip (if any) to whatever widget the branch below submits last.
        const auto applyTooltip = [&]() {
            if (attr && attr->tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", attr->tooltip);
        };

        // int
        if (type.info() == entt::type_id<int>())
        {
            int val = value.cast<int>();
            bool edited = false;
            if (attr && attr->hasRange)
            {
                if (widget == Widget::Slider)
                    edited = ImGui::SliderInt(label, &val, static_cast<int>(attr->vmin), static_cast<int>(attr->vmax));
                else
                    edited = ImGui::DragInt(label, &val, attr->step > 0.f ? attr->step : 1.0f,
                                            static_cast<int>(attr->vmin), static_cast<int>(attr->vmax));
            }
            else
            {
                edited = ImGui::DragInt(label, &val);
            }
            applyTooltip();
            if (edited && !readOnly)
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // float
        else if (type.info() == entt::type_id<float>())
        {
            float val = value.cast<float>();
            bool edited = false;
            if (attr && attr->hasRange)
            {
                if (widget == Widget::Slider)
                    edited = ImGui::SliderFloat(label, &val, attr->vmin, attr->vmax);
                else
                    edited = ImGui::DragFloat(label, &val, attr->step > 0.f ? attr->step : 0.1f,
                                              attr->vmin, attr->vmax);
            }
            else
            {
                edited = ImGui::DragFloat(label, &val, 0.1f);
            }
            applyTooltip();
            if (edited && !readOnly)
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // bool
        else if (type.info() == entt::type_id<bool>())
        {
            bool val = value.cast<bool>();
            bool edited = ImGui::Checkbox(label, &val);
            applyTooltip();
            if (edited && !readOnly)
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
            bool edited = false;
            if (attr && widget == Widget::MultilineText)
                edited = ImGui::InputTextMultiline(label, buffer, sizeof(buffer));
            else
                edited = ImGui::InputText(label, buffer, sizeof(buffer));
            applyTooltip();
            if (edited && !readOnly)
            {
                data.set(instance, std::string(buffer));
                changed = true;
            }
        }
        // glm::vec2
        else if (type.info() == entt::type_id<glm::vec2>())
        {
            auto val = value.cast<glm::vec2>();
            bool edited = ImGui::DragFloat2(label, &val.x, 0.1f);
            applyTooltip();
            if (edited && !readOnly)
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // glm::vec3
        else if (type.info() == entt::type_id<glm::vec3>())
        {
            auto val = value.cast<glm::vec3>();
            bool edited = false;
            if (attr && widget == Widget::Color)
                edited = ImGui::ColorEdit3(label, &val.x);
            else
                edited = ImGui::DragFloat3(label, &val.x, 0.1f);
            applyTooltip();
            if (edited && !readOnly)
            {
                data.set(instance, val);
                changed = true;
            }
        }
        // glm::vec4
        else if (type.info() == entt::type_id<glm::vec4>())
        {
            auto val = value.cast<glm::vec4>();
            bool edited = false;
            if (attr && widget == Widget::Color)
                edited = ImGui::ColorEdit4(label, &val.x);
            else
                edited = ImGui::DragFloat4(label, &val.x, 0.1f);
            applyTooltip();
            if (edited && !readOnly)
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
            bool edited = ImGui::DragFloat3(label, &euler.x, 0.5f);
            applyTooltip();
            if (edited && !readOnly)
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
                if (ImGui::BeginCombo(label, currentName ? currentName : "???"))
                {
                    for (int i = 0; i < static_cast<int>(enumEntries.size()); ++i)
                    {
                        bool selected = (i == currentIndex);
                        if (ImGui::Selectable(enumEntries[i].first, selected))
                        {
                            if (!readOnly)
                            {
                                data.set(instance, enumEntries[i].second);
                                changed = true;
                            }
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                applyTooltip();
            }
            else
            {
                ImGui::TextDisabled("%s (enum, no values registered)", baseLabel);
            }
        }
        // AssetRef — drag-drop asset picker (engine resolves the GUID).
        else if (type.info() == entt::type_id<asset::AssetRef>())
        {
            auto ref = value.cast<asset::AssetRef>();
            if (ref.isValid())
            {
                std::string filename = ref.resolve();
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                    filename = filename.substr(lastSlash + 1);
                ImGui::Text("%s: %s", baseLabel, filename.empty() ? "(unresolved)" : filename.c_str());
            }
            else
            {
                ImGui::Text("%s: ", baseLabel);
                ImGui::SameLine();
                ImGui::TextDisabled("No asset");
            }
            applyTooltip();
            // Restrict the picker to a single extension when the field declares one.
            std::optional<std::string> dropped =
                (attr && attr->assetFilter[0])
                    ? windows::acceptAssetDropOnLastItem(name, {attr->assetFilter})
                    : windows::acceptAssetDropOnLastItem(name, {});
            if (dropped && !readOnly)
            {
                data.set(instance, asset::AssetRef::fromPath(*dropped));
                changed = true;
            }
            ImGui::SameLine();
            bool wasEmpty = !ref.isValid();
            if (wasEmpty) ImGui::BeginDisabled();
            std::string clearId = std::string("Clear##") + name;
            if (ImGui::SmallButton(clearId.c_str()) && !readOnly)
            {
                data.set(instance, asset::AssetRef::invalid());
                changed = true;
            }
            if (wasEmpty) ImGui::EndDisabled();
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

                    auto result = drawElementValue(label.c_str(), elem, elemType);
                    if (result.has_value())
                    {
                        view[i].assign(*result);
                        containerChanged = true;
                    }

                    ImGui::PopID();
                }

                // Add / Remove buttons
                if (auto defVal = makeDefault(elemType); defVal && ImGui::SmallButton("+ Add"))
                {
                    view.insert(view.end(), defVal);
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

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                    auto result = drawElementValue(valueLabel.c_str(), val, valType);
                    if (result.has_value())
                    {
                        // Erase then re-insert to update (insert alone won't overwrite existing keys)
                        auto k = makeKeyAny();
                        view.erase(k);
                        view.insert(makeKeyAny(), *result);
                        containerChanged = true;
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
                    auto defVal = makeDefault(valType);
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
            ImGui::TextDisabled("%s (unsupported type)", baseLabel);
        }

        if (readOnly)
            ImGui::EndDisabled();

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

            std::string displayName = bridge.name.empty() ? "Unknown" : bridge.name;

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

                const char* compName = bridge.name.c_str();

                // Pass 1: snapshot the fields in EnTT iteration order and collect the
                // distinct group names in first-seen order (ungrouped = empty string).
                std::vector<entt::meta_data> fields;
                std::vector<std::string> groupOrder; // non-empty group names, first-seen
                for (auto&& [id, member] : metaType.data())
                {
                    fields.push_back(member);

                    const char* fieldName = member.name();
                    if (!fieldName)
                        continue;
                    const auto* attr = plugin::PluginContextImpl::getFieldAttributes(compName, fieldName);
                    if (attr && attr->group[0])
                    {
                        const std::string g = attr->group;
                        if (std::find(groupOrder.begin(), groupOrder.end(), g) == groupOrder.end())
                            groupOrder.push_back(g);
                    }
                }

                // Returns the field's group name ("" when ungrouped).
                const auto groupOf = [&](const entt::meta_data& member) -> std::string {
                    const char* fieldName = member.name();
                    if (!fieldName)
                        return std::string();
                    const auto* attr = plugin::PluginContextImpl::getFieldAttributes(compName, fieldName);
                    return (attr && attr->group[0]) ? std::string(attr->group) : std::string();
                };

                // Pass 2a: ungrouped fields first, at the current indent.
                for (auto& member : fields)
                {
                    if (groupOf(member).empty())
                        drawMetaData(member, instance, compName);
                }

                // Pass 2b: each non-empty group under its own collapsing header.
                for (const auto& groupName : groupOrder)
                {
                    std::string headerLabel = groupName + "##meta_group_" + std::to_string(bridge.typeId) + "_" + groupName;
                    if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        for (auto& member : fields)
                        {
                            if (groupOf(member) == groupName)
                                drawMetaData(member, instance, compName);
                        }
                    }
                }

                ImGui::PopID();
                ImGui::Unindent(8.0f);
                ImGui::Spacing();
            }
        }
    }
}
