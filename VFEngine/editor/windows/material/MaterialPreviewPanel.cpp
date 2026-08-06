#include "MaterialPreviewPanel.hpp"
#include "MaterialGraphEvaluator.hpp"
#include "../../camera/OrbitCamera.hpp"
#include "../preview/PreviewInputHandler.hpp"
#include "../preview/PreviewToolbar.hpp"
#include <events/EventDispatcher.hpp>
#include <events/render/PreviewEvents.hpp>
#include <material/ToonProfileManager.hpp>
#include <nfd/FileDialog.hpp>
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
        // Reserve 100px for the blend-mode controls below the viewport.
        float viewportWidth = std::max(previewSize.x, 100.0f);
        float viewportHeight = std::max(previewSize.y - 100.0f, 100.0f);

        ImGui::BeginChild("PreviewViewport", ImVec2(viewportWidth, viewportHeight), true,
                         ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 imageSize = ImGui::GetContentRegionAvail();
            imageSize.x = std::max(imageSize.x, 1.0f);
            imageSize.y = std::max(imageSize.y, 1.0f);
            camera->setAspectRatio(imageSize.x / imageSize.y);

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
                ImGui::Image(textureHandle.imguiDescriptorSet, imageSize);
            } else {
                ImGui::TextDisabled("Initializing preview...");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        if (materialData && showSettings) {
            // VK-1493: shading model + toon profile picker. Toggling to/from Toon or changing
            // the profile changes the compiled shader, so flag a recompile (user clicks Compile).
            ImGui::Text("Shading Model");
            const char* shadingModels[] = { "Default Lit", "Unlit", "Toon" };
            int shadingModel = static_cast<int>(materialData->shadingModel);
            if (ImGui::Combo("##ShadingModel", &shadingModel, shadingModels, IM_ARRAYSIZE(shadingModels))) {
                materialData->shadingModel = static_cast<::material::ShadingModel>(shadingModel);
                materialData->needsRecompile = true;
                if (onBlendModeChanged) {
                    onBlendModeChanged();
                }
            }

            if (materialData->shadingModel == ::material::ShadingModel::Toon) {
                ImGui::Text("Toon Profile");
                ImGui::TextWrapped("%s", materialData->toonProfile.empty()
                                             ? "(built-in default)" : materialData->toonProfile.c_str());
                if (ImGui::Button("Browse##ToonProfile")) {
                    nfd::FileDialog fd;
                    std::string picked = fd.openFileDialog(
                        {{L"Toon Profile (*.vfToonProfile)", L"*.vfToonProfile"}});
                    if (!picked.empty()) {
                        materialData->toonProfile = picked;
                        if (auto p = ::material::ToonProfileManager::instance().getOrLoad(picked)) {
                            materialData->toonProfileValues = *p;
                        }
                        materialData->needsRecompile = true;
                        if (onBlendModeChanged) {
                            onBlendModeChanged();
                        }
                    }
                }
                if (!materialData->toonProfile.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ToonProfile")) {
                        materialData->toonProfile.clear();
                        materialData->needsRecompile = true;
                        if (onBlendModeChanged) {
                            onBlendModeChanged();
                        }
                    }
                }
            }

            // VK-1580: foliage wind gate. Render-time flag only (no shader recompile); the
            // callback refreshes the render-side material cache so the FoliageWind flag re-packs.
            ImGui::Separator();
            if (ImGui::Checkbox("Foliage Wind", &materialData->receiveWind)) {
                if (onBlendModeChanged) {
                    onBlendModeChanged();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sway this material's meshes with the global scene wind\n(trees & bushes). Uses the shared grass WindSystem.");
            }

            // VK-1620: mesh-into-terrain blending. Render-time flag like the wind gate above — the
            // same callback re-packs it into ObjectFlags — but this one has parameters, shown
            // indented only while it is on (the TerrainMaterialEditorWindow hex-tiling idiom).
            if (ImGui::Checkbox("Blend To Terrain", &materialData->blendToTerrain)) {
                if (onBlendModeChanged) {
                    onBlendModeChanged();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Melt this material's meshes into the terrain at their base:\n"
                                  "pixels within the band below blend toward the terrain surface.\n"
                                  "Requires Render Settings > Virtual Texturing > World Height Plane.");
            }
            if (materialData->blendToTerrain) {
                ImGui::Indent(8.0f);
                if (ImGui::DragFloat("Blend Height##TerrainBlend", &materialData->terrainBlendBand,
                                     0.01f, ::material::MIN_TERRAIN_BLEND_BAND,
                                     ::material::MAX_TERRAIN_BLEND_BAND, "%.2f m")) {
                    materialData->terrainBlendBand =
                        ::material::clampTerrainBlendBand(materialData->terrainBlendBand);
                    if (onBlendModeChanged) { onBlendModeChanged(); }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How far above the terrain surface the blend reaches.\n"
                                      "A pebble wants a few centimetres; a cliff base wants metres.");
                }
                if (ImGui::DragFloat("Blend Contrast##TerrainBlend", &materialData->terrainBlendContrast,
                                     0.05f, ::material::MIN_TERRAIN_BLEND_CONTRAST,
                                     ::material::MAX_TERRAIN_BLEND_CONTRAST, "%.2f")) {
                    materialData->terrainBlendContrast =
                        ::material::clampTerrainBlendContrast(materialData->terrainBlendContrast);
                    if (onBlendModeChanged) { onBlendModeChanged(); }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Shapes the falloff. 1 = linear-ish across the whole band;\n"
                                      "higher pulls the blend down so only the contact line takes\n"
                                      "terrain colour.");
                }
                ImGui::Unindent(8.0f);
            }

            ImGui::Separator();
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
