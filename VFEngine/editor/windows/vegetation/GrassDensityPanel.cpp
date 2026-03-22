#include "GrassDensityPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/vegetation/GrassEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <filesystem>

namespace windows
{
    GrassDensityPanel::~GrassDensityPanel()
    {
        if (subscribed)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.unsubscribe(modeToken);
        }
    }

    void GrassDensityPanel::subscribe()
    {
        if (subscribed) return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const auto& n)
            {
                visible = n.isActive;
            });

        subscribed = true;
    }

    void GrassDensityPanel::draw()
    {
        if (!subscribed) subscribe();
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Vegetation Brush", &visible))
        {
            ImGui::End();
            return;
        }

        // Vegetation type palette
        drawVegetationTypePalette();

        ImGui::Separator();

        const char* brushTypes[] = {"Paint", "Erase", "Smooth", "Fill"};
        if (ImGui::Combo("Brush Type", &selectedBrushType, brushTypes, IM_ARRAYSIZE(brushTypes)))
        {
            events::vegetationBrush::SetDensityBrushTypeCommand cmd;
            cmd.type = static_cast<vegetation::DensityBrushType>(selectedBrushType);
            events::EventDispatcher::instance().execute(cmd);
        }

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f))
        {
            vegetation::DensityBrushParams params;
            params.radius = brushRadius;
            params.strength = brushStrength;
            params.opacity = brushOpacity;
            params.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            params.shape = static_cast<terrain::BrushShape>(shapeIndex);

            events::vegetationBrush::SetDensityBrushParamsCommand cmd;
            cmd.params = params;
            events::EventDispatcher::instance().execute(cmd);
        }

        ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f);
        ImGui::SliderFloat("Opacity", &brushOpacity, 0.0f, 1.0f);

        const char* falloffTypes[] = {"Constant", "Linear", "Smooth", "Sharp"};
        ImGui::Combo("Falloff", &falloffIndex, falloffTypes, IM_ARRAYSIZE(falloffTypes));

        ImGui::Separator();
        drawGrassConfigSection();

        ImGui::End();

        if (!visible)
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void GrassDensityPanel::drawVegetationTypePalette()
    {
        ImGui::Text("Vegetation Type");

        const char* typeNames[] = {"Grass", "Billboard"};
        const ImVec4 typeColors[] = {
            {0.2f, 0.6f, 0.1f, 1.0f},   // Grass: green
            {0.3f, 0.6f, 0.8f, 1.0f}    // Billboard: blue
        };

        for (int i = 0; i < static_cast<int>(vegetation::VEGETATION_TYPE_COUNT); ++i)
        {
            if (i > 0) ImGui::SameLine();

            bool isSelected = (selectedVegetationType == i);
            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, typeColors[i]);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, typeColors[i]);
            }

            if (ImGui::Button(typeNames[i], ImVec2(65, 25)))
            {
                selectedVegetationType = i;
                pushVegetationType();
            }

            if (isSelected)
            {
                ImGui::PopStyleColor(2);
            }
        }

        // Mixed mode
        if (ImGui::Checkbox("Mixed Mode", &mixedModeEnabled))
        {
            pushMixedBrushConfig();
        }

        if (mixedModeEnabled)
        {
            bool ratioChanged = false;
            for (int i = 0; i < static_cast<int>(vegetation::VEGETATION_TYPE_COUNT); ++i)
            {
                char label[32];
                snprintf(label, sizeof(label), "%s Ratio", typeNames[i]);
                ratioChanged |= ImGui::SliderFloat(label, &mixedRatios[i], 0.0f, 1.0f, "%.2f");
            }

            if (ratioChanged)
            {
                pushMixedBrushConfig();
            }
        }

        // Billboard texture selection
        if (selectedVegetationType == static_cast<int>(vegetation::VegetationType::Billboard))
        {
            ImGui::Spacing();
            ImGui::Text("Billboard Texture");
            ImGui::InputText("##billboardTex", billboardTexturePath, sizeof(billboardTexturePath),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##billboard"))
            {
                nfd::FileDialog fileDialog;
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"VF Image", L"*.vfImage"}
                };
                std::string selectedPath = fileDialog.openFileDialog(filters);
                if (!selectedPath.empty())
                {
                    // Make path relative to project
                    namespace fs = std::filesystem;
                    fs::path absPath(selectedPath);
                    std::string relPath = absPath.filename().string();
                    snprintf(billboardTexturePath, sizeof(billboardTexturePath), "%s", selectedPath.c_str());
                }
            }
        }
    }

    void GrassDensityPanel::pushVegetationType()
    {
        events::vegetationBrush::SetActiveVegetationTypeCommand cmd;
        cmd.type = static_cast<vegetation::VegetationType>(selectedVegetationType);
        events::EventDispatcher::instance().execute(cmd);
    }

    void GrassDensityPanel::pushMixedBrushConfig()
    {
        vegetation::MixedBrushConfig config;
        config.enabled = mixedModeEnabled;
        for (int i = 0; i < static_cast<int>(vegetation::VEGETATION_TYPE_COUNT); ++i)
            config.ratios[i] = mixedRatios[i];
        config.normalize();

        // Sync back normalized ratios
        for (int i = 0; i < static_cast<int>(vegetation::VEGETATION_TYPE_COUNT); ++i)
            mixedRatios[i] = config.ratios[i];

        events::vegetationBrush::SetMixedBrushConfigCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);
    }

    void GrassDensityPanel::drawGrassConfigSection()
    {
        if (!configLoaded)
        {
            grassConfig = events::EventDispatcher::instance().query(
                events::vegetation::GetGlobalGrassConfigQuery{});
            configLoaded = true;
        }

        if (!ImGui::CollapsingHeader("Grass Appearance", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        bool changed = false;

        changed |= ImGui::DragFloat("Height Min", &grassConfig.heightMin, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("Height Max", &grassConfig.heightMax, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("Width Min", &grassConfig.widthMin, 0.005f, 0.005f, 2.0f);
        changed |= ImGui::DragFloat("Width Max", &grassConfig.widthMax, 0.005f, 0.005f, 2.0f);

        ImGui::Spacing();
        changed |= ImGui::ColorEdit4("Base Color", &grassConfig.baseColor.x);
        changed |= ImGui::ColorEdit4("Tip Color", &grassConfig.tipColor.x);

        ImGui::Spacing();
        changed |= ImGui::DragFloat("Slope Limit", &grassConfig.slopeLimit, 0.01f, 0.0f, 1.0f,
                                     "%.2f", ImGuiSliderFlags_None);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum surface normal.y for grass placement (0=vertical, 1=flat)");
        changed |= ImGui::DragFloat("Density Multiplier", &grassConfig.densityMultiplier, 0.1f, 0.1f, 10.0f);

        ImGui::Spacing();
        changed |= ImGui::DragFloat("Fade Start", &grassConfig.fadeStartDistance, 1.0f, 1.0f, 500.0f);
        changed |= ImGui::DragFloat("Fade End", &grassConfig.fadeEndDistance, 1.0f, 1.0f, 500.0f);

        if (ImGui::CollapsingHeader("Wind"))
        {
            changed |= ImGui::DragFloat3("Direction", &grassConfig.windDirection.x, 0.01f, -1.0f, 1.0f);
            changed |= ImGui::DragFloat("Speed", &grassConfig.windSpeed, 0.1f, 0.0f, 20.0f);
            changed |= ImGui::DragFloat("Strength", &grassConfig.windStrength, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat("Gust Strength", &grassConfig.gustStrength, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Gust Frequency", &grassConfig.gustFrequency, 0.1f, 0.0f, 5.0f);
        }

        if (ImGui::CollapsingHeader("Subsurface Scattering"))
        {
            changed |= ImGui::SliderFloat("SSS Distortion", &grassConfig.sssDistortion, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Normal distortion for translucency (0=pure backlit, 1=normal-dependent)");

            changed |= ImGui::SliderFloat("SSS Power", &grassConfig.sssPower, 1.0f, 16.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Falloff exponent for translucency highlight (lower=broader)");

            changed |= ImGui::SliderFloat("SSS Scale", &grassConfig.sssScale, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Overall translucency intensity (0=disabled)");
        }

        if (ImGui::CollapsingHeader("Density Fadeout"))
        {
            changed |= ImGui::SliderFloat("Fade Start Factor", &grassConfig.densityFadeStartFactor, 0.1f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Density fade starts at Fade Start Distance * this factor");

            changed |= ImGui::SliderFloat("Min Density Scale", &grassConfig.minDensityScale, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Minimum density fraction at max distance (0=none, 1=full)");

            bool lodIntegration = grassConfig.terrainLODIntegration;
            if (ImGui::Checkbox("Terrain LOD Integration", &lodIntegration))
            {
                grassConfig.terrainLODIntegration = lodIntegration;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scale vegetation density with terrain LOD level");
        }

        if (changed)
        {
            pushGrassConfig();
        }
    }

    void GrassDensityPanel::pushGrassConfig()
    {
        events::vegetation::SetGlobalGrassConfigCommand cmd;
        cmd.config = grassConfig;
        events::EventDispatcher::instance().execute(cmd);
    }
}
