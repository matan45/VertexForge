#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <terrain/TerrainMaterialTypes.hpp>
#include <memory>
#include <string>

namespace windows
{
    class TerrainMaterialEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string materialPath;
        std::string windowTitle;
        std::shared_ptr<terrain::TerrainMaterialData> materialData;

        bool isOpen = true;
        bool isDirty = false;
        bool showCompileError = false;
        std::string compileErrorMessage;

    public:
        explicit TerrainMaterialEditorWindow(const std::string& materialPath);
        ~TerrainMaterialEditorWindow() override = default;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        void loadMaterial();
        void saveMaterial();
        void compileMaterial();

        void drawToolbar();
        // VK-1611 material-global anti-tiling (macro variation + distance tiling rescale).
        void drawAntiTilingProperties();
        // VK-1625 material-global parallax (POM-lite from the ORM-alpha height).
        void drawParallaxProperties();
        void drawLayerProperties();

        void removeLayer(int removeIndex, int currentCount);

        void onChanged();
    };
}
