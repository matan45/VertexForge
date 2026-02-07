#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <terrain/TerrainMaterialTypes.hpp>
#include <material/MaterialTypes.hpp>
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

        int autoCompileCountdown = 0;
        static constexpr int AUTO_COMPILE_DELAY_FRAMES = 10;

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
        void syncLayersFromGraph();
        void syncLayersToGraph();

        void drawToolbar();
        void drawGraphPanel();
        void drawPropertiesPanel();

        // Layer Stack specific UI - per-layer texture pickers
        bool drawLayerStackProperties(material::ShaderNode& node);
        // Generic property editing for other node types
        bool drawGenericProperties(material::ShaderNode& node);

        // Layer management helper
        void removeLayer(material::ShaderNode& node, int removeIndex, int currentCount);

        void onGraphChanged();
    };
}
