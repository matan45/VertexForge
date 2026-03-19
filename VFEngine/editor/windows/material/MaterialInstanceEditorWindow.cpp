#include "MaterialInstanceEditorWindow.hpp"
#include "../../camera/OrbitCamera.hpp"
#include <material/MaterialManager.hpp>
#include <material/MaterialGraphHelper.hpp>
#include <resource/ResourceManager.hpp>
#include "events/EventDispatcher.hpp"
#include "events/render/PreviewEvents.hpp"
#include "events/render/MaterialEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "time/Timer.hpp"
#include "nfd/FileDialog.hpp"
#include <asset/AssetRef.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <filesystem>

namespace windows
{
    namespace fs = std::filesystem;

    MaterialInstanceEditorWindow::MaterialInstanceEditorWindow(const std::string& instancePath)
        : instancePath(instancePath)
          , previewCamera(std::make_unique<editor::OrbitCamera>())
    {
        fs::path path(instancePath);
        windowTitle = "Instance: " + path.stem().string();

        previewCamera->target = glm::vec3(0.0f);
        previewCamera->distance = 3.0f;
        previewCamera->yaw = 45.0f;
        previewCamera->pitch = 30.0f;
        previewCamera->minDistance = 1.5f;
        previewCamera->maxDistance = 10.0f;
    }

    MaterialInstanceEditorWindow::~MaterialInstanceEditorWindow() = default;


    void MaterialInstanceEditorWindow::initEditor()
    {
        loadInstance();
        if (instanceData)
        {
            loadParent();
            syncFromInstanceData();
            updatePreviewMaterial();
        }
        needsInit = false;
    }

    void MaterialInstanceEditorWindow::loadInstance()
    {
        if (material::MaterialManager::instance().reloadInstance(instancePath))
        {
            instanceData = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(instancePath));
        }

        if (!instanceData)
        {
            instanceData = std::make_shared<material::MaterialInstanceData>();
            instanceData->name = "New Instance";
        }
    }

    void MaterialInstanceEditorWindow::loadParent()
    {
        if (instanceData && instanceData->parentMaterialRef.isValid())
        {
            parentMaterial = resource::ResourceManager::loadMaterial(instanceData->parentMaterialRef);
            if (parentMaterial)
            {
                parentPBR = material::MaterialGraphHelper::extractPBRFromGraph(*parentMaterial);
            }
        }
    }

    void MaterialInstanceEditorWindow::saveInstance()
    {
        if (!instanceData) return;

        syncToInstanceData();

        auto& manager = material::MaterialManager::instance();
        if (manager.saveInstance(instancePath, *instanceData))
        {
            isDirty = false;
            updatePreviewMaterial();

            events::material::MaterialFileSavedNotification notification;
            notification.materialPath = instancePath;
            events::EventDispatcher::instance().publish(notification);

            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = instancePath;
            events::EventDispatcher::instance().publish(assetNotif);
        }
    }

    void MaterialInstanceEditorWindow::syncFromInstanceData()
    {
        if (!instanceData || !parentMaterial) return;

        auto syncScalar = [](const auto& override, auto& temp, bool& enabled, auto parentVal) {
            if (override.has_value()) { temp = *override; enabled = true; }
            else { temp = parentVal; enabled = false; }
        };

        syncScalar(instanceData->albedoOverride, tempAlbedo, albedoOverrideEnabled, parentPBR.albedo);
        syncScalar(instanceData->metallicOverride, tempMetallic, metallicOverrideEnabled, parentPBR.metallic);
        syncScalar(instanceData->roughnessOverride, tempRoughness, roughnessOverrideEnabled, parentPBR.roughness);
        syncScalar(instanceData->aoOverride, tempAo, aoOverrideEnabled, parentPBR.ao);
        syncScalar(instanceData->emissionOverride, tempEmission, emissionOverrideEnabled, parentPBR.emission);
        syncScalar(instanceData->iblDiffuseOverride, tempIblDiffuse, iblDiffuseOverrideEnabled, parentPBR.iblDiffuse);
        syncScalar(instanceData->iblSpecularOverride, tempIblSpecular, iblSpecularOverrideEnabled, parentPBR.iblSpecular);
    }

    void MaterialInstanceEditorWindow::syncToInstanceData()
    {
        if (!instanceData) return;

        auto applyOverride = [](bool enabled, auto& target, const auto& value) {
            if (enabled) target = value; else target.reset();
        };

        applyOverride(albedoOverrideEnabled, instanceData->albedoOverride, tempAlbedo);
        applyOverride(metallicOverrideEnabled, instanceData->metallicOverride, tempMetallic);
        applyOverride(roughnessOverrideEnabled, instanceData->roughnessOverride, tempRoughness);
        applyOverride(aoOverrideEnabled, instanceData->aoOverride, tempAo);
        applyOverride(emissionOverrideEnabled, instanceData->emissionOverride, tempEmission);
        applyOverride(iblDiffuseOverrideEnabled, instanceData->iblDiffuseOverride, tempIblDiffuse);
        applyOverride(iblSpecularOverrideEnabled, instanceData->iblSpecularOverride, tempIblSpecular);
    }

    void MaterialInstanceEditorWindow::initPreview()
    {
        if (!previewNeedsInit) return;

        services::events::preview::InitMaterialPreviewCommand initCmd;
        initCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(initCmd);

        previewNeedsInit = false;
        updatePreviewMaterial();
    }

    void MaterialInstanceEditorWindow::draw()
    {
        if (!isOpen && !previewNeedsInit)
        {
            services::events::preview::CleanUpMaterialPreviewCommand cleanupCmd;
            cleanupCmd.instanceId = services::PreviewInstanceId(this);
            events::EventDispatcher::instance().execute(cleanupCmd);
            previewNeedsInit = true;
            return;
        }

        if (needsInit)
        {
            initEditor();
        }

        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);

        std::string windowId = windowTitle + "###" + instancePath;
        if (!ImGui::Begin(windowId.c_str(), &isOpen, ImGuiWindowFlags_MenuBar))
        {
            ImGui::End();
            return;
        }

        drawToolbar();

        float availWidth = ImGui::GetContentRegionAvail().x;
        float propertiesWidth = availWidth - previewPanelWidth - 8.0f;

        ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, 0), true);
        drawPreviewPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("PropertiesPanel", ImVec2(propertiesWidth, 0), true);
        drawParentInfo();
        ImGui::Separator();
        drawScalarOverrides();
        ImGui::Separator();
        drawTextureOverrides();
        ImGui::EndChild();

        ImGui::End();
    }

    void MaterialInstanceEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::Button("Save"))
            {
                saveInstance();
            }

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "*");
            }

            if (ImGui::Button("Revert"))
            {
                loadInstance();
                if (instanceData)
                {
                    loadParent();
                    syncFromInstanceData();
                }
                isDirty = false;
            }

            ImGui::EndMenuBar();
        }
    }

    void MaterialInstanceEditorWindow::drawParentInfo()
    {
        ImGui::Text("Parent Material:");
        if (instanceData && instanceData->parentMaterialRef.isValid())
        {
            std::string parentMatPath = instanceData->parentMaterialRef.resolve();
            fs::path parentPath(parentMatPath);
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", parentPath.filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", parentMatPath.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "No parent material!");
        }
    }

    void MaterialInstanceEditorWindow::drawScalarOverrides()
    {
        ImGui::Text("Scalar Overrides");
        ImGui::Spacing();

        auto sliderOverride = [](const char* checkId, bool& enabled, const char* label, float& val, float mn, float mx) {
            bool changed = ImGui::Checkbox(checkId, &enabled);
            ImGui::SameLine();
            ImGui::BeginDisabled(!enabled);
            changed |= ImGui::SliderFloat(label, &val, mn, mx);
            ImGui::EndDisabled();
            return changed;
        };

        bool changed = false;
        changed |= ImGui::Checkbox("##AlbedoOverride", &albedoOverrideEnabled);
        ImGui::SameLine();
        ImGui::BeginDisabled(!albedoOverrideEnabled);
        changed |= ImGui::ColorEdit4("Albedo", &tempAlbedo.x);
        ImGui::EndDisabled();

        changed |= sliderOverride("##MetallicOverride", metallicOverrideEnabled, "Metallic", tempMetallic, 0.0f, 1.0f);
        changed |= sliderOverride("##RoughnessOverride", roughnessOverrideEnabled, "Roughness", tempRoughness, 0.0f, 1.0f);
        changed |= sliderOverride("##AOOverride", aoOverrideEnabled, "AO", tempAo, 0.0f, 1.0f);
        changed |= sliderOverride("##EmissionOverride", emissionOverrideEnabled, "Emission", tempEmission, 0.0f, 10.0f);

        if (changed) { isDirty = true; updatePreviewMaterial(); }
    }

    bool MaterialInstanceEditorWindow::drawTextureOverrideSlot(
        const char* label,
        material::TextureSlot slot,
        const std::string& parentTexture)
    {
        bool changed = false;

        ImGui::PushID(static_cast<int>(slot));

        bool hasOverride = instanceData && instanceData->isTextureOverridden(slot);
        auto overrideRef = hasOverride ? instanceData->getTextureOverride(slot) : asset::AssetRef::invalid();
        std::string currentPath = hasOverride && overrideRef.isValid() ? overrideRef.resolve() : parentTexture;

        bool overrideEnabled = hasOverride;
        if (ImGui::Checkbox("##TexOverride", &overrideEnabled))
        {
            if (overrideEnabled)
            {
                instanceData->textureOverrides[slot] = parentTexture.empty()
                    ? asset::AssetRef::invalid()
                    : asset::AssetRef::fromPath(parentTexture);
            }
            else
            {
                instanceData->textureOverrides.erase(slot);
            }
            changed = true;
        }

        ImGui::SameLine();

        fs::path texPath(currentPath);
        std::string displayName = currentPath.empty() || currentPath == " " ? "(none)" : texPath.filename().string();

        ImGui::Text("%s:", label);
        ImGui::SameLine();

        if (overrideEnabled)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", displayName.c_str());
        }
        else
        {
            ImGui::TextDisabled("%s", displayName.c_str());
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
        {
            nfd::FileDialog fileDialog;
            std::vector<std::pair<std::wstring, std::wstring>> filters = {
                {L"Textures", L"*.vfImage"}
            };
            std::string selectedPath = fileDialog.openFileDialog(filters);
            if (!selectedPath.empty())
            {
                instanceData->textureOverrides[slot] = asset::AssetRef::fromPath(selectedPath);
                changed = true;
            }
        }

        if (overrideEnabled)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                instanceData->textureOverrides.erase(slot);
                changed = true;
            }
        }

        if (!overrideEnabled && !parentTexture.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(from parent)");
        }

        ImGui::PopID();

        return changed;
    }

    void MaterialInstanceEditorWindow::drawTextureOverrides()
    {
        ImGui::Text("Texture Overrides");
        ImGui::Spacing();

        if (!instanceData) return;

        bool changed = false;
        changed |= drawTextureOverrideSlot("Albedo", material::TextureSlot::Albedo, parentPBR.albedoTexturePath);
        changed |= drawTextureOverrideSlot("Normal", material::TextureSlot::Normal, parentPBR.normalTexturePath);
        changed |= drawTextureOverrideSlot("ORM", material::TextureSlot::ORM, parentPBR.ormTexturePath);
        changed |= drawTextureOverrideSlot("Metallic", material::TextureSlot::Metallic, parentPBR.metallicTexturePath);
        changed |= drawTextureOverrideSlot("Roughness", material::TextureSlot::Roughness, parentPBR.roughnessTexturePath);
        changed |= drawTextureOverrideSlot("AO", material::TextureSlot::AO, parentPBR.aoTexturePath);
        changed |= drawTextureOverrideSlot("Emission", material::TextureSlot::Emission, parentPBR.emissionTexturePath);
        changed |= drawTextureOverrideSlot("Height", material::TextureSlot::Height, parentPBR.heightTexturePath);

        if (changed)
        {
            isDirty = true;
            updatePreviewMaterial();
        }
    }

    void MaterialInstanceEditorWindow::drawPreviewPanel()
    {
        ImGui::Text("Preview");
        ImGui::Separator();

        if (previewNeedsInit)
        {
            initPreview();
        }

        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        float viewportSize = std::min(previewSize.x - 10.0f, previewSize.y - 20.0f);
        viewportSize = std::max(viewportSize, 100.0f);

        ImGui::BeginChild("PreviewViewport", ImVec2(viewportSize, viewportSize), true,
                          ImGuiWindowFlags_NoScrollbar);
        {
            previewCamera->setAspectRatio(1.0f);

            handlePreviewInput();

            auto& dispatcher = events::EventDispatcher::instance();

            services::events::preview::UpdateMaterialCameraCommand cameraCmd;
            cameraCmd.instanceId = services::PreviewInstanceId(this);
            cameraCmd.view = previewCamera->getViewMatrix();
            cameraCmd.projection = previewCamera->getProjectionMatrix();
            cameraCmd.cameraPos = previewCamera->getPosition();
            cameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
            dispatcher.execute(cameraCmd);

            services::events::preview::RenderMaterialPreviewQuery renderQuery;
            renderQuery.instanceId = services::PreviewInstanceId(this);
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
    }

    void MaterialInstanceEditorWindow::handlePreviewInput()
    {
        if (!previewCamera) return;

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
            float zoomFactor = 1.0f - io.MouseWheel * previewCamera->zoomSensitivity * 0.1f;
            previewCamera->setDistance(previewCamera->distance * zoomFactor);
            previewCamera->updateMatrices();
        }

        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                previewCamera->yaw += delta.x * previewCamera->orbitSensitivity;
                previewCamera->pitch -= delta.y * previewCamera->orbitSensitivity;
                previewCamera->pitch = glm::clamp(previewCamera->pitch, -89.0f, 89.0f);
                previewCamera->updateMatrices();
            }
        }
    }

    void MaterialInstanceEditorWindow::updatePreviewMaterial()
    {
        if (!instanceData) return;

        services::MaterialPreviewParams params;
        params.useCustomShader = false;

        params.albedo = albedoOverrideEnabled ? tempAlbedo : parentPBR.albedo;
        params.metallic = metallicOverrideEnabled ? tempMetallic : parentPBR.metallic;
        params.roughness = roughnessOverrideEnabled ? tempRoughness : parentPBR.roughness;
        params.ao = aoOverrideEnabled ? tempAo : parentPBR.ao;
        params.emission = emissionOverrideEnabled ? tempEmission : parentPBR.emission;

        auto getTexture = [this](material::TextureSlot slot, const std::string& parentPath) -> std::string
        {
            if (instanceData->isTextureOverridden(slot))
            {
                auto ref = instanceData->getTextureOverride(slot);
                if (ref.isValid()) return ref.resolve();
            }
            return parentPath;
        };

        params.albedoTexturePath = getTexture(material::TextureSlot::Albedo, parentPBR.albedoTexturePath);
        params.normalTexturePath = getTexture(material::TextureSlot::Normal, parentPBR.normalTexturePath);
        params.ormTexturePath = getTexture(material::TextureSlot::ORM, parentPBR.ormTexturePath);
        params.metallicTexturePath = getTexture(material::TextureSlot::Metallic, parentPBR.metallicTexturePath);
        params.roughnessTexturePath = getTexture(material::TextureSlot::Roughness, parentPBR.roughnessTexturePath);
        params.aoTexturePath = getTexture(material::TextureSlot::AO, parentPBR.aoTexturePath);
        params.emissionTexturePath = getTexture(material::TextureSlot::Emission, parentPBR.emissionTexturePath);
        params.heightTexturePath = getTexture(material::TextureSlot::Height, parentPBR.heightTexturePath);

        services::events::preview::SetMaterialParamsCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(this);
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }
}
