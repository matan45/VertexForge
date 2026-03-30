#include "MaterialPreviewPanel.hpp"
#include "MaterialGraphEvaluator.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "../preview/PreviewInputHandler.hpp"
#include "../preview/PreviewToolbar.hpp"
#include <events/EventDispatcher.hpp>
#include <events/render/PreviewEvents.hpp>
#include <time/Timer.hpp>
#include "imgui.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace editor::materialeditor
{
    MaterialPreviewPanel::MaterialPreviewPanel(void* instanceId)
        : instanceId(instanceId)
        , camera(std::make_unique<OrbitCamera>())
    {
        camera->target = glm::vec3(0.0f);
        camera->distance = 3.0f;
        camera->yaw = 45.0f;
        camera->pitch = 30.0f;
        camera->minDistance = 1.5f;
        camera->maxDistance = 10.0f;
    }

    MaterialPreviewPanel::~MaterialPreviewPanel()
    {
        cleanup();
    }

    void MaterialPreviewPanel::init()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        services::events::preview::InitMaterialPreviewCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        dispatcher.execute(cmd);

        camera->updateMatrices();
        needsInit = false;
    }

    void MaterialPreviewPanel::cleanup()
    {
        if (!needsInit) {
            services::events::preview::CleanUpMaterialPreviewCommand cleanupCmd;
            cleanupCmd.instanceId = services::PreviewInstanceId(instanceId);
            events::EventDispatcher::instance().execute(cleanupCmd);
            needsInit = true;
        }
    }

    void MaterialPreviewPanel::handleInput()
    {
        preview::PreviewInputHandler::handleInput(camera.get(), isDraggingOrbit, isDraggingPan);
    }

    void MaterialPreviewPanel::updateFromGraph(
        std::shared_ptr<::material::MaterialData> materialData,
        const std::string& materialPath,
        bool useCustomShader)
    {
        if (!materialData) return;

        auto result = MaterialGraphEvaluator::evaluate(materialData->graph);
        auto params = MaterialGraphEvaluator::toPreviewParams(result, materialPath, materialData, useCustomShader);

        services::events::preview::SetMaterialParamsCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(instanceId);
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }

    void MaterialPreviewPanel::draw(
        std::shared_ptr<::material::MaterialData> materialData,
        const std::string& materialPath)
    {
        ImGui::Text("Preview");
        ImGui::Separator();

        if (needsInit) {
            init();
        }

        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        float viewportSize = std::min(previewSize.x - 10.0f, previewSize.y - 100.0f);
        viewportSize = std::max(viewportSize, 100.0f);

        ImGui::BeginChild("PreviewViewport", ImVec2(viewportSize, viewportSize), true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            camera->setAspectRatio(1.0f);

            handleInput();

            auto& dispatcher = events::EventDispatcher::instance();

            services::events::preview::UpdateMaterialCameraCommand cameraCmd;
            cameraCmd.instanceId = services::PreviewInstanceId(instanceId);
            cameraCmd.view = camera->getViewMatrix();
            cameraCmd.projection = camera->getProjectionMatrix();
            cameraCmd.cameraPos = camera->getPosition();
            cameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
            dispatcher.execute(cameraCmd);

            services::events::preview::RenderMaterialPreviewQuery renderQuery;
            renderQuery.instanceId = services::PreviewInstanceId(instanceId);
            auto textureHandle = dispatcher.query(renderQuery);

            services::events::preview::GetMaterialShaderErrorQuery errorQuery;
            errorQuery.instanceId = services::PreviewInstanceId(instanceId);
            std::string shaderError = dispatcher.query(errorQuery);
            if (!shaderError.empty()) {
                lastShaderError = "SPIR-V: " + shaderError;
            }

            if (textureHandle.imguiDescriptorSet) {
                ImVec2 size(viewportSize - 16, viewportSize - 16);
                ImGui::Image(textureHandle.imguiDescriptorSet, size);
            } else {
                ImGui::TextDisabled("Initializing preview...");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        if (materialData) {
            ImGui::Text("Blend Mode");
            const char* blendModes[] = { "Opaque", "Masked", "Translucent", "Additive", "Multiply" };
            int blendMode = static_cast<int>(materialData->blendMode);
            if (ImGui::Combo("##BlendMode", &blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                materialData->blendMode = static_cast<::material::BlendMode>(blendMode);
                if (onBlendModeChanged) {
                    onBlendModeChanged();
                }
            }

            if (materialData->blendMode == ::material::BlendMode::Translucent) {
                ImGui::Text("Opacity");
                if (ImGui::SliderFloat("##Opacity", &materialData->opacity, 0.0f, 1.0f, "%.2f")) {
                    if (onBlendModeChanged) {
                        onBlendModeChanged();
                    }
                }
            }

            if (materialData->blendMode == ::material::BlendMode::Masked) {
                ImGui::Text("Alpha Cutoff");
                if (ImGui::SliderFloat("##AlphaCutoff", &materialData->alphaCutoff, 0.0f, 1.0f, "%.2f")) {
                    if (onBlendModeChanged) {
                        onBlendModeChanged();
                    }
                }
            }
        }
    }
}
