#pragma once
#include "nfd/FileDialog.hpp"
#include "config/Config.hpp"
#include <map>
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
        // Per-file values for importer-declared options (rendered generically)
        std::vector<std::map<std::string, importConfig::ImportOptionValue>> customOptions;
        importConfig::TextureCompressionMode compressionMode = importConfig::TextureCompressionMode::BC;
        importConfig::TextureCompressionQuality compressionQuality = importConfig::TextureCompressionQuality::Balanced;
        importConfig::AudioCompressionQuality audioQuality = importConfig::AudioCompressionQuality::Medium;
        importConfig::AudioLoadType audioLoadType = importConfig::AudioLoadType::Auto;
        bool audioForceMono = false;
        bool openModal = false;

    public:
        void draw();

        // Generic UI for importer-declared options of the file at fileIndex
        void drawCustomOptions(size_t fileIndex);

        // Opens file selection dialog and queues modal
        void openImportDialog();

        bool isOpen() const { return openModal; }
    };
}
