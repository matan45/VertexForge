#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include <material/MaterialTypes.hpp>
#include <memory>
#include <string>

namespace editor
{
    class OrbitCamera;
}

namespace editor::graph
{
    class ShaderGraphEditor;
}

namespace windows
{
    class MaterialEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string materialPath;
        std::string windowTitle;
        std::shared_ptr<material::MaterialData> materialData;
        std::unique_ptr<editor::graph::ShaderGraphEditor> graphEditor;
        // Preview rendering - uses 'this' pointer as instanceId for service calls
        std::unique_ptr<editor::OrbitCamera> previewCamera;
        bool previewNeedsInit = true;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool showCompileError = false;
        std::string compileErrorMessage;

        // ORM Packing dialog state
        bool showOrmPackDialog = false;
        std::string ormAoPath;
        std::string ormRoughnessPath;
        std::string ormMetallicPath;
        std::string ormOutputPath;
        std::string ormPackError;
        float ormPackProgress = 0.0f;
        bool ormPackInProgress = false;

        // Preview panel state
        float previewPanelWidth = 250.0f;
        bool isDraggingPreview = false; // Track if drag started in preview

        // Async loading state
        services::IBLLoadingProgress iblLoadingProgress;
        bool useAsyncLoading = true;

    public:
        explicit MaterialEditorWindow(const std::string& materialPath);
        ~MaterialEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getMaterialPath() const { return materialPath; }

    private:
        void initEditor();
        void loadMaterial();
        void saveMaterial();
        void compileMaterial();

        void initPreview();
        void drawToolbar();
        void drawPreviewPanel();
        void drawGraphPanel();
        void drawParameterPanel();
        void drawPropertiesPanel();

        void handlePreviewInput();
        void updatePreviewMaterial(bool useCustomShader = false);

        void onGraphChanged();

        void drawOrmPackDialog();
        void packOrmTextures();

        // Async loading
        void updateAsyncLoading();
        void drawLoadingIndicator(float width, float height);
    };
}
