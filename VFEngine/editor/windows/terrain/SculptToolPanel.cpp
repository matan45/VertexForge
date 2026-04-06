#include "SculptToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/BrushEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>

namespace windows
{
    SculptToolPanel::~SculptToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (sculptModeToken.isValid())
        {
            dispatcher.unsubscribe(sculptModeToken);
        }
        if (brushTypeToken.isValid())
        {
            dispatcher.unsubscribe(brushTypeToken);
        }
        if (brushParamsToken.isValid())
        {
            dispatcher.unsubscribe(brushParamsToken);
        }
        if (stampImageToken.isValid())
        {
            dispatcher.unsubscribe(stampImageToken);
        }
    }

    void SculptToolPanel::subscribe()
    {
        if (subscribed)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    // Sync with current state
                    auto& d = events::EventDispatcher::instance();
                    auto params = d.query(events::brush::GetBrushParamsQuery{});
                    brushRadius = params.radius;
                    brushStrength = params.strength;
                    falloffIndex = static_cast<int>(params.falloff);
                    shapeIndex = static_cast<int>(params.shape);
                    stampRotation = params.stampRotation;
                    stampScale = params.stampScale;
                    talusAngle = params.talusAngle;
                    terraceStepHeight = params.terraceStepHeight;
                    terraceSharpness = params.terraceSharpness;
                    rampWidth = params.rampWidth;
                    rampFalloff = params.rampFalloff;

                    auto type = d.query(events::brush::GetBrushTypeQuery{});
                    selectedBrushType = static_cast<int>(type);
                }
            });

        brushTypeToken = dispatcher.subscribe<events::brush::BrushTypeChangedNotification>(
            [this](const events::brush::BrushTypeChangedNotification& n)
            {
                selectedBrushType = static_cast<int>(n.type);
            });

        brushParamsToken = dispatcher.subscribe<events::brush::BrushParamsChangedNotification>(
            [this](const events::brush::BrushParamsChangedNotification& n)
            {
                brushRadius = n.params.radius;
                brushStrength = n.params.strength;
                falloffIndex = static_cast<int>(n.params.falloff);
                shapeIndex = static_cast<int>(n.params.shape);
                stampRotation = n.params.stampRotation;
                stampScale = n.params.stampScale;
                talusAngle = n.params.talusAngle;
                terraceStepHeight = n.params.terraceStepHeight;
                terraceSharpness = n.params.terraceSharpness;
                rampWidth = n.params.rampWidth;
                rampFalloff = n.params.rampFalloff;
            });

        stampImageToken = dispatcher.subscribe<events::brush::StampImageChangedNotification>(
            [this](const events::brush::StampImageChangedNotification& n)
            {
                stampImagePath = n.filePath;
                stampLoaded = n.loaded;
            });

        subscribed = true;
    }

    void SculptToolPanel::draw()
    {
        subscribe();

        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Sculpt Tools", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Brush Type");
        ImGui::Separator();

        const char* brushLabels[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Stamp", "Erosion", "Terrace", "Ramp"};
        bool typeChanged = false;

        for (int i = 0; i < 9; ++i)
        {
            if (ImGui::RadioButton(brushLabels[i], &selectedBrushType, i))
            {
                typeChanged = true;
            }
            if (i < 8)
            {
                ImGui::SameLine();
            }
        }

        if (typeChanged)
        {
            events::brush::SetBrushTypeCommand cmd;
            cmd.type = static_cast<terrain::BrushType>(selectedBrushType);
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Text("Brush Parameters");
        ImGui::Separator();

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f, "%.1f"))
        {
            events::brush::SetBrushRadiusCommand cmd;
            cmd.radius = brushRadius;
            dispatcher.execute(cmd);
        }

        if (ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f, "%.2f"))
        {
            events::brush::SetBrushStrengthCommand cmd;
            cmd.strength = brushStrength;
            dispatcher.execute(cmd);
        }

        const char* falloffLabels[] = {"Constant", "Linear", "Smooth", "Sharp"};
        if (ImGui::Combo("Falloff", &falloffIndex, falloffLabels, 4))
        {
            events::brush::SetBrushFalloffCommand cmd;
            cmd.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
            dispatcher.execute(cmd);
        }

        if (selectedBrushType != static_cast<int>(terrain::BrushType::Stamp))
        {
            const char* shapeLabels[] = {"Circle", "Square"};
            if (ImGui::Combo("Shape", &shapeIndex, shapeLabels, 2))
            {
                events::brush::SetBrushShapeCommand cmd;
                cmd.shape = static_cast<terrain::BrushShape>(shapeIndex);
                dispatcher.execute(cmd);
            }
        }

        // Stamp brush controls
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Stamp))
        {
            ImGui::Spacing();
            ImGui::Text("Stamp Image");
            ImGui::Separator();

            if (stampLoaded)
            {
                std::string filename = std::filesystem::path(stampImagePath).filename().string();
                ImGui::TextWrapped("Loaded: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No stamp loaded");
            }

            if (stampLoaded)
            {
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                {
                    events::brush::ClearStampImageCommand cmd;
                    dispatcher.execute(cmd);
                }
            }

            if (ImGui::Button("Load Stamp Image"))
            {
                nfd::FileDialog fileDialog;
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"Heightmap Image", L"*.vfImage"}
                };
                std::string selectedPath = fileDialog.openFileDialog(filters);
                if (!selectedPath.empty())
                {
                    selectedPath.erase(
                        std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                        selectedPath.end());

                    events::brush::SetStampImageCommand cmd;
                    cmd.filePath = selectedPath;
                    dispatcher.execute(cmd);
                }
            }

            const char* modeLabels[] = {"Add", "Subtract"};
            if (ImGui::Combo("Mode", &stampMode, modeLabels, 2))
            {
                events::brush::SetStampModeCommand cmd;
                cmd.subtract = (stampMode == 1);
                dispatcher.execute(cmd);
            }

            float rotationDeg = glm::degrees(stampRotation);
            if (ImGui::SliderFloat("Rotation", &rotationDeg, 0.0f, 360.0f, "%.1f deg"))
            {
                stampRotation = glm::radians(rotationDeg);
                events::brush::SetStampRotationCommand cmd;
                cmd.rotation = stampRotation;
                dispatcher.execute(cmd);
            }

            if (ImGui::SliderFloat("Scale", &stampScale, 0.1f, 50.0f, "%.1f"))
            {
                events::brush::SetStampScaleCommand cmd;
                cmd.scale = stampScale;
                dispatcher.execute(cmd);
            }
        }

        // Erosion brush controls
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Erosion))
        {
            ImGui::Spacing();
            ImGui::Text("Erosion Settings");
            ImGui::Separator();

            if (ImGui::SliderFloat("Talus Angle", &talusAngle, 5.0f, 85.0f, "%.1f deg"))
            {
                events::brush::SetTalusAngleCommand cmd;
                cmd.angle = talusAngle;
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("Lower angle = more erosion");
        }

        // Terrace brush controls
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Terrace))
        {
            ImGui::Spacing();
            ImGui::Text("Terrace Settings");
            ImGui::Separator();

            if (ImGui::SliderFloat("Step Height", &terraceStepHeight, 0.5f, 20.0f, "%.1f"))
            {
                events::brush::SetTerraceStepHeightCommand cmd;
                cmd.stepHeight = terraceStepHeight;
                dispatcher.execute(cmd);
            }

            if (ImGui::SliderFloat("Sharpness", &terraceSharpness, 0.0f, 1.0f, "%.2f"))
            {
                events::brush::SetTerraceSharpnessCommand cmd;
                cmd.sharpness = terraceSharpness;
                dispatcher.execute(cmd);
            }
        }

        // Ramp brush controls
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Ramp))
        {
            ImGui::Spacing();
            ImGui::Text("Ramp Settings");
            ImGui::Separator();

            if (ImGui::SliderFloat("Width", &rampWidth, 1.0f, 50.0f, "%.1f"))
            {
                events::brush::SetRampWidthCommand cmd;
                cmd.width = rampWidth;
                dispatcher.execute(cmd);
            }

            if (ImGui::SliderFloat("Edge Falloff", &rampFalloff, 0.0f, 20.0f, "%.1f"))
            {
                events::brush::SetRampFalloffCommand cmd;
                cmd.falloff = rampFalloff;
                dispatcher.execute(cmd);
            }

            bool startCaptured = dispatcher.query(events::brush::IsRampStartCapturedQuery{});

            if (startCaptured)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Start point set - click end point");
                if (ImGui::Button("Reset Start Point"))
                {
                    events::brush::ResetRampCommand cmd;
                    dispatcher.execute(cmd);
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Click to set start point");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Ramp))
        {
            ImGui::TextDisabled("Click start point, then click end point");
        }
        else
        {
            ImGui::TextDisabled("Left-click to sculpt");
            ImGui::TextDisabled("Hold Shift to invert");
        }

        ImGui::End();

        // If user closed the panel, deactivate sculpt mode
        if (!visible)
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }
}
