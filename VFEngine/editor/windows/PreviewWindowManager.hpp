#pragma once
#include "ContentBrowserTypes.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    class MeshPreviewWindow;
    class ImagePreviewWindow;
    class AudioPreviewWindow;
    class MaterialEditorWindow;
    class MaterialInstanceEditorWindow;
    class PrefabPreviewWindow;

    class PreviewWindowManager
    {
    private:
        std::unordered_map<std::string, std::weak_ptr<MeshPreviewWindow>> openMeshPreviews;
        std::unordered_map<std::string, std::weak_ptr<ImagePreviewWindow>> openImagePreviews;
        std::unordered_map<std::string, std::weak_ptr<AudioPreviewWindow>> openAudioPreviews;
        std::unordered_map<std::string, std::weak_ptr<MaterialEditorWindow>> openMaterialEditors;
        std::unordered_map<std::string, std::weak_ptr<MaterialInstanceEditorWindow>> openInstanceEditors;
        std::unordered_map<std::string, std::weak_ptr<PrefabPreviewWindow>> openPrefabPreviews;
    public:
        explicit PreviewWindowManager() = default;
        ~PreviewWindowManager() = default;

        bool openPreview(const fs::path& filePath, AssetType type);
        bool isPreviewOpen(const std::string& path) const;

    private:
        void openMeshPreview(const std::string& path);
        void openImagePreview(const std::string& path, bool isHDR);
        void openAudioPreview(const std::string& path);
        void openMaterialEditor(const std::string& path);
        void openMaterialInstanceEditor(const std::string& path);
        void openPrefabPreview(const std::string& path);
    };
}
