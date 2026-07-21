#include "ReflectionProbeDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ReflectionProbeEvents.hpp"
#include <imgui.h>

namespace windows::details
{
    bool ReflectionProbeDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasReflectionProbeComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetReflectionProbeDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("ReflectionProbeComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::ReflectionProbeData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Local environment capture; overrides IBL specular in bounds");
            ImGui::Spacing();

            changed |= drawShapeSettings(data);
            ImGui::Spacing();
            changed |= drawBlendSettings(data);
            ImGui::Spacing();
            changed |= drawCaptureSettings(data);
            ImGui::Spacing();
            changed |= drawDebugSettings(data);
            ImGui::Spacing();
            drawBakeControls(handle);

            if (changed)
            {
                events::scene::SetReflectionProbeDataCommand cmd;
                cmd.entity = handle;
                cmd.data = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemoveReflectionProbeComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool ReflectionProbeDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ReflectionProbeHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Reflection Probe");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveReflectionProbe", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool ReflectionProbeDrawer::drawShapeSettings(services::ReflectionProbeData& data)
    {
        bool changed = false;

        ImGui::Text("Influence:");
        ImGui::Indent(10.0f);

        const char* shapeNames[] = {"Box", "Sphere"};
        int currentShape = static_cast<int>(data.shape);
        if (ImGui::Combo("Shape##RP", &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
        {
            data.shape = static_cast<uint8_t>(currentShape);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Box uses parallax correction against the box faces (best for rooms).\n"
                              "Sphere corrects against a bounding sphere (best for open alcoves).");
        }

        if (data.shape == 0)
        {
            if (ImGui::DragFloat3("Half Extents##RP", &data.halfExtents.x, 0.1f, 0.1f, 500.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Half-size of the influence box in each axis.\n"
                                  "Match this to the room the probe sits in so parallax lands on the walls.");
            }
        }
        else
        {
            // Sphere radius lives in .x; keep y/z in lockstep so the gizmo and the sort volume
            // cannot disagree with what the inspector shows.
            if (ImGui::DragFloat("Radius##RP", &data.halfExtents.x, 0.1f, 0.1f, 500.0f, "%.1f"))
            {
                data.halfExtents.y = data.halfExtents.x;
                data.halfExtents.z = data.halfExtents.x;
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Radius of the influence sphere");
            }
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReflectionProbeDrawer::drawBlendSettings(services::ReflectionProbeData& data)
    {
        bool changed = false;

        ImGui::Text("Blend:");
        ImGui::Indent(10.0f);

        if (ImGui::DragFloat("Blend Distance##RP", &data.blendDistance, 0.05f, 0.0f, 100.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("World units of cross-fade measured INWARD from the bounds surface.\n"
                              "0 = hard edge (reflections pop). Larger = smoother hand-off to the\n"
                              "global environment as objects leave the probe.");
        }

        if (ImGui::DragFloat("Intensity##RP", &data.intensity, 0.01f, 0.0f, 10.0f, "%.2f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Multiplier on this probe's reflection contribution");
        }

        if (ImGui::DragInt("Priority##RP", &data.priority, 0.1f, -100, 100))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Where probes overlap, higher priority wins.\n"
                              "At equal priority the smaller probe wins (the more specific answer).");
        }

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReflectionProbeDrawer::drawCaptureSettings(services::ReflectionProbeData& data)
    {
        bool changed = false;

        ImGui::Text("Capture:");
        ImGui::Indent(10.0f);

        if (ImGui::DragFloat("Near##RP", &data.nearPlane, 0.01f, 0.001f, 100.0f, "%.3f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Capture near plane. Raise it if the probe sits inside geometry that\n"
                              "would otherwise fill its own cubemap.");
        }

        if (ImGui::DragFloat("Far##RP", &data.farPlane, 1.0f, 1.0f, 10000.0f, "%.0f"))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Capture far plane");
        }

        if (ImGui::Checkbox("Capture Shadows##RP", &data.captureShadows))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("EXPERIMENTAL. Off-camera captures sample the main view's shadow\n"
                              "clipmap, which is centered for the primary camera and can misproject.\n"
                              "Leave off unless you have verified the result.");
        }

        ImGui::TextDisabled("Changing Near/Far/Shadows triggers a re-bake.");

        ImGui::Unindent(10.0f);

        return changed;
    }

    bool ReflectionProbeDrawer::drawDebugSettings(services::ReflectionProbeData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Show Gizmo##RP", &data.showGizmo))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Draw the influence bounds and the inner blend boundary in the editor");
        }

        return changed;
    }

    void ReflectionProbeDrawer::drawBakeControls(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::Button("Bake##RP"))
        {
            events::scene::BakeReflectionProbesCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Re-capture this probe from its current position.\n"
                              "Capture is spread over several frames, so the reflection\n"
                              "updates a moment after the click.");
        }

        ImGui::SameLine();

        if (ImGui::Button("Bake All##RP"))
        {
            events::scene::BakeReflectionProbesCommand cmd;
            cmd.entity = services::EntityHandle::invalid(); // invalid handle == every probe
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Re-capture every reflection probe in the scene");
        }
    }
}
