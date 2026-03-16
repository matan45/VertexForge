#include "InputActionMappingWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/input/ActionMappingEvents.hpp"
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
    constexpr int VF_KEY_0 = 48;
    constexpr int VF_KEY_9 = 57;
    constexpr int VF_KEY_SEMICOLON = 59;
    constexpr int VF_KEY_EQUAL = 61;
    constexpr int VF_KEY_A = 65;
    constexpr int VF_KEY_Z = 90;
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
    constexpr int VF_KEY_F1 = 290;
    constexpr int VF_KEY_F12 = 301;
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
        auto& dispatcher = events::EventDispatcher::instance();

        auto names = dispatcher.query(events::input::GetAllActionNamesQuery{});
        for (const auto& name : names)
        {
            events::input::GetActionBindingsQuery query;
            query.actionName = name;
            auto bindings = dispatcher.query(query);

            ActionEntry entry;
            entry.name = name;
            entry.bindings = bindings;
            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(),
            [](const ActionEntry& a, const ActionEntry& b) { return a.name < b.name; });
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
            if (ImGui::Button("Refresh"))
                needsRefresh = true;

            ImGui::SameLine();
            if (ImGui::Button("Reset All"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                dispatcher.execute(events::input::ResetAllActionBindingsCommand{});
                needsRefresh = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Save"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::SaveActionBindingsCommand cmd;
                cmd.filePath = "config/default.vfInputMapping";
                dispatcher.execute(cmd);
            }

            ImGui::SameLine();
            if (ImGui::Button("Load"))
            {
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::LoadActionBindingsCommand cmd;
                cmd.filePath = "config/default.vfInputMapping";
                dispatcher.execute(cmd);
                needsRefresh = true;
            }

            ImGui::Separator();
            ImGui::Spacing();

            if (entries.empty())
            {
                ImGui::TextDisabled("No actions registered. Register actions from scripts using InputAction::register()");
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
                    drawActionEntry(i);
                }
            }
        }
        ImGui::End();
    }

    void InputActionMappingWindow::drawActionEntry(int index)
    {
        auto& entry = entries[index];
        ImGui::PushID(entry.name.c_str());

        if (ImGui::CollapsingHeader(entry.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(16.0f);

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
