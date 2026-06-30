#include "VFXPreviewPanel.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "../preview/PreviewInputHandler.hpp"
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
        preview::PreviewInputHandler::handleInput(camera.get(), isDraggingOrbit, isDraggingPan);
    }

    void VFXPreviewPanel::setParams(const services::VFXPreviewParams& params)
    {
        services::events::vfxpreview::SetVFXParamsCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }

    void VFXPreviewPanel::setSequence(const services::VFXSequencePreviewDesc& desc)
    {
        services::events::vfxpreview::SetVFXSequencePreviewCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.desc = desc;
        events::EventDispatcher::instance().execute(cmd);
    }

    void VFXPreviewPanel::seek(float seconds)
    {
        services::events::vfxpreview::SeekVFXPreviewCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.seconds = seconds;
        events::EventDispatcher::instance().execute(cmd);
    }

    void VFXPreviewPanel::setRate(float rate)
    {
        services::events::vfxpreview::SetVFXPreviewRateCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.rate = rate;
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
        // Reserve 100px for the playback controls below the viewport.
        float viewportWidth = std::max(previewSize.x, 100.0f);
        float viewportHeight = std::max(previewSize.y - 100.0f, 100.0f);

        ImGui::BeginChild("VFXPreviewViewport", ImVec2(viewportWidth, viewportHeight), true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 imageSize = ImGui::GetContentRegionAvail();
            imageSize.x = std::max(imageSize.x, 1.0f);
            imageSize.y = std::max(imageSize.y, 1.0f);
            camera->setAspectRatio(imageSize.x / imageSize.y);

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
                ImGui::Image(textureHandle.imguiDescriptorSet, imageSize);
            }
            else
            {
                ImGui::TextDisabled("Initializing preview...");
            }
        }
        ImGui::EndChild();

        if (builtInControls)
            drawPlaybackControls();
    }
}
