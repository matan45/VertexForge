#include "PreviewWindowManager.hpp"
#include "../MeshPreviewWindow.hpp"
#include "../ImagePreviewWindow.hpp"
#include "../AudioPreviewWindow.hpp"
#include "../MaterialEditorWindow.hpp"
#include "../MaterialInstanceEditorWindow.hpp"
#include "../PrefabPreviewWindow.hpp"
#include "../FontPreviewWindow.hpp"
#include "../animation/AnimationPreviewWindow.hpp"
#include "../animation/AnimatorEditorWindow.hpp"
#include "../vfx/VFXEditorWindow.hpp"
#include "../TerrainMaterialEditorWindow.hpp"
#include "../LightmapPreviewWindow.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "string/StringUtil.hpp"

namespace windows
{
    void PreviewWindowManager::cleanupExpired()
    {
        eraseExpired(openMeshPreviews);
        eraseExpired(openImagePreviews);
        eraseExpired(openAudioPreviews);
        eraseExpired(openMaterialEditors);
        eraseExpired(openInstanceEditors);
        eraseExpired(openPrefabPreviews);
        eraseExpired(openFontPreviews);
        eraseExpired(openAnimationPreviews);
        eraseExpired(openAnimatorEditors);
        eraseExpired(openVFXEditors);
        eraseExpired(openTerrainMaterialEditors);
        eraseExpired(openLightmapPreviews);
    }

    bool PreviewWindowManager::openPreview(const fs::path& filePath, AssetType type)
    {
        cleanupExpired();

        std::string path = StringUtil::wstringToUtf8(filePath.wstring());

        switch (type)
        {
        case AssetType::Texture:
            openImagePreview(path, false);
            return true;
        case AssetType::HDR:
            openImagePreview(path, true);
            return true;
        case AssetType::Model:
            openMeshPreview(path);
            return true;
        case AssetType::Audio:
            openAudioPreview(path);
            return true;
        case AssetType::Material:
            openMaterialEditor(path);
            return true;
        case AssetType::MaterialInstance:
            openMaterialInstanceEditor(path);
            return true;
        case AssetType::Prefab:
            openPrefabPreview(path);
            return true;
        case AssetType::Font:
            openFontPreview(path);
            return true;
        case AssetType::Animation:
            openAnimationPreview(path);
            return true;
        case AssetType::Animator:
            openAnimatorEditor(path);
            return true;
        case AssetType::VFX:
            openVFXEditor(path);
            return true;
        case AssetType::TerrainMaterial:
            openTerrainMaterialEditor(path);
            return true;
        case AssetType::Lightmap:
            openLightmapPreview(path);
            return true;
        default:
            return false;
        }
    }

    bool PreviewWindowManager::isPreviewOpen(const std::string& path) const
    {
        auto meshIt = openMeshPreviews.find(path);
        if (meshIt != openMeshPreviews.end() && !meshIt->second.expired())
            return true;

        auto imageIt = openImagePreviews.find(path);
        if (imageIt != openImagePreviews.end() && !imageIt->second.expired())
            return true;

        auto audioIt = openAudioPreviews.find(path);
        if (audioIt != openAudioPreviews.end() && !audioIt->second.expired())
            return true;

        auto materialIt = openMaterialEditors.find(path);
        if (materialIt != openMaterialEditors.end() && !materialIt->second.expired())
            return true;

        auto instanceIt = openInstanceEditors.find(path);
        if (instanceIt != openInstanceEditors.end() && !instanceIt->second.expired())
            return true;

        auto prefabIt = openPrefabPreviews.find(path);
        if (prefabIt != openPrefabPreviews.end() && !prefabIt->second.expired())
            return true;

        auto fontIt = openFontPreviews.find(path);
        if (fontIt != openFontPreviews.end() && !fontIt->second.expired())
            return true;

        auto animIt = openAnimationPreviews.find(path);
        if (animIt != openAnimationPreviews.end() && !animIt->second.expired())
            return true;

        auto animatorIt = openAnimatorEditors.find(path);
        if (animatorIt != openAnimatorEditors.end() && !animatorIt->second.expired())
            return true;

        auto vfxIt = openVFXEditors.find(path);
        if (vfxIt != openVFXEditors.end() && !vfxIt->second.expired())
            return true;

        auto terrainMatIt = openTerrainMaterialEditors.find(path);
        if (terrainMatIt != openTerrainMaterialEditors.end() && !terrainMatIt->second.expired())
            return true;

        auto lightmapIt = openLightmapPreviews.find(path);
        if (lightmapIt != openLightmapPreviews.end() && !lightmapIt->second.expired())
            return true;

        return false;
    }

    void PreviewWindowManager::openMeshPreview(const std::string& path)
    {
        auto it = openMeshPreviews.find(path);
        if (it == openMeshPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<MeshPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openMeshPreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openImagePreview(const std::string& path, bool isHDR)
    {
        auto it = openImagePreviews.find(path);
        if (it == openImagePreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<ImagePreviewWindow>(path, isHDR);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openImagePreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openAudioPreview(const std::string& path)
    {
        auto it = openAudioPreviews.find(path);
        if (it == openAudioPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<AudioPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openAudioPreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openMaterialEditor(const std::string& path)
    {
        auto it = openMaterialEditors.find(path);
        if (it == openMaterialEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<MaterialEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openMaterialEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openMaterialInstanceEditor(const std::string& path)
    {
        auto it = openInstanceEditors.find(path);
        if (it == openInstanceEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<MaterialInstanceEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openInstanceEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openPrefabPreview(const std::string& path)
    {
        auto it = openPrefabPreviews.find(path);
        if (it == openPrefabPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<PrefabPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openPrefabPreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openFontPreview(const std::string& path)
    {
        auto it = openFontPreviews.find(path);
        if (it == openFontPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<FontPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openFontPreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openAnimationPreview(const std::string& path)
    {
        auto it = openAnimationPreviews.find(path);
        if (it == openAnimationPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<AnimationPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openAnimationPreviews[path] = previewWindow;
        }
    }

    void PreviewWindowManager::openAnimatorEditor(const std::string& path)
    {
        auto it = openAnimatorEditors.find(path);
        if (it == openAnimatorEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<AnimatorEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openAnimatorEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openVFXEditor(const std::string& path)
    {
        auto it = openVFXEditors.find(path);
        if (it == openVFXEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<VFXEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openVFXEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openTerrainMaterialEditor(const std::string& path)
    {
        auto it = openTerrainMaterialEditors.find(path);
        if (it == openTerrainMaterialEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<TerrainMaterialEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openTerrainMaterialEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openLightmapPreview(const std::string& path)
    {
        auto it = openLightmapPreviews.find(path);
        if (it == openLightmapPreviews.end() || it->second.expired())
        {
            auto previewWindow = std::make_shared<LightmapPreviewWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
            openLightmapPreviews[path] = previewWindow;
        }
    }
}
