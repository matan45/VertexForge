#pragma once
#include <string>
#include "config/Config.hpp"

namespace editor::materialeditor
{
    class OrmPackingDialog
    {
    private:
        bool showDialog = false;

        std::string aoPath;
        std::string roughnessPath;
        std::string metallicPath;
        std::string heightPath; // VK-1609: packs into ORM alpha for terrain height blending
        std::string outputPath;

        importConfig::TextureCompressionMode compressionMode = importConfig::TextureCompressionMode::BC;
        importConfig::TextureCompressionQuality compressionQuality = importConfig::TextureCompressionQuality::Balanced;


        std::string errorMessage;
        float progress = 0.0f;
        bool packInProgress = false;

    public:
        explicit OrmPackingDialog() = default;
        ~OrmPackingDialog() = default;

        void open();
        void draw();
        bool isOpen() const { return showDialog; }

    private:
        void resetState();
        void doPack();
    };
}
