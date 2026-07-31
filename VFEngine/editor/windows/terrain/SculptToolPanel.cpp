#include "SculptToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/BrushEvents.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <iterator>

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

    // VK-1613 documented the failure this collapses: stampMode was the one field missing from both
    // of the two hand-maintained sync paths, so the combo reverted to "Add" on re-entering sculpt
    // mode while the service still held Subtract -- the control worked, the readout lied. There is
    // now one field list, so a new brush param can only be forgotten once.
    void SculptToolPanel::applyParams(const terrain::BrushParams& params)
    {
        brushRadius = params.radius;
        brushStrength = params.strength;
        falloffIndex = static_cast<int>(params.falloff);
        shapeIndex = static_cast<int>(params.shape);
        stampMode = params.stampSubtract ? 1 : 0;
        stampRotation = params.stampRotation;
        stampScale = params.stampScale;
        talusAngle = params.talusAngle;
        terraceStepHeight = params.terraceStepHeight;
        terraceSharpness = params.terraceSharpness;
        rampWidth = params.rampWidth;
        rampFalloff = params.rampFalloff;
        hydraulicRainRate = params.hydraulicRainRate;
        hydraulicSedimentCapacity = params.hydraulicSedimentCapacity;
        hydraulicEvaporation = params.hydraulicEvaporation;
        hydraulicHardness = params.hydraulicHardness;
        hydraulicSmoothing = params.hydraulicSmoothing;
        hydraulicIterations = static_cast<int>(params.hydraulicIterations);
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
                    applyParams(d.query(events::brush::GetBrushParamsQuery{}));

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
                applyParams(n.params);
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

        // VK-1616: the label array is now the single source of the count, and a static_assert ties
        // it to the enum. The two hard-coded literals this replaces (a loop bound of 9 and a
        // SameLine guard of 8) are exactly the pair the next brush would have forgotten.
        constexpr const char* brushLabels[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise",
                                               "Stamp", "Erosion", "Terrace", "Ramp", "Hydraulic"};
        constexpr int brushTypeCount = static_cast<int>(std::size(brushLabels));
        static_assert(brushTypeCount == static_cast<int>(terrain::BrushType::Hydraulic) + 1,
                      "brushLabels must cover every BrushType, in enum order");

        bool typeChanged = false;

        // These no longer fit one row in a 250 px window, so wrap on content width rather than
        // letting the last buttons run off the edge.
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float buttonPadding = ImGui::GetFrameHeight() + spacing;
        for (int i = 0; i < brushTypeCount; ++i)
        {
            if (i > 0)
            {
                const float width = ImGui::CalcTextSize(brushLabels[i]).x + buttonPadding;
                if (ImGui::GetContentRegionAvail().x >= width)
                    ImGui::SameLine();
            }
            if (ImGui::RadioButton(brushLabels[i], &selectedBrushType, i))
            {
                typeChanged = true;
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

        // Hydraulic erosion brush controls
        if (selectedBrushType == static_cast<int>(terrain::BrushType::Hydraulic))
        {
            ImGui::Spacing();
            ImGui::Text("Hydraulic Erosion");
            ImGui::Separator();

            if (ImGui::SliderFloat("Rain Amount", &hydraulicRainRate, 0.0f, 2.0f, "%.2f"))
            {
                events::brush::SetHydraulicRainRateCommand cmd;
                cmd.rainRate = hydraulicRainRate;
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("More water = deeper channels");

            if (ImGui::SliderFloat("Sediment Capacity", &hydraulicSedimentCapacity, 0.1f, 5.0f, "%.2f"))
            {
                events::brush::SetHydraulicSedimentCapacityCommand cmd;
                cmd.capacity = hydraulicSedimentCapacity;
                dispatcher.execute(cmd);
            }

            if (ImGui::SliderInt("Iterations", &hydraulicIterations, 1, 128))
            {
                events::brush::SetHydraulicIterationsCommand cmd;
                cmd.iterations = static_cast<uint32_t>(hydraulicIterations);
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("Sim steps per frame; auto-reduced on huge brushes");

            if (ImGui::SliderFloat("Evaporation", &hydraulicEvaporation, 0.0f, 0.2f, "%.3f"))
            {
                events::brush::SetHydraulicEvaporationCommand cmd;
                cmd.evaporation = hydraulicEvaporation;
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("High = short gullies, low = long channels");

            if (ImGui::SliderFloat("Sediment Hardness", &hydraulicHardness, 0.0f, 1.0f, "%.2f"))
            {
                events::brush::SetHydraulicHardnessCommand cmd;
                cmd.hardness = hydraulicHardness;
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("Soft ground carves, hard ground silts up");

            if (ImGui::SliderFloat("Smoothing", &hydraulicSmoothing, 0.0f, 1.0f, "%.2f"))
            {
                events::brush::SetHydraulicSmoothingCommand cmd;
                cmd.smoothing = hydraulicSmoothing;
                dispatcher.execute(cmd);
            }
            ImGui::TextDisabled("Talus relaxation between channels (uses Talus Angle)");

            if (ImGui::SliderFloat("Talus Angle", &talusAngle, 5.0f, 85.0f, "%.1f deg"))
            {
                events::brush::SetTalusAngleCommand cmd;
                cmd.angle = talusAngle;
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
        else if (selectedBrushType == static_cast<int>(terrain::BrushType::Hydraulic))
        {
            ImGui::TextDisabled("Hold to erode; keep holding for deeper channels");
            ImGui::TextDisabled("Hold Shift to deposit instead of carve");
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
