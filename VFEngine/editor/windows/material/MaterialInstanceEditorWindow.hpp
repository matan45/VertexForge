#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <material/MaterialInstanceTypes.hpp>
#include <material/MaterialTypes.hpp>
#include <material/MaterialGraphHelper.hpp>
#include <memory>
#include <string>

namespace editor
{
    class OrbitCamera;
}

namespace windows
{
    class MaterialInstanceEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string instancePath;
        std::string windowTitle;
        std::shared_ptr<material::MaterialInstanceData> instanceData;
        std::shared_ptr<material::MaterialData> parentMaterial;
        material::ExtractedParentPBR parentPBR;

        std::unique_ptr<editor::OrbitCamera> previewCamera;
        bool previewNeedsInit = true;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;

        float previewPanelWidth = 250.0f;
        bool isDraggingPreview = false;

        glm::vec4 tempAlbedo{1.0f};
        float tempMetallic = 0.0f;
        float tempRoughness = 0.5f;
        float tempAo = 1.0f;
        float tempEmission = 0.0f;
        float tempIblDiffuse = 1.0f;
        float tempIblSpecular = 0.5f;

        bool albedoOverrideEnabled = false;
        bool metallicOverrideEnabled = false;
        bool roughnessOverrideEnabled = false;
        bool aoOverrideEnabled = false;
        bool emissionOverrideEnabled = false;
        bool iblDiffuseOverrideEnabled = false;
        bool iblSpecularOverrideEnabled = false;

    public:
        explicit MaterialInstanceEditorWindow(const std::string& instancePath);
        ~MaterialInstanceEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getInstancePath() const { return instancePath; }

    private:
        void initEditor();
        void loadInstance();
        void loadParent();
        void saveInstance();

        void initPreview();
        void drawToolbar();
        void drawParentInfo();
        void drawScalarOverrides();
        void drawTextureOverrides();
        void drawPreviewPanel();

        void handlePreviewInput();
        void updatePreviewMaterial();

        void syncFromInstanceData();
        void syncToInstanceData();

        bool drawTextureOverrideSlot(
            const char* label,
            material::TextureSlot slot,
            const std::string& parentTexture);
    };
}
