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

        // Load 1D axes
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

        // Load 2D axes
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
            // Toolbar
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

            // Create new action
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
                // Key capture overlay
                if (waitingForKey)
                {
                    // Show which modifiers are held
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

                    // Capture keyboard (skip modifier keys themselves)
                    for (int key = input::Key::Space; key <= input::Key::Last; ++key)
                    {
                        // Don't capture modifier keys as the primary key
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

                    // Capture mouse buttons
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

        // Delete action button on the right
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

            // Context selector
            ImGui::SetNextItemWidth(150.0f);
            if (drawContextCombo("Context##actionctx", entry.context))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::SetActionContextCommand cmd;
                cmd.actionName = entry.name;
                cmd.context = entry.context;
                dispatcher.execute(cmd);
            }

            // List current bindings
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

            // Add binding button
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

    void InputActionMappingWindow::drawAxis1DSection()
    {
        if (ImGui::CollapsingHeader("1D Axes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(16.0f);

            // Create new 1D axis
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##newaxis1d", "Axis name...", newAxis1DName, sizeof(newAxis1DName));
            ImGui::SameLine();
            if (ImGui::Button("+ New 1D Axis"))
            {
                std::string name(newAxis1DName);
                if (!name.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::RegisterAxis1DCommand cmd;
                    cmd.axisName = name;
                    dispatcher.execute(cmd);
                    newAxis1DName[0] = '\0';
                    needsRefresh = true;
                }
            }

            ImGui::Spacing();

            for (int i = 0; i < static_cast<int>(axis1DEntries.size()); ++i)
            {
                auto& entry = axis1DEntries[i];
                ImGui::PushID(("axis1d_" + entry.name).c_str());

                ImGui::Text("%s", entry.name.c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::SmallButton("Del"))
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::UnregisterAxis1DCommand cmd;
                    cmd.axisName = entry.name;
                    dispatcher.execute(cmd);
                    needsRefresh = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::Indent(16.0f);
                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Positive (+1)##pos", entry.positiveAction))
                {
                    // Re-register with updated actions
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::UnregisterAxis1DCommand unreg;
                    unreg.axisName = entry.name;
                    dispatcher.execute(unreg);
                    events::input::RegisterAxis1DCommand reg;
                    reg.axisName = entry.name;
                    reg.positiveAction = entry.positiveAction;
                    reg.negativeAction = entry.negativeAction;
                    dispatcher.execute(reg);
                }

                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Negative (-1)##neg", entry.negativeAction))
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::UnregisterAxis1DCommand unreg;
                    unreg.axisName = entry.name;
                    dispatcher.execute(unreg);
                    events::input::RegisterAxis1DCommand reg;
                    reg.axisName = entry.name;
                    reg.positiveAction = entry.positiveAction;
                    reg.negativeAction = entry.negativeAction;
                    dispatcher.execute(reg);
                }
                ImGui::Unindent(16.0f);

                ImGui::Spacing();
                ImGui::PopID();
            }

            if (axis1DEntries.empty())
                ImGui::TextDisabled("No 1D axes defined.");

            ImGui::Unindent(16.0f);
        }
    }

    void InputActionMappingWindow::drawAxis2DSection()
    {
        if (ImGui::CollapsingHeader("2D Axes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(16.0f);

            // Create new 2D axis
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##newaxis2d", "Axis name...", newAxis2DName, sizeof(newAxis2DName));
            ImGui::SameLine();
            if (ImGui::Button("+ New 2D Axis"))
            {
                std::string name(newAxis2DName);
                if (!name.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::RegisterAxis2DCommand cmd;
                    cmd.axisName = name;
                    cmd.normalize = true;
                    dispatcher.execute(cmd);
                    newAxis2DName[0] = '\0';
                    needsRefresh = true;
                }
            }

            ImGui::Spacing();

            for (int i = 0; i < static_cast<int>(axis2DEntries.size()); ++i)
            {
                auto& entry = axis2DEntries[i];
                ImGui::PushID(("axis2d_" + entry.name).c_str());

                ImGui::Text("%s", entry.name.c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::SmallButton("Del"))
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::UnregisterAxis2DCommand cmd;
                    cmd.axisName = entry.name;
                    dispatcher.execute(cmd);
                    needsRefresh = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::Indent(16.0f);

                auto reregister = [&]()
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::UnregisterAxis2DCommand unreg;
                    unreg.axisName = entry.name;
                    dispatcher.execute(unreg);
                    events::input::RegisterAxis2DCommand reg;
                    reg.axisName = entry.name;
                    reg.upAction = entry.upAction;
                    reg.downAction = entry.downAction;
                    reg.leftAction = entry.leftAction;
                    reg.rightAction = entry.rightAction;
                    reg.normalize = entry.normalize;
                    dispatcher.execute(reg);
                };

                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Up (+Y)##up", entry.upAction)) reregister();
                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Down (-Y)##down", entry.downAction)) reregister();
                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Left (-X)##left", entry.leftAction)) reregister();
                ImGui::SetNextItemWidth(200.0f);
                if (drawActionCombo("Right (+X)##right", entry.rightAction)) reregister();

                if (ImGui::Checkbox("Normalize", &entry.normalize)) reregister();

                ImGui::Unindent(16.0f);

                ImGui::Spacing();
                ImGui::PopID();
            }

            if (axis2DEntries.empty())
                ImGui::TextDisabled("No 2D axes defined.");

            ImGui::Unindent(16.0f);
        }
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

    void InputActionMappingWindow::drawContextSection()
    {
        if (ImGui::CollapsingHeader("Contexts", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(16.0f);

            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##newcontext", "Context name...", newContextName, sizeof(newContextName));
            ImGui::SameLine();
            if (ImGui::Button("+ New Context"))
            {
                std::string name(newContextName);
                if (!name.empty())
                {
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::input::CreateContextCommand cmd;
                    cmd.contextName = name;
                    cmd.blocking = true;
                    dispatcher.execute(cmd);
                    newContextName[0] = '\0';
                    needsRefresh = true;
                }
            }

            ImGui::Spacing();

            for (const auto& ctxName : contextNames)
            {
                ImGui::PushID(("ctx_" + ctxName).c_str());

                auto& dispatcher = events::EventDispatcher::instance();
                events::input::IsContextActiveQuery activeQ;
                activeQ.contextName = ctxName;
                bool active = dispatcher.query(activeQ);

                ImGui::Text("%s", ctxName.c_str());
                if (ctxName != "Default")
                {
                    ImGui::SameLine();
                    if (active)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("Active"))
                        {
                            events::input::PopContextCommand cmd;
                            cmd.contextName = ctxName;
                            dispatcher.execute(cmd);
                        }
                        ImGui::PopStyleColor();
                    }
                    else
                    {
                        if (ImGui::SmallButton("Inactive"))
                        {
                            events::input::PushContextCommand cmd;
                            cmd.contextName = ctxName;
                            dispatcher.execute(cmd);
                        }
                    }

                    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 25.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::SmallButton("Del"))
                    {
                        events::input::RemoveContextCommand cmd;
                        cmd.contextName = ctxName;
                        dispatcher.execute(cmd);
                        needsRefresh = true;
                    }
                    ImGui::PopStyleColor(2);
                }

                ImGui::PopID();
            }

            if (contextNames.empty())
                ImGui::TextDisabled("No contexts defined.");

            ImGui::Unindent(16.0f);
        }
    }

    std::string InputActionMappingWindow::getBindingDisplayName(const services::InputBinding& binding) const
    {
        std::string prefix;
        if (binding.requireShift) prefix += "Shift+";
        if (binding.requireCtrl) prefix += "Ctrl+";
        if (binding.requireAlt) prefix += "Alt+";

        if (binding.type == services::BindingType::MouseButton)
        {
            return prefix + "Mouse " + getMouseButtonName(binding.code);
        }
        else
        {
            return prefix + getKeyName(binding.code);
        }
    }

    std::string InputActionMappingWindow::getKeyName(int keyCode) const
    {
        switch (keyCode)
        {
        case input::Key::Space: return "Space";
        case input::Key::Apostrophe: return "'";
        case input::Key::Comma: return ",";
        case input::Key::Minus: return "-";
        case input::Key::Period: return ".";
        case input::Key::Slash: return "/";
        case input::Key::Num0: return "0"; case input::Key::Num1: return "1"; case input::Key::Num2: return "2";
        case input::Key::Num3: return "3"; case input::Key::Num4: return "4"; case input::Key::Num5: return "5";
        case input::Key::Num6: return "6"; case input::Key::Num7: return "7"; case input::Key::Num8: return "8";
        case input::Key::Num9: return "9";
        case input::Key::Semicolon: return ";";
        case input::Key::Equal: return "=";
        case input::Key::A: return "A"; case input::Key::B: return "B"; case input::Key::C: return "C";
        case input::Key::D: return "D"; case input::Key::E: return "E"; case input::Key::F: return "F";
        case input::Key::G: return "G"; case input::Key::H: return "H"; case input::Key::I: return "I";
        case input::Key::J: return "J"; case input::Key::K: return "K"; case input::Key::L: return "L";
        case input::Key::M: return "M"; case input::Key::N: return "N"; case input::Key::O: return "O";
        case input::Key::P: return "P"; case input::Key::Q: return "Q"; case input::Key::R: return "R";
        case input::Key::S: return "S"; case input::Key::T: return "T"; case input::Key::U: return "U";
        case input::Key::V: return "V"; case input::Key::W: return "W"; case input::Key::X: return "X";
        case input::Key::Y: return "Y"; case input::Key::Z: return "Z";
        case input::Key::LeftBracket: return "["; case input::Key::Backslash: return "\\";
        case input::Key::RightBracket: return "]"; case input::Key::GraveAccent: return "`";
        case input::Key::Escape: return "Escape";
        case input::Key::Enter: return "Enter";
        case input::Key::Tab: return "Tab";
        case input::Key::Backspace: return "Backspace";
        case input::Key::Insert: return "Insert";
        case input::Key::Delete: return "Delete";
        case input::Key::Right: return "Right"; case input::Key::Left: return "Left";
        case input::Key::Down: return "Down"; case input::Key::Up: return "Up";
        case input::Key::PageUp: return "PageUp"; case input::Key::PageDown: return "PageDown";
        case input::Key::Home: return "Home"; case input::Key::End: return "End";
        case input::Key::CapsLock: return "CapsLock";
        case input::Key::ScrollLock: return "ScrollLock";
        case input::Key::NumLock: return "NumLock";
        case input::Key::PrintScreen: return "PrintScreen";
        case input::Key::Pause: return "Pause";
        case input::Key::F1: return "F1"; case input::Key::F2: return "F2"; case input::Key::F3: return "F3";
        case input::Key::F4: return "F4"; case input::Key::F5: return "F5"; case input::Key::F6: return "F6";
        case input::Key::F7: return "F7"; case input::Key::F8: return "F8"; case input::Key::F9: return "F9";
        case input::Key::F10: return "F10"; case input::Key::F11: return "F11"; case input::Key::F12: return "F12";
        case input::Key::LeftShift: return "Left Shift";
        case input::Key::LeftControl: return "Left Ctrl";
        case input::Key::LeftAlt: return "Left Alt";
        case input::Key::LeftSuper: return "Left Super";
        case input::Key::RightShift: return "Right Shift";
        case input::Key::RightControl: return "Right Ctrl";
        case input::Key::RightAlt: return "Right Alt";
        case input::Key::RightSuper: return "Right Super";
        case input::Key::Menu: return "Menu";
        default:
        {
            return "Key " + std::to_string(keyCode);
        }
        }
    }

    std::string InputActionMappingWindow::getMouseButtonName(int button) const
    {
        switch (button)
        {
        case input::Mouse::Left: return "Left";
        case input::Mouse::Right: return "Right";
        case input::Mouse::Middle: return "Middle";
        case 3: return "Button 4";
        case 4: return "Button 5";
        default:
        {
            return "Button " + std::to_string(button);
        }
        }
    }
}
