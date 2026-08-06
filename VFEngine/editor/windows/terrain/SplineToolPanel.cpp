#include "SplineToolPanel.hpp"
#include "RoadMeshGenerator.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/render/DebugDrawEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    void copyToBuffer(std::array<char, 64>& buffer, const std::string& value)
    {
        const size_t count = std::min(value.size(), buffer.size() - 1);
        std::memcpy(buffer.data(), value.data(), count);
        buffer[count] = '\0';
    }

    bool toggleOp(const char* label, terrain::SplineOps& ops, terrain::SplineOps op)
    {
        bool enabled = terrain::hasOp(ops, op);
        if (!ImGui::Checkbox(label, &enabled))
            return false;

        const auto bit = static_cast<uint8_t>(op);
        auto value = static_cast<uint8_t>(ops);
        ops = static_cast<terrain::SplineOps>(enabled ? (value | bit) : (value & ~bit));
        return true;
    }
}

namespace windows
{
    SplineToolPanel::~SplineToolPanel()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (modeToken.isValid())
            dispatcher.unsubscribe(modeToken);
        if (pointToken.isValid())
            dispatcher.unsubscribe(pointToken);
        if (paramsToken.isValid())
            dispatcher.unsubscribe(paramsToken);
    }

    void SplineToolPanel::subscribe()
    {
        if (subscribed)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        modeToken = dispatcher.subscribe<events::splineTerrain::SplineModeChangedNotification>(
            [this](const events::splineTerrain::SplineModeChangedNotification& n)
            {
                visible = n.isActive;
                if (n.isActive)
                {
                    applyParams(events::EventDispatcher::instance().query(
                        events::splineTerrain::GetSplineParamsQuery{}));

                    pointCount = events::EventDispatcher::instance().query(
                        events::splineTerrain::GetActiveSplinePointCountQuery{});
                    ++previewRevision;
                }
            });

        pointToken = dispatcher.subscribe<events::splineTerrain::SplinePointCountChangedNotification>(
            [this](const events::splineTerrain::SplinePointCountChangedNotification& n)
            {
                pointCount = n.pointCount;
                ++previewRevision;
            });

        paramsToken = dispatcher.subscribe<events::splineTerrain::SplineParamsChangedNotification>(
            [this](const events::splineTerrain::SplineParamsChangedNotification& n)
            {
                applyParams(n.params);
            });

        subscribed = true;
    }

    void SplineToolPanel::applyParams(const terrain::SplineParams& incoming)
    {
        params = incoming;
        paintLayer = static_cast<int>(params.paintLayer);
        roadProfileIsCustom = !deriveRoadScalars(params.road);
        copyToBuffer(roadNameBuffer, params.roadName);
        ++previewRevision;
    }

    void SplineToolPanel::pushParams()
    {
        params.paintLayer = static_cast<uint32_t>(std::max(0, paintLayer));
        params.roadName = roadNameBuffer.data();

        events::splineTerrain::SetSplineParamsCommand cmd;
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
        ++previewRevision;
    }

    void SplineToolPanel::rebuildRoadColumns()
    {
        const terrain::RoadProfile generated =
            terrain::makeDefaultRoadProfile(roadHalfWidth, roadShoulderWidth, roadShoulderDrop);
        params.road.columns = generated.columns;
        roadProfileIsCustom = false;
    }

    bool SplineToolPanel::deriveRoadScalars(const terrain::RoadProfile& profile)
    {
        if (profile.columns.size() != 4)
            return false;

        const float halfWidth = profile.columns[2].offset;
        const float shoulderWidth = profile.columns[3].offset - profile.columns[2].offset;
        const float shoulderDrop = -profile.columns[3].heightOffset;
        if (halfWidth <= 0.0f || shoulderWidth < 0.0f)
            return false;

        // Only accept it if regenerating from the scalars reproduces the columns; anything else is
        // a hand-authored cross-section that the three sliders would quietly destroy.
        const terrain::RoadProfile rebuilt =
            terrain::makeDefaultRoadProfile(halfWidth, shoulderWidth, shoulderDrop);
        for (size_t i = 0; i < 4; ++i)
        {
            if (std::abs(rebuilt.columns[i].offset - profile.columns[i].offset) > 1e-4f ||
                std::abs(rebuilt.columns[i].heightOffset - profile.columns[i].heightOffset) > 1e-4f ||
                std::abs(rebuilt.columns[i].terrainBlend - profile.columns[i].terrainBlend) > 1e-4f)
                return false;
        }

        roadHalfWidth = halfWidth;
        roadShoulderWidth = shoulderWidth;
        roadShoulderDrop = shoulderDrop;
        return true;
    }

    void SplineToolPanel::drawOperations(bool& changed)
    {
        ImGui::Text("Operations");
        ImGui::Separator();

        changed |= toggleOp("Sculpt Corridor", params.ops, terrain::SplineOps::Sculpt);
        changed |= toggleOp("Paint Layer", params.ops, terrain::SplineOps::Paint);
        changed |= toggleOp("Generate Road Mesh", params.ops, terrain::SplineOps::Mesh);

        if (params.ops == terrain::SplineOps::None)
            ImGui::TextDisabled("Pick at least one operation");
    }

    void SplineToolPanel::drawCorridorParams(bool& changed)
    {
        ImGui::Spacing();
        ImGui::Text("Corridor");
        ImGui::Separator();

        changed |= ImGui::SliderFloat("Corridor Width", &params.corridorWidth, 1.0f, 50.0f, "%.1f");
        changed |= ImGui::SliderFloat("Edge Falloff", &params.falloffWidth, 0.0f, 20.0f, "%.1f");

        if (terrain::hasOp(params.ops, terrain::SplineOps::Sculpt))
            changed |= ImGui::SliderFloat("Embankment", &params.embankmentHeight, -5.0f, 10.0f, "%.1f");

        if (terrain::hasOp(params.ops, terrain::SplineOps::Paint))
        {
            if (ImGui::InputInt("Paint Layer", &paintLayer))
            {
                paintLayer = std::clamp(paintLayer, 0, 7);
                changed = true;
            }
        }
    }

    void SplineToolPanel::drawRoadParams(bool& changed)
    {
        if (!terrain::hasOp(params.ops, terrain::SplineOps::Mesh))
            return;

        ImGui::Spacing();
        ImGui::Text("Road Mesh");
        ImGui::Separator();

        if (ImGui::InputText("Road Name", roadNameBuffer.data(), roadNameBuffer.size()))
            changed = true;

        if (roadProfileIsCustom)
        {
            ImGui::TextDisabled("Custom cross-section (%zu columns)", params.road.columns.size());
            if (ImGui::Button("Reset To Default Profile"))
            {
                rebuildRoadColumns();
                changed = true;
            }
        }
        else
        {
            bool shape = false;
            shape |= ImGui::SliderFloat("Half Width", &roadHalfWidth, 0.5f, 25.0f, "%.2f m");
            shape |= ImGui::SliderFloat("Shoulder Width", &roadShoulderWidth, 0.0f, 10.0f, "%.2f m");
            shape |= ImGui::SliderFloat("Shoulder Drop", &roadShoulderDrop, 0.0f, 2.0f, "%.2f m");
            if (shape)
            {
                rebuildRoadColumns();
                changed = true;
            }
        }

        changed |= ImGui::SliderFloat("Ring Spacing", &params.road.ringSpacing, 0.25f, 8.0f, "%.2f m");
        changed |= ImGui::DragFloat("UV Tiling U", &params.road.uvTilingU, 0.05f, 0.05f, 16.0f, "%.2f");
        changed |= ImGui::DragFloat("Length Per V Repeat", &params.road.uvTilingV, 0.25f, 0.5f, 64.0f, "%.2f m");
        changed |= ImGui::SliderFloat("Height Offset", &params.road.zOffset, 0.0f, 0.5f, "%.3f m");
        changed |= ImGui::Checkbox("Flat Cross-Section", &params.road.flatCrossSection);
        ImGui::SetItemTooltip("On: the road surface is flat across its width (paved road on a "
                              "sculpted corridor).\nOff: every column follows the terrain under it "
                              "(dirt path on untouched ground).");
        changed |= ImGui::Checkbox("Generate Collider", &params.roadCollider);

        char materialBuffer[260] = {};
        const size_t materialCount = std::min(params.roadMaterialPath.size(), sizeof(materialBuffer) - 1);
        std::memcpy(materialBuffer, params.roadMaterialPath.data(), materialCount);
        ImGui::InputText("Material", materialBuffer, sizeof(materialBuffer), ImGuiInputTextFlags_ReadOnly);
        ImGui::SameLine();
        if (ImGui::Button("Browse##roadMaterial"))
        {
            const std::vector<std::pair<std::wstring, std::wstring>> filters = {
                {L"Material Files", L"*.vfMat;*.vfMatInstance"}};
            const std::string selected = fileDialog.openFileDialog(filters);
            if (!selected.empty())
            {
                params.roadMaterialPath = selected;
                changed = true;
            }
        }
        if (params.roadMaterialPath.empty())
            ImGui::TextDisabled("No material — the road will use the engine default");
    }

    void SplineToolPanel::drawPointList()
    {
        if (pointCount == 0)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        if (!ImGui::CollapsingHeader("Control Point Positions"))
            return;

        const auto points = dispatcher.query(events::splineTerrain::GetActiveSplinePointsQuery{});
        for (size_t i = 0; i < points.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            glm::vec3 position = points[i].position;
            if (ImGui::DragFloat3("##point", &position.x, 0.05f, 0.0f, 0.0f, "%.2f"))
            {
                events::splineTerrain::SetSplinePointCommand cmd;
                cmd.index = static_cast<uint32_t>(i);
                cmd.position = position;
                dispatcher.query(cmd);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                events::splineTerrain::RemoveSplinePointCommand cmd;
                cmd.index = static_cast<uint32_t>(i);
                dispatcher.query(cmd);
                ImGui::PopID();
                break; // the list just shifted under us
            }

            ImGui::PopID();
        }
    }

    void SplineToolPanel::drawRoadList()
    {
        ImGui::Spacing();
        if (!ImGui::CollapsingHeader("Roads In Scene"))
            return;

        const auto roads = RoadMeshGenerator::listRoads();
        if (roads.empty())
        {
            ImGui::TextDisabled("No generated roads");
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        for (size_t i = 0; i < roads.size(); ++i)
        {
            const RoadMeshGenerator::RoadEntry& road = roads[i];
            ImGui::PushID(static_cast<int>(i));

            ImGui::TextUnformatted(road.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%u chunk%s)", road.chunkCount, road.chunkCount == 1 ? "" : "s");

            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
            {
                // Seeds the active spline from the road's persisted component; the next Apply
                // replaces this road rather than spawning a second one beside it.
                if (RoadMeshGenerator::openForEdit(road.entity))
                    editingRoadName = road.name;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
            {
                events::scene::SelectEntityCommand selectCmd;
                selectCmd.entity = road.entity;
                dispatcher.execute(selectCmd);
            }

            ImGui::PopID();
        }
    }

    void SplineToolPanel::drawPreview()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Control-point markers. Without them a draggable point is invisible until the curve moves.
        if (pointCount > 0)
        {
            const auto points = dispatcher.query(events::splineTerrain::GetActiveSplinePointsQuery{});
            const float markerSize = std::max(0.5f, params.corridorWidth * 0.25f);
            for (const terrain::SplineControlPoint& point : points)
            {
                const glm::vec4 markerColor(1.0f, 0.35f, 0.1f, 1.0f);
                events::debugdraw::DrawLineCommand acrossX;
                acrossX.start = point.position - glm::vec3(markerSize, 0.0f, 0.0f);
                acrossX.end = point.position + glm::vec3(markerSize, 0.0f, 0.0f);
                acrossX.color = markerColor;
                dispatcher.execute(acrossX);

                events::debugdraw::DrawLineCommand acrossZ;
                acrossZ.start = point.position - glm::vec3(0.0f, 0.0f, markerSize);
                acrossZ.end = point.position + glm::vec3(0.0f, 0.0f, markerSize);
                acrossZ.color = markerColor;
                dispatcher.execute(acrossZ);
            }
        }

        if (pointCount < 2)
            return;

        if (previewRevision != cachedPreviewRevision)
        {
            events::splineTerrain::GetActiveSplinePreviewQuery previewQuery;
            cachedSamples = dispatcher.query(previewQuery);
            cachedPreviewRevision = previewRevision;
        }

        const bool showRoad = terrain::hasOp(params.ops, terrain::SplineOps::Mesh);
        const float roadHalf = showRoad ? (roadHalfWidth + roadShoulderWidth) : 0.0f;

        for (size_t i = 0; i + 1 < cachedSamples.size(); ++i)
        {
            events::debugdraw::DrawLineCommand centerLine;
            centerLine.start = cachedSamples[i];
            centerLine.end = cachedSamples[i + 1];
            centerLine.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            dispatcher.execute(centerLine);

            const glm::vec3 dir = cachedSamples[i + 1] - cachedSamples[i];
            const glm::vec2 dir2D(dir.x, dir.z);
            const float len = glm::length(dir2D);
            if (len <= 0.001f)
                continue;

            // Same perpendicular the road mesh builder uses (right = (-t.z, 0, t.x)), so the
            // preview lines land exactly where the generated edges will.
            const glm::vec2 perp = glm::normalize(glm::vec2(-dir2D.y, dir2D.x));
            const glm::vec3 offset(perp.x, 0.0f, perp.y);

            auto drawEdge = [&](float width, const glm::vec4& color)
            {
                events::debugdraw::DrawLineCommand left;
                left.start = cachedSamples[i] + offset * width;
                left.end = cachedSamples[i + 1] + offset * width;
                left.color = color;
                dispatcher.execute(left);

                events::debugdraw::DrawLineCommand right;
                right.start = cachedSamples[i] - offset * width;
                right.end = cachedSamples[i + 1] - offset * width;
                right.color = color;
                dispatcher.execute(right);
            };

            drawEdge(params.corridorWidth, glm::vec4(1.0f, 1.0f, 1.0f, 0.4f));
            if (showRoad && roadHalf > 0.0f)
                drawEdge(roadHalf, glm::vec4(0.2f, 0.8f, 1.0f, 0.7f));
        }
    }

    void SplineToolPanel::draw()
    {
        subscribe();

        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Spline Tool", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        bool changed = false;

        drawOperations(changed);
        drawCorridorParams(changed);
        drawRoadParams(changed);

        if (changed)
            pushParams();

        ImGui::Spacing();
        ImGui::Text("Control Points: %u", pointCount);
        if (!editingRoadName.empty())
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Editing road \"%s\"", editingRoadName.c_str());
        ImGui::Separator();

        drawPointList();

        const bool canApply = pointCount >= 2 && params.ops != terrain::SplineOps::None;
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button(editingRoadName.empty() ? "Apply Spline" : "Regenerate Road"))
        {
            events::splineTerrain::FinalizeSplineCommand cmd;
            dispatcher.execute(cmd);
            pointCount = 0;
            editingRoadName.clear();
            ++previewRevision;
        }
        ImGui::EndDisabled();

        if (pointCount > 0)
        {
            ImGui::SameLine();
            if (ImGui::Button("Undo Last Point"))
            {
                events::splineTerrain::RemoveLastSplinePointCommand cmd;
                dispatcher.execute(cmd);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All"))
            {
                events::splineTerrain::ClearActiveSplineCommand cmd;
                dispatcher.execute(cmd);
                pointCount = 0;
                editingRoadName.clear();
                ++previewRevision;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Click terrain to add a point");
        ImGui::TextDisabled("Drag a point to move it, Alt+click to delete it");
        ImGui::TextDisabled("Need at least 2 points to apply");

        drawRoadList();
        drawPreview();

        ImGui::End();

        if (!visible)
        {
            events::splineTerrain::SetSplineModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }
}
