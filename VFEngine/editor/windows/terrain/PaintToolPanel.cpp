#include "PaintToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/PaintBrushEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "terrain/PaintBrushTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows
{
    PaintToolPanel::~PaintToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
        }
        if (brushTypeToken.isValid())
        {
            dispatcher.unsubscribe(brushTypeToken);
        }
        if (brushParamsToken.isValid())
        {
            dispatcher.unsubscribe(brushParamsToken);
        }
    }

    void PaintToolPanel::loadMaterialFromTarget()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto targetEntity = dispatcher.query(events::paint::GetPaintTargetEntityQuery{});
        if (!targetEntity.has_value())
        {
            materialData.reset();
            materialPath.clear();
            return;
        }

        events::terrain::GetTerrainDataQuery query;
        query.entity = targetEntity.value();
        auto terrainData = dispatcher.query(query);
        if (!terrainData.has_value() || terrainData->terrainMaterialPath.empty())
        {
            materialData.reset();
            materialPath.clear();
            return;
        }

        if (terrainData->terrainMaterialPath != materialPath)
        {
            materialPath = terrainData->terrainMaterialPath;
            materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));
        }
    }

    void PaintToolPanel::subscribe()
    {
        if (subscribed)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    auto& d = events::EventDispatcher::instance();
                    auto params = d.query(events::paintBrush::GetPaintBrushParamsQuery{});
                    brushRadius = params.radius;
                    brushStrength = params.strength;
                    brushOpacity = params.opacity;
                    activeLayer = static_cast<int>(params.activeLayer);
                    falloffIndex = static_cast<int>(params.falloff);
                    shapeIndex = static_cast<int>(params.shape);
                    selectedTarget = static_cast<int>(params.target);

                    auto type = d.query(events::paintBrush::GetPaintBrushTypeQuery{});
                    selectedBrushType = static_cast<int>(type);

                    loadMaterialFromTarget();
                }
                else
                {
                    materialData.reset();
                    materialPath.clear();
                }
            });

        brushTypeToken = dispatcher.subscribe<events::paintBrush::PaintBrushTypeChangedNotification>(
            [this](const events::paintBrush::PaintBrushTypeChangedNotification& n)
            {
                selectedBrushType = static_cast<int>(n.type);
            });

        brushParamsToken = dispatcher.subscribe<events::paintBrush::PaintBrushParamsChangedNotification>(
            [this](const events::paintBrush::PaintBrushParamsChangedNotification& n)
            {
                brushRadius = n.params.radius;
                brushStrength = n.params.strength;
                brushOpacity = n.params.opacity;
                activeLayer = static_cast<int>(n.params.activeLayer);
                falloffIndex = static_cast<int>(n.params.falloff);
                shapeIndex = static_cast<int>(n.params.shape);
                selectedTarget = static_cast<int>(n.params.target);
            });

        subscribed = true;
    }

    void PaintToolPanel::draw()
    {
        subscribe();

        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Paint Tools", &visible))
        {
            ImGui::End();
            return;
        }

        drawPaintTarget();
        drawBrushType();
        // VK-1614: layers and the surface mask are different data. The material and layer pickers
        // only mean something for a Layers stroke, so they are hidden rather than left inert — the
        // dead-control failure mode VK-1613 existed to clean up.
        const bool paintingLayers = (selectedTarget == static_cast<int>(terrain::PaintTarget::Layers));
        if (paintingLayers)
        {
            drawTerrainMaterial();
            drawLayerSelection();
        }
        drawBrushParams();

        bool isBaseLayerMode = paintingLayers
            && selectedBrushType == static_cast<int>(terrain::PaintBrushType::SetBaseLayer);
        ImGui::Spacing();
        ImGui::Separator();
        if (isBaseLayerMode)
        {
            ImGui::TextDisabled("Left-click to set base layer on tiles");
        }
        else
        {
            ImGui::TextDisabled("Left-click to paint");
            ImGui::TextDisabled("Hold Shift to erase");
        }

        ImGui::End();

        if (!visible)
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void PaintToolPanel::drawPaintTarget()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Paint Target");
        ImGui::Separator();

        const bool hasMask = dispatcher.query(events::terrain::HasSurfaceMaskQuery{});

        const char* targetLabels[] = {"Layers", "Wetness", "Snow"};
        bool targetChanged = false;

        for (int i = 0; i < 3; ++i)
        {
            // Wetness/Snow need a mask to write into. Disabled rather than hidden so the feature is
            // discoverable, with the tooltip pointing at where to create one.
            const bool needsMask = (i != static_cast<int>(terrain::PaintTarget::Layers));
            if (needsMask && !hasMask)
            {
                ImGui::BeginDisabled(true);
            }
            if (ImGui::RadioButton(targetLabels[i], &selectedTarget, i))
            {
                targetChanged = true;
            }
            if (needsMask && !hasMask)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("No surface mask on this terrain.\n"
                                      "Create one in the Terrain inspector (Surface Mask).");
                }
            }
            if (i < 2)
            {
                ImGui::SameLine();
            }
        }

        // A mask cleared while Wetness/Snow was selected would leave strokes writing nowhere.
        if (!hasMask && selectedTarget != static_cast<int>(terrain::PaintTarget::Layers))
        {
            selectedTarget = static_cast<int>(terrain::PaintTarget::Layers);
            targetChanged = true;
        }

        if (targetChanged)
        {
            events::paintBrush::SetPaintTargetCommand cmd;
            cmd.target = static_cast<terrain::PaintTarget>(selectedTarget);
            dispatcher.execute(cmd);
        }
    }

    void PaintToolPanel::drawBrushType()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Brush Type");
        ImGui::Separator();

        const char* brushLabels[] = {"Paint", "Erase", "Smooth", "Fill", "Base"};
        bool typeChanged = false;

        for (int i = 0; i < 5; ++i)
        {
            if (ImGui::RadioButton(brushLabels[i], &selectedBrushType, i))
            {
                typeChanged = true;
            }
            if (i < 4)
            {
                ImGui::SameLine();
            }
        }

        if (typeChanged)
        {
            events::paintBrush::SetPaintBrushTypeCommand cmd;
            cmd.type = static_cast<terrain::PaintBrushType>(selectedBrushType);
            dispatcher.execute(cmd);
        }
    }

    void PaintToolPanel::drawTerrainMaterial()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::Text("Terrain Material");
        ImGui::Separator();

        if (!materialPath.empty())
        {
            std::string displayName = materialPath;
            auto lastSlash = displayName.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                displayName = displayName.substr(lastSlash + 1);
            }
            ImGui::TextWrapped("%s", displayName.c_str());
            ImGui::SameLine();
        }

        if (ImGui::Button(materialPath.empty() ? "Browse Material..." : "Change"))
        {
            nfd::FileDialog fileDialog;
            std::vector<std::pair<std::wstring, std::wstring>> filters = {
                {L"Terrain Material", L"*.vfTerrainMat"}
            };
            std::string selectedPath = fileDialog.openFileDialog(filters);
            if (!selectedPath.empty())
            {
                selectedPath.erase(
                    std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                    selectedPath.end());

                auto targetEntity = dispatcher.query(events::paint::GetPaintTargetEntityQuery{});
                if (targetEntity.has_value())
                {
                    events::terrain::SetTerrainMaterialPathCommand cmd;
                    cmd.terrainEntity = targetEntity.value();
                    cmd.materialPath = selectedPath;
                    dispatcher.execute(cmd);

                    materialPath = selectedPath;
                    materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));
                }
            }
        }
    }

    void PaintToolPanel::drawLayerSelection()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::Text("Layer Selection");
        ImGui::Separator();

        if (materialData && materialData->activeLayerCount > 0)
        {
            int maxLayer = static_cast<int>(materialData->activeLayerCount) - 1;
            activeLayer = std::clamp(activeLayer, 0, maxLayer);

            if (ImGui::BeginCombo("Layer", materialData->layers[activeLayer].name.c_str()))
            {
                for (int i = 0; i <= maxLayer; ++i)
                {
                    const auto& layer = materialData->layers[i];
                    if (!layer.enabled)
                    {
                        continue;
                    }

                    bool isSelected = (activeLayer == i);
                    std::string label = std::to_string(i) + ": " + layer.name;
                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        activeLayer = i;
                        events::paintBrush::SetPaintActiveLayerCommand cmd;
                        cmd.layer = static_cast<uint32_t>(activeLayer);
                        dispatcher.execute(cmd);
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            ImGui::TextDisabled("No layers - assign a terrain material above");
        }
    }

    void PaintToolPanel::drawBrushParams()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::Text("Brush Parameters");
        ImGui::Separator();

        bool isBaseLayerMode = (selectedBrushType == static_cast<int>(terrain::PaintBrushType::SetBaseLayer));

        if (ImGui::SliderFloat("Radius", &brushRadius, 0.1f, 100.0f, "%.1f"))
        {
            events::paintBrush::SetPaintBrushRadiusCommand cmd;
            cmd.radius = brushRadius;
            dispatcher.execute(cmd);
        }

        if (!isBaseLayerMode)
        {
            if (ImGui::SliderFloat("Strength", &brushStrength, 0.0f, 100.0f, "%.2f"))
            {
                events::paintBrush::SetPaintBrushStrengthCommand cmd;
                cmd.strength = brushStrength;
                dispatcher.execute(cmd);
            }

            if (ImGui::SliderFloat("Opacity", &brushOpacity, 0.0f, 1.0f, "%.2f"))
            {
                events::paintBrush::SetPaintBrushOpacityCommand cmd;
                cmd.opacity = brushOpacity;
                dispatcher.execute(cmd);
            }

            const char* falloffLabels[] = {"Constant", "Linear", "Smooth", "Sharp"};
            if (ImGui::Combo("Falloff", &falloffIndex, falloffLabels, 4))
            {
                events::paintBrush::SetPaintBrushFalloffCommand cmd;
                cmd.falloff = static_cast<terrain::BrushFalloff>(falloffIndex);
                dispatcher.execute(cmd);
            }

            const char* shapeLabels[] = {"Circle", "Square"};
            if (ImGui::Combo("Shape", &shapeIndex, shapeLabels, 2))
            {
                events::paintBrush::SetPaintBrushShapeCommand cmd;
                cmd.shape = static_cast<terrain::BrushShape>(shapeIndex);
                dispatcher.execute(cmd);
            }
        }
    }
}
