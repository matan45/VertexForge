#include "InputActionMappingWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "events/input/InputContextEvents.hpp"
#include "events/input/InputEvents.hpp"
#include "input/KeyCodes.hpp"
#include <imgui.h>

namespace windows
{
    InputActionMappingWindow::InputActionMappingWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        mappingChangedToken = dispatcher.subscribe<events::input::ActionMappingChangedNotification>(
            [this](const events::input::ActionMappingChangedNotification&)
            {
                needsRefresh = true;
            });
    }

    InputActionMappingWindow::~InputActionMappingWindow()
    {
        if (mappingChangedToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(mappingChangedToken);
        }
    }

    void InputActionMappingWindow::refresh()
    {
        entries.clear();
        axis1DEntries.clear();
        axis2DEntries.clear();
        auto& dispatcher = events::EventDispatcher::instance();

        auto names = dispatcher.query(events::input::GetAllActionNamesQuery{});
        actionNames = names;
        std::sort(actionNames.begin(), actionNames.end());

        contextNames = dispatcher.query(events::input::GetAllContextNamesQuery{});
        std::sort(contextNames.begin(), contextNames.end());

        for (const auto& name : names)
        {
            events::input::GetActionBindingsQuery bQuery;
            bQuery.actionName = name;
            auto bindings = dispatcher.query(bQuery);

            events::input::GetActionContextQuery cQuery;
            cQuery.actionName = name;
            auto ctx = dispatcher.query(cQuery);

            ActionEntry entry;
            entry.name = name;
            entry.context = ctx;
            entry.bindings = bindings;
            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(),
            [](const ActionEntry& a, const ActionEntry& b) { return a.name < b.name; });

        auto axis1DNames = dispatcher.query(events::input::GetAllAxis1DNamesQuery{});
        for (const auto& name : axis1DNames)
        {
            events::input::GetAxis1DDefinitionQuery q;
            q.axisName = name;
            auto def = dispatcher.query(q);
            if (def.has_value())
            {
                Axis1DEntry e;
                e.name = def->name;
                e.positiveAction = def->positiveAction;
                e.negativeAction = def->negativeAction;
                axis1DEntries.push_back(std::move(e));
            }
        }
        std::sort(axis1DEntries.begin(), axis1DEntries.end(),
            [](const Axis1DEntry& a, const Axis1DEntry& b) { return a.name < b.name; });

        auto axis2DNames = dispatcher.query(events::input::GetAllAxis2DNamesQuery{});
        for (const auto& name : axis2DNames)
        {
            events::input::GetAxis2DDefinitionQuery q;
            q.axisName = name;
            auto def = dispatcher.query(q);
            if (def.has_value())
            {
                Axis2DEntry e;
                e.name = def->name;
                e.upAction = def->upAction;
                e.downAction = def->downAction;
                e.leftAction = def->leftAction;
                e.rightAction = def->rightAction;
                e.normalize = def->normalize;
                axis2DEntries.push_back(std::move(e));
            }
        }
        std::sort(axis2DEntries.begin(), axis2DEntries.end(),
            [](const Axis2DEntry& a, const Axis2DEntry& b) { return a.name < b.name; });
    }

    void InputActionMappingWindow::draw()
    {
        if (!visible)
            return;

        if (needsRefresh)
        {
            refresh();
            needsRefresh = false;
        }

        ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Input Action Mapping", &visible))
        {
            if (ImGui::Button("Reset All"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                dispatcher.execute(events::input::ResetAllActionBindingsCommand{});
                needsRefresh = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Input Mapping (*.vfInputMapping)", L"*.vfInputMapping"}
                };
                std::string savePath = fileDialog.saveFileDialog(fileTypes, L"vfInputMapping");
                if (!savePath.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::SaveActionBindingsCommand cmd;
                    cmd.filePath = savePath;
                    dispatcher.execute(cmd);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Input Mapping (*.vfInputMapping)", L"*.vfInputMapping"}
                };
                std::string loadPath = fileDialog.openFileDialog(fileTypes);
                if (!loadPath.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::LoadActionBindingsCommand cmd;
                    cmd.filePath = loadPath;
                    dispatcher.execute(cmd);
                    needsRefresh = true;
                }
            }

            ImGui::Separator();
            ImGui::Spacing();

            drawContextFilter();

            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##newaction", "Action name...", newActionName, sizeof(newActionName));
            ImGui::SameLine();
            if (ImGui::Button("+ New Action"))
            {
                std::string name(newActionName);
                if (!name.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::RegisterActionCommand cmd;
                    cmd.actionName = name;
                    cmd.context = selectedContextFilter != "All" ? selectedContextFilter : "Default";
                    dispatcher.execute(cmd);
                    newActionName[0] = '\0';
                    needsRefresh = true;
                }
            }

            ImGui::Separator();
            ImGui::Spacing();

            if (entries.empty())
            {
                ImGui::TextDisabled("No actions defined. Use '+ New Action' above to create one.");
            }
            else
            {
                if (waitingForKey)
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::IsKeyDownQuery shiftQ; shiftQ.keyCode = input::Key::LeftShift;
                    events::input::IsKeyDownQuery shiftQ2; shiftQ2.keyCode = input::Key::RightShift;
                    events::input::IsKeyDownQuery ctrlQ; ctrlQ.keyCode = input::Key::LeftControl;
                    events::input::IsKeyDownQuery ctrlQ2; ctrlQ2.keyCode = input::Key::RightControl;
                    events::input::IsKeyDownQuery altQ; altQ.keyCode = input::Key::LeftAlt;
                    events::input::IsKeyDownQuery altQ2; altQ2.keyCode = input::Key::RightAlt;
                    bool shiftHeld = dispatcher.query(shiftQ) || dispatcher.query(shiftQ2);
                    bool ctrlHeld = dispatcher.query(ctrlQ) || dispatcher.query(ctrlQ2);
                    bool altHeld = dispatcher.query(altQ) || dispatcher.query(altQ2);

                    std::string hint = "Press any key or mouse button to bind...";
                    if (shiftHeld || ctrlHeld || altHeld)
                    {
                        hint = "Combo: ";
                        if (shiftHeld) hint += "Shift+";
                        if (ctrlHeld) hint += "Ctrl+";
                        if (altHeld) hint += "Alt+";
                        hint += "?";
                    }
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", hint.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cancel"))
                    {
                        waitingForKey = false;
                        captureActionIndex = -1;
                    }

                    for (int key = input::Key::Space; key <= input::Key::Last; ++key)
                    {
                        if (key == input::Key::LeftShift || key == input::Key::RightShift ||
                            key == input::Key::LeftControl || key == input::Key::RightControl ||
                            key == input::Key::LeftAlt || key == input::Key::RightAlt)
                            continue;

                        events::input::IsKeyPressedQuery query;
                        query.keyCode = key;
                        if (dispatcher.query(query))
                        {
                            if (captureActionIndex >= 0 && captureActionIndex < static_cast<int>(entries.size()))
                            {
                                services::InputBinding binding;
                                binding.type = services::BindingType::Key;
                                binding.code = key;
                                binding.requireShift = shiftHeld;
                                binding.requireCtrl = ctrlHeld;
                                binding.requireAlt = altHeld;

                                events::input::AddActionBindingCommand cmd;
                                cmd.actionName = entries[captureActionIndex].name;
                                cmd.binding = binding;
                                dispatcher.execute(cmd);
                                needsRefresh = true;
                            }
                            waitingForKey = false;
                            captureActionIndex = -1;
                            break;
                        }
                    }

                    if (waitingForKey)
                    {
                        for (int btn = 0; btn <= input::Mouse::Last; ++btn)
                        {
                            events::input::IsMouseButtonPressedQuery query;
                            query.button = btn;
                            if (dispatcher.query(query))
                            {
                                if (captureActionIndex >= 0 && captureActionIndex < static_cast<int>(entries.size()))
                                {
                                    services::InputBinding binding;
                                    binding.type = services::BindingType::MouseButton;
                                    binding.code = btn;
                                    binding.requireShift = shiftHeld;
                                    binding.requireCtrl = ctrlHeld;
                                    binding.requireAlt = altHeld;

                                    events::input::AddActionBindingCommand cmd;
                                    cmd.actionName = entries[captureActionIndex].name;
                                    cmd.binding = binding;
                                    dispatcher.execute(cmd);
                                    needsRefresh = true;
                                }
                                waitingForKey = false;
                                captureActionIndex = -1;
                                break;
                            }
                        }
                    }

                    ImGui::Separator();
                    ImGui::Spacing();
                }

                for (int i = 0; i < static_cast<int>(entries.size()); ++i)
                {
                    if (selectedContextFilter != "All" && entries[i].context != selectedContextFilter)
                        continue;
                    drawActionEntry(i);
                }
            }

            ImGui::Separator();
            ImGui::Spacing();
            drawAxis1DSection();

            ImGui::Separator();
            ImGui::Spacing();
            drawAxis2DSection();

            ImGui::Separator();
            ImGui::Spacing();
            drawContextSection();
        }
        ImGui::End();
    }

    void InputActionMappingWindow::drawActionEntry(int index)
    {
        auto& entry = entries[index];
        ImGui::PushID(entry.name.c_str());

        std::string headerLabel = entry.name;
        if (entry.context != "Default")
            headerLabel += "  [" + entry.context + "]";

        bool headerOpen = ImGui::CollapsingHeader(headerLabel.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::SmallButton("Del"))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::input::UnregisterActionCommand cmd;
            cmd.actionName = entry.name;
            dispatcher.execute(cmd);
            needsRefresh = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete action '%s'", entry.name.c_str());

        if (headerOpen)
        {
            ImGui::Indent(16.0f);

            ImGui::SetNextItemWidth(150.0f);
            if (drawContextCombo("Context##actionctx", entry.context))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::SetActionContextCommand cmd;
                cmd.actionName = entry.name;
                cmd.context = entry.context;
                dispatcher.execute(cmd);
            }

            for (int i = 0; i < static_cast<int>(entry.bindings.size()); ++i)
            {
                ImGui::PushID(i);
                std::string displayName = getBindingDisplayName(entry.bindings[i]);
                ImGui::Text("%s", displayName.c_str());

                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 30.0f);
                if (ImGui::SmallButton("X"))
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::RemoveActionBindingCommand cmd;
                    cmd.actionName = entry.name;
                    cmd.binding = entry.bindings[i];
                    dispatcher.execute(cmd);
                    needsRefresh = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove this binding");

                ImGui::PopID();
            }

            if (entry.bindings.empty())
            {
                ImGui::TextDisabled("No bindings");
            }

            if (!waitingForKey)
            {
                if (ImGui::SmallButton("+ Add Binding"))
                {
                    waitingForKey = true;
                    captureActionIndex = index;
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Reset"))
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::ResetActionBindingsCommand cmd;
                    cmd.actionName = entry.name;
                    dispatcher.execute(cmd);
                    needsRefresh = true;
                }
            }

            ImGui::Unindent(16.0f);
            ImGui::Spacing();
        }

        ImGui::PopID();
    }

    bool InputActionMappingWindow::drawActionCombo(const char* label, std::string& current)
    {
        bool changed = false;
        int selectedIdx = -1;
        for (int i = 0; i < static_cast<int>(actionNames.size()); ++i)
        {
            if (actionNames[i] == current)
            {
                selectedIdx = i;
                break;
            }
        }

        const char* preview = selectedIdx >= 0 ? actionNames[selectedIdx].c_str() : "(none)";
        if (ImGui::BeginCombo(label, preview))
        {
            for (int i = 0; i < static_cast<int>(actionNames.size()); ++i)
            {
                bool isSelected = (i == selectedIdx);
                if (ImGui::Selectable(actionNames[i].c_str(), isSelected))
                {
                    current = actionNames[i];
                    changed = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void InputActionMappingWindow::drawContextFilter()
    {
        ImGui::Text("Context:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##contextfilter", selectedContextFilter.c_str()))
        {
            if (ImGui::Selectable("All", selectedContextFilter == "All"))
                selectedContextFilter = "All";
            for (const auto& ctx : contextNames)
            {
                bool selected = (ctx == selectedContextFilter);
                if (ImGui::Selectable(ctx.c_str(), selected))
                    selectedContextFilter = ctx;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
    }

    bool InputActionMappingWindow::drawContextCombo(const char* label, std::string& current)
    {
        bool changed = false;
        int selectedIdx = -1;
        for (int i = 0; i < static_cast<int>(contextNames.size()); ++i)
        {
            if (contextNames[i] == current)
            {
                selectedIdx = i;
                break;
            }
        }

        const char* preview = selectedIdx >= 0 ? contextNames[selectedIdx].c_str() : "Default";
        if (ImGui::BeginCombo(label, preview))
        {
            for (int i = 0; i < static_cast<int>(contextNames.size()); ++i)
            {
                bool isSelected = (i == selectedIdx);
                if (ImGui::Selectable(contextNames[i].c_str(), isSelected))
                {
                    current = contextNames[i];
                    changed = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    std::string InputActionMappingWindow::getBindingDisplayName(const services::InputBinding& binding) const
    {
        std::string prefix;
        if (binding.requireShift) prefix += "Shift+";
        if (binding.requireCtrl) prefix += "Ctrl+";
        if (binding.requireAlt) prefix += "Alt+";

        if (binding.type == services::BindingType::MouseButton)
            return prefix + "Mouse " + getMouseButtonName(binding.code);
        return prefix + getKeyName(binding.code);
    }
}
