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
    class FontPreviewWindow;
    class AnimationPreviewWindow;
    class AnimatorEditorWindow;
    class VFXEditorWindow;
    class TerrainMaterialEditorWindow;
    class SVTPreviewWindow;
}

namespace editor::windows
{
    class BehaviorTreeEditorWindow;
}

namespace windows
{
    class PreviewWindowManager
    {
    private:
        std::unordered_map<std::string, std::weak_ptr<MeshPreviewWindow>> openMeshPreviews;
        std::unordered_map<std::string, std::weak_ptr<ImagePreviewWindow>> openImagePreviews;
        std::unordered_map<std::string, std::weak_ptr<AudioPreviewWindow>> openAudioPreviews;
        std::unordered_map<std::string, std::weak_ptr<MaterialEditorWindow>> openMaterialEditors;
        std::unordered_map<std::string, std::weak_ptr<MaterialInstanceEditorWindow>> openInstanceEditors;
        std::unordered_map<std::string, std::weak_ptr<PrefabPreviewWindow>> openPrefabPreviews;
        std::unordered_map<std::string, std::weak_ptr<FontPreviewWindow>> openFontPreviews;
        std::unordered_map<std::string, std::weak_ptr<AnimationPreviewWindow>> openAnimationPreviews;
        std::unordered_map<std::string, std::weak_ptr<AnimatorEditorWindow>> openAnimatorEditors;
        std::unordered_map<std::string, std::weak_ptr<VFXEditorWindow>> openVFXEditors;
        std::unordered_map<std::string, std::weak_ptr<TerrainMaterialEditorWindow>> openTerrainMaterialEditors;
        std::unordered_map<std::string, std::weak_ptr<editor::windows::BehaviorTreeEditorWindow>> openBehaviorTreeEditors;
        std::unordered_map<std::string, std::weak_ptr<SVTPreviewWindow>> openSVTPreviews;
        template<typename T>
        static void eraseExpired(std::unordered_map<std::string, std::weak_ptr<T>>& map)
        {
            for (auto it = map.begin(); it != map.end();)
            {
                if (it->second.expired())
                    it = map.erase(it);
                else
                    ++it;
            }
        }

        void cleanupExpired();

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
        void openFontPreview(const std::string& path);
        void openAnimationPreview(const std::string& path);
        void openAnimatorEditor(const std::string& path);
        void openVFXEditor(const std::string& path);
        void openTerrainMaterialEditor(const std::string& path);
        void openBehaviorTreeEditor(const std::string& path);
        void openSVTPreview(const std::string& path);
    };
}
