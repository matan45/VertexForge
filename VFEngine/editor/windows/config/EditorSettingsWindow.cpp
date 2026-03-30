#include "EditorSettingsWindow.hpp"
#include "ProjectSettingsWindow.hpp"
#include "EditorCameraWindow.hpp"
#include "PhysicsConfigWindow.hpp"
#include "AudioConfigWindow.hpp"
#include "../audio/AudioMixerWindow.hpp"
#include "RenderConfigWindow.hpp"
#include "InputActionMappingWindow.hpp"
#include "../plugin/PluginManagerWindow.hpp"
#include <imgui.h>
#include <IconsFontAwesome6.h>

namespace windows
{
    void EditorSettingsWindow::setWindows(ProjectSettingsWindow* project, EditorCameraWindow* camera,
                                          PhysicsConfigWindow* physics, AudioConfigWindow* audio,
                                          AudioMixerWindow* mixer, RenderConfigWindow* render,
                                          InputActionMappingWindow* input, PluginManagerWindow* plugins)
    {
        projectSettings = project;
        editorCameraWindow = camera;
        physicsConfig = physics;
        audioConfig = audio;
        audioMixer = mixer;
        renderConfig = render;
        inputMapping = input;
        pluginManager = plugins;
    }

    void EditorSettingsWindow::show(int category)
    {
        visible = true;
        if (category >= 0 && category < COUNT)
        {
            selectedCategory = category;
        }
    }

    void EditorSettingsWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Settings", &visible, ImGuiWindowFlags_NoCollapse))
        {
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            float categoryWidth = 160.0f;

            ImGui::BeginChild("CategoryList", ImVec2(categoryWidth, contentSize.y), true);
            drawCategoryList();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("SettingsContent", ImVec2(0, contentSize.y), true);
            drawCategoryContent();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void EditorSettingsWindow::drawCategoryList()
    {
        struct CategoryEntry
        {
            const char* icon;
            const char* label;
        };

        static const CategoryEntry categories[] = {
            { ICON_FA_FOLDER_OPEN,   "Project" },
            { ICON_FA_VIDEO,         "Editor Camera" },
            { ICON_FA_ATOM,          "Physics" },
            { ICON_FA_VOLUME_HIGH,   "Audio" },
            { ICON_FA_SLIDERS,       "Audio Mixer" },
            { ICON_FA_DISPLAY,       "Rendering" },
            { ICON_FA_GAMEPAD,       "Input Mapping" },
            { ICON_FA_PUZZLE_PIECE,  "Plugins" },
        };

        for (int i = 0; i < COUNT; ++i)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s  %s", categories[i].icon, categories[i].label);

            if (ImGui::Selectable(label, selectedCategory == i, 0, ImVec2(0, 24)))
            {
                selectedCategory = i;
            }
        }
    }

    void EditorSettingsWindow::drawCategoryContent()
    {
        switch (selectedCategory)
        {
        case Project:
            if (projectSettings) projectSettings->drawContent();
            break;
        case EditorCamera:
            if (editorCameraWindow) editorCameraWindow->drawContent();
            break;
        case Physics:
            if (physicsConfig) physicsConfig->drawContent();
            break;
        case AudioConfig:
            if (audioConfig) audioConfig->drawContent();
            break;
        case AudioMixer:
            if (audioMixer) audioMixer->drawContent();
            break;
        case Rendering:
            if (renderConfig) renderConfig->drawContent();
            break;
        case InputMapping:
            if (inputMapping) inputMapping->drawContent();
            break;
        case Plugins:
            if (pluginManager) pluginManager->drawContent();
            break;
        default:
            ImGui::TextDisabled("Select a category");
            break;
        }
    }
}
