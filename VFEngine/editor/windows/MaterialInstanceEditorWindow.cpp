#include "MaterialInstanceEditorWindow.hpp"
#include "../camera/OrbitCamera.hpp"
#include <material/MaterialManager.hpp>
#include <material/MaterialGraphHelper.hpp>
#include <resource/ResourceManager.hpp>
#include "events/EventDispatcher.hpp"
#include "events/PreviewEvents.hpp"
#include "events/MaterialEvents.hpp"
#include "time/Timer.hpp"
#include "nfd/FileDialog.hpp"
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
        // Extract filename for window title
        fs::path path(instancePath);
        windowTitle = "Instance: " + path.stem().string();

        // Configure camera for material preview sphere
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
        instanceData = resource::ResourceManager::loadMaterialInstance(instancePath);
        if (!instanceData)
        {
            // Create default instance if load failed
            instanceData = std::make_shared<material::MaterialInstanceData>();
            instanceData->name = "New Instance";
        }
    }

    void MaterialInstanceEditorWindow::loadParent()
    {
        if (instanceData && !instanceData->parentMaterialPath.empty())
        {
            parentMaterial = resource::ResourceManager::loadMaterial(instanceData->parentMaterialPath);
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

            // Notify for cache invalidation in rendering systems
            events::material::MaterialFileSavedNotification notification;
            notification.materialPath = instancePath;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void MaterialInstanceEditorWindow::syncFromInstanceData()
    {
        if (!instanceData || !parentMaterial) return;

        // Initialize temp values from instance overrides or parent values
        // Albedo
        if (instanceData->albedoOverride.has_value())
        {
            tempAlbedo = *instanceData->albedoOverride;
            albedoOverrideEnabled = true;
        }
        else
        {
            tempAlbedo = parentPBR.albedo;
            albedoOverrideEnabled = false;
        }

        // Metallic
        if (instanceData->metallicOverride.has_value())
        {
            tempMetallic = *instanceData->metallicOverride;
            metallicOverrideEnabled = true;
        }
        else
        {
            tempMetallic = parentPBR.metallic;
            metallicOverrideEnabled = false;
        }

        // Roughness
        if (instanceData->roughnessOverride.has_value())
        {
            tempRoughness = *instanceData->roughnessOverride;
            roughnessOverrideEnabled = true;
        }
        else
        {
            tempRoughness = parentPBR.roughness;
            roughnessOverrideEnabled = false;
        }

        // AO
        if (instanceData->aoOverride.has_value())
        {
            tempAo = *instanceData->aoOverride;
            aoOverrideEnabled = true;
        }
        else
        {
            tempAo = parentPBR.ao;
            aoOverrideEnabled = false;
        }

        // Emission
        if (instanceData->emissionOverride.has_value())
        {
            tempEmission = *instanceData->emissionOverride;
            emissionOverrideEnabled = true;
        }
        else
        {
            tempEmission = parentPBR.emission;
            emissionOverrideEnabled = false;
        }

        // IBL
        if (instanceData->iblDiffuseOverride.has_value())
        {
            tempIblDiffuse = *instanceData->iblDiffuseOverride;
            iblDiffuseOverrideEnabled = true;
        }
        else
        {
            tempIblDiffuse = parentPBR.iblDiffuse;
            iblDiffuseOverrideEnabled = false;
        }

        if (instanceData->iblSpecularOverride.has_value())
        {
            tempIblSpecular = *instanceData->iblSpecularOverride;
            iblSpecularOverrideEnabled = true;
        }
        else
        {
            tempIblSpecular = parentPBR.iblSpecular;
            iblSpecularOverrideEnabled = false;
        }
    }

    void MaterialInstanceEditorWindow::syncToInstanceData()
    {
        if (!instanceData) return;

        // Apply or clear overrides based on toggle state
        if (albedoOverrideEnabled)
            instanceData->albedoOverride = tempAlbedo;
        else
            instanceData->albedoOverride.reset();

        if (metallicOverrideEnabled)
            instanceData->metallicOverride = tempMetallic;
        else
            instanceData->metallicOverride.reset();

        if (roughnessOverrideEnabled)
            instanceData->roughnessOverride = tempRoughness;
        else
            instanceData->roughnessOverride.reset();

        if (aoOverrideEnabled)
            instanceData->aoOverride = tempAo;
        else
            instanceData->aoOverride.reset();

        if (emissionOverrideEnabled)
            instanceData->emissionOverride = tempEmission;
        else
            instanceData->emissionOverride.reset();

        if (iblDiffuseOverrideEnabled)
            instanceData->iblDiffuseOverride = tempIblDiffuse;
        else
            instanceData->iblDiffuseOverride.reset();

        if (iblSpecularOverrideEnabled)
            instanceData->iblSpecularOverride = tempIblSpecular;
        else
            instanceData->iblSpecularOverride.reset();
    }

    void MaterialInstanceEditorWindow::initPreview()
    {
        if (!previewNeedsInit) return;

        // Initialize preview via service
        services::events::preview::InitMaterialPreviewCommand initCmd;
        initCmd.instanceId = services::PreviewInstanceId(this);
        events::EventDispatcher::instance().execute(initCmd);

        previewNeedsInit = false;

        // Now that preview is initialized, send material params
        updatePreviewMaterial();
    }

    void MaterialInstanceEditorWindow::draw()
    {
        // Handle cleanup when window is closing - must happen BEFORE any ImGui rendering
        // that might reference preview resources (like ImGui::Image with preview descriptor set)
        if (!isOpen && !previewNeedsInit)
        {
            services::events::preview::CleanUpMaterialPreviewCommand cleanupCmd;
            cleanupCmd.instanceId = services::PreviewInstanceId(this);
            events::EventDispatcher::instance().execute(cleanupCmd);
            previewNeedsInit = true; // Mark as cleaned up
            return; // Don't render anything - window is closing
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

        // Split into preview on left, properties on right
        float availWidth = ImGui::GetContentRegionAvail().x;
        float propertiesWidth = availWidth - previewPanelWidth - 8.0f;

        // Preview panel (left)
        ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, 0), true);
        drawPreviewPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        // Properties panel (right)
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
        if (instanceData && !instanceData->parentMaterialPath.empty())
        {
            fs::path parentPath(instanceData->parentMaterialPath);
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", parentPath.filename().string().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", instanceData->parentMaterialPath.c_str());
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

        // Albedo color
        bool changed = false;
        if (ImGui::Checkbox("##AlbedoOverride", &albedoOverrideEnabled))
        {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!albedoOverrideEnabled);
        if (ImGui::ColorEdit4("Albedo", &tempAlbedo.x))
        {
            changed = true;
        }
        ImGui::EndDisabled();

        // Metallic
        if (ImGui::Checkbox("##MetallicOverride", &metallicOverrideEnabled))
        {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!metallicOverrideEnabled);
        if (ImGui::SliderFloat("Metallic", &tempMetallic, 0.0f, 1.0f))
        {
            changed = true;
        }
        ImGui::EndDisabled();

        // Roughness
        if (ImGui::Checkbox("##RoughnessOverride", &roughnessOverrideEnabled))
        {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!roughnessOverrideEnabled);
        if (ImGui::SliderFloat("Roughness", &tempRoughness, 0.0f, 1.0f))
        {
            changed = true;
        }
        ImGui::EndDisabled();

        // AO
        if (ImGui::Checkbox("##AOOverride", &aoOverrideEnabled))
        {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!aoOverrideEnabled);
        if (ImGui::SliderFloat("AO", &tempAo, 0.0f, 1.0f))
        {
            changed = true;
        }
        ImGui::EndDisabled();

        // Emission
        if (ImGui::Checkbox("##EmissionOverride", &emissionOverrideEnabled))
        {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!emissionOverrideEnabled);
        if (ImGui::SliderFloat("Emission", &tempEmission, 0.0f, 10.0f))
        {
            changed = true;
        }
        ImGui::EndDisabled();

        if (changed)
        {
            isDirty = true;
            updatePreviewMaterial();
        }
    }

    bool MaterialInstanceEditorWindow::drawTextureOverrideSlot(
        const char* label,
        material::TextureSlot slot,
        const std::string& parentTexture)
    {
        bool changed = false;

        // Use slot index as unique ID scope
        ImGui::PushID(static_cast<int>(slot));

        bool hasOverride = instanceData && instanceData->isTextureOverridden(slot);
        std::string currentPath = hasOverride ? instanceData->getTextureOverride(slot) : parentTexture;

        // Override checkbox
        bool overrideEnabled = hasOverride;
        if (ImGui::Checkbox("##TexOverride", &overrideEnabled))
        {
            if (overrideEnabled)
            {
                // Enable override - use parent's texture or placeholder if empty
                std::string initialPath = parentTexture.empty() ? " " : parentTexture;
                instanceData->textureOverrides[slot] = initialPath;
            }
            else
            {
                // Clear override
                instanceData->textureOverrides.erase(slot);
            }
            changed = true;
        }

        ImGui::SameLine();

        // Texture path display
        fs::path texPath(currentPath);
        std::string displayName = currentPath.empty() || currentPath == " " ? "(none)" : texPath.filename().string();

        // Label
        ImGui::Text("%s:", label);
        ImGui::SameLine();

        // Highlight if this is an override
        if (overrideEnabled)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", displayName.c_str());
        }
        else
        {
            ImGui::TextDisabled("%s", displayName.c_str());
        }

        // Browse button
        ImGui::SameLine();
        if (ImGui::SmallButton("..."))
        {
            nfd::FileDialog fileDialog;
            std::vector<std::pair<std::wstring, std::wstring>> filters = {
                {L"Textures", L"*.vfImage;*.vfHdr;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr"}
            };
            std::string selectedPath = fileDialog.openFileDialog(filters);
            if (!selectedPath.empty())
            {
                instanceData->textureOverrides[slot] = selectedPath;
                changed = true;
            }
        }

        // Clear button (only if override is enabled)
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

        // Calculate square viewport size
        ImVec2 previewSize = ImGui::GetContentRegionAvail();
        float viewportSize = std::min(previewSize.x - 10.0f, previewSize.y - 20.0f);
        viewportSize = std::max(viewportSize, 100.0f); // Minimum size

        ImGui::BeginChild("PreviewViewport", ImVec2(viewportSize, viewportSize), true,
                          ImGuiWindowFlags_NoScrollbar);
        {
            // Update camera aspect ratio
            previewCamera->setAspectRatio(1.0f); // Square

            handlePreviewInput();

            auto& dispatcher = events::EventDispatcher::instance();

            // Update camera via service
            services::events::preview::UpdateMaterialCameraCommand cameraCmd;
            cameraCmd.instanceId = services::PreviewInstanceId(this);
            cameraCmd.view = previewCamera->getViewMatrix();
            cameraCmd.projection = previewCamera->getProjectionMatrix();
            cameraCmd.cameraPos = previewCamera->getPosition();
            cameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
            dispatcher.execute(cameraCmd);

            // Render and get texture handle
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

        // Track drag start/end
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDraggingPreview = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingPreview = false;
        }

        // Only process input when hovered
        if (!isHovered) return;

        ImGuiIO& io = ImGui::GetIO();

        // Scroll to zoom
        if (io.MouseWheel != 0.0f)
        {
            float zoomFactor = 1.0f - io.MouseWheel * previewCamera->zoomSensitivity * 0.1f;
            previewCamera->setDistance(previewCamera->distance * zoomFactor);
            previewCamera->updateMatrices();
        }

        // Left mouse drag to orbit - only if drag started in preview
        if (isDraggingPreview && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f)
            {
                previewCamera->yaw += delta.x * previewCamera->orbitSensitivity;
                previewCamera->pitch -= delta.y * previewCamera->orbitSensitivity;

                // Clamp pitch to avoid gimbal lock
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

        // Apply scalar values (use override if enabled, otherwise parent values)
        params.albedo = albedoOverrideEnabled ? tempAlbedo : parentPBR.albedo;
        params.metallic = metallicOverrideEnabled ? tempMetallic : parentPBR.metallic;
        params.roughness = roughnessOverrideEnabled ? tempRoughness : parentPBR.roughness;
        params.ao = aoOverrideEnabled ? tempAo : parentPBR.ao;
        params.emission = emissionOverrideEnabled ? tempEmission : parentPBR.emission;

        // Helper to get texture path (override or parent)
        auto getTexture = [this](material::TextureSlot slot, const std::string& parentPath) -> std::string
        {
            if (instanceData->isTextureOverridden(slot))
            {
                std::string path = instanceData->getTextureOverride(slot);
                if (path != " " && !path.empty()) return path;
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

        // Send params to preview service
        services::events::preview::SetMaterialParamsCommand cmd;
        cmd.instanceId = services::PreviewInstanceId(this);
        cmd.params = params;
        events::EventDispatcher::instance().execute(cmd);
    }
}
