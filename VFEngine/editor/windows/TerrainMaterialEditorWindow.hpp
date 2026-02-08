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

        int autoCompileCountdown = 0;
        static constexpr int AUTO_COMPILE_DELAY_FRAMES = 10;

    public:
        explicit TerrainMaterialEditorWindow(const std::string& materialPath);
        ~TerrainMaterialEditorWindow() override = default;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

        const std::string& getMaterialPath() const { return materialPath; }

    private:
        void loadMaterial();
        void saveMaterial();
        void compileMaterial();

        void drawToolbar();
        void drawLayerProperties();

        void removeLayer(int removeIndex, int currentCount);

        void onChanged();
    };
}
