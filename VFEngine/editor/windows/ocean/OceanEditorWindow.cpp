#include "OceanEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/OceanEvents.hpp"
#include <imgui.h>

namespace windows
{
    void OceanEditorWindow::show()
    {
        visible = true;
        refreshOceanState();
    }

    void OceanEditorWindow::refreshOceanState()
    {
        hasOcean = false;
        oceanEntity = {};

        auto& dispatcher = events::EventDispatcher::instance();

        events::ocean::GetOceanEntityQuery entityQuery;
        oceanEntity = dispatcher.query(entityQuery);

        if (!oceanEntity.isValid())
            return;

        hasOcean = true;

        events::ocean::GetOceanDataQuery dataQuery;
        dataQuery.entity = oceanEntity;
        auto dataOpt = dispatcher.query(dataQuery);

        if (dataOpt.has_value())
        {
            visualSettings.shallowColor = dataOpt->shallowColor;
            visualSettings.deepColor = dataOpt->deepColor;
            visualSettings.maxVisibleDepth = dataOpt->maxVisibleDepth;
            visualSettings.fresnelPower = dataOpt->fresnelPower;
            visualSettings.refractionStrength = dataOpt->refractionStrength;
            visualSettings.refractionChromatic = dataOpt->refractionChromatic;
            visualSettings.refractionDepthScale = dataOpt->refractionDepthScale;
            visualSettings.causticStrength = dataOpt->causticStrength;
            visualSettings.causticDepthFalloff = dataOpt->causticDepthFalloff;
            visualSettingsDirty = false;

            physicsSettings.physicsEnabled = dataOpt->physicsEnabled;
            physicsSettingsDirty = false;
        }

        oceanConfig = dispatcher.query(events::ocean::GetOceanFFTConfigQuery{});
        oceanConfigDirty = false;
    }

    void OceanEditorWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Ocean Editor", &visible))
        {
            if (!hasOcean)
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
                if (ImGui::Button("Delete Ocean", ImVec2(-1, 0)))
                {
                    deleteOcean();
                }
                ImGui::PopStyleColor(3);
            }
        }
        ImGui::End();
    }

    void OceanEditorWindow::drawCreationSection()
    {
        ImGui::Text("Create Ocean");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Water Height");
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##WaterHeight", &waterHeight, 0.1f, -1000.0f, 1000.0f, "%.2f");
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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.2f, 0.5f, 1.0f));
        if (ImGui::Button("Create Ocean", ImVec2(-1, 30)))
        {
            createOcean();
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Or Load Existing");
        if (ImGui::Button("Load Ocean", ImVec2(-1, 30)))
        {
            loadOcean();
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

    void OceanEditorWindow::drawSettingsSection()
    {
        bool anyDirty = false;

        if (ImGui::CollapsingHeader("Visual Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Text("Shallow Color");
            visualSettingsDirty |= ImGui::ColorEdit4("##SettingsShallowColor", &visualSettings.shallowColor.x);
            ImGui::Text("Deep Color");
            visualSettingsDirty |= ImGui::ColorEdit4("##SettingsDeepColor", &visualSettings.deepColor.x);
            visualSettingsDirty |= labeledDragFloat("Max Visible Depth", "##MaxVisibleDepth", &visualSettings.maxVisibleDepth, 0.1f, 0.1f, 100.0f, "%.1f");
            visualSettingsDirty |= labeledDragFloat("Fresnel Power", "##FresnelPower", &visualSettings.fresnelPower, 0.1f, 0.1f, 20.0f, "%.1f");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            physicsSettingsDirty |= ImGui::Checkbox("Enabled", &physicsSettings.physicsEnabled);
            physicsSettingsDirty |= labeledDragFloat("Density (kg/m3)", "##Density", &physicsSettings.density, 1.0f, 1.0f, 10000.0f, "%.0f");
            physicsSettingsDirty |= labeledDragFloat("Drag", "##Drag", &physicsSettings.drag, 0.01f, 0.0f, 10.0f, "%.2f");
            physicsSettingsDirty |= labeledDragFloat("Buoyancy Strength", "##BuoyancyStrength", &physicsSettings.buoyancyStrength, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            visualSettingsDirty |= labeledDragFloat("Strength", "##RefrStrength",
                &visualSettings.refractionStrength, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("0 = disabled, 0.5 = subtle, 1.0+ = strong");

            visualSettingsDirty |= labeledDragFloat("Chromatic Aberration", "##RefrChromatic",
                &visualSettings.refractionChromatic, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("0 = off, higher = more color fringing");

            visualSettingsDirty |= labeledDragFloat("Depth Scale", "##RefrDepthScale",
                &visualSettings.refractionDepthScale, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("How much depth increases distortion");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Caustics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            visualSettingsDirty |= labeledDragFloat("Strength", "##CausticStrength",
                &visualSettings.causticStrength, 0.01f, 0.0f, 3.0f, "%.2f");
            ImGui::TextDisabled("0 = disabled, 1.0 = default, higher = brighter");

            visualSettingsDirty |= labeledDragFloat("Depth Falloff", "##CausticDepthFalloff",
                &visualSettings.causticDepthFalloff, 0.01f, 0.1f, 2.0f, "%.2f");
            ImGui::TextDisabled("How quickly caustics fade with depth");
            ImGui::Unindent();
        }

        anyDirty = visualSettingsDirty || physicsSettingsDirty;

        ImGui::Spacing();
        if (ImGui::Button("Apply", ImVec2(100, 0)))
        {
            applyVisualSettings();
            applyPhysicsSettings();
        }
        if (anyDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0)))
            refreshOceanState();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Save Ocean", ImVec2(-1, 0)))
            saveOcean();
    }

    void OceanEditorWindow::drawOceanFFTSection()
    {
        if (!ImGui::CollapsingHeader("Ocean FFT", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();

        ImGui::Text("Resolution");
        ImGui::PushItemWidth(-1);
        static const uint32_t resolutions[] = {64, 128, 256, 512};
        int resIndex = 2;
        for (int i = 0; i < 4; ++i)
        {
            if (resolutions[i] == oceanConfig.resolution)
            {
                resIndex = i;
                break;
            }
        }
        if (ImGui::Combo("##OceanResolution", &resIndex, "64\0" "128\0" "256\0" "512\0"))
        {
            oceanConfig.resolution = resolutions[resIndex];
            oceanConfigDirty = true;
        }
        ImGui::PopItemWidth();

        oceanConfigDirty |= labeledDragFloat("Patch Size (world units)", "##OceanPatchSize", &oceanConfig.patchSize, 1.0f, 10.0f, 2000.0f, "%.0f");
        oceanConfigDirty |= labeledDragFloat("Amplitude", "##OceanAmplitude", &oceanConfig.amplitude, 0.000001f, 0.00001f, 0.001f, "%.6f");
        ImGui::Spacing();
        oceanConfigDirty |= labeledDragFloat("Wind Speed (m/s)", "##OceanWindSpeed", &oceanConfig.windSpeed, 0.01f, 0.1f, 100.0f, "%.2f");
        ImGui::Text("Wind Direction");
        ImGui::PushItemWidth(-1);
        oceanConfigDirty |= ImGui::SliderFloat("##OceanWindDir", &oceanConfig.windDirection, 0.0f, 360.0f, "%.0f deg");
        ImGui::PopItemWidth();
        ImGui::Spacing();
        oceanConfigDirty |= labeledDragFloat("Choppiness", "##OceanChoppiness", &oceanConfig.choppiness, 0.01f, 0.0f, 5.0f, "%.2f");
        oceanConfigDirty |= labeledDragFloat("Foam Threshold", "##OceanFoamThreshold", &oceanConfig.foamThreshold, 0.01f, -1.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("Lower = more foam");
        oceanConfigDirty |= labeledDragFloat("Wave Height Scale", "##OceanDisplacementScale", &oceanConfig.displacementScale, 0.1f, 0.1f, 50.0f, "%.1f");
        ImGui::TextDisabled("Multiplier for wave height (1.0 = default)");
        ImGui::Spacing();
        if (ImGui::Button("Apply Ocean", ImVec2(100, 0)))
            applyOceanConfig();
        if (oceanConfigDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::Unindent();
    }

    void OceanEditorWindow::drawInfoSection()
    {
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            events::ocean::GetOceanDataQuery query;
            query.entity = oceanEntity;
            auto data = dispatcher.query(query);

            if (data.has_value())
            {
                ImGui::Text("Water Height: %.2f", data->waterHeight);
                ImGui::Text("Physics: %s", data->physicsEnabled ? "Enabled" : "Disabled");
            }
            else
            {
                ImGui::TextDisabled("No ocean data available");
            }

            ImGui::Unindent();
        }
    }

    void OceanEditorWindow::createOcean()
    {
        services::OceanCreationData config;
        config.waterHeight = waterHeight;
        config.physicsEnabled = physicsEnabled;
        config.shallowColor = glm::vec4(shallowColor[0], shallowColor[1], shallowColor[2], shallowColor[3]);
        config.deepColor = glm::vec4(deepColor[0], deepColor[1], deepColor[2], deepColor[3]);
        config.oceanConfig = services::OceanFFTConfigData{};
        config.oceanConfig.enabled = true;

        events::ocean::CreateOceanCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);

        refreshOceanState();
    }

    void OceanEditorWindow::deleteOcean()
    {
        if (!hasOcean)
            return;

        events::ocean::DeleteOceanCommand cmd;
        cmd.oceanEntity = oceanEntity;
        events::EventDispatcher::instance().execute(cmd);

        hasOcean = false;
        oceanEntity = {};
    }

    void OceanEditorWindow::applyOceanConfig()
    {
        events::ocean::SetOceanFFTConfigCommand cmd;
        cmd.config = oceanConfig;
        events::EventDispatcher::instance().execute(cmd);
        oceanConfigDirty = false;
    }

    void OceanEditorWindow::applyVisualSettings()
    {
        if (!hasOcean || !visualSettingsDirty)
            return;

        events::ocean::SetOceanVisualSettingsCommand cmd;
        cmd.oceanEntity = oceanEntity;
        cmd.settings = visualSettings;
        events::EventDispatcher::instance().execute(cmd);
        visualSettingsDirty = false;
    }

    void OceanEditorWindow::applyPhysicsSettings()
    {
        if (!hasOcean || !physicsSettingsDirty)
            return;

        events::ocean::SetOceanPhysicsSettingsCommand cmd;
        cmd.oceanEntity = oceanEntity;
        cmd.settings = physicsSettings;
        events::EventDispatcher::instance().execute(cmd);
        physicsSettingsDirty = false;
    }

    void OceanEditorWindow::saveOcean()
    {
        if (!hasOcean)
            return;

        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Ocean (*.vfOcean)", L"*.vfOcean"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfOcean");
        if (!path.empty())
        {
            events::ocean::SaveOceanCommand cmd;
            cmd.oceanEntity = oceanEntity;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void OceanEditorWindow::loadOcean()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Ocean (*.vfOcean)", L"*.vfOcean"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            events::ocean::LoadOceanCommand cmd;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);

            refreshOceanState();
        }
    }
}
