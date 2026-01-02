#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <material/MaterialTypes.hpp>
#include <memory>
#include <string>

namespace editor::graph
{
    class ShaderGraphEditor;
}

namespace editor::materialeditor
{
    class MaterialPreviewPanel;
    class OrmPackingDialog;
    class MaterialPropertyPanel;
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

        std::unique_ptr<editor::materialeditor::MaterialPreviewPanel> previewPanel;
        std::unique_ptr<editor::materialeditor::OrmPackingDialog> ormPackDialog;
        std::unique_ptr<editor::materialeditor::MaterialPropertyPanel> propertyPanel;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool showCompileError = false;
        std::string compileErrorMessage;

        float previewPanelWidth = 250.0f;

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

        void drawToolbar();
        void drawGraphPanel();

        void onGraphChanged();
    };
}
