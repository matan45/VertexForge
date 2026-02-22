#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "VFXPropertyPanel.hpp"
#include <vfx/VFXTypes.hpp>
#include <memory>
#include <string>

namespace editor::graph
{
    class VFXGraphEditor;
}

namespace editor::vfxeditor
{
    class VFXPreviewPanel;
}

namespace windows
{
    class VFXEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string vfxPath;
        std::string windowTitle;
        std::unique_ptr<vfx::VFXData> vfxData;
        std::unique_ptr<editor::graph::VFXGraphEditor> graphEditor;
        std::unique_ptr<editor::vfxeditor::VFXPreviewPanel> previewPanel;
        editor::vfxeditor::VFXPropertyPanel propertyPanel;

        bool isOpen = true;
        bool needsInit = true;
        bool needsPreviewUpdate = false;
        bool isDirty = false;
        float previewPanelWidth = 280.0f;
        float propertyPanelHeight = 200.0f;

    public:
        explicit VFXEditorWindow(const std::string& vfxPath);
        ~VFXEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getVFXPath() const { return vfxPath; }

    private:
        void initEditor();
        void loadVFX();
        void saveVFX();

        void drawToolbar();
        void drawGraphPanel();
        void drawPropertyPanel();

        void onGraphChanged();
        void updatePreviewFromGraph();
    };
}
