#include "AnimationViewport.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "events/EventDispatcher.hpp"
#include "events/AnimationPreviewEvents.hpp"
#include "imgui.h"
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace windows::animation
{
    void AnimationViewport::draw(const ViewportDrawContext& ctx)
    {
        ImGui::Text("3D Preview");
        ImGui::SameLine();

        float panStep = ctx.camera ? ctx.camera->distance * 0.1f : 0.5f;
        if (ImGui::ArrowButton("##CamUp", ImGuiDir_Up))
        {
            if (ctx.camera)
            {
                ctx.camera->target.y += panStep;
                ctx.camera->updateMatrices();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move camera up");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##CamDown", ImGuiDir_Down))
        {
            if (ctx.camera)
            {
                ctx.camera->target.y -= panStep;
                ctx.camera->updateMatrices();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move camera down");

        ImGui::Separator();

        ImVec2 availSize = ImGui::GetContentRegionAvail();

        if (!ctx.meshLoadedInPreview || !ctx.animationLoadedInPreview)
        {
            ImVec2 windowPos = ImGui::GetCursorScreenPos();
            drawPlaceholder(windowPos, availSize);
            return;
        }

        if (availSize.x <= 0 || availSize.y <= 0) return;

        ctx.camera->setAspectRatio(availSize.x / availSize.y);
        handlePreviewInput(ctx.camera, ctx.isDraggingPreview);

        if (ctx.isPlaying)
        {
            float deltaTime = static_cast<float>(ImGui::GetIO().DeltaTime);
            services::events::animpreview::UpdateAnimationPreviewCommand updateCmd;
            updateCmd.instanceId = ctx.instanceId;
            updateCmd.deltaTime = deltaTime;
            events::EventDispatcher::instance().execute(updateCmd);
        }

        glm::mat4 model = glm::mat4(1.0f);
        services::AnimationPreviewParams params;
        params.modelMatrix = model;
        params.albedo = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        params.metallic = 0.0f;
        params.roughness = 0.5f;

        services::events::animpreview::SetAnimationPreviewParamsCommand paramsCmd;
        paramsCmd.instanceId = ctx.instanceId;
        paramsCmd.params = params;
        events::EventDispatcher::instance().execute(paramsCmd);

        services::events::animpreview::UpdateAnimationCameraCommand cameraCmd;
        cameraCmd.instanceId = ctx.instanceId;
        cameraCmd.view = ctx.camera->getViewMatrix();
        cameraCmd.projection = ctx.camera->getProjectionMatrix();
        cameraCmd.cameraPos = ctx.camera->getPosition();
        events::EventDispatcher::instance().execute(cameraCmd);

        services::events::animpreview::RenderAnimationPreviewQuery renderQuery;
        renderQuery.instanceId = ctx.instanceId;
        auto textureHandle = events::EventDispatcher::instance().query(renderQuery);

        if (textureHandle.imguiDescriptorSet)
        {
            ImVec2 viewportPos = ImGui::GetCursorScreenPos();
            ImGui::Image(textureHandle.imguiDescriptorSet, availSize);

            if (ctx.showBoneVisualization && !ctx.evaluatedBones.empty())
            {
                drawBoneVisualization(viewportPos, availSize, ctx.evaluatedBones, ctx.selectedChannel, ctx.camera);
            }

            if (ctx.showColliderOverlay && ctx.physicsConfig && ctx.boneNameToIndex && !ctx.evaluatedBones.empty())
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                colliderOverlay.draw(drawList, viewportPos, availSize, ctx.camera,
                                     ctx.evaluatedBones, *ctx.physicsConfig, *ctx.boneNameToIndex, ctx.selectedChannel);
            }

            if (ctx.showSocketVisualization && ctx.socketDefinitions && !ctx.socketDefinitions->empty() && !ctx.evaluatedBones.empty())
            {
                drawSocketVisualization(viewportPos, availSize, ctx);
            }
        }
        else
        {
            ImGui::Dummy(availSize);
        }
    }

    void AnimationViewport::handlePreviewInput(editor::OrbitCamera* camera, bool& isDraggingPreview)
    {
        bool isHovered = ImGui::IsWindowHovered();

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDraggingPreview = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingPreview = false;
        }

        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * camera->zoomSensitivity * 0.1f;
            camera->setDistance(camera->distance * zoomFactor);
            camera->updateMatrices();
        }

        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                camera->yaw += delta.x * camera->orbitSensitivity;
                camera->pitch -= delta.y * camera->orbitSensitivity;
                camera->pitch = glm::clamp(camera->pitch, -89.0f, 89.0f);
                camera->updateMatrices();
            }
        }
    }

    void AnimationViewport::drawPlaceholder(const ImVec2& windowPos, const ImVec2& availSize)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
            IM_COL32(25, 25, 30, 255)
        );

        float gridSpacing = 30.0f;
        ImU32 gridColor = IM_COL32(50, 50, 55, 255);

        for (float x = windowPos.x; x < windowPos.x + availSize.x; x += gridSpacing)
        {
            drawList->AddLine(
                ImVec2(x, windowPos.y),
                ImVec2(x, windowPos.y + availSize.y),
                gridColor
            );
        }
        for (float y = windowPos.y; y < windowPos.y + availSize.y; y += gridSpacing)
        {
            drawList->AddLine(
                ImVec2(windowPos.x, y),
                ImVec2(windowPos.x + availSize.x, y),
                gridColor
            );
        }

        const char* placeholderText = "3D Preview Unavailable";
        const char* subText = "Mesh required for 3D visualization";

        ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
        ImVec2 subTextSize = ImGui::CalcTextSize(subText);

        float centerX = windowPos.x + availSize.x * 0.5f;
        float centerY = windowPos.y + availSize.y * 0.5f;

        ImVec2 textPos(centerX - textSize.x * 0.5f, centerY - 10.0f);
        ImVec2 subTextPos(centerX - subTextSize.x * 0.5f, centerY + 15.0f);

        drawList->AddText(textPos, IM_COL32(180, 180, 180, 255), placeholderText);
        drawList->AddText(subTextPos, IM_COL32(120, 120, 130, 255), subText);

        ImGui::Dummy(availSize);
    }

    void AnimationViewport::drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize,
                                                   const std::vector<services::EvaluatedBoneInfo>& evaluatedBones,
                                                   int selectedChannel,
                                                   const editor::OrbitCamera* camera)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImU32 boneColor = IM_COL32(255, 255, 0, 255);
        ImU32 jointColor = IM_COL32(255, 100, 100, 255);
        ImU32 selectedColor = IM_COL32(0, 255, 255, 255);

        for (size_t i = 0; i < evaluatedBones.size(); ++i)
        {
            const auto& bone = evaluatedBones[i];
            glm::vec3 boneWorldPos = bone.skinnedPosition;
            ImVec2 screenPos = worldToScreen(boneWorldPos, viewportPos, viewportSize, camera);

            if (screenPos.x < viewportPos.x - 100 || screenPos.x > viewportPos.x + viewportSize.x + 100 ||
                screenPos.y < viewportPos.y - 100 || screenPos.y > viewportPos.y + viewportSize.y + 100)
            {
                continue;
            }

            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(evaluatedBones.size()))
            {
                const auto& parentBone = evaluatedBones[bone.parentIndex];
                glm::vec3 parentWorldPos = parentBone.skinnedPosition;

                ImVec2 parentScreenPos = worldToScreen(parentWorldPos, viewportPos, viewportSize, camera);

                ImU32 lineColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : boneColor;
                drawList->AddLine(parentScreenPos, screenPos, lineColor, 2.0f);
            }

            float jointRadius = (static_cast<int>(i) == selectedChannel) ? 6.0f : 4.0f;
            ImU32 circleColor = (static_cast<int>(i) == selectedChannel) ? selectedColor : jointColor;
            drawList->AddCircleFilled(screenPos, jointRadius, circleColor);

            if (i < 10)
            {
                char label[8];
                snprintf(label, sizeof(label), "%zu", i);
                drawList->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(255, 255, 255, 255), label);
            }
        }
    }

    void AnimationViewport::drawSocketVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize,
                                                     const ViewportDrawContext& ctx)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImU32 socketColor = IM_COL32(0, 200, 100, 255);
        ImU32 socketOutline = IM_COL32(255, 255, 255, 200);

        for (const auto& socket : *ctx.socketDefinitions)
        {
            int32_t boneIdx = socket.boneIndex;
            if (boneIdx < 0 || boneIdx >= static_cast<int32_t>(ctx.evaluatedBones.size()))
                continue;

            glm::vec3 bonePos = ctx.evaluatedBones[boneIdx].skinnedPosition;
            glm::vec3 socketPos = bonePos + socket.localPosition;

            ImVec2 screenPos = worldToScreen(socketPos, viewportPos, viewportSize, ctx.camera);

            if (screenPos.x < viewportPos.x - 100 || screenPos.x > viewportPos.x + viewportSize.x + 100 ||
                screenPos.y < viewportPos.y - 100 || screenPos.y > viewportPos.y + viewportSize.y + 100)
            {
                continue;
            }

            // Draw diamond shape for sockets
            float size = 6.0f;
            ImVec2 top(screenPos.x, screenPos.y - size);
            ImVec2 right(screenPos.x + size, screenPos.y);
            ImVec2 bottom(screenPos.x, screenPos.y + size);
            ImVec2 left(screenPos.x - size, screenPos.y);

            drawList->AddQuadFilled(top, right, bottom, left, socketColor);
            drawList->AddQuad(top, right, bottom, left, socketOutline, 1.5f);

            // Label
            drawList->AddText(ImVec2(screenPos.x + 10, screenPos.y - 8),
                              IM_COL32(0, 220, 120, 255), socket.name.c_str());
        }
    }

    ImVec2 AnimationViewport::worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos,
                                             const ImVec2& viewportSize, const editor::OrbitCamera* camera) const
    {
        glm::vec4 clipPos = camera->getProjectionMatrix() * camera->getViewMatrix() * glm::vec4(worldPos, 1.0f);

        if (std::abs(clipPos.w) < 0.0001f)
        {
            return ImVec2(-10000, -10000);
        }

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

        float screenX = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        float screenY = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;

        return ImVec2(screenX, screenY);
    }
}
