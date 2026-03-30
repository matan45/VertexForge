#include "EditorPreferencesWindow.hpp"
#include "SettingsTooltip.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorKeybindingEvents.hpp"
#include "data/EditorKeybindingTypes.hpp"
#include "input/KeyCodes.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace windows
{
    // ============================================
    // General
    // ============================================

    void EditorPreferencesWindow::drawGeneralSection()
    {
        ImGui::Text("Project");
        ImGui::Spacing();

        char nameBuffer[256];
        std::strncpy(nameBuffer, settings.general.projectName.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##ProjectName", nameBuffer, sizeof(nameBuffer)))
        {
            settings.general.projectName = std::string(nameBuffer);
            markDirty();
        }
        drawSettingTooltip("Project name displayed in the title bar");

        char sceneBuffer[256];
        std::strncpy(sceneBuffer, settings.general.defaultScene.c_str(), sizeof(sceneBuffer) - 1);
        sceneBuffer[sizeof(sceneBuffer) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##DefaultScene", sceneBuffer, sizeof(sceneBuffer)))
        {
            settings.general.defaultScene = std::string(sceneBuffer);
            markDirty();
        }
        drawSettingTooltip("Scene to load by default when opening the project");

        ImGui::Spacing();
        ImGui::Text("Auto Save");
        ImGui::Spacing();

        if (ImGui::Checkbox("Enable Auto Save", &settings.general.autoSaveEnabled))
            markDirty();
        drawSettingTooltip("Automatically save the project at regular intervals");

        if (settings.general.autoSaveEnabled)
        {
            if (ImGui::DragInt("Interval (minutes)", &settings.general.autoSaveIntervalMinutes, 1, 1, 60))
                markDirty();
            drawSettingTooltip("Time between automatic saves in minutes");
        }

        ImGui::Spacing();
        ImGui::Text("Startup");
        ImGui::Spacing();

        if (ImGui::Checkbox("Load Last Project", &settings.general.loadLastProject))
            markDirty();
        drawSettingTooltip("Automatically load the last opened project on startup");

        if (ImGui::Checkbox("Show Splash Screen", &settings.general.showSplashScreen))
            markDirty();
        drawSettingTooltip("Display splash screen when the editor starts");
    }

    // ============================================
    // Appearance
    // ============================================

    void EditorPreferencesWindow::drawAppearanceSection()
    {
        ImGui::Text("Theme");
        ImGui::Spacing();

        const char* themes[] = {"Dark", "Light"};
        int themeIdx = (settings.appearance.theme == "Light") ? 1 : 0;
        if (ImGui::Combo("Theme", &themeIdx, themes, 2))
        {
            settings.appearance.theme = themes[themeIdx];
            markDirty();
        }
        drawSettingTooltip("Editor color theme");

        if (ImGui::ColorEdit4("Accent Color", &settings.appearance.accentColor.x))
            markDirty();
        drawSettingTooltip("Accent color used for highlights and active elements");

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
    // Input
    // ============================================

    void EditorPreferencesWindow::drawInputSection()
    {
        ImGui::Text("Camera");
        ImGui::Spacing();

        if (ImGui::DragFloat("Speed", &settings.input.cameraSpeed, 0.1f, 0.1f, 50.0f, "%.1f"))
            markDirty();
        drawSettingTooltip("Camera movement speed in the viewport");

        if (ImGui::DragFloat("Sensitivity", &settings.input.cameraSensitivity, 0.01f, 0.1f, 5.0f, "%.2f"))
            markDirty();
        drawSettingTooltip("Mouse sensitivity for camera rotation");

        ImGui::Spacing();
        ImGui::Text("Controls");
        ImGui::Spacing();

        const char* orbitModes[] = {"Unreal", "Maya", "Blender"};
        int orbitIdx = 0;
        if (settings.input.orbitMode == "Maya") orbitIdx = 1;
        else if (settings.input.orbitMode == "Blender") orbitIdx = 2;
        if (ImGui::Combo("Orbit Mode", &orbitIdx, orbitModes, 3))
        {
            settings.input.orbitMode = orbitModes[orbitIdx];
            markDirty();
        }
        drawSettingTooltip("Camera orbit control scheme\nUnreal: RMB orbit, Maya: Alt+LMB, Blender: MMB");

        if (ImGui::Checkbox("Invert Y Axis", &settings.input.invertYAxis))
            markDirty();
        drawSettingTooltip("Invert vertical mouse movement for camera control");

        ImGui::Spacing();
        ImGui::Text("Navigation");
        ImGui::Spacing();

        if (ImGui::Checkbox("Smooth Camera", &settings.input.smoothCamera))
            markDirty();
        drawSettingTooltip("Enable smooth interpolation for camera movement");

        if (ImGui::Checkbox("Focus on Selection (F)", &settings.input.focusOnSelection))
            markDirty();
        drawSettingTooltip("Press F to focus the camera on the selected entity");
    }

    // ============================================
    // Rendering
    // ============================================

    void EditorPreferencesWindow::drawRenderingSection()
    {
        ImGui::Text("Display");
        ImGui::Spacing();

        if (ImGui::Checkbox("VSync", &settings.rendering.vsync))
            markDirty();
        drawSettingTooltip("Synchronize frame rate with monitor refresh rate");

        const char* hdrModes[] = {"Disabled", "Enabled"};
        int hdrIdx = (settings.rendering.hdrMode == "Enabled") ? 1 : 0;
        if (ImGui::Combo("HDR Mode", &hdrIdx, hdrModes, 2))
        {
            settings.rendering.hdrMode = hdrModes[hdrIdx];
            markDirty();
        }
        drawSettingTooltip("Enable High Dynamic Range rendering output");

        ImGui::Spacing();
        ImGui::Text("Anti-Aliasing");
        ImGui::Spacing();

        const char* msaaOptions[] = {"1x", "2x", "4x", "8x"};
        int msaaValues[] = {1, 2, 4, 8};
        int msaaIdx = 2;
        for (int i = 0; i < 4; ++i)
        {
            if (msaaValues[i] == settings.rendering.msaaSamples)
                msaaIdx = i;
        }
        if (ImGui::Combo("MSAA", &msaaIdx, msaaOptions, 4))
        {
            settings.rendering.msaaSamples = msaaValues[msaaIdx];
            markDirty();
        }
        drawSettingTooltip("Multisample anti-aliasing sample count\nHigher values reduce jagged edges at cost of performance");

        ImGui::Spacing();
        ImGui::Text("Shadows");
        ImGui::Spacing();

        const char* shadowQualities[] = {"Off", "Low", "Medium", "High", "Ultra"};
        int shadowIdx = 3;
        if (settings.rendering.shadowQuality == "Off") shadowIdx = 0;
        else if (settings.rendering.shadowQuality == "Low") shadowIdx = 1;
        else if (settings.rendering.shadowQuality == "Medium") shadowIdx = 2;
        else if (settings.rendering.shadowQuality == "High") shadowIdx = 3;
        else if (settings.rendering.shadowQuality == "Ultra") shadowIdx = 4;
        if (ImGui::Combo("Shadow Quality", &shadowIdx, shadowQualities, 5))
        {
            settings.rendering.shadowQuality = shadowQualities[shadowIdx];
            markDirty();
        }
        drawSettingTooltip("Shadow map resolution and filtering quality");

        if (ImGui::DragFloat("Shadow Distance", &settings.rendering.shadowDistance, 5.0f, 10.0f, 2000.0f, "%.0f m"))
            markDirty();
        drawSettingTooltip("Maximum distance at which shadows are rendered");

        ImGui::Spacing();
        ImGui::Text("Advanced");
        ImGui::Spacing();

        if (ImGui::Checkbox("Mesh Shaders", &settings.rendering.meshShaders))
            markDirty();
        drawSettingTooltip("Use mesh shader pipeline for geometry processing\nRequires compatible GPU");

        if (ImGui::Checkbox("Bindless Textures", &settings.rendering.bindlessTextures))
            markDirty();
        drawSettingTooltip("Use bindless texture descriptors for improved batching");

        if (ImGui::Checkbox("Ray Tracing", &settings.rendering.rayTracing))
            markDirty();
        drawSettingTooltip("Enable hardware ray tracing for reflections and shadows\nRequires RTX-capable GPU");
    }

    // ============================================
    // Editor
    // ============================================

    void EditorPreferencesWindow::drawEditorSection()
    {
        ImGui::Text("Viewport");
        ImGui::Spacing();

        if (ImGui::DragFloat("Grid Size", &settings.editor.gridSize, 0.1f, 0.1f, 100.0f, "%.1f"))
            markDirty();
        drawSettingTooltip("Size of the viewport grid cells in world units");

        if (ImGui::DragFloat("Gizmo Size", &settings.editor.gizmoSize, 0.1f, 0.1f, 5.0f, "%.1f"))
            markDirty();
        drawSettingTooltip("Scale of the transform gizmo in the viewport");

        ImGui::Spacing();
        ImGui::Text("Snapping");
        ImGui::Spacing();

        if (ImGui::DragFloat("Translate", &settings.editor.snapTranslate, 0.1f, 0.01f, 100.0f, "%.2f"))
            markDirty();
        drawSettingTooltip("Snap increment for position changes in world units");

        if (ImGui::DragFloat("Rotate (degrees)", &settings.editor.snapRotate, 1.0f, 1.0f, 90.0f, "%.0f"))
            markDirty();
        drawSettingTooltip("Snap increment for rotations in degrees");

        if (ImGui::DragFloat("Scale", &settings.editor.snapScale, 0.01f, 0.01f, 10.0f, "%.2f"))
            markDirty();
        drawSettingTooltip("Snap increment for scale changes");

        ImGui::Spacing();
        ImGui::Text("Behavior");
        ImGui::Spacing();

        if (ImGui::Checkbox("Show Grid", &settings.editor.showGrid))
            markDirty();
        drawSettingTooltip("Display the reference grid in the viewport");

        if (ImGui::Checkbox("Highlight Selection", &settings.editor.highlightSelection))
            markDirty();
        drawSettingTooltip("Highlight selected entities with an outline");

        if (ImGui::Checkbox("Enable Gizmos", &settings.editor.enableGizmos))
            markDirty();
        drawSettingTooltip("Show transform gizmos on selected entities");
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
        drawSettingTooltip("Display frames per second counter");

        if (ImGui::Checkbox("Show GPU Time", &settings.debug.showGPUTime))
            markDirty();
        drawSettingTooltip("Display GPU frame time in milliseconds");

        if (ImGui::Checkbox("Show Draw Calls", &settings.debug.showDrawCalls))
            markDirty();
        drawSettingTooltip("Display the number of draw calls per frame");

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
        drawSettingTooltip("Minimum severity level for log output");

        ImGui::Spacing();
        ImGui::Text("Validation");
        ImGui::Spacing();

        if (ImGui::Checkbox("Vulkan Validation Layers", &settings.debug.vulkanValidation))
            markDirty();
        drawSettingTooltip("Enable Vulkan validation layers for API error checking\nMay impact performance");

        if (ImGui::Checkbox("GPU Crash Debugging", &settings.debug.gpuCrashDebugging))
            markDirty();
        drawSettingTooltip("Enable extended GPU crash diagnostics\nSignificant performance impact");

        ImGui::Spacing();
        ImGui::Text("Rendering Debug");
        ImGui::Spacing();

        if (ImGui::Checkbox("Show Shadow Cascades", &settings.debug.showShadowCascades))
            markDirty();
        drawSettingTooltip("Visualize shadow cascade boundaries with colored overlays");

        if (ImGui::Checkbox("Show Overdraw", &settings.debug.showOverdraw))
            markDirty();
        drawSettingTooltip("Visualize pixel overdraw as a heatmap");
    }

    // ============================================
    // Keybindings
    // ============================================

    static std::string getKeyName(int code)
    {
        const char* name = ImGui::GetKeyName(static_cast<ImGuiKey>(code));
        return name ? name : "Unknown";
    }

    static std::string getBindingDisplayName(const services::InputBinding& b)
    {
        std::string result;
        if (b.requireCtrl) result += "Ctrl+";
        if (b.requireShift) result += "Shift+";
        if (b.requireAlt) result += "Alt+";

        if (b.type == services::BindingType::Key)
            result += getKeyName(b.code);
        else
            result += "Mouse " + std::to_string(b.code);

        return result;
    }

    void EditorPreferencesWindow::drawKeybindingsSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto allActions = dispatcher.query(events::editor::GetAllEditorActionsQuery{});

        // Key capture overlay
        if (waitingForKey)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            ImGui::Text("Press a key combination for \"%s\"... (Escape to cancel)", captureAction.c_str());
            ImGui::PopStyleColor();

            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                waitingForKey = false;
                captureAction.clear();
            }
            else
            {
                bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
                bool ctrlHeld = ImGui::IsKeyDown(ImGuiMod_Ctrl);
                bool altHeld = ImGui::IsKeyDown(ImGuiMod_Alt);

                for (int key = ImGuiKey_Space; key < ImGuiKey_COUNT; ++key)
                {
                    if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                        key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
                        key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
                        continue;

                    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false))
                    {
                        services::InputBinding newBinding;
                        newBinding.type = services::BindingType::Key;
                        newBinding.code = key;
                        newBinding.requireShift = shiftHeld;
                        newBinding.requireCtrl = ctrlHeld;
                        newBinding.requireAlt = altHeld;

                        // Check for conflicts before applying
                        events::editor::GetKeybindingConflictsQuery conflictQuery;
                        conflictQuery.actionName = captureAction;
                        conflictQuery.binding = newBinding;
                        auto conflicts = dispatcher.query(conflictQuery);

                        if (conflicts.empty())
                        {
                            events::editor::SetEditorActionBindingsCommand cmd;
                            cmd.actionName = captureAction;
                            cmd.bindings = {newBinding};
                            dispatcher.execute(cmd);
                        }
                        else
                        {
                            pendingBinding = newBinding;
                            pendingAction = captureAction;
                            pendingConflicts = conflicts;
                            showConflictModal = true;
                            ImGui::OpenPopup("Keybinding Conflict");
                        }

                        waitingForKey = false;
                        captureAction.clear();
                        break;
                    }
                }
            }

            ImGui::Separator();
            ImGui::Spacing();
        }

        // Group actions by category
        std::map<std::string, std::vector<services::EditorActionInfo>> grouped;
        for (const auto& action : allActions)
            grouped[action.category].push_back(action);

        for (const auto& [category, actions] : grouped)
        {
            if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(10.0f);
                for (const auto& action : actions)
                {
                    ImGui::PushID(action.name.c_str());

                    bool isConflicting = showConflictModal && std::any_of(
                        pendingConflicts.begin(), pendingConflicts.end(),
                        [&](const auto& c) { return c.conflictingAction == action.name; });

                    if (isConflicting)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

                    ImGui::Text("%s", action.displayName.c_str());
                    ImGui::SameLine(200.0f);

                    if (!action.currentBindings.empty())
                        ImGui::Text("%s", getBindingDisplayName(action.currentBindings[0]).c_str());
                    else
                        ImGui::TextDisabled("None");

                    ImGui::SameLine(350.0f);

                    if (!waitingForKey)
                    {
                        if (ImGui::SmallButton("Rebind"))
                        {
                            waitingForKey = true;
                            captureAction = action.name;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset"))
                        {
                            events::editor::ResetEditorActionBindingsCommand cmd;
                            cmd.actionName = action.name;
                            dispatcher.execute(cmd);
                        }
                    }

                    if (isConflicting)
                        ImGui::PopStyleColor();

                    ImGui::PopID();
                }
                ImGui::Unindent(10.0f);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset All Keybindings"))
        {
            events::editor::ResetEditorActionBindingsCommand cmd;
            dispatcher.execute(cmd);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Keybindings"))
        {
            dispatcher.execute(events::editor::SaveEditorKeybindingsCommand{});
        }

        // Conflict detection modal
        if (showConflictModal)
        {
            ImGui::OpenPopup("Keybinding Conflict");
        }

        if (ImGui::BeginPopupModal("Keybinding Conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("This binding conflicts with:");
            ImGui::Spacing();
            for (const auto& conflict : pendingConflicts)
            {
                ImGui::BulletText("%s", conflict.conflictingAction.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Override will clear the binding from conflicting action(s).");
            ImGui::Spacing();

            if (ImGui::Button("Override", ImVec2(120, 0)))
            {
                for (const auto& conflict : pendingConflicts)
                {
                    events::editor::SetEditorActionBindingsCommand clearCmd;
                    clearCmd.actionName = conflict.conflictingAction;
                    clearCmd.bindings = {};
                    dispatcher.execute(clearCmd);
                }

                events::editor::SetEditorActionBindingsCommand cmd;
                cmd.actionName = pendingAction;
                cmd.bindings = {pendingBinding};
                dispatcher.execute(cmd);

                showConflictModal = false;
                pendingConflicts.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                showConflictModal = false;
                pendingConflicts.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ============================================
    // Settings Registry (for search)
    // ============================================

    void EditorPreferencesWindow::buildSettingsRegistry()
    {
        settingsRegistry.clear();

        // General
        settingsRegistry.push_back({"Project Name", "Project name displayed in the title bar", {"project", "name", "general"}, General,
            [this]() {
                char buf[256];
                std::strncpy(buf, settings.general.projectName.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##ProjectName_s", buf, sizeof(buf))) { settings.general.projectName = buf; markDirty(); }
                drawSettingTooltip("Project name displayed in the title bar");
            }});

        settingsRegistry.push_back({"Default Scene", "Scene to load by default", {"scene", "startup", "general"}, General,
            [this]() {
                char buf[256];
                std::strncpy(buf, settings.general.defaultScene.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##DefaultScene_s", buf, sizeof(buf))) { settings.general.defaultScene = buf; markDirty(); }
                drawSettingTooltip("Scene to load by default when opening the project");
            }});

        settingsRegistry.push_back({"Auto Save", "Automatically save at regular intervals", {"save", "automatic", "interval"}, General,
            [this]() {
                if (ImGui::Checkbox("Auto Save##s", &settings.general.autoSaveEnabled)) markDirty();
                drawSettingTooltip("Automatically save the project at regular intervals");
            }});

        settingsRegistry.push_back({"Auto Save Interval", "Time between automatic saves", {"save", "interval", "minutes"}, General,
            [this]() {
                if (ImGui::DragInt("Interval (min)##s", &settings.general.autoSaveIntervalMinutes, 1, 1, 60)) markDirty();
                drawSettingTooltip("Time between automatic saves in minutes");
            }});

        settingsRegistry.push_back({"Load Last Project", "Load last project on startup", {"startup", "project", "load"}, General,
            [this]() {
                if (ImGui::Checkbox("Load Last Project##s", &settings.general.loadLastProject)) markDirty();
                drawSettingTooltip("Automatically load the last opened project on startup");
            }});

        settingsRegistry.push_back({"Show Splash Screen", "Display splash screen on start", {"startup", "splash"}, General,
            [this]() {
                if (ImGui::Checkbox("Show Splash Screen##s", &settings.general.showSplashScreen)) markDirty();
                drawSettingTooltip("Display splash screen when the editor starts");
            }});

        // Appearance
        settingsRegistry.push_back({"Theme", "Editor color theme", {"theme", "dark", "light", "appearance"}, Appearance,
            [this]() {
                const char* themes[] = {"Dark", "Light"};
                int idx = (settings.appearance.theme == "Light") ? 1 : 0;
                if (ImGui::Combo("Theme##s", &idx, themes, 2)) { settings.appearance.theme = themes[idx]; markDirty(); }
                drawSettingTooltip("Editor color theme");
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

        // Input
        settingsRegistry.push_back({"Camera Speed", "Camera movement speed", {"camera", "speed", "movement"}, Input,
            [this]() {
                if (ImGui::DragFloat("Speed##s", &settings.input.cameraSpeed, 0.1f, 0.1f, 50.0f, "%.1f")) markDirty();
                drawSettingTooltip("Camera movement speed in the viewport");
            }});

        settingsRegistry.push_back({"Camera Sensitivity", "Mouse sensitivity for camera", {"camera", "mouse", "sensitivity"}, Input,
            [this]() {
                if (ImGui::DragFloat("Sensitivity##s", &settings.input.cameraSensitivity, 0.01f, 0.1f, 5.0f, "%.2f")) markDirty();
                drawSettingTooltip("Mouse sensitivity for camera rotation");
            }});

        settingsRegistry.push_back({"Orbit Mode", "Camera orbit control scheme", {"orbit", "camera", "maya", "unreal", "blender"}, Input,
            [this]() {
                const char* modes[] = {"Unreal", "Maya", "Blender"};
                int idx = 0;
                if (settings.input.orbitMode == "Maya") idx = 1;
                else if (settings.input.orbitMode == "Blender") idx = 2;
                if (ImGui::Combo("Orbit Mode##s", &idx, modes, 3)) { settings.input.orbitMode = modes[idx]; markDirty(); }
                drawSettingTooltip("Camera orbit control scheme");
            }});

        settingsRegistry.push_back({"Invert Y Axis", "Invert vertical mouse movement", {"invert", "mouse", "axis"}, Input,
            [this]() {
                if (ImGui::Checkbox("Invert Y Axis##s", &settings.input.invertYAxis)) markDirty();
                drawSettingTooltip("Invert vertical mouse movement for camera control");
            }});

        settingsRegistry.push_back({"Smooth Camera", "Smooth camera interpolation", {"smooth", "camera", "interpolation"}, Input,
            [this]() {
                if (ImGui::Checkbox("Smooth Camera##s", &settings.input.smoothCamera)) markDirty();
                drawSettingTooltip("Enable smooth interpolation for camera movement");
            }});

        settingsRegistry.push_back({"Focus on Selection", "Focus camera on selected entity", {"focus", "selection", "camera"}, Input,
            [this]() {
                if (ImGui::Checkbox("Focus on Selection##s", &settings.input.focusOnSelection)) markDirty();
                drawSettingTooltip("Press F to focus the camera on the selected entity");
            }});

        // Rendering
        settingsRegistry.push_back({"VSync", "Sync frame rate with monitor", {"vsync", "sync", "framerate"}, Rendering,
            [this]() {
                if (ImGui::Checkbox("VSync##s", &settings.rendering.vsync)) markDirty();
                drawSettingTooltip("Synchronize frame rate with monitor refresh rate");
            }});

        settingsRegistry.push_back({"HDR Mode", "High Dynamic Range rendering", {"hdr", "rendering", "display"}, Rendering,
            [this]() {
                const char* modes[] = {"Disabled", "Enabled"};
                int idx = (settings.rendering.hdrMode == "Enabled") ? 1 : 0;
                if (ImGui::Combo("HDR Mode##s", &idx, modes, 2)) { settings.rendering.hdrMode = modes[idx]; markDirty(); }
                drawSettingTooltip("Enable High Dynamic Range rendering output");
            }});

        settingsRegistry.push_back({"MSAA", "Multisample anti-aliasing", {"msaa", "antialiasing", "samples"}, Rendering,
            [this]() {
                const char* opts[] = {"1x", "2x", "4x", "8x"};
                int vals[] = {1, 2, 4, 8};
                int idx = 2;
                for (int i = 0; i < 4; ++i) { if (vals[i] == settings.rendering.msaaSamples) idx = i; }
                if (ImGui::Combo("MSAA##s", &idx, opts, 4)) { settings.rendering.msaaSamples = vals[idx]; markDirty(); }
                drawSettingTooltip("Multisample anti-aliasing sample count");
            }});

        settingsRegistry.push_back({"Shadow Quality", "Shadow map resolution", {"shadow", "quality", "rendering"}, Rendering,
            [this]() {
                const char* quals[] = {"Off", "Low", "Medium", "High", "Ultra"};
                int idx = 3;
                if (settings.rendering.shadowQuality == "Off") idx = 0;
                else if (settings.rendering.shadowQuality == "Low") idx = 1;
                else if (settings.rendering.shadowQuality == "Medium") idx = 2;
                else if (settings.rendering.shadowQuality == "High") idx = 3;
                else if (settings.rendering.shadowQuality == "Ultra") idx = 4;
                if (ImGui::Combo("Shadow Quality##s", &idx, quals, 5)) { settings.rendering.shadowQuality = quals[idx]; markDirty(); }
                drawSettingTooltip("Shadow map resolution and filtering quality");
            }});

        settingsRegistry.push_back({"Shadow Distance", "Max shadow render distance", {"shadow", "distance", "rendering"}, Rendering,
            [this]() {
                if (ImGui::DragFloat("Shadow Distance##s", &settings.rendering.shadowDistance, 5.0f, 10.0f, 2000.0f, "%.0f m")) markDirty();
                drawSettingTooltip("Maximum distance at which shadows are rendered");
            }});

        settingsRegistry.push_back({"Mesh Shaders", "Use mesh shader pipeline", {"mesh", "shader", "gpu"}, Rendering,
            [this]() {
                if (ImGui::Checkbox("Mesh Shaders##s", &settings.rendering.meshShaders)) markDirty();
                drawSettingTooltip("Use mesh shader pipeline for geometry processing");
            }});

        settingsRegistry.push_back({"Bindless Textures", "Use bindless texture descriptors", {"bindless", "texture", "gpu"}, Rendering,
            [this]() {
                if (ImGui::Checkbox("Bindless Textures##s", &settings.rendering.bindlessTextures)) markDirty();
                drawSettingTooltip("Use bindless texture descriptors for improved batching");
            }});

        settingsRegistry.push_back({"Ray Tracing", "Hardware ray tracing", {"raytracing", "rtx", "gpu"}, Rendering,
            [this]() {
                if (ImGui::Checkbox("Ray Tracing##s", &settings.rendering.rayTracing)) markDirty();
                drawSettingTooltip("Enable hardware ray tracing\nRequires RTX-capable GPU");
            }});

        // Editor
        settingsRegistry.push_back({"Grid Size", "Viewport grid cell size", {"grid", "size", "viewport"}, Editor,
            [this]() {
                if (ImGui::DragFloat("Grid Size##s", &settings.editor.gridSize, 0.1f, 0.1f, 100.0f, "%.1f")) markDirty();
                drawSettingTooltip("Size of the viewport grid cells in world units");
            }});

        settingsRegistry.push_back({"Gizmo Size", "Transform gizmo scale", {"gizmo", "size", "viewport"}, Editor,
            [this]() {
                if (ImGui::DragFloat("Gizmo Size##s", &settings.editor.gizmoSize, 0.1f, 0.1f, 5.0f, "%.1f")) markDirty();
                drawSettingTooltip("Scale of the transform gizmo in the viewport");
            }});

        settingsRegistry.push_back({"Snap Translate", "Position snap increment", {"snap", "translate", "position"}, Editor,
            [this]() {
                if (ImGui::DragFloat("Translate##s", &settings.editor.snapTranslate, 0.1f, 0.01f, 100.0f, "%.2f")) markDirty();
                drawSettingTooltip("Snap increment for position changes in world units");
            }});

        settingsRegistry.push_back({"Snap Rotate", "Rotation snap increment", {"snap", "rotate", "degrees"}, Editor,
            [this]() {
                if (ImGui::DragFloat("Rotate (deg)##s", &settings.editor.snapRotate, 1.0f, 1.0f, 90.0f, "%.0f")) markDirty();
                drawSettingTooltip("Snap increment for rotations in degrees");
            }});

        settingsRegistry.push_back({"Snap Scale", "Scale snap increment", {"snap", "scale"}, Editor,
            [this]() {
                if (ImGui::DragFloat("Scale##s", &settings.editor.snapScale, 0.01f, 0.01f, 10.0f, "%.2f")) markDirty();
                drawSettingTooltip("Snap increment for scale changes");
            }});

        settingsRegistry.push_back({"Show Grid", "Display viewport reference grid", {"grid", "show", "viewport"}, Editor,
            [this]() {
                if (ImGui::Checkbox("Show Grid##s", &settings.editor.showGrid)) markDirty();
                drawSettingTooltip("Display the reference grid in the viewport");
            }});

        settingsRegistry.push_back({"Highlight Selection", "Outline selected entities", {"highlight", "selection", "outline"}, Editor,
            [this]() {
                if (ImGui::Checkbox("Highlight Selection##s", &settings.editor.highlightSelection)) markDirty();
                drawSettingTooltip("Highlight selected entities with an outline");
            }});

        settingsRegistry.push_back({"Enable Gizmos", "Show transform gizmos", {"gizmo", "transform", "enable"}, Editor,
            [this]() {
                if (ImGui::Checkbox("Enable Gizmos##s", &settings.editor.enableGizmos)) markDirty();
                drawSettingTooltip("Show transform gizmos on selected entities");
            }});

        // Debug
        settingsRegistry.push_back({"Show FPS", "Frames per second counter", {"fps", "performance", "stats"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show FPS##s", &settings.debug.showFPS)) markDirty();
                drawSettingTooltip("Display frames per second counter");
            }});

        settingsRegistry.push_back({"Show GPU Time", "GPU frame time display", {"gpu", "time", "performance"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show GPU Time##s", &settings.debug.showGPUTime)) markDirty();
                drawSettingTooltip("Display GPU frame time in milliseconds");
            }});

        settingsRegistry.push_back({"Show Draw Calls", "Draw call counter", {"draw", "calls", "performance"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show Draw Calls##s", &settings.debug.showDrawCalls)) markDirty();
                drawSettingTooltip("Display the number of draw calls per frame");
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

        settingsRegistry.push_back({"Vulkan Validation", "API error checking layers", {"vulkan", "validation", "debug"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Vulkan Validation##s", &settings.debug.vulkanValidation)) markDirty();
                drawSettingTooltip("Enable Vulkan validation layers for API error checking");
            }});

        settingsRegistry.push_back({"GPU Crash Debugging", "Extended crash diagnostics", {"gpu", "crash", "debug"}, Debug,
            [this]() {
                if (ImGui::Checkbox("GPU Crash Debugging##s", &settings.debug.gpuCrashDebugging)) markDirty();
                drawSettingTooltip("Enable extended GPU crash diagnostics");
            }});

        settingsRegistry.push_back({"Show Shadow Cascades", "Shadow cascade debug view", {"shadow", "cascade", "debug"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Shadow Cascades##s", &settings.debug.showShadowCascades)) markDirty();
                drawSettingTooltip("Visualize shadow cascade boundaries with colored overlays");
            }});

        settingsRegistry.push_back({"Show Overdraw", "Overdraw heatmap visualization", {"overdraw", "heatmap", "debug"}, Debug,
            [this]() {
                if (ImGui::Checkbox("Show Overdraw##s", &settings.debug.showOverdraw)) markDirty();
                drawSettingTooltip("Visualize pixel overdraw as a heatmap");
            }});
    }
}
