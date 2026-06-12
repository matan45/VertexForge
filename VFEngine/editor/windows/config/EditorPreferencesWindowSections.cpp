#include "EditorPreferencesWindow.hpp"
#include "SettingsTooltip.hpp"
#include "../../handlers/EditorLayoutManager.hpp"
#include "string/StringUtil.hpp"
#include <imgui.h>
#include <cstring>

namespace windows
{
    // ============================================
    // Appearance
    // ============================================

    void EditorPreferencesWindow::drawThemeCombo(const char* label)
    {
        if (ImGui::BeginCombo(label, settings.appearance.theme.c_str()))
        {
            const char* builtIns[] = {"Dark", "Light"};
            for (const char* name : builtIns)
            {
                bool selected = (settings.appearance.theme == name);
                if (ImGui::Selectable(name, selected))
                    selectTheme(name);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            if (!customThemes.empty())
                ImGui::Separator();

            for (const auto& name : customThemes)
            {
                bool selected = (settings.appearance.theme == name);
                if (ImGui::Selectable(name.c_str(), selected))
                    selectTheme(name);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        drawSettingTooltip("Editor color theme (changes preview immediately, Apply to keep)");
    }

    void EditorPreferencesWindow::drawThemeManagement()
    {
        ImGui::SetNextItemWidth(220.0f);
        bool submitted = ImGui::InputTextWithHint("##NewThemeName", "New theme name", newThemeNameBuffer,
                                                  sizeof(newThemeNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(newThemeNameBuffer[0] == '\0');
        if (ImGui::Button("Create") || submitted)
            createThemeFromCurrent();
        ImGui::EndDisabled();
        drawSettingTooltip("Save the current colors as a new editable theme");

        if (editingCustomTheme)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete"))
                ImGui::OpenPopup("DeleteThemeConfirm");
        }

        if (!themeError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", themeError.c_str());

        if (ImGui::BeginPopupModal("DeleteThemeConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete theme \"%s\"? This cannot be undone.", settings.appearance.theme.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Delete", ImVec2(80, 0)))
            {
                deleteSelectedTheme();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void EditorPreferencesWindow::drawThemeColorEditor()
    {
        if (!editingCustomTheme)
        {
            ImGui::TextDisabled("Create a new theme to edit individual colors");
            return;
        }

        if (!ImGui::CollapsingHeader("Theme Colors"))
            return;

        char filterBuffer[128];
        std::strncpy(filterBuffer, themeColorFilter.c_str(), sizeof(filterBuffer) - 1);
        filterBuffer[sizeof(filterBuffer) - 1] = '\0';
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputTextWithHint("##ThemeColorFilter", "Filter colors", filterBuffer, sizeof(filterBuffer)))
            themeColorFilter = filterBuffer;

        std::string filterLower = StringUtil::toLower(themeColorFilter);

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::BeginChild("ThemeColorList", ImVec2(0, 280.0f), true);
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            const char* colorName = ImGui::GetStyleColorName(i);
            if (!filterLower.empty() && StringUtil::toLower(colorName).find(filterLower) == std::string::npos)
                continue;

            // Edit the live style directly for instant preview; mirror into the theme.
            if (ImGui::ColorEdit4(colorName, &style.Colors[i].x))
            {
                const ImVec4& c = style.Colors[i];
                editedTheme.colors[colorName] = glm::vec4(c.x, c.y, c.z, c.w);
                themePreviewActive = true;
                markDirty();
            }
        }
        ImGui::EndChild();
    }

    void EditorPreferencesWindow::drawAppearanceSection()
    {
        ImGui::Text("Theme");
        ImGui::Spacing();

        drawThemeCombo("Theme");
        drawThemeManagement();
        drawThemeColorEditor();

        ImGui::Spacing();

        ImGui::BeginDisabled(editingCustomTheme);
        if (ImGui::ColorEdit4("Accent Color", &settings.appearance.accentColor.x))
            markDirty();
        ImGui::EndDisabled();
        drawSettingTooltip(editingCustomTheme
            ? "Custom themes define their own colors"
            : "Accent color used for highlights and active elements");

        ImGui::Spacing();
        ImGui::Text("UI");
        ImGui::Spacing();

        if (ImGui::DragFloat("UI Scale", &settings.appearance.uiScale, 0.05f, 0.5f, 3.0f, "%.2f"))
            markDirty();
        drawSettingTooltip("Scale factor for all UI elements");

        if (ImGui::DragInt("Font Size", &settings.appearance.fontSize, 1, 8, 32))
            markDirty();
        drawSettingTooltip("Base font size in pixels");

        ImGui::Spacing();
        ImGui::Text("Panels");
        ImGui::Spacing();

        if (ImGui::Checkbox("Rounded Corners", &settings.appearance.roundedCorners))
            markDirty();
        drawSettingTooltip("Enable rounded corners on panels and buttons");

        if (ImGui::Checkbox("Panel Shadows", &settings.appearance.panelShadows))
            markDirty();
        drawSettingTooltip("Render subtle shadows behind panels for depth");

        if (ImGui::Checkbox("Compact Mode", &settings.appearance.compactMode))
            markDirty();
        drawSettingTooltip("Reduce padding and spacing for more compact UI layout");
    }

    // ============================================
    // Debug
    // ============================================

    void EditorPreferencesWindow::drawDebugSection()
    {
        ImGui::Text("Stats Overlay");
        ImGui::Spacing();

        if (ImGui::Checkbox("Show FPS", &settings.debug.showFPS))
            markDirty();
        drawSettingTooltip("Display frames per second counter in the status bar");

        if (ImGui::Checkbox("Show GPU Time", &settings.debug.showGPUTime))
            markDirty();
        drawSettingTooltip("Display frame time in milliseconds in the status bar");

        if (ImGui::Checkbox("Show Draw Calls", &settings.debug.showDrawCalls))
            markDirty();
        drawSettingTooltip("Display the number of visible objects per frame in the status bar");

        ImGui::Spacing();
        ImGui::Text("Logging");
        ImGui::Spacing();

        const char* logLevels[] = {"Trace", "Debug", "Info", "Warning", "Error"};
        int logIdx = 2;
        if (settings.debug.logLevel == "Trace") logIdx = 0;
        else if (settings.debug.logLevel == "Debug") logIdx = 1;
        else if (settings.debug.logLevel == "Info") logIdx = 2;
        else if (settings.debug.logLevel == "Warning") logIdx = 3;
        else if (settings.debug.logLevel == "Error") logIdx = 4;
        if (ImGui::Combo("Log Level", &logIdx, logLevels, 5))
        {
            settings.debug.logLevel = logLevels[logIdx];
            markDirty();
        }
        drawSettingTooltip("Minimum severity level for log output (applied at runtime)");
    }

    // ============================================
    // Window Layout
    // ============================================

    void EditorPreferencesWindow::drawWindowLayoutSection()
    {
        ImGui::TextWrapped("Editor dock layouts are stored as ImGui .ini presets. "
                           "Save the current arrangement as a named preset, load one, or reset to the default.");
        ImGui::Spacing();

        auto layouts = handlers::EditorLayoutManager::getSavedLayouts();

        ImGui::Text("Save Current Layout");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##SaveLayoutName", "Preset name", saveLayoutNameBuffer, sizeof(saveLayoutNameBuffer));
        ImGui::SameLine();
        ImGui::BeginDisabled(saveLayoutNameBuffer[0] == '\0');
        if (ImGui::Button("Save As"))
        {
            handlers::EditorLayoutManager::saveLayout(saveLayoutNameBuffer);
            saveLayoutNameBuffer[0] = '\0';
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Saved Layouts");
        ImGui::Spacing();
        if (layouts.empty())
        {
            ImGui::TextDisabled("No saved layouts");
        }
        else
        {
            for (const auto& name : layouts)
            {
                ImGui::PushID(name.c_str());
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine(220.0f);
                if (ImGui::SmallButton("Load"))
                    handlers::EditorLayoutManager::loadLayout(name);
                ImGui::PopID();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Default Layout");
        ImGui::Spacing();
        if (ImGui::Button("Reset to Default Layout"))
            handlers::EditorLayoutManager::resetLayout(dockSpaceId);
        drawSettingTooltip("Rebuild the standard editor dock arrangement");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Startup Layout");
        ImGui::Spacing();

        // Build the combo options: "Default" plus every saved layout.
        std::vector<std::string> options;
        options.push_back("Default");
        for (const auto& name : layouts)
            options.push_back(name);

        int currentIdx = 0;
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            if (options[i] == settings.windowLayout.startupLayout)
            {
                currentIdx = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Load on Startup", options[currentIdx].c_str()))
        {
            for (int i = 0; i < static_cast<int>(options.size()); ++i)
            {
                bool selected = (i == currentIdx);
                if (ImGui::Selectable(options[i].c_str(), selected))
                {
                    settings.windowLayout.startupLayout = options[i];
                    markDirty();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        drawSettingTooltip("Layout applied automatically when the editor starts");
    }

    // ============================================
    // Settings Registry (for search)
    // ============================================

    void EditorPreferencesWindow::buildSettingsRegistry()
    {
        settingsRegistry.clear();

        // Appearance
        settingsRegistry.push_back({"Theme", "Editor color theme", {"theme", "dark", "light", "custom", "color", "appearance"}, Appearance,
            [this]() {
                drawThemeCombo("Theme##s");
            }});

        settingsRegistry.push_back({"Accent Color", "Highlight and active element color", {"color", "accent", "appearance"}, Appearance,
            [this]() {
                if (ImGui::ColorEdit4("Accent Color##s", &settings.appearance.accentColor.x)) markDirty();
                drawSettingTooltip("Accent color used for highlights and active elements");
            }});

        settingsRegistry.push_back({"UI Scale", "Scale factor for UI elements", {"scale", "size", "zoom", "appearance"}, Appearance,
            [this]() {
                if (ImGui::DragFloat("UI Scale##s", &settings.appearance.uiScale, 0.05f, 0.5f, 3.0f, "%.2f")) markDirty();
                drawSettingTooltip("Scale factor for all UI elements");
            }});

        settingsRegistry.push_back({"Font Size", "Base font size in pixels", {"font", "text", "size", "appearance"}, Appearance,
            [this]() {
                if (ImGui::DragInt("Font Size##s", &settings.appearance.fontSize, 1, 8, 32)) markDirty();
                drawSettingTooltip("Base font size in pixels");
            }});

        settingsRegistry.push_back({"Rounded Corners", "Round panel and button corners", {"corners", "round", "appearance"}, Appearance,
            [this]() {
                if (ImGui::Checkbox("Rounded Corners##s", &settings.appearance.roundedCorners)) markDirty();
                drawSettingTooltip("Enable rounded corners on panels and buttons");
            }});

        settingsRegistry.push_back({"Panel Shadows", "Subtle shadows behind panels", {"shadows", "panels", "appearance"}, Appearance,
            [this]() {
                if (ImGui::Checkbox("Panel Shadows##s", &settings.appearance.panelShadows)) markDirty();
                drawSettingTooltip("Render subtle shadows behind panels for depth");
            }});

        settingsRegistry.push_back({"Compact Mode", "Reduce UI padding and spacing", {"compact", "dense", "appearance"}, Appearance,
            [this]() {
                if (ImGui::Checkbox("Compact Mode##s", &settings.appearance.compactMode)) markDirty();
                drawSettingTooltip("Reduce padding and spacing for more compact UI layout");
            }});

        // Debug
        settingsRegistry.push_back({"Show FPS", "Frames per second counter", {"fps", "performance", "stats"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show FPS##s", &settings.debug.showFPS)) markDirty();
                drawSettingTooltip("Display frames per second counter in the status bar");
            }});

        settingsRegistry.push_back({"Show GPU Time", "GPU frame time display", {"gpu", "time", "performance"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show GPU Time##s", &settings.debug.showGPUTime)) markDirty();
                drawSettingTooltip("Display frame time in milliseconds in the status bar");
            }});

        settingsRegistry.push_back({"Show Draw Calls", "Draw call counter", {"draw", "calls", "performance"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show Draw Calls##s", &settings.debug.showDrawCalls)) markDirty();
                drawSettingTooltip("Display the number of visible objects per frame in the status bar");
            }});

        settingsRegistry.push_back({"Log Level", "Minimum log severity", {"log", "level", "debug", "trace"}, Debug,
            [this]() {
                const char* levels[] = {"Trace", "Debug", "Info", "Warning", "Error"};
                int idx = 2;
                if (settings.debug.logLevel == "Trace") idx = 0;
                else if (settings.debug.logLevel == "Debug") idx = 1;
                else if (settings.debug.logLevel == "Info") idx = 2;
                else if (settings.debug.logLevel == "Warning") idx = 3;
                else if (settings.debug.logLevel == "Error") idx = 4;
                if (ImGui::Combo("Log Level##s", &idx, levels, 5)) { settings.debug.logLevel = levels[idx]; markDirty(); }
                drawSettingTooltip("Minimum severity level for log output");
            }});
    }
}
