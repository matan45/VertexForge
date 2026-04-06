#include "SplineToolPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/render/DebugDrawEvents.hpp"
#include <imgui.h>
#include <glm/glm.hpp>

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
                    auto params = events::EventDispatcher::instance().query(
                        events::splineTerrain::GetSplineParamsQuery{});
                    corridorWidth = params.corridorWidth;
                    falloffWidth = params.falloffWidth;
                    embankmentHeight = params.embankmentHeight;

                    pointCount = events::EventDispatcher::instance().query(
                        events::splineTerrain::GetActiveSplinePointCountQuery{});
                }
            });

        pointToken = dispatcher.subscribe<events::splineTerrain::SplinePointAddedNotification>(
            [this](const events::splineTerrain::SplinePointAddedNotification& n)
            {
                pointCount = n.pointCount;
            });

        paramsToken = dispatcher.subscribe<events::splineTerrain::SplineParamsChangedNotification>(
            [this](const events::splineTerrain::SplineParamsChangedNotification& n)
            {
                corridorWidth = n.params.corridorWidth;
                falloffWidth = n.params.falloffWidth;
                embankmentHeight = n.params.embankmentHeight;
            });

        subscribed = true;
    }

    void SplineToolPanel::draw()
    {
        subscribe();

        if (!visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Spline Tool", &visible))
        {
            ImGui::End();
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Text("Spline Parameters");
        ImGui::Separator();

        bool paramsChanged = false;

        if (ImGui::SliderFloat("Corridor Width", &corridorWidth, 1.0f, 50.0f, "%.1f"))
            paramsChanged = true;

        if (ImGui::SliderFloat("Edge Falloff", &falloffWidth, 0.0f, 20.0f, "%.1f"))
            paramsChanged = true;

        if (ImGui::SliderFloat("Embankment", &embankmentHeight, -5.0f, 10.0f, "%.1f"))
            paramsChanged = true;

        if (paramsChanged)
        {
            events::splineTerrain::SetSplineParamsCommand cmd;
            cmd.params.corridorWidth = corridorWidth;
            cmd.params.falloffWidth = falloffWidth;
            cmd.params.embankmentHeight = embankmentHeight;
            dispatcher.execute(cmd);
        }

        ImGui::Spacing();
        ImGui::Text("Control Points: %u", pointCount);
        ImGui::Separator();

        if (pointCount >= 2)
        {
            if (ImGui::Button("Apply Spline"))
            {
                events::splineTerrain::FinalizeSplineCommand cmd;
                dispatcher.execute(cmd);
                pointCount = 0;
            }
            ImGui::SameLine();
        }

        if (pointCount > 0)
        {
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
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Click terrain to add points");
        ImGui::TextDisabled("Need at least 2 points to apply");

        // Draw spline preview each frame
        if (pointCount >= 2)
        {
            events::splineTerrain::GetActiveSplinePreviewQuery previewQuery;
            auto samples = dispatcher.query(previewQuery);
            float halfWidth = corridorWidth;

            for (size_t i = 0; i + 1 < samples.size(); ++i)
            {
                events::debugdraw::DrawLineCommand centerLine;
                centerLine.start = samples[i];
                centerLine.end = samples[i + 1];
                centerLine.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
                dispatcher.execute(centerLine);

                glm::vec3 dir = samples[i + 1] - samples[i];
                glm::vec2 dir2D(dir.x, dir.z);
                float len = glm::length(dir2D);
                if (len > 0.001f)
                {
                    glm::vec2 perp = glm::normalize(glm::vec2(-dir2D.y, dir2D.x)) * halfWidth;
                    glm::vec3 offset(perp.x, 0.0f, perp.y);

                    events::debugdraw::DrawLineCommand left;
                    left.start = samples[i] + offset;
                    left.end = samples[i + 1] + offset;
                    left.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.4f);
                    dispatcher.execute(left);

                    events::debugdraw::DrawLineCommand right;
                    right.start = samples[i] - offset;
                    right.end = samples[i + 1] - offset;
                    right.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.4f);
                    dispatcher.execute(right);
                }
            }
        }

        ImGui::End();

        if (!visible)
        {
            events::splineTerrain::SetSplineModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
    }
}
