#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <terrain/TerrainMaterialTypes.hpp>
#include <memory>
#include <string>

namespace editor::graph
{
    class ShaderGraphEditor;
}

namespace windows
{
    class TerrainMaterialEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string materialPath;
        std::string windowTitle;
        std::shared_ptr<terrain::TerrainMaterialData> materialData;
        std::unique_ptr<editor::graph::ShaderGraphEditor> graphEditor;

        bool isOpen = true;
        bool needsInit = true;
        bool isDirty = false;
        bool showCompileError = false;
        std::string compileErrorMessage;

    public:
        explicit TerrainMaterialEditorWindow(const std::string& materialPath);
        ~TerrainMaterialEditorWindow() override;

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
