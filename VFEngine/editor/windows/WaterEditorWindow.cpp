#include "WaterEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/WaterEvents.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>

namespace windows
{
    void WaterEditorWindow::show()
    {
        visible = true;
        refreshWaterState();
    }

    void WaterEditorWindow::refreshWaterState()
    {
        hasWater = false;
        waterEntity = {};

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WaterComponent>();
        for (auto entity : view)
        {
            waterEntity.id = static_cast<uint64_t>(entity);
            hasWater = true;

            // Load global settings from current water
            auto& comp = view.get<components::WaterComponent>(entity);
            globalSettings.density = comp.globalDensity;
            globalSettings.drag = comp.globalDrag;
            globalSettings.buoyancyStrength = comp.globalBuoyancyStrength;
            globalSettings.waveSpeed = comp.waveSpeed;
            globalSettings.waveAmplitude = comp.waveAmplitude;
            globalSettings.waveFrequency = comp.waveFrequency;
            globalSettings.shallowColor = comp.shallowColor;
            globalSettings.deepColor = comp.deepColor;
            globalSettings.maxVisibleDepth = comp.maxVisibleDepth;
            globalSettings.fresnelPower = comp.fresnelPower;
            globalSettings.dudvTiling = comp.dudvTiling;
            globalSettings.dudvStrength = comp.dudvStrength;
            settingsDirty = false;
            break;
        }
    }

    void WaterEditorWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Water Editor", &visible))
        {
            if (!hasWater)
            {
                drawCreationSection();
            }
            else
            {
                drawSettingsSection();
                ImGui::Spacing();
                drawInfoSection();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button("Delete Water", ImVec2(-1, 0)))
                {
                    deleteWater();
                }
                ImGui::PopStyleColor(3);
            }
        }
        ImGui::End();
    }

    void WaterEditorWindow::drawCreationSection()
    {
        ImGui::Text("Create Water Body");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Grid Size");
        ImGui::PushItemWidth(100);
        ImGui::DragInt("##TilesX", &tilesX, 0.1f, 1, 16, "X: %d");
        ImGui::SameLine();
        ImGui::DragInt("##TilesZ", &tilesZ, 0.1f, 1, 16, "Z: %d");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Tile Size (world units)");
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##WorldTileSize", &worldTileSize, 0.5f, 4.0f, 256.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Water Height");
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##WaterHeight", &waterHeight, 0.1f, -1000.0f, 1000.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Wave Intensity");
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##WaveIntensity", &waveIntensity, 0.01f, 0.0f, 10.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Shallow Color");
        ImGui::ColorEdit4("##ShallowColor", shallowColor);

        ImGui::Text("Deep Color");
        ImGui::ColorEdit4("##DeepColor", deepColor);

        ImGui::Spacing();

        ImGui::Checkbox("Physics Enabled", &physicsEnabled);
        ImGui::TextDisabled("Creates sensor bodies for water detection");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Create Water", ImVec2(-1, 30)))
        {
            createWater();
        }

        ImGui::Spacing();

        if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
        {
            resetCreationDefaults();
        }
    }

    void WaterEditorWindow::drawSettingsSection()
    {
        if (ImGui::CollapsingHeader("Wave Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Wave Speed");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##WaveSpeed", &globalSettings.waveSpeed, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Wave Amplitude");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##WaveAmplitude", &globalSettings.waveAmplitude, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Wave Frequency");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##WaveFrequency", &globalSettings.waveFrequency, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();

            ImGui::Text("DuDv Tiling");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##DuDvTiling", &globalSettings.dudvTiling, 0.1f, 0.5f, 20.0f, "%.1f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("DuDv Strength");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##DuDvStrength", &globalSettings.dudvStrength, 0.001f, 0.0f, 0.1f, "%.3f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Visual Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Shallow Color");
            if (ImGui::ColorEdit4("##SettingsShallowColor", &globalSettings.shallowColor.x))
            {
                settingsDirty = true;
            }

            ImGui::Text("Deep Color");
            if (ImGui::ColorEdit4("##SettingsDeepColor", &globalSettings.deepColor.x))
            {
                settingsDirty = true;
            }

            ImGui::Text("Max Visible Depth");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##MaxVisibleDepth", &globalSettings.maxVisibleDepth, 0.1f, 0.1f, 100.0f, "%.1f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Fresnel Power");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##FresnelPower", &globalSettings.fresnelPower, 0.1f, 0.1f, 20.0f, "%.1f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Density (kg/m3)");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##Density", &globalSettings.density, 1.0f, 1.0f, 10000.0f, "%.0f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Drag");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##Drag", &globalSettings.drag, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Buoyancy Strength");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##BuoyancyStrength", &globalSettings.buoyancyStrength, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                settingsDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }

        ImGui::Spacing();

        if (ImGui::Button("Apply", ImVec2(100, 0)))
        {
            applySettings();
        }

        if (settingsDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0)))
        {
            refreshWaterState();
        }
    }

    void WaterEditorWindow::drawInfoSection()
    {
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            events::water::GetWaterDataQuery query;
            query.entity = waterEntity;
            auto data = dispatcher.query(query);

            if (data.has_value())
            {
                ImGui::Text("Tile Size: %.1f", data->worldTileSize);
                ImGui::Text("Grid: [%d,%d] to [%d,%d]", data->gridMinX, data->gridMinZ, data->gridMaxX, data->gridMaxZ);
                ImGui::Text("Total Tiles: %u", data->tileCount);
                ImGui::Text("Active Tiles: %u", data->activeTileCount);
                ImGui::Text("Water Height: %.2f", data->defaultWaterHeight);
                ImGui::Text("Physics: %s", data->physicsEnabled ? "Enabled" : "Disabled");
            }
            else
            {
                ImGui::TextDisabled("No water data available");
            }

            ImGui::Unindent();
        }
    }

    void WaterEditorWindow::createWater()
    {
        services::WaterCreationData config;
        config.tilesX = tilesX;
        config.tilesZ = tilesZ;
        config.worldTileSize = worldTileSize;
        config.waterHeight = waterHeight;
        config.waveIntensity = waveIntensity;
        config.physicsEnabled = physicsEnabled;
        config.shallowColor = glm::vec4(shallowColor[0], shallowColor[1], shallowColor[2], shallowColor[3]);
        config.deepColor = glm::vec4(deepColor[0], deepColor[1], deepColor[2], deepColor[3]);

        events::water::CreateWaterCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);

        refreshWaterState();
    }

    void WaterEditorWindow::deleteWater()
    {
        if (!hasWater)
            return;

        events::water::DeleteWaterCommand cmd;
        cmd.waterEntity = waterEntity;
        events::EventDispatcher::instance().execute(cmd);

        hasWater = false;
        waterEntity = {};
    }

    void WaterEditorWindow::applySettings()
    {
        if (!hasWater)
            return;

        events::water::SetWaterGlobalSettingsCommand cmd;
        cmd.waterEntity = waterEntity;
        cmd.settings = globalSettings;
        events::EventDispatcher::instance().execute(cmd);

        settingsDirty = false;
    }

    void WaterEditorWindow::resetCreationDefaults()
    {
        tilesX = 4;
        tilesZ = 4;
        worldTileSize = 32.0f;
        waterHeight = 0.0f;
        waveIntensity = 1.0f;
        physicsEnabled = true;
        shallowColor[0] = 0.0f; shallowColor[1] = 0.5f; shallowColor[2] = 0.7f; shallowColor[3] = 0.6f;
        deepColor[0] = 0.0f; deepColor[1] = 0.1f; deepColor[2] = 0.3f; deepColor[3] = 0.9f;
    }
}
