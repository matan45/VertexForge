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

        // Billboard palette
        drawBillboardPalette();

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

        // Wind & SSS settings
        ensureConfigLoaded();
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

        ImGui::End();

        if (!visible)
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void GrassDensityPanel::drawBillboardPalette()
    {
        ImGui::Text("Billboard Palette");
        ImGui::Separator();

        // Paint entry selector
        {
            const char* preview = (selectedBillboardIndex < 0) ? "All (Random)" : "---";
            if (selectedBillboardIndex >= 0 && selectedBillboardIndex < static_cast<int>(billboardEntries.size()))
            {
                auto& e = billboardEntries[selectedBillboardIndex];
                preview = e.texturePath.empty() ? "(empty)" : e.texturePath.c_str();
            }

            if (ImGui::BeginCombo("Paint Entry", preview))
            {
                if (ImGui::Selectable("All (Random)", selectedBillboardIndex == -1))
                {
                    selectedBillboardIndex = -1;
                    pushBillboardPalette();
                }

                for (int i = 0; i < static_cast<int>(billboardEntries.size()); ++i)
                {
                    auto& e = billboardEntries[i];
                    std::string label = e.texturePath.empty()
                        ? std::string("(empty) ##") + std::to_string(i)
                        : std::filesystem::path(e.texturePath).filename().string() + "##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selectedBillboardIndex == i))
                    {
                        selectedBillboardIndex = i;
                        pushBillboardPalette();
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Draw each entry
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(billboardEntries.size()); ++i)
        {
            drawBillboardEntry(i, removeIndex);
        }

        if (removeIndex >= 0)
        {
            billboardEntries.erase(billboardEntries.begin() + removeIndex);
            if (selectedBillboardIndex >= static_cast<int>(billboardEntries.size()))
                selectedBillboardIndex = -1;
            pushBillboardPalette();
        }

        if (ImGui::Button("Add Billboard Entry"))
        {
            billboardEntries.emplace_back();
        }
    }

    void GrassDensityPanel::drawBillboardEntry(int index, int& removeIndex)
    {
        ImGui::PushID(index);
        auto& entry = billboardEntries[index];

        bool isSelected = (selectedBillboardIndex == index);
        ImVec4 headerColor = isSelected
            ? ImVec4(0.2f, 0.4f, 0.8f, 1.0f)
            : ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
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
            if (ImGui::DragFloat("Density", &entry.densityMultiplier, 0.1f, 0.1f, 20.0f))
                pushBillboardPalette();

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
        cmd.activeEntry = selectedBillboardIndex;
        events::EventDispatcher::instance().execute(cmd);
    }
}
