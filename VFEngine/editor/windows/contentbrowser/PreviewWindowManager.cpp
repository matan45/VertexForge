#include "PreviewWindowManager.hpp"
#include "../preview/MeshPreviewWindow.hpp"
#include "../preview/ImagePreviewWindow.hpp"
#include "../preview/AudioPreviewWindow.hpp"
#include "../material/MaterialEditorWindow.hpp"
#include "../material/MaterialInstanceEditorWindow.hpp"
#include "../preview/PrefabPreviewWindow.hpp"
#include "../preview/FontPreviewWindow.hpp"
#include "../animation/AnimationPreviewWindow.hpp"
#include "../animation/AnimatorEditorWindow.hpp"
#include "../vfx/VFXEditorWindow.hpp"
#include "../vfx/VFXSequenceEditorWindow.hpp"
#include "../terrain/TerrainMaterialEditorWindow.hpp"
#include "../ai/BehaviorTreeEditorWindow.hpp"
#include "../animation/RetargetingEditorWindow.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "core/PluginEventBus.hpp"          // VK-1449: route plugin-asset open requests
#include <asset/AssetTypeRegistry.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

namespace windows
{
    namespace
    {
        // VK-1435 — cheap peek: is this .vfPrefab a UI layer? UI prefabs are authored either
        // UICanvas-rooted OR as canvas-less fragments (a panel/widget subtree parented under the
        // game's screen canvas at runtime) — both have a UIRect on the root. Either way it opens
        // in the UI Layer Builder, not the mesh-only rig Prefab Preview. Hand-parses the JSON so
        // the editor stays off utilities/serialization (same approach as PrefabTransformWriter).
        bool prefabRootIsUI(const std::string& path)
        {
            try
            {
                std::ifstream f(path);
                if (!f.is_open()) return false;
                nlohmann::json j;
                f >> j;
                auto p = j.find("prefab");
                if (p == j.end() || !p->is_object()) return false;
                auto e = p->find("entity");
                if (e == p->end() || !e->is_object()) return false;
                auto c = e->find("components");
                if (c == e->end() || !c->is_object()) return false;
                // UIRect is the common denominator of every UI element (incl. a UICanvas root).
                return c->contains("uiRect") || c->contains("uiCanvas");
            }
            catch (...)
            {
                return false;
            }
        }
    }

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
        eraseExpired(openVFXSequenceEditors);
        eraseExpired(openTerrainMaterialEditors);
        eraseExpired(openBehaviorTreeEditors);
        eraseExpired(openRetargetEditors);
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
        case AssetType::ToonProfile:
        {
            // VK-1493: the Toon Profile editor is a MainImguiWindow member — route via a
            // notification (same pattern as the UI-prefab -> UI Layer Builder route).
            events::application::OpenToonProfileEditorNotification note;
            note.filePath = path;
            events::EventDispatcher::instance().publish(note);
            return true;
        }
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
        case AssetType::VFXSequence:
            openVFXSequenceEditor(path);
            return true;
        case AssetType::TerrainMaterial:
            openTerrainMaterialEditor(path);
            return true;
        case AssetType::BehaviorTree:
            openBehaviorTreeEditor(path);
            return true;
        case AssetType::Retarget:
            openRetargetEditor(path);
            return true;
        case AssetType::Plugin:
        {
            // VK-1449: route to the owning plugin via the DLL-safe event bus.
            // The plugin (e.g. GameplayAbilitySystem) subscribes to
            // "vf.asset.open" and opens its own editor for the matching typeId.
            // If no plugin is loaded for this extension, the publish is a
            // harmless no-op (graceful — the asset still browses).
            asset::AssetTypeRecord rec;
            std::string typeId;
            if (asset::AssetTypeRegistry::instance().findByExtension(filePath.extension().string(), rec))
                typeId = rec.typeId;
            nlohmann::json payload;
            payload["typeId"] = typeId;
            payload["path"] = path;
            plugin::PluginEventBus::instance().publish("vf.asset.open", payload);
            return true;
        }
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

        auto vfxSeqIt = openVFXSequenceEditors.find(path);
        if (vfxSeqIt != openVFXSequenceEditors.end() && !vfxSeqIt->second.expired())
            return true;

        auto terrainMatIt = openTerrainMaterialEditors.find(path);
        if (terrainMatIt != openTerrainMaterialEditors.end() && !terrainMatIt->second.expired())
            return true;

        auto btIt = openBehaviorTreeEditors.find(path);
        if (btIt != openBehaviorTreeEditors.end() && !btIt->second.expired())
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
        // VK-1435: a UICanvas-rooted prefab is a UI layer, not a rig — route it to the UI Layer
        // Builder (the rig Prefab Preview renders only 3D mesh parts, so it would be empty). The
        // singleton builder is owned by MainImguiWindow, which handles this notification.
        if (prefabRootIsUI(path))
        {
            events::application::OpenUILayerBuilderNotification note;
            note.filePath = path;
            events::EventDispatcher::instance().publish(note);
            return;
        }

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

    void PreviewWindowManager::openVFXSequenceEditor(const std::string& path)
    {
        auto it = openVFXSequenceEditors.find(path);
        if (it == openVFXSequenceEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<VFXSequenceEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openVFXSequenceEditors[path] = editorWindow;
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

    void PreviewWindowManager::openRetargetEditor(const std::string& path)
    {
        auto it = openRetargetEditors.find(path);
        if (it == openRetargetEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<RetargetingEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openRetargetEditors[path] = editorWindow;
        }
    }

    void PreviewWindowManager::openBehaviorTreeEditor(const std::string& path)
    {
        auto it = openBehaviorTreeEditors.find(path);
        if (it == openBehaviorTreeEditors.end() || it->second.expired())
        {
            auto editorWindow = std::make_shared<editor::windows::BehaviorTreeEditorWindow>(path);
            controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
            openBehaviorTreeEditors[path] = editorWindow;
        }
    }

}
