#pragma once
#include "imguiHandler/ImguiWindow.hpp"
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

        // Preview panel state
        float previewPanelWidth = 250.0f;
        bool isDraggingPreview = false; // Track if drag started in preview

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
    };
}
