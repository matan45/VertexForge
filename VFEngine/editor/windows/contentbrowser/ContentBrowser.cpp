#include "ContentBrowser.hpp"
#include "AssetQueryParser.hpp"
#include "../scene/FolderStructureWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/project/FileOperationsEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/editor/EditorKeybindingEvents.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "../../clipboard/ClipboardManager.hpp"
#include "../../dragdrop/DragDropManager.hpp"
#include "Import.hpp"
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <algorithm>
#include <windows.h>
#include <shellapi.h>

namespace windows
{
    ContentBrowser::ContentBrowser()
        : gridRenderer(std::make_unique<AssetGridRenderer>())
          , modals(std::make_unique<ContentBrowserModals>([this]() { loadDirectory(currentPath); }))
          , previewManager(std::make_unique<PreviewWindowManager>())
          , bookmarkManager(std::make_unique<BookmarkManager>())
    {
        modals->setClipboardCallbacks(
            [this]() { performCut(); },
            [this]() { performCopy(); },
            [this]() { return performPaste(); },
            []() { return ClipboardManager::instance().hasItems(); }
        );

        modals->setSelectionProvider([this]() { return getSelectedPaths(); });

        auto& dispatcher = events::EventDispatcher::instance();

        // Register default editor keybindings
        auto reg = [&](const std::string& name, const std::string& display, int key, bool ctrl = false, bool shift = false) {
            events::editor::RegisterEditorActionCommand cmd;
            cmd.actionName = name;
            cmd.category = "Content Browser";
            cmd.displayName = display;
            services::InputBinding b;
            b.type = services::BindingType::Key;
            b.code = key;
            b.requireCtrl = ctrl;
            cmd.defaultBindings = {b};
            dispatcher.execute(cmd);
        };
        reg("ContentBrowser.Copy", "Copy", ImGuiKey_C, true);
        reg("ContentBrowser.Cut", "Cut", ImGuiKey_X, true);
        reg("ContentBrowser.Paste", "Paste", ImGuiKey_V, true);
        reg("ContentBrowser.Undo", "Undo", ImGuiKey_Z, true);
        reg("ContentBrowser.Redo", "Redo", ImGuiKey_Y, true);
        reg("ContentBrowser.SelectAll", "Select All", ImGuiKey_A, true);
        reg("ContentBrowser.Delete", "Delete", ImGuiKey_Delete);
        reg("ContentBrowser.Cancel", "Cancel", ImGuiKey_Escape);

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (projectOpt && !projectOpt->workingDirectory.empty())
        {
            currentPath = projectOpt->workingDirectory;
        }
        else
        {
            currentPath = "C:\\";
        }

        if (fs::exists(currentPath) && fs::is_directory(currentPath))
        {
            loadDirectory(currentPath);
        }

        projectLoadedToken = dispatcher.subscribe<events::project::ProjectLoadedNotification>(
            [this](const events::project::ProjectLoadedNotification& notification)
            {
                typeCache.clear();
                thumbnailCache.invalidateAll();
                if (!notification.project.workingDirectory.empty())
                {
                    navigateTo(notification.project.workingDirectory);
                }
            });

        importCompletedToken = dispatcher.subscribe<events::resource::ImportCompletedNotification>(
            [this](const events::resource::ImportCompletedNotification&)
            {
                projectResultsStale.store(true);
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                    loadDirectory(currentPath);
            });

        assetSavedToken = dispatcher.subscribe<events::resource::AssetSavedNotification>(
            [this](const events::resource::AssetSavedNotification& notification)
            {
                projectResultsStale.store(true);
                thumbnailCache.invalidate(notification.filePath);
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                    loadDirectory(currentPath);
            });

        fileMovedToken = dispatcher.subscribe<events::fileops::FileMovedNotification>(
            [this](const events::fileops::FileMovedNotification& notification)
            {
                projectResultsStale.store(true);
                thumbnailCache.invalidate(notification.oldPath);
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                    clearSelection();
                }
            });

        fileDeletedToken = dispatcher.subscribe<events::fileops::FileDeletedNotification>(
            [this](const events::fileops::FileDeletedNotification& notification)
            {
                projectResultsStale.store(true);
                thumbnailCache.invalidate(notification.path);
                if (fs::exists(currentPath) && fs::is_directory(currentPath))
                {
                    loadDirectory(currentPath);
                    clearSelection();
                }
            });

        folderSelectedToken = dispatcher.subscribe<FolderSelectedNotification>(
            [this](const FolderSelectedNotification& notification)
            {
                navigateTo(notification.folderPath);
            });

        batchCompletedToken = dispatcher.subscribe<events::fileops::FileOpBatchCompletedNotification>(
            [this](const events::fileops::FileOpBatchCompletedNotification&)
            {
                pendingRefresh.store(true);
                projectResultsStale.store(true);
            });
    }

    ContentBrowser::~ContentBrowser()
    {
        thumbnailCache.shutdown();

        auto& dispatcher = events::EventDispatcher::instance();
        if (projectLoadedToken.isValid()) dispatcher.unsubscribe(projectLoadedToken);
        if (importCompletedToken.isValid()) dispatcher.unsubscribe(importCompletedToken);
        if (assetSavedToken.isValid()) dispatcher.unsubscribe(assetSavedToken);
        if (fileMovedToken.isValid()) dispatcher.unsubscribe(fileMovedToken);
        if (fileDeletedToken.isValid()) dispatcher.unsubscribe(fileDeletedToken);
        if (folderSelectedToken.isValid()) dispatcher.unsubscribe(folderSelectedToken);
        if (batchCompletedToken.isValid()) dispatcher.unsubscribe(batchCompletedToken);
    }

    void ContentBrowser::draw()
    {
        if (pendingRefresh.exchange(false))
        {
            if (fs::exists(currentPath) && fs::is_directory(currentPath))
            {
                // Batch file ops (mass move/delete) arrive coarse, so drop every
                // cached thumbnail; the freshness check repopulates what's still
                // on disk on the next draw.
                thumbnailCache.invalidateAll();
                loadDirectory(currentPath);
                clearSelection();
            }
        }

        // Drive the cache once per frame here (not in drawContentPanel) so it
        // ticks exactly once regardless of which panels render.
        thumbnailCache.update();

        gridRenderer->ensureIconsLoaded();

        if (!importLocationSet)
        {
            controllers::Import::setLocation(currentPath.string());
            importLocationSet = true;
        }

        updateCutState();
        modals->processModals(currentPath, selectedFile);
        drawContentPanel();
    }

    void ContentBrowser::handleDoubleClick()
    {
        if (selectedType == AssetType::Script)
        {
            std::string filePath = StringUtil::wstringToUtf8(selectedFile.wstring());
            std::string args = "\"" + filePath + "\"";
            ShellExecuteA(nullptr, "open", "code", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        else if (selectedType == AssetType::Terrain)
        {
            events::terrain::BeginTerrainLoadCommand cmd;
            cmd.path = StringUtil::wstringToUtf8(selectedFile.wstring());
            events::EventDispatcher::instance().execute(cmd);
        }
        else if (selectedType == AssetType::Ocean)
        {
            events::ocean::LoadOceanCommand cmd;
            cmd.path = StringUtil::wstringToUtf8(selectedFile.wstring());
            events::EventDispatcher::instance().execute(cmd);
        }
        else if (selectedType == AssetType::Scene)
        {
            events::scene::LoadSceneCommand cmd;
            cmd.filePath = StringUtil::wstringToUtf8(selectedFile.wstring());
            events::EventDispatcher::instance().execute(cmd);
        }
        else if (selectedType == AssetType::InputMapping)
        {
            events::EventDispatcher::instance().publish(events::application::OpenInputMappingWindowNotification{});
        }
        else if (selectedType == AssetType::Project)
        {
            events::EventDispatcher::instance().publish(events::application::OpenProjectSettingsWindowNotification{});
        }
        else if (selectedType != AssetType::Navmesh && selectedType != AssetType::PhysAnim)
        {
            showFileWindow = true;
        }
    }

    AssetType ContentBrowser::detectAssetType(const fs::directory_entry& entry,
                                              uint64_t fileSize, int64_t lastModified)
    {
        using enum windows::AssetType;

        std::error_code statusEc;
        if (entry.is_directory(statusEc) || statusEc)
            return Other;

        std::string extension = entry.path().extension().string();

        if (extension == ".vfMat") return Material;
        if (extension == ".vfFont") return Font;
        if (extension == ".vfproj") return Project;
        if (extension == ".vfMatInstance") return MaterialInstance;
        if (extension == ".vfAnimator") return Animator;
        if (extension == ".vfVFX") return VFX;
        if (extension == ".vfPrefab") return Prefab;
        if (extension == ".vfTerrainMat") return TerrainMaterial;
        if (extension == ".vfTerrain") return Terrain;
        if (extension == ".vfNavmesh") return Navmesh;
        if (extension == ".vfNavIndex") return Navmesh;
        if (extension == ".vfNavTile") return Navmesh;
        if (extension == ".vfPhysAnim") return PhysAnim;
        if (extension == ".vfOcean") return Ocean;
        if (extension == ".vfBehaviorTree") return BehaviorTree;
        if (extension == ".mt") return Script;
        if (extension == ".vfplugin") return Plugin;
        if (extension == ".vfInputMapping") return InputMapping;

        bool isVfAsset = (extension == ".vfImage" || extension == ".vfHdr" ||
            extension == ".vfMesh" || extension == ".vfAudio" ||
            extension == ".vfAnim" || extension == ".vfScene");

        if (!isVfAsset)
            return Other;

        std::string pathKey = StringUtil::wstringToUtf8(entry.path().wstring());
        if (auto it = typeCache.find(pathKey); it != typeCache.end() &&
            it->second.fileSize == fileSize && it->second.lastModified == lastModified)
        {
            return it->second.type;
        }

        // Asset database already holds path -> type in memory; only unregistered
        // files still pay the header read.
        AssetType resolved = Other;
        events::assetdb::GetAssetTypeQuery query;
        query.path = pathKey;
        if (auto dbType = events::EventDispatcher::instance().query(query))
            resolved = fromResourceType(*dbType);

        if (resolved == Other)
        {
            resource::FileType fileType = resource::ResourceManager::readHeaderFile(entry);

            if (fileType == resource::FileType::TEXTURE) resolved = Texture;
            else if (fileType == resource::FileType::SCENE) resolved = Scene;
            else if (fileType == resource::FileType::HDR) resolved = HDR;
            else if (fileType == resource::FileType::MESH) resolved = Model;
            else if (fileType == resource::FileType::AUDIO) resolved = Audio;
            else if (fileType == resource::FileType::ANIMATION) resolved = Animation;
        }

        typeCache[pathKey] = {fileSize, lastModified, resolved};
        return resolved;
    }

    void ContentBrowser::loadDirectory(const fs::path& path)
    {
        assets.clear();

        std::error_code ec;
        for (auto& entry : fs::directory_iterator(path, ec))
        {
            if (ec) { ec.clear(); continue; }

            try
            {
                std::string filename = entry.path().filename().string();
                if (filename.empty() || filename == "nul" || filename == "con" ||
                    filename == "prn" || filename == "aux" || filename == "." || filename == "..")
                    continue;

                if (entry.path().extension() == ".vfmeta" || filename == "assetdb.json")
                    continue;

                Asset asset;
                asset.path = StringUtil::wstringToUtf8(entry.path().wstring());
                asset.name = StringUtil::wstringToUtf8(entry.path().filename().wstring());
                asset.extension = entry.path().extension().string();

                std::error_code dirEc;
                asset.isDirectory = entry.is_directory(dirEc) && !dirEc;

                std::error_code sizeEc;
                if (entry.is_regular_file(sizeEc))
                {
                    asset.fileSize = entry.file_size(sizeEc);
                    asset.fileSizeKnown = !sizeEc;
                }

                std::error_code timeEc;
                auto ftime = entry.last_write_time(timeEc);
                if (!timeEc)
                    asset.lastModified = std::chrono::duration_cast<std::chrono::seconds>(
                        ftime.time_since_epoch()).count();

                asset.type = detectAssetType(entry, asset.fileSize, asset.lastModified);

                assets.push_back(asset);
            }
            catch (const std::exception&) { continue; }
        }
    }

    void ContentBrowser::navigateTo(const fs::path& path)
    {
        if (fs::exists(path) && fs::is_directory(path))
        {
            currentPath = path;
            controllers::Import::setLocation(currentPath.string());
            loadDirectory(currentPath);
            selectedFile.clear();
            selectedType = AssetType::Other;
            clearSelection();
            isEditingPath = false;
        }
    }

    void ContentBrowser::handleKeyboardShortcuts()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        auto isPressed = [&](const std::string& action) {
            events::editor::IsEditorActionPressedQuery q;
            q.actionName = action;
            return dispatcher.query(q);
        };

        if (isPressed("ContentBrowser.Copy"))
            performCopy();
        if (isPressed("ContentBrowser.Cut"))
            performCut();
        if (isPressed("ContentBrowser.Paste"))
            performPaste();
        if (isPressed("ContentBrowser.Undo"))
            dispatcher.execute(events::undoredo::UndoCommand{});
        if (isPressed("ContentBrowser.Redo"))
            dispatcher.execute(events::undoredo::RedoCommand{});
        if (isPressed("ContentBrowser.SelectAll"))
        {
            for (const auto& asset : assets)
                selectedPaths.insert(asset.path);
        }

        if (isPressed("ContentBrowser.Cancel"))
        {
            auto& clipboard = ClipboardManager::instance();
            if (clipboard.isCut())
            {
                clipboard.clear();
                updateCutState();
            }
        }

        if (isPressed("ContentBrowser.Delete"))
        {
            auto paths = getSelectedPaths();
            if (!paths.empty() && !selectedFile.empty())
            {
                modals->triggerDeleteModal();
            }
        }
    }

    void ContentBrowser::selectAsset(size_t index, bool ctrlHeld, bool shiftHeld)
    {
        if (index >= assets.size())
            return;

        const std::string& path = assets[index].path;

        if (shiftHeld && lastSelectedIndex >= 0)
        {
            size_t start = std::min(static_cast<size_t>(lastSelectedIndex), index);
            size_t end = std::max(static_cast<size_t>(lastSelectedIndex), index);
            if (!ctrlHeld)
                selectedPaths.clear();
            for (size_t i = start; i <= end; ++i)
                selectedPaths.insert(assets[i].path);
        }
        else if (ctrlHeld)
        {
            if (selectedPaths.find(path) != selectedPaths.end())
                selectedPaths.erase(path);
            else
                selectedPaths.insert(path);
        }
        else
        {
            if (selectedPaths.find(path) == selectedPaths.end())
            {
                selectedPaths.clear();
                selectedPaths.insert(path);
            }
        }

        lastSelectedIndex = static_cast<int>(index);
    }

    void ContentBrowser::clearSelection()
    {
        selectedPaths.clear();
        lastSelectedIndex = -1;
    }

    std::vector<std::string> ContentBrowser::getSelectedPaths() const
    {
        return std::vector<std::string>(selectedPaths.begin(), selectedPaths.end());
    }

    void ContentBrowser::updateCutState()
    {
        auto& clipboard = ClipboardManager::instance();
        const auto& cutPaths = clipboard.getCutPaths();
        for (auto& asset : assets)
            asset.isCut = cutPaths.find(asset.path) != cutPaths.end();
    }

    std::vector<ClipboardItem> ContentBrowser::buildClipboardItems() const
    {
        std::vector<ClipboardItem> items;
        for (const auto& path : getSelectedPaths())
        {
            ClipboardItem item;
            item.path = path;
            item.isDirectory = fs::is_directory(path);
            for (const auto& asset : assets)
            {
                if (asset.path == path)
                {
                    item.assetType = asset.type;
                    break;
                }
            }
            items.push_back(item);
        }
        return items;
    }

    void ContentBrowser::performCut()
    {
        auto items = buildClipboardItems();
        if (!items.empty())
            ClipboardManager::instance().cut(items);
    }

    void ContentBrowser::performCopy()
    {
        auto items = buildClipboardItems();
        if (!items.empty())
            ClipboardManager::instance().copy(items);
    }

    bool ContentBrowser::performPaste()
    {
        auto& clipboard = ClipboardManager::instance();
        if (clipboard.hasItems())
        {
            clipboard.paste(currentPath.string());
            return true;
        }
        return false;
    }

    void ContentBrowser::updateResolvedFilter()
    {
        if (filter.searchQuery == lastResolvedQuery)
            return;
        lastResolvedQuery = filter.searchQuery;
        resolvedFilter = {};

        ParsedAssetQuery parsed = parseAssetQuery(filter.searchQuery);
        resolvedFilter.terms = std::move(parsed.terms);
        resolvedFilter.extToken = parsed.extToken;

        auto& dispatcher = events::EventDispatcher::instance();

        if (!parsed.typeToken.empty())
        {
            // Match the table labels case- and space-insensitively, so both
            // type:materialinstance and type:"material instance" work.
            std::string wanted = parsed.typeToken;
            wanted.erase(std::remove(wanted.begin(), wanted.end(), ' '), wanted.end());

            bool found = false;
            for (const auto& info : assetTypeTable())
            {
                std::string label = StringUtil::toLower(info.label);
                label.erase(std::remove(label.begin(), label.end(), ' '), label.end());
                if (label == wanted)
                {
                    resolvedFilter.queryType = info.type;
                    found = true;
                    break;
                }
            }
            if (!found)
                resolvedFilter.matchNothing = true;
        }

        if (!parsed.guidToken.empty())
        {
            resolvedFilter.hasGuidToken = true;
            if (asset::AssetGUID::isStrictHex16(parsed.guidToken))
            {
                events::assetdb::GetAssetPathQuery pathQuery;
                pathQuery.guid = asset::AssetGUID::fromString(parsed.guidToken);
                auto pathOpt = dispatcher.query(pathQuery);
                if (pathOpt)
                    resolvedFilter.guidPath = normalizePathForCompare(*pathOpt);
                else
                    resolvedFilter.matchNothing = true;
            }
            else
            {
                resolvedFilter.matchNothing = true;
            }
        }

        if (!parsed.refToken.empty())
        {
            resolvedFilter.hasRefToken = true;

            asset::AssetGUID target = asset::AssetGUID::invalid();
            if (asset::AssetGUID::isStrictHex16(parsed.refToken))
            {
                target = asset::AssetGUID::fromString(parsed.refToken);
            }
            else
            {
                events::assetdb::GetAssetGUIDQuery guidQuery;
                guidQuery.path = parsed.refToken;
                if (auto guidOpt = dispatcher.query(guidQuery))
                    target = *guidOpt;
            }

            if (target.isValid())
            {
                events::assetdb::GetAssetDependentsQuery dependentsQuery;
                dependentsQuery.guid = target;
                for (const auto& dependent : dispatcher.query(dependentsQuery))
                {
                    events::assetdb::GetAssetPathQuery pathQuery;
                    pathQuery.guid = dependent;
                    if (auto pathOpt = dispatcher.query(pathQuery))
                        resolvedFilter.refMatchPaths.insert(normalizePathForCompare(*pathOpt));
                }
            }
            else
            {
                resolvedFilter.matchNothing = true;
            }
        }
    }

    void ContentBrowser::updateProjectSearchResults()
    {
        using namespace std::chrono;
        auto now = steady_clock::now();

        if (filter.searchQuery != pendingProjectQuery)
        {
            pendingProjectQuery = filter.searchQuery;
            projectQueryEditTime = now;
            projectResultsPending = true;
        }

        bool stale = projectResultsStale.exchange(false);
        bool debounceElapsed = projectResultsPending && (now - projectQueryEditTime >= milliseconds(250));
        if (!stale && !debounceElapsed)
            return;
        projectResultsPending = false;

        projectResults.clear();
        auto entries = events::EventDispatcher::instance().query(events::assetdb::GetAllAssetsQuery{});
        projectResults.reserve(entries.size());
        for (const auto& entry : entries)
        {
            fs::path entryPath(entry.path);

            Asset asset;
            asset.path = entry.path;
            asset.name = StringUtil::wstringToUtf8(entryPath.filename().wstring());
            asset.extension = entryPath.extension().string();
            asset.type = fromResourceType(entry.type);
            projectResults.push_back(std::move(asset));
        }
    }

    void ContentBrowser::handleProjectResultClick(const AssetClickResult& clickResult)
    {
        if (!clickResult.wasClicked && !clickResult.wasRightClicked)
            return;

        selectedFile = clickResult.clickedPath;
        selectedType = clickResult.clickedType;
        selectedPaths.clear();
        selectedPaths.insert(StringUtil::wstringToUtf8(selectedFile.wstring()));

        // Same double-click behavior as the directory view: open the asset's
        // preview/editor (scenes load, scripts open externally, etc.).
        if (clickResult.wasClicked && clickResult.wasDoubleClicked)
        {
            handleDoubleClick();
        }
    }
}
