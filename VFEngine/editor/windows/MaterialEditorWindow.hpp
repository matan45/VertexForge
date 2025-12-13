#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <material/MaterialTypes.hpp>
#include <memory>
#include <string>

namespace editor::graph {
    class ShaderGraphEditor;
}

namespace windows {

    class MaterialEditorWindow : public controllers::imguiHandler::ImguiWindow {
    public:
        explicit MaterialEditorWindow(const std::string& materialPath);
        ~MaterialEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getMaterialPath() const { return materialPath; }

    private:
        std::string materialPath;
        std::string windowTitle;
        std::shared_ptr<material::MaterialData> materialData;
        std::unique_ptr<editor::graph::ShaderGraphEditor> graphEditor;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool showCompileError = false;
        std::string compileErrorMessage;

        // Preview panel state
        float previewPanelWidth = 250.0f;

        void initEditor();
        void loadMaterial();
        void saveMaterial();
        void compileMaterial();

        void drawToolbar();
        void drawPreviewPanel(float height);
        void drawGraphPanel(float width, float height);
        void drawParameterPanel();
        void drawPropertiesPanel();

        void onGraphChanged();
    };

}
