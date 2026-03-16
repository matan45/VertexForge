#include "InputActionMappingWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "events/input/InputContextEvents.hpp"
#include "events/input/InputEvents.hpp"
#include <imgui.h>

// GLFW key constants (avoid direct GLFW header dependency in editor)
namespace {
    constexpr int VF_KEY_SPACE = 32;
    constexpr int VF_KEY_APOSTROPHE = 39;
    constexpr int VF_KEY_COMMA = 44;
    constexpr int VF_KEY_MINUS = 45;
    constexpr int VF_KEY_PERIOD = 46;
    constexpr int VF_KEY_SLASH = 47;
    constexpr int VF_KEY_0 = 48; constexpr int VF_KEY_1 = 49; constexpr int VF_KEY_2 = 50;
    constexpr int VF_KEY_3 = 51; constexpr int VF_KEY_4 = 52; constexpr int VF_KEY_5 = 53;
    constexpr int VF_KEY_6 = 54; constexpr int VF_KEY_7 = 55; constexpr int VF_KEY_8 = 56;
    constexpr int VF_KEY_9 = 57;
    constexpr int VF_KEY_SEMICOLON = 59;
    constexpr int VF_KEY_EQUAL = 61;
    constexpr int VF_KEY_A = 65; constexpr int VF_KEY_B = 66; constexpr int VF_KEY_C = 67;
    constexpr int VF_KEY_D = 68; constexpr int VF_KEY_E = 69; constexpr int VF_KEY_F = 70;
    constexpr int VF_KEY_G = 71; constexpr int VF_KEY_H = 72; constexpr int VF_KEY_I = 73;
    constexpr int VF_KEY_J = 74; constexpr int VF_KEY_K = 75; constexpr int VF_KEY_L = 76;
    constexpr int VF_KEY_M = 77; constexpr int VF_KEY_N = 78; constexpr int VF_KEY_O = 79;
    constexpr int VF_KEY_P = 80; constexpr int VF_KEY_Q = 81; constexpr int VF_KEY_R = 82;
    constexpr int VF_KEY_S = 83; constexpr int VF_KEY_T = 84; constexpr int VF_KEY_U = 85;
    constexpr int VF_KEY_V = 86; constexpr int VF_KEY_W = 87; constexpr int VF_KEY_X = 88;
    constexpr int VF_KEY_Y = 89; constexpr int VF_KEY_Z = 90;
    constexpr int VF_KEY_LEFT_BRACKET = 91;
    constexpr int VF_KEY_BACKSLASH = 92;
    constexpr int VF_KEY_RIGHT_BRACKET = 93;
    constexpr int VF_KEY_GRAVE_ACCENT = 96;
    constexpr int VF_KEY_ESCAPE = 256;
    constexpr int VF_KEY_ENTER = 257;
    constexpr int VF_KEY_TAB = 258;
    constexpr int VF_KEY_BACKSPACE = 259;
    constexpr int VF_KEY_INSERT = 260;
    constexpr int VF_KEY_DELETE = 261;
    constexpr int VF_KEY_RIGHT = 262;
    constexpr int VF_KEY_LEFT = 263;
    constexpr int VF_KEY_DOWN = 264;
    constexpr int VF_KEY_UP = 265;
    constexpr int VF_KEY_PAGE_UP = 266;
    constexpr int VF_KEY_PAGE_DOWN = 267;
    constexpr int VF_KEY_HOME = 268;
    constexpr int VF_KEY_END = 269;
    constexpr int VF_KEY_CAPS_LOCK = 280;
    constexpr int VF_KEY_SCROLL_LOCK = 281;
    constexpr int VF_KEY_NUM_LOCK = 282;
    constexpr int VF_KEY_PRINT_SCREEN = 283;
    constexpr int VF_KEY_PAUSE = 284;
    constexpr int VF_KEY_F1 = 290; constexpr int VF_KEY_F2 = 291; constexpr int VF_KEY_F3 = 292;
    constexpr int VF_KEY_F4 = 293; constexpr int VF_KEY_F5 = 294; constexpr int VF_KEY_F6 = 295;
    constexpr int VF_KEY_F7 = 296; constexpr int VF_KEY_F8 = 297; constexpr int VF_KEY_F9 = 298;
    constexpr int VF_KEY_F10 = 299; constexpr int VF_KEY_F11 = 300; constexpr int VF_KEY_F12 = 301;
    constexpr int VF_KEY_LEFT_SHIFT = 340;
    constexpr int VF_KEY_LEFT_CONTROL = 341;
    constexpr int VF_KEY_LEFT_ALT = 342;
    constexpr int VF_KEY_LEFT_SUPER = 343;
    constexpr int VF_KEY_RIGHT_SHIFT = 344;
    constexpr int VF_KEY_RIGHT_CONTROL = 345;
    constexpr int VF_KEY_RIGHT_ALT = 346;
    constexpr int VF_KEY_RIGHT_SUPER = 347;
    constexpr int VF_KEY_MENU = 348;
    constexpr int VF_KEY_LAST = 348;
    constexpr int VF_MOUSE_BUTTON_LEFT = 0;
    constexpr int VF_MOUSE_BUTTON_RIGHT = 1;
    constexpr int VF_MOUSE_BUTTON_MIDDLE = 2;
    constexpr int VF_MOUSE_BUTTON_LAST = 7;
}

namespace windows
{
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

        refresh();

        ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Input Action Mapping", &visible))
        {
            // Toolbar
            if (ImGui::Button("Reset All"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                dispatcher.execute(events::input::ResetAllActionBindingsCommand{});
                refresh();
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
                    refresh();
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
                    refresh();
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
                    events::input::IsKeyDownQuery shiftQ; shiftQ.keyCode = VF_KEY_LEFT_SHIFT;
                    events::input::IsKeyDownQuery shiftQ2; shiftQ2.keyCode = VF_KEY_RIGHT_SHIFT;
                    events::input::IsKeyDownQuery ctrlQ; ctrlQ.keyCode = VF_KEY_LEFT_CONTROL;
                    events::input::IsKeyDownQuery ctrlQ2; ctrlQ2.keyCode = VF_KEY_RIGHT_CONTROL;
                    events::input::IsKeyDownQuery altQ; altQ.keyCode = VF_KEY_LEFT_ALT;
                    events::input::IsKeyDownQuery altQ2; altQ2.keyCode = VF_KEY_RIGHT_ALT;
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
                    for (int key = VF_KEY_SPACE; key <= VF_KEY_LAST; ++key)
                    {
                        // Don't capture modifier keys as the primary key
                        if (key == VF_KEY_LEFT_SHIFT || key == VF_KEY_RIGHT_SHIFT ||
                            key == VF_KEY_LEFT_CONTROL || key == VF_KEY_RIGHT_CONTROL ||
                            key == VF_KEY_LEFT_ALT || key == VF_KEY_RIGHT_ALT)
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
                                refresh();
                            }
                            waitingForKey = false;
                            captureActionIndex = -1;
                            break;
                        }
                    }

                    // Capture mouse buttons
                    if (waitingForKey)
                    {
                        for (int btn = 0; btn <= VF_MOUSE_BUTTON_LAST; ++btn)
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
                                    refresh();
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
            refresh();
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
                    refresh();
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
                    refresh();
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
                    refresh();
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
                    refresh();
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
                    refresh();
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
                    refresh();
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
                    refresh();
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
                        refresh();
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

    const char* InputActionMappingWindow::getKeyName(int keyCode) const
    {
        switch (keyCode)
        {
        case VF_KEY_SPACE: return "Space";
        case VF_KEY_APOSTROPHE: return "'";
        case VF_KEY_COMMA: return ",";
        case VF_KEY_MINUS: return "-";
        case VF_KEY_PERIOD: return ".";
        case VF_KEY_SLASH: return "/";
        case VF_KEY_0: return "0"; case VF_KEY_1: return "1"; case VF_KEY_2: return "2";
        case VF_KEY_3: return "3"; case VF_KEY_4: return "4"; case VF_KEY_5: return "5";
        case VF_KEY_6: return "6"; case VF_KEY_7: return "7"; case VF_KEY_8: return "8";
        case VF_KEY_9: return "9";
        case VF_KEY_SEMICOLON: return ";";
        case VF_KEY_EQUAL: return "=";
        case VF_KEY_A: return "A"; case VF_KEY_B: return "B"; case VF_KEY_C: return "C";
        case VF_KEY_D: return "D"; case VF_KEY_E: return "E"; case VF_KEY_F: return "F";
        case VF_KEY_G: return "G"; case VF_KEY_H: return "H"; case VF_KEY_I: return "I";
        case VF_KEY_J: return "J"; case VF_KEY_K: return "K"; case VF_KEY_L: return "L";
        case VF_KEY_M: return "M"; case VF_KEY_N: return "N"; case VF_KEY_O: return "O";
        case VF_KEY_P: return "P"; case VF_KEY_Q: return "Q"; case VF_KEY_R: return "R";
        case VF_KEY_S: return "S"; case VF_KEY_T: return "T"; case VF_KEY_U: return "U";
        case VF_KEY_V: return "V"; case VF_KEY_W: return "W"; case VF_KEY_X: return "X";
        case VF_KEY_Y: return "Y"; case VF_KEY_Z: return "Z";
        case VF_KEY_LEFT_BRACKET: return "["; case VF_KEY_BACKSLASH: return "\\";
        case VF_KEY_RIGHT_BRACKET: return "]"; case VF_KEY_GRAVE_ACCENT: return "`";
        case VF_KEY_ESCAPE: return "Escape";
        case VF_KEY_ENTER: return "Enter";
        case VF_KEY_TAB: return "Tab";
        case VF_KEY_BACKSPACE: return "Backspace";
        case VF_KEY_INSERT: return "Insert";
        case VF_KEY_DELETE: return "Delete";
        case VF_KEY_RIGHT: return "Right"; case VF_KEY_LEFT: return "Left";
        case VF_KEY_DOWN: return "Down"; case VF_KEY_UP: return "Up";
        case VF_KEY_PAGE_UP: return "PageUp"; case VF_KEY_PAGE_DOWN: return "PageDown";
        case VF_KEY_HOME: return "Home"; case VF_KEY_END: return "End";
        case VF_KEY_CAPS_LOCK: return "CapsLock";
        case VF_KEY_SCROLL_LOCK: return "ScrollLock";
        case VF_KEY_NUM_LOCK: return "NumLock";
        case VF_KEY_PRINT_SCREEN: return "PrintScreen";
        case VF_KEY_PAUSE: return "Pause";
        case VF_KEY_F1: return "F1"; case VF_KEY_F2: return "F2"; case VF_KEY_F3: return "F3";
        case VF_KEY_F4: return "F4"; case VF_KEY_F5: return "F5"; case VF_KEY_F6: return "F6";
        case VF_KEY_F7: return "F7"; case VF_KEY_F8: return "F8"; case VF_KEY_F9: return "F9";
        case VF_KEY_F10: return "F10"; case VF_KEY_F11: return "F11"; case VF_KEY_F12: return "F12";
        case VF_KEY_LEFT_SHIFT: return "Left Shift";
        case VF_KEY_LEFT_CONTROL: return "Left Ctrl";
        case VF_KEY_LEFT_ALT: return "Left Alt";
        case VF_KEY_LEFT_SUPER: return "Left Super";
        case VF_KEY_RIGHT_SHIFT: return "Right Shift";
        case VF_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case VF_KEY_RIGHT_ALT: return "Right Alt";
        case VF_KEY_RIGHT_SUPER: return "Right Super";
        case VF_KEY_MENU: return "Menu";
        default:
        {
            static char buf[16];
            snprintf(buf, sizeof(buf), "Key %d", keyCode);
            return buf;
        }
        }
    }

    const char* InputActionMappingWindow::getMouseButtonName(int button) const
    {
        switch (button)
        {
        case VF_MOUSE_BUTTON_LEFT: return "Left";
        case VF_MOUSE_BUTTON_RIGHT: return "Right";
        case VF_MOUSE_BUTTON_MIDDLE: return "Middle";
        case 3: return "Button 4";
        case 4: return "Button 5";
        default:
        {
            static char buf[16];
            snprintf(buf, sizeof(buf), "Button %d", button);
            return buf;
        }
        }
    }
}
