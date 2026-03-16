#pragma once
#include "nfd/FileDialog.hpp"
#include "config/Config.hpp"
#include <vector>
#include <string>

namespace windows
{
    class ImportModalDialog
    {
    private:
        nfd::FileDialog fileDialog;
        std::vector<std::string> files;
        std::vector<bool> isFlip;
        std::vector<importConfig::MeshImportConfig> meshConfigs;
        importConfig::TextureCompressionMode compressionMode = importConfig::TextureCompressionMode::BC;
        importConfig::TextureCompressionQuality compressionQuality = importConfig::TextureCompressionQuality::Balanced;
        bool openModal = false;

    public:
        void draw();

        // Opens file selection dialog and queues modal
        void openImportDialog();

        bool isOpen() const { return openModal; }
    };
}
