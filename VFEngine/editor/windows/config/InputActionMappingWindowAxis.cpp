#include "InputActionMappingWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "events/input/InputContextEvents.hpp"
#include "input/KeyCodes.hpp"
#include <imgui.h>

namespace windows
{
    void InputActionMappingWindow::drawAxis1DSection()
    {
        if (ImGui::CollapsingHeader("1D Axes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(16.0f);

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
        default: return "Key " + std::to_string(keyCode);
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
        default: return "Button " + std::to_string(button);
        }
    }
}
