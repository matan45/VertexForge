#include "GrassDensityPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/vegetation/GrassEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <filesystem>
#include <cmath>

namespace
{
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
}

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
                if (visible)
                    configLoaded = false; // Force reload palette when panel opens
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

        drawBillboardPalette();
        ImGui::Separator();
        drawBrushControls();
        ImGui::Separator();
        ensureConfigLoaded();
        drawWindControls();
        drawSSSControls();
        ImGui::End();

        if (!visible)
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void GrassDensityPanel::drawBrushControls()
    {
        const char* brushTypes[] = {"Paint", "Erase"};
        if (ImGui::Combo("Brush Type", &selectedBrushType, brushTypes, IM_ARRAYSIZE(brushTypes)))
        {
            events::vegetationBrush::SetVegetationBrushTypeCommand cmd;
            cmd.type = static_cast<vegetation::VegetationBrushType>(selectedBrushType);
            events::EventDispatcher::instance().execute(cmd);
        }

        bool paramsChanged = false;
        paramsChanged |= ImGui::SliderFloat("Radius", &brushParams.radius, 0.1f, 100.0f);
        paramsChanged |= ImGui::SliderFloat("Spacing", &brushParams.spacing, 0.1f, 5.0f);
        paramsChanged |= ImGui::SliderFloat("Density", &brushParams.density, 0.1f, 10.0f);
        paramsChanged |= ImGui::SliderFloat("Jitter", &brushParams.positionJitter, 0.0f, 1.0f);

        int falloffIdx = static_cast<int>(brushParams.falloff);
        const char* falloffNames[] = {"Constant", "Linear", "Smooth", "Sharp"};
        if (ImGui::Combo("Falloff", &falloffIdx, falloffNames, IM_ARRAYSIZE(falloffNames)))
        {
            brushParams.falloff = static_cast<terrain::BrushFalloff>(falloffIdx);
            paramsChanged = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Density falloff toward the brush edge");

        int modeIdx = static_cast<int>(brushParams.placementMode);
        const char* modeNames[] = {"Spray", "Single"};
        if (ImGui::Combo("Placement", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames)))
        {
            brushParams.placementMode = static_cast<vegetation::VegetationPlacementMode>(modeIdx);
            paramsChanged = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spray = scatter many; Single = one hero instance per click");

        if (brushParams.placementMode == vegetation::VegetationPlacementMode::Spray)
        {
            paramsChanged |= ImGui::SliderFloat("Flow (sprays/s)", &brushParams.flowRate, 0.0f, 30.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = paint on cursor movement; >0 = airbrush (paints while held)");
        }

        drawPlacementMaskControls(paramsChanged);

        if (paramsChanged)
            pushBrushParams();
    }

    void GrassDensityPanel::drawPlacementMaskControls(bool& paramsChanged)
    {
        if (!ImGui::CollapsingHeader("Placement Mask"))
            return;

        // Slope mask
        if (ImGui::Checkbox("Slope Mask", &brushParams.useSlopeMask))
            paramsChanged = true;
        if (brushParams.useSlopeMask)
        {
            bool slopeChanged = false;
            slopeChanged |= ImGui::SliderFloat("Min Slope (deg)", &slopeMinDeg, 0.0f, 90.0f);
            slopeChanged |= ImGui::SliderFloat("Max Slope (deg)", &slopeMaxDeg, 0.0f, 90.0f);
            if (slopeChanged)
            {
                if (slopeMaxDeg < slopeMinDeg) slopeMaxDeg = slopeMinDeg;
                // Steeper slope -> smaller normal.y. Reject outside [cos(max), cos(min)].
                brushParams.slopeMinCos = std::cos(slopeMaxDeg * kDegToRad);
                brushParams.slopeMaxCos = std::cos(slopeMinDeg * kDegToRad);
                paramsChanged = true;
            }
        }

        if (ImGui::Checkbox("Align To Normal", &brushParams.alignToNormal))
            paramsChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tilt instances to follow the terrain surface normal");

        // Height mask
        if (ImGui::Checkbox("Height Mask", &brushParams.useHeightMask))
            paramsChanged = true;
        if (brushParams.useHeightMask)
        {
            paramsChanged |= ImGui::DragFloat("Min Height", &brushParams.heightMin, 0.5f);
            paramsChanged |= ImGui::DragFloat("Max Height", &brushParams.heightMax, 0.5f);
        }

        // Noise / scatter mask
        if (ImGui::Checkbox("Noise Mask", &brushParams.useNoiseMask))
            paramsChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Clump placement using world-space noise");
        if (brushParams.useNoiseMask)
        {
            paramsChanged |= ImGui::SliderFloat("Noise Freq", &brushParams.noiseFrequency, 0.01f, 1.0f, "%.3f");
            paramsChanged |= ImGui::SliderFloat("Noise Threshold", &brushParams.noiseThreshold, 0.0f, 1.0f);
            int seed = static_cast<int>(brushParams.noiseSeed);
            if (ImGui::DragInt("Noise Seed", &seed, 1.0f, 0, 1000000))
            {
                brushParams.noiseSeed = static_cast<uint32_t>(seed < 0 ? 0 : seed);
                paramsChanged = true;
            }
        }

        // Layer-aware avoidance
        if (ImGui::Checkbox("Avoid Other Layers", &brushParams.avoidOtherLayers))
            paramsChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keep this layer away from other palette layers");
        if (brushParams.avoidOtherLayers)
            paramsChanged |= ImGui::SliderFloat("Avoid Radius", &brushParams.layerAvoidRadius, 0.1f, 5.0f);
    }

    void GrassDensityPanel::pushBrushParams()
    {
        events::vegetationBrush::SetVegetationBrushParamsCommand cmd;
        cmd.params = brushParams;
        events::EventDispatcher::instance().execute(cmd);
    }

    void GrassDensityPanel::drawWindControls()
    {
        bool configChanged = false;
        if (ImGui::CollapsingHeader("Wind"))
        {
            configChanged |= ImGui::DragFloat3("Direction", &grassConfig.windDirection.x, 0.01f, -1.0f, 1.0f);
            configChanged |= ImGui::DragFloat("Speed", &grassConfig.windSpeed, 0.1f, 0.0f, 20.0f);
            configChanged |= ImGui::DragFloat("Strength", &grassConfig.windStrength, 0.1f, 0.0f, 10.0f);
            configChanged |= ImGui::DragFloat("Gust Strength", &grassConfig.gustStrength, 0.01f, 0.0f, 1.0f);
            configChanged |= ImGui::DragFloat("Gust Frequency", &grassConfig.gustFrequency, 0.1f, 0.0f, 5.0f);
        }

        if (ImGui::CollapsingHeader("Density Fadeout"))
        {
            configChanged |= ImGui::DragFloat("Fade Start", &grassConfig.fadeStartDistance, 1.0f, 1.0f, 500.0f);
            configChanged |= ImGui::DragFloat("Fade End", &grassConfig.fadeEndDistance, 1.0f, 1.0f, 500.0f);

            configChanged |= ImGui::SliderFloat("Fade Start Factor", &grassConfig.densityFadeStartFactor, 0.1f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Density fade starts at Fade Start * this factor");

            configChanged |= ImGui::SliderFloat("Min Density Scale", &grassConfig.minDensityScale, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Minimum density at max distance (0=none, 1=full)");

            bool lodIntegration = grassConfig.terrainLODIntegration;
            if (ImGui::Checkbox("Terrain LOD Integration", &lodIntegration))
            {
                grassConfig.terrainLODIntegration = lodIntegration;
                configChanged = true;
            }
        }

        if (configChanged)
            pushGrassConfig();
    }

    void GrassDensityPanel::drawSSSControls()
    {
        bool configChanged = false;
        if (ImGui::CollapsingHeader("Subsurface Scattering"))
        {
            configChanged |= ImGui::SliderFloat("SSS Distortion", &grassConfig.sssDistortion, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Normal distortion for translucency (0=pure backlit, 1=normal-dependent)");

            configChanged |= ImGui::SliderFloat("SSS Power", &grassConfig.sssPower, 1.0f, 16.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Falloff exponent for translucency highlight (lower=broader)");

            configChanged |= ImGui::SliderFloat("SSS Scale", &grassConfig.sssScale, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Overall translucency intensity (0=disabled)");
        }

        if (configChanged)
            pushGrassConfig();
    }

    void GrassDensityPanel::drawBillboardPalette()
    {
        ImGui::Text("Billboard Palette");
        ImGui::Separator();

        // Draw each entry
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(billboardEntries.size()); ++i)
        {
            drawBillboardEntry(i, removeIndex);
        }

        if (removeIndex >= 0)
        {
            billboardEntries.erase(billboardEntries.begin() + removeIndex);
            pushBillboardPalette();
        }

        if (ImGui::Button("Add Billboard Entry"))
        {
            billboardEntries.emplace_back();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All"))
        {
            events::vegetation::ClearAllBillboardInstancesCommand clearCmd;
            events::EventDispatcher::instance().execute(clearCmd);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove all placed billboard instances from terrain");
    }

    void GrassDensityPanel::drawBillboardEntry(int index, int& removeIndex)
    {
        ImGui::PushID(index);
        auto& entry = billboardEntries[index];

        // Visibility toggle (eye)
        if (ImGui::Checkbox("##visible", &entry.visible))
            pushBillboardPalette();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show/hide");
        ImGui::SameLine();

        // Paint enable checkbox
        if (ImGui::Checkbox("##paint", &entry.paintEnabled))
        {
            pushBillboardPalette();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include in paint brush");
        ImGui::SameLine();

        ImVec4 headerColor = entry.visible
            ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f)
            : ImVec4(0.4f, 0.4f, 0.4f, 1.0f); // Dimmed when inactive
        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(headerColor.x + 0.1f, headerColor.y + 0.1f, headerColor.z + 0.1f, 1.0f));

        std::string label = entry.texturePath.empty()
            ? "Entry " + std::to_string(index)
            : std::filesystem::path(entry.texturePath).filename().string();

        if (ImGui::TreeNode("Entry", "%s", label.c_str()))
        {
            // Texture path with browse
            std::string displayPath = entry.texturePath.empty() ? "(none)" : entry.texturePath;
            ImGui::Text("Texture: %s", displayPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse"))
            {
                nfd::FileDialog fileDialog;
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"VF Image", L"*.vfImage"}
                };
                std::string selectedPath = fileDialog.openFileDialog(filters);
                if (!selectedPath.empty())
                {
                    entry.texturePath = selectedPath;
                    pushBillboardPalette();
                }
            }

            int modeIdx = static_cast<int>(entry.mode);
            const char* modeNames[] = {"Cross (X)", "Camera Facing"};
            if (ImGui::Combo("Mode", &modeIdx, modeNames, IM_ARRAYSIZE(modeNames)))
            {
                entry.mode = static_cast<vegetation::BillboardMode>(modeIdx);
                pushBillboardPalette();
            }

            if (ImGui::DragFloat("Weight", &entry.weight, 0.1f, 0.01f, 100.0f))
                pushBillboardPalette();
            if (ImGui::DragFloat2("Scale Range", &entry.scaleRange.x, 0.01f, 0.1f, 10.0f))
                pushBillboardPalette();
            if (ImGui::DragFloat2("Height Range", &entry.heightRange.x, 0.01f, 0.1f, 5.0f))
                pushBillboardPalette();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Per-instance random height multiplier (min/max)");
            if (ImGui::SliderFloat("Tint Jitter", &entry.tintJitter, 0.0f, 1.0f))
                pushBillboardPalette();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Per-instance brightness variation");
            if (ImGui::Button("Remove"))
                removeIndex = index;

            ImGui::TreePop();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    void GrassDensityPanel::ensureConfigLoaded()
    {
        if (!configLoaded)
        {
            grassConfig = events::EventDispatcher::instance().query(
                events::vegetation::GetGlobalGrassConfigQuery{});

            try {
                billboardEntries = events::EventDispatcher::instance().query(
                    events::vegetation::GetBillboardPaletteQuery{});
            } catch (...) {}

            configLoaded = true;
        }
    }

    void GrassDensityPanel::pushGrassConfig()
    {
        ensureConfigLoaded();
        events::vegetation::SetGlobalGrassConfigCommand cmd;
        cmd.config = grassConfig;
        events::EventDispatcher::instance().execute(cmd);
    }

    void GrassDensityPanel::pushBillboardPalette()
    {
        events::vegetation::SetBillboardPaletteCommand cmd;
        cmd.entries = billboardEntries;
        cmd.activeEntry = -1; // All active entries render (controlled by per-entry checkbox)
        events::EventDispatcher::instance().execute(cmd);
    }
}
