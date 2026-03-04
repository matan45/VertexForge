#include "VFXPreviewPanel.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXPreviewEvents.hpp>
#include <time/Timer.hpp>
#include "imgui.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace editor::vfxeditor
{
    VFXPreviewPanel::VFXPreviewPanel(void* instanceId)
        : instanceId(instanceId)
        , camera(std::make_unique<OrbitCamera>())
    {
        camera->target = glm::vec3(0.0f, 1.0f, 0.0f);
        camera->distance = 8.0f;
        camera->yaw = 45.0f;
        camera->pitch = 20.0f;
        camera->minDistance = 2.0f;
        camera->maxDistance = 50.0f;
    }

    VFXPreviewPanel::~VFXPreviewPanel()
    {
        cleanup();
    }

    void VFXPreviewPanel::init()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        services::events::vfxpreview::InitVFXPreviewCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        dispatcher.execute(cmd);

        camera->updateMatrices();
        needsInit = false;
        lastFrameTime = static_cast<float>(engineTime::Timer::getElapsedTime());
    }

    void VFXPreviewPanel::cleanup()
    {
        if (!needsInit)
        {
            services::events::vfxpreview::CleanUpVFXPreviewCommand cleanupCmd;
            cleanupCmd.instanceId = services::PreviewInstanceId(instanceId);
            events::EventDispatcher::instance().execute(cleanupCmd);
            needsInit = true;
        }
    }

    void VFXPreviewPanel::handleInput()
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

    void VFXPreviewPanel::setParams(const services::VFXPreviewParams& params)
    {
        services::events::vfxpreview::SetVFXParamsCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }

    void VFXPreviewPanel::play()
    {
        services::events::vfxpreview::PlayVFXCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        events::EventDispatcher::instance().execute(cmd);
        isPlaying = true;
    }

    void VFXPreviewPanel::pause()
    {
        services::events::vfxpreview::PauseVFXCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        events::EventDispatcher::instance().execute(cmd);
        isPlaying = false;
    }

    void VFXPreviewPanel::stop()
    {
        services::events::vfxpreview::StopVFXCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        events::EventDispatcher::instance().execute(cmd);
        isPlaying = false;
    }

    void VFXPreviewPanel::drawPlaybackControls()
    {
        ImGui::Separator();

        float buttonWidth = 60.0f;
        float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
        float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (isPlaying)
        {
            if (ImGui::Button("Pause", ImVec2(buttonWidth, 0)))
            {
                pause();
            }
        }
        else
        {
            if (ImGui::Button("Play", ImVec2(buttonWidth, 0)))
            {
                play();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(buttonWidth, 0)))
        {
            stop();
        }

        ImGui::SameLine();
        if (ImGui::Button("Restart", ImVec2(buttonWidth, 0)))
        {
            stop();
            play();
        }
    }

    void VFXPreviewPanel::draw()
    {
        ImGui::Text("VFX Preview");
        ImGui::Separator();

        if (needsInit)
        {
            init();
        }

        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        float viewportSize = std::min(previewSize.x - 10.0f, previewSize.y - 100.0f);
        viewportSize = std::max(viewportSize, 100.0f);

        ImGui::BeginChild("VFXPreviewViewport", ImVec2(viewportSize, viewportSize), true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            camera->setAspectRatio(1.0f);

            handleInput();

            auto& dispatcher = events::EventDispatcher::instance();

            float currentTime = static_cast<float>(engineTime::Timer::getElapsedTime());
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            services::events::vfxpreview::UpdateVFXCameraCommand cameraCmd;
            cameraCmd.instanceId = services::PreviewInstanceId(instanceId);
            cameraCmd.view = camera->getViewMatrix();
            cameraCmd.projection = camera->getProjectionMatrix();
            cameraCmd.cameraPos = camera->getPosition();
            cameraCmd.time = currentTime;
            dispatcher.execute(cameraCmd);

            if (isPlaying)
            {
                services::events::vfxpreview::UpdateVFXSimulationCommand simCmd;
                simCmd.instanceId = services::PreviewInstanceId(instanceId);
                simCmd.deltaTime = deltaTime;
                dispatcher.execute(simCmd);
            }

            services::events::vfxpreview::RenderVFXPreviewQuery renderQuery;
            renderQuery.instanceId = services::PreviewInstanceId(instanceId);
            auto textureHandle = dispatcher.query(renderQuery);

            if (textureHandle.imguiDescriptorSet)
            {
                ImVec2 size(viewportSize - 16, viewportSize - 16);
                ImGui::Image(textureHandle.imguiDescriptorSet, size);
            }
            else
            {
                ImGui::TextDisabled("Initializing preview...");
            }
        }
        ImGui::EndChild();

        drawPlaybackControls();
    }
}
