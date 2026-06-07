#pragma once

#include "FogOfWar.hpp"
#include "imguiHandler/ImguiWindow.hpp"
#include <imgui.h>
#include <memory>

// Editor control panel — registered via PluginContext::registerEditorWindow,
// appears under the editor's Plugins menu. draw() runs inside the engine's
// ImGui frame; ImGui::SetCurrentContext was pointed at the engine context
// during plugin init.
class FogOfWarWindow : public controllers::imguiHandler::ImguiWindow
{
private:
    std::shared_ptr<FogSettings> settings;

public:
    explicit FogOfWarWindow(std::shared_ptr<FogSettings> settings)
        : settings(std::move(settings))
    {
    }

    void draw() override
    {
        if (!ImGui::Begin("Fog of War"))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Checkbox("Enabled (F10)", &settings->enabled))
            settings->paramsDirty = true;

        ImGui::SeparatorText("Look");
        if (ImGui::SliderFloat("Terrain Dim", &settings->terrainDimMin, 0.0f, 1.0f, "%.2f"))
            settings->paramsDirty = true;
        ImGui::SetItemTooltip("Albedo multiplier for unexplored terrain (0 = black, 1 = no dimming)");
        ImGui::SliderFloat("Explored Brightness", &settings->exploredBrightness, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Mask value for explored-but-not-visible areas (between unexplored = 0\n"
                              "and visible = 1). Keep below the hide threshold so enemies stay hidden\n"
                              "in areas you have scouted and left.");
        if (ImGui::SliderFloat("Entity Hide Threshold", &settings->entityDiscardBelow, 0.0f, 1.0f, "%.2f"))
            settings->paramsDirty = true;
        ImGui::SetItemTooltip("Entities are hidden where the visibility mask is below this value");
        if (ImGui::Button("Reset Explored"))
            settings->resetExploredRequested = true;
        ImGui::SetItemTooltip("Forget all explored areas (back to unexplored black)");
        ImGui::SameLine();
        if (ImGui::Button("Save Settings"))
            settings->saveRequested = true;
        ImGui::SetItemTooltip("Persist these settings to plugins/data/RTSGameplay/config.json\n"
                              "(also saved automatically when the editor closes)");

        ImGui::SeparatorText("Stats");
        ImGui::Text("Vision sources: %d", settings->visionSources);
        ImGui::Text("Grid update: %.3f ms (budget 0.5)", settings->gridUpdateMs);
        if (settings->bound)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Mask: bound");
        else
            ImGui::TextDisabled("Mask: inactive (add Vision components to player entities)");

        ImGui::End();
    }
};
