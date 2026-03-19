#include "WaterEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/WaterEvents.hpp"
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

        auto& dispatcher = events::EventDispatcher::instance();

        events::water::GetWaterEntityQuery entityQuery;
        waterEntity = dispatcher.query(entityQuery);

        if (!waterEntity.isValid())
            return;

        hasWater = true;

        events::water::GetWaterGlobalSettingsQuery settingsQuery;
        settingsQuery.entity = waterEntity;
        globalSettings = dispatcher.query(settingsQuery);
        settingsDirty = false;

        oceanConfig = dispatcher.query(events::water::GetOceanFFTConfigQuery{});
        oceanConfigDirty = false;
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
                drawOceanFFTSection();
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

        ImGui::Text("Grid Size (Tiles):");
        ImGui::SliderInt("Tiles X", &tilesX, 1, 100);
        ImGui::SliderInt("Tiles Z", &tilesZ, 1, 100);

        int totalTiles = tilesX * tilesZ;
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Total tiles: %d", totalTiles);

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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.2f, 0.5f, 1.0f));
        if (ImGui::Button("Create Ocean", ImVec2(-1, 30)))
        {
            createOcean();
        }
        ImGui::PopStyleColor(3);
        ImGui::TextDisabled("Creates large water grid with ocean waves enabled");

        ImGui::Spacing();

        if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
        {
            resetCreationDefaults();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Or Load Existing");
        if (ImGui::Button("Load Water", ImVec2(-1, 30)))
        {
            loadWater();
        }
    }

    static bool labeledDragFloat(const char* label, const char* id, float* v, float speed, float mn, float mx, const char* fmt)
    {
        ImGui::Text("%s", label);
        ImGui::PushItemWidth(-1);
        bool changed = ImGui::DragFloat(id, v, speed, mn, mx, fmt);
        ImGui::PopItemWidth();
        return changed;
    }

    void WaterEditorWindow::drawSettingsSection()
    {
        if (!oceanConfig.enabled) {
            if (ImGui::CollapsingHeader("Wave Settings (Gerstner)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                settingsDirty |= labeledDragFloat("Wave Speed", "##WaveSpeed", &globalSettings.waveSpeed, 0.01f, 0.0f, 10.0f, "%.2f");
                settingsDirty |= labeledDragFloat("Wave Amplitude", "##WaveAmplitude", &globalSettings.waveAmplitude, 0.01f, 0.0f, 10.0f, "%.2f");
                settingsDirty |= labeledDragFloat("Wave Frequency", "##WaveFrequency", &globalSettings.waveFrequency, 0.01f, 0.0f, 10.0f, "%.2f");
                ImGui::Text("Wave Direction"); ImGui::PushItemWidth(-1);
                settingsDirty |= ImGui::SliderFloat("##WaveDirection", &globalSettings.waveDirectionDegrees, 0.0f, 360.0f, "%.0f deg");
                ImGui::PopItemWidth(); ImGui::Spacing();
                settingsDirty |= labeledDragFloat("DuDv Tiling", "##DuDvTiling", &globalSettings.dudvTiling, 0.1f, 0.5f, 20.0f, "%.1f");
                settingsDirty |= labeledDragFloat("DuDv Strength", "##DuDvStrength", &globalSettings.dudvStrength, 0.001f, 0.0f, 0.1f, "%.3f");
                ImGui::Unindent();
            }
        }

        if (ImGui::CollapsingHeader("Visual Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("Shallow Color");
            settingsDirty |= ImGui::ColorEdit4("##SettingsShallowColor", &globalSettings.shallowColor.x);
            ImGui::Text("Deep Color");
            settingsDirty |= ImGui::ColorEdit4("##SettingsDeepColor", &globalSettings.deepColor.x);
            settingsDirty |= labeledDragFloat("Max Visible Depth", "##MaxVisibleDepth", &globalSettings.maxVisibleDepth, 0.1f, 0.1f, 100.0f, "%.1f");
            settingsDirty |= labeledDragFloat("Fresnel Power", "##FresnelPower", &globalSettings.fresnelPower, 0.1f, 0.1f, 20.0f, "%.1f");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            settingsDirty |= labeledDragFloat("Density (kg/m3)", "##Density", &globalSettings.density, 1.0f, 1.0f, 10000.0f, "%.0f");
            settingsDirty |= labeledDragFloat("Drag", "##Drag", &globalSettings.drag, 0.01f, 0.0f, 10.0f, "%.2f");
            settingsDirty |= labeledDragFloat("Buoyancy Strength", "##BuoyancyStrength", &globalSettings.buoyancyStrength, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::Unindent();
        }

        ImGui::Spacing();
        if (ImGui::Button("Apply", ImVec2(100, 0))) applySettings();
        if (settingsDirty) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)"); }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0))) refreshWaterState();

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Save Water", ImVec2(-1, 0))) saveWater();
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

    void WaterEditorWindow::drawOceanFFTSection()
    {
        if (!ImGui::CollapsingHeader("Ocean")) return;
        ImGui::Indent();

        auto& dispatcher = events::EventDispatcher::instance();
        if (ImGui::Checkbox("Enable Ocean", &oceanConfig.enabled)) {
            events::water::SetOceanFFTEnabledCommand cmd;
            cmd.enabled = oceanConfig.enabled;
            dispatcher.execute(cmd);
        }
        ImGui::TextDisabled("Replaces Gerstner waves with ocean simulation");

        if (oceanConfig.enabled) {
            ImGui::Spacing();
            ImGui::Text("Resolution"); ImGui::PushItemWidth(-1);
            static const uint32_t resolutions[] = {64, 128, 256, 512};
            int resIndex = 2;
            for (int i = 0; i < 4; ++i) { if (resolutions[i] == oceanConfig.resolution) { resIndex = i; break; } }
            if (ImGui::Combo("##OceanResolution", &resIndex, "64\0" "128\0" "256\0" "512\0"))
                { oceanConfig.resolution = resolutions[resIndex]; oceanConfigDirty = true; }
            ImGui::PopItemWidth();

            oceanConfigDirty |= labeledDragFloat("Patch Size (world units)", "##OceanPatchSize", &oceanConfig.patchSize, 1.0f, 10.0f, 2000.0f, "%.0f");
            oceanConfigDirty |= labeledDragFloat("Amplitude", "##OceanAmplitude", &oceanConfig.amplitude, 0.000001f, 0.00001f, 0.001f, "%.6f");
            ImGui::Spacing();
            oceanConfigDirty |= labeledDragFloat("Wind Speed (m/s)", "##OceanWindSpeed", &oceanConfig.windSpeed, 0.01f, 0.1f, 100.0f, "%.2f");
            ImGui::Text("Wind Direction"); ImGui::PushItemWidth(-1);
            oceanConfigDirty |= ImGui::SliderFloat("##OceanWindDir", &oceanConfig.windDirection, 0.0f, 360.0f, "%.0f deg");
            ImGui::PopItemWidth(); ImGui::Spacing();
            oceanConfigDirty |= labeledDragFloat("Choppiness", "##OceanChoppiness", &oceanConfig.choppiness, 0.01f, 0.0f, 5.0f, "%.2f");
            oceanConfigDirty |= labeledDragFloat("Foam Threshold", "##OceanFoamThreshold", &oceanConfig.foamThreshold, 0.01f, -1.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("Lower = more foam");
            oceanConfigDirty |= labeledDragFloat("Wave Height Scale", "##OceanDisplacementScale", &oceanConfig.displacementScale, 0.1f, 0.1f, 50.0f, "%.1f");
            ImGui::TextDisabled("Multiplier for wave height (1.0 = default)");
            ImGui::Spacing();
            if (ImGui::Button("Apply Ocean", ImVec2(100, 0))) applyOceanConfig();
            if (oceanConfigDirty) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)"); }
        }
        ImGui::Unindent();
    }

    void WaterEditorWindow::applyOceanConfig()
    {
        events::water::SetOceanFFTConfigCommand cmd;
        cmd.config = oceanConfig;
        events::EventDispatcher::instance().execute(cmd);
        oceanConfigDirty = false;
    }

    void WaterEditorWindow::createOcean()
    {
        services::WaterCreationData config;
        config.tilesX = 10;
        config.tilesZ = 10;
        config.worldTileSize = 100.0f;
        config.waterHeight = waterHeight;
        config.waveIntensity = 1.0f;
        config.physicsEnabled = physicsEnabled;
        config.shallowColor = glm::vec4(0.0f, 0.4f, 0.6f, 0.7f);
        config.deepColor = glm::vec4(0.0f, 0.05f, 0.2f, 0.95f);

        events::water::CreateWaterCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);

        refreshWaterState();

        oceanConfig = services::OceanFFTConfigData{};
        oceanConfig.enabled = true;

        auto& dispatcher = events::EventDispatcher::instance();

        events::water::SetOceanFFTEnabledCommand enableCmd;
        enableCmd.enabled = true;
        dispatcher.execute(enableCmd);

        events::water::SetOceanFFTConfigCommand cfgCmd;
        cfgCmd.config = oceanConfig;
        dispatcher.execute(cfgCmd);

        oceanConfigDirty = false;
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

    void WaterEditorWindow::saveWater()
    {
        if (!hasWater)
            return;

        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Water (*.vfWater)", L"*.vfWater"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfWater");
        if (!path.empty())
        {
            events::water::SaveWaterCommand cmd;
            cmd.waterEntity = waterEntity;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void WaterEditorWindow::loadWater()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Water (*.vfWater)", L"*.vfWater"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            events::water::LoadWaterCommand cmd;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);

            refreshWaterState();
        }
    }
}
