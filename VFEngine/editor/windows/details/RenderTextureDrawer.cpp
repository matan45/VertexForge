#include "RenderTextureDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderTextureEvents.hpp"
#include <rendertexture/RenderTextureTypes.hpp>
#include <imgui.h>

namespace windows::details
{
    bool RenderTextureDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasRenderTextureComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasComponent = dispatcher.query(hasQuery);

        if (!hasComponent)
        {
            return false;
        }

        events::scene::GetRenderTextureDataQuery getQuery;
        getQuery.entity = handle;
        auto dataOpt = dispatcher.query(getQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("RenderTextureComponent");

        bool removeComponent = false;
        bool isOpen = drawHeader(removeComponent);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::RenderTextureData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Renders camera view to a texture");
            ImGui::Spacing();

            events::scene::HasCameraComponentQuery camQuery;
            camQuery.entity = handle;
            bool hasCamera = dispatcher.query(camQuery);
            if (!hasCamera)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "No Camera - this RTT renders nothing.");
                if (ImGui::Button("Add Camera##RT"))
                {
                    events::scene::AddCameraComponentCommand addCam;
                    addCam.entity = handle;
                    dispatcher.execute(addCam);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Adds a Camera (and Transform) so the Render Texture has a view to render.");
                }
                ImGui::Spacing();
            }

            changed |= drawResolution(data);
            ImGui::Spacing();
            changed |= drawSettings(data);

            ImGui::Spacing();
            ImGui::SeparatorText("Source Camera");
            // VK-1414: optionally render from a SEPARATE camera entity instead of this entity's own
            // camera. Filtered to Camera entities; empty selection = legacy self-camera behavior.
            changed |= sourceCameraPicker.draw(
                "RenderTextureSourceCamera",
                data.sourceCameraName,
                data.sourceCamera,
                services::ComponentTypeId::Camera,
                "Source Camera",
                "Render from a separate Camera entity instead of this entity's own camera.\n"
                "Leave as None to render from this entity (legacy behavior).");

            if (changed)
            {
                events::scene::SetRenderTextureDataCommand cmd;
                cmd.entity = handle;
                cmd.renderTextureData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Preview");
            if (data.runtimeTextureId != rendertexture::INVALID_RENDER_TEXTURE_ID)
            {
                services::events::rendertexture::GetRenderTextureHandleQuery handleQuery;
                handleQuery.textureId = data.runtimeTextureId;
                auto texHandle = dispatcher.query(handleQuery);
                if (texHandle.imguiDescriptorSet)
                {
                    float aspect = (texHandle.height > 0)
                        ? static_cast<float>(texHandle.width) / static_cast<float>(texHandle.height)
                        : 1.0f;
                    float previewW = 256.0f;
                    float previewH = previewW / (aspect > 0.0f ? aspect : 1.0f);
                    ImGui::Image(texHandle.imguiDescriptorSet, ImVec2(previewW, previewH));
                }
                else
                {
                    ImGui::TextDisabled("Rendering...");
                }
            }
            else
            {
                ImGui::TextDisabled("Preview available in Play mode");
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeComponent)
        {
            events::scene::RemoveRenderTextureComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool RenderTextureDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##RenderTextureHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Render Texture");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveRenderTexture", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool RenderTextureDrawer::drawResolution(services::RenderTextureData& data)
    {
        bool changed = false;

        int w = static_cast<int>(data.width);
        int h = static_cast<int>(data.height);

        if (ImGui::DragInt("Width##RT", &w, 1.0f, 64, 4096))
        {
            data.width = static_cast<uint32_t>(std::max(64, w));
            changed = true;
        }

        if (ImGui::DragInt("Height##RT", &h, 1.0f, 64, 4096))
        {
            data.height = static_cast<uint32_t>(std::max(64, h));
            changed = true;
        }

        return changed;
    }

    bool RenderTextureDrawer::drawSettings(services::RenderTextureData& data)
    {
        bool changed = false;

        if (ImGui::Checkbox("Enabled##RT", &data.enabled))
        {
            changed = true;
        }

        if (ImGui::Checkbox("Render Shadows##RT", &data.renderShadows))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Sample the scene's shadow map in this render texture.\n"
                              "Off (default) = flat-lit. Useful for minimaps/tactical maps,\n"
                              "whose top-down view doesn't match the primary camera's shadow clipmap.");
        }

        const char* updateModes[] = {"Every Frame", "On Demand", "Fixed Interval"};
        int currentMode = static_cast<int>(data.updateMode);
        if (ImGui::Combo("Update Mode##RT", &currentMode, updateModes, IM_ARRAYSIZE(updateModes)))
        {
            data.updateMode = static_cast<uint8_t>(currentMode);
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Every Frame re-renders the whole scene each frame (expensive).\n"
                              "Prefer On Demand or Fixed Interval for mostly-static views.");
        }

        if (data.updateMode == 2) // FixedInterval
        {
            if (ImGui::DragFloat("Interval (s)##RT", &data.fixedIntervalSeconds, 0.001f, 0.001f, 10.0f, "%.3f"))
            {
                changed = true;
            }
        }

        if (ImGui::ColorEdit4("Clear Color##RT", &data.clearColor.x))
        {
            changed = true;
        }

        int priority = static_cast<int>(data.priority);
        if (ImGui::DragInt("Priority##RT", &priority, 1.0f, 0, 100))
        {
            data.priority = static_cast<uint32_t>(std::max(0, priority));
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Lower priority renders first");
        }

        return changed;
    }
}
