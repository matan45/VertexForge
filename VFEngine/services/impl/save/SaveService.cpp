#include "SaveService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "serialization/SceneSerialization.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/save/SaveEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/scripting/ScriptingEvents.hpp"
#include "../../providers/scripting/IScriptingProvider.hpp"
#include "print/Log.hpp"
#include "resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace services
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    SaveService::SaveService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                             IScriptingProvider* scriptingProvider,
                             std::filesystem::path stateFolder,
                             std::filesystem::path noProjectFolder)
        : sceneGraph(sceneGraph)
        , scriptingProvider(scriptingProvider)
        , stateFolder(std::move(stateFolder))
        , noProjectFolder(std::move(noProjectFolder))
    {
    }

    SaveService::~SaveService()
    {
        // The subscriptions unsubscribe themselves. The handlers capture `this`: unregister them, but only when this
        // instance registered them (the dispatcher keys handlers by type, and an instance that never registered must
        // not remove another one's).
        if (handlersRegistered)
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            dispatcher.unregisterCommandHandler<::events::save::CreateSaveSlotCommand>();
            dispatcher.unregisterCommandHandler<::events::save::SaveGameCommand>();
            dispatcher.unregisterCommandHandler<::events::save::LoadGameCommand>();
            dispatcher.unregisterCommandHandler<::events::save::DeleteSaveSlotCommand>();
            dispatcher.unregisterQueryHandler<::events::save::ListSaveSlotsQuery>();
            dispatcher.unregisterQueryHandler<::events::save::GetSaveMetadataQuery>();
        }
    }

    void SaveService::registerEventHandlers(::events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<::events::save::CreateSaveSlotCommand>(
            [this](const ::events::save::CreateSaveSlotCommand& cmd)
            {
                return createSlot(cmd.slotName);
            });

        dispatcher.registerCommandHandler<::events::save::SaveGameCommand>(
            [this](const ::events::save::SaveGameCommand& cmd)
            {
                return saveGame(cmd.slotName);
            });

        dispatcher.registerCommandHandler<::events::save::LoadGameCommand>(
            [this](const ::events::save::LoadGameCommand& cmd)
            {
                return loadGame(cmd.slotName);
            });

        dispatcher.registerCommandHandler<::events::save::DeleteSaveSlotCommand>(
            [this](const ::events::save::DeleteSaveSlotCommand& cmd)
            {
                return deleteSlot(cmd.slotName);
            });

        dispatcher.registerQueryHandler<::events::save::ListSaveSlotsQuery>(
            [this](const ::events::save::ListSaveSlotsQuery&)
            {
                return listSlots();
            });

        dispatcher.registerQueryHandler<::events::save::GetSaveMetadataQuery>(
            [this](const ::events::save::GetSaveMetadataQuery& q)
            {
                return getMetadata(q.slotName);
            });

        handlersRegistered = true;
        subscribeToRestoreEvents(dispatcher);
    }

    void SaveService::subscribeToRestoreEvents(::events::EventDispatcher& dispatcher)
    {
        loadingStartedSubscription = ::events::ScopedSubscription(
            dispatcher.subscribe<::events::scene::SceneLoadingStartedNotification>(
                [this](const ::events::scene::SceneLoadingStartedNotification& n)
                {
                    onSceneLoadingStarted(n.scenePath);
                }));

        sceneClearedSubscription = ::events::ScopedSubscription(
            dispatcher.subscribe<::events::scene::SceneClearedNotification>(
                [this](const ::events::scene::SceneClearedNotification&)
                {
                    onSceneCleared();
                }));

        loadingCompletedSubscription = ::events::ScopedSubscription(
            dispatcher.subscribe<::events::scene::SceneLoadingCompletedNotification>(
                [this](const ::events::scene::SceneLoadingCompletedNotification& n)
                {
                    onSceneLoadingCompleted(n.scenePath, n.success);
                }));

        instanceCreatedSubscription = ::events::ScopedSubscription(
            dispatcher.subscribe<::events::scripting::ScriptInstanceCreatedNotification>(
                [this](const ::events::scripting::ScriptInstanceCreatedNotification& n)
                {
                    onScriptInstanceCreated(n.instanceId, n.entityUUID, n.className);
                }));

        modeChangedSubscription = ::events::ScopedSubscription(
            dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
                [this](const ::events::editor::EditorModeChangedNotification& n)
                {
                    if (n.previousMode == EditorMode::Play && n.currentMode != EditorMode::Play)
                    {
                        onPlayModeEnded();
                    }
                }));
    }

    bool SaveService::isValidSlotName(const std::string& slotName)
    {
        if (slotName.empty()) return false;
        if (slotName.find("..") != std::string::npos) return false;
        if (slotName.find('/') != std::string::npos) return false;
        if (slotName.find('\\') != std::string::npos) return false;
        if (slotName.find(':') != std::string::npos) return false;
        return true;
    }

    bool SaveService::createSlot(const std::string& slotName)
    {
        if (!isValidSlotName(slotName))
        {
            vfLogError("[SaveService] Invalid slot name: '{}'", slotName);
            return false;
        }

        std::string slotDir = getSlotDirectory(slotName);
        try
        {
            fs::create_directories(slotDir);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to create save slot '{}': {}", slotName, e.what());
            return false;
        }
    }

    bool SaveService::saveGame(const std::string& slotName)
    {
        if (!isValidSlotName(slotName))
        {
            vfLogError("[SaveService] Invalid slot name: '{}'", slotName);
            return false;
        }

        if (!sceneGraph || !scriptingProvider)
        {
            vfLogError("[SaveService] Cannot save: missing sceneGraph or scriptingProvider");
            return false;
        }

        std::string slotDir = getSlotDirectory(slotName);

        try
        {
            fs::create_directories(slotDir);
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to create save directory: {}", e.what());
            return false;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();

        // 1. Save scene
        std::string scenePath = slotDir + "/scene.vfScene";
        bool sceneSuccess = serialization::SceneSerialization::saveScene(*sceneGraph, scenePath);
        if (!sceneSuccess)
        {
            vfLogError("[SaveService] Failed to save scene for slot '{}'", slotName);
            return false;
        }

        // 2. Save script states
        bool scriptsSuccess = saveScriptStates(slotDir);

        // 3. Write metadata
        writeMetadata(slotDir, slotName);

        bool overallSuccess = sceneSuccess && scriptsSuccess;

        ::events::save::SaveGameCompletedNotification notif;
        notif.slotName = slotName;
        notif.success = overallSuccess;
        if (!overallSuccess)
        {
            notif.errorMessage = "Failed to save script states";
        }
        dispatcher.publish(notif);

        if (overallSuccess)
        {
            vfLogInfo("[SaveService] Game saved to slot '{}'", slotName);
        }
        return overallSuccess;
    }

    bool SaveService::loadGame(const std::string& slotName)
    {
        if (!isValidSlotName(slotName))
        {
            vfLogError("[SaveService] Invalid slot name: '{}'", slotName);
            return false;
        }

        std::string slotDir = getSlotDirectory(slotName);
        std::string scenePath = slotDir + "/scene.vfScene";

        std::error_code existsError;
        if (!fs::exists(scenePath, existsError))
        {
            vfLogError("[SaveService] Save slot '{}' does not contain a scene file", slotName);
            return false;
        }

        // Read the saved script states first: an unreadable scripts.json refuses the load before the current scene
        // is replaced.
        PendingRestore restore;
        restore.slotName = slotName;
        restore.scenePath = scenePath;
        if (!readScriptStates(slotDir, restore))
        {
            return false;
        }

        // A load still in flight is replaced by this one; leftovers of a finished one are dropped.
        if (pendingRestore && pendingRestore->phase != PendingRestore::Phase::Live)
        {
            failPendingRestore("replaced by loading slot '" + slotName + "'");
        }
        passCompletedSubscription.unsubscribe();
        pendingRestore = std::move(restore);

        // Deferred: the scene loads over the next frames (SceneLoadingStarted is published from inside this call,
        // for this scene's path).
        ::events::scene::LoadSceneCommand loadCmd;
        loadCmd.filePath = scenePath;
        bool accepted = false;
        try
        {
            accepted = ::events::EventDispatcher::instance().execute(loadCmd);
        }
        catch (...)
        {
            pendingRestore.reset();
            throw;
        }
        if (!accepted)
        {
            pendingRestore.reset();
            vfLogError("[SaveService] The scene of save slot '{}' could not be queued for loading", slotName);
            return false;
        }
        return true;
    }

    void SaveService::onSceneLoadingStarted(const std::string& scenePath)
    {
        if (!pendingRestore)
        {
            return;
        }
        switch (pendingRestore->phase)
        {
        case PendingRestore::Phase::Queued:
        case PendingRestore::Phase::Loading:
            if (scenePath != pendingRestore->scenePath)
            {
                failPendingRestore("another scene load ('" + scenePath + "') replaced it");
            }
            break;
        case PendingRestore::Phase::AwaitingScripts:
            // A new load before the script pass: report what was applied so far.
            finishPendingRestore(false);
            break;
        case PendingRestore::Phase::Live:
            pendingRestore.reset();
            break;
        }
    }

    void SaveService::onSceneCleared()
    {
        if (!pendingRestore)
        {
            return;
        }
        switch (pendingRestore->phase)
        {
        case PendingRestore::Phase::Queued:
            // The slot's load cleared the old scene: from here on, a new instance belongs to the slot's scene (an old
            // instance created before the clear would carry the same entity UUIDs).
            pendingRestore->phase = PendingRestore::Phase::Loading;
            break;
        case PendingRestore::Phase::Loading:
            break;
        case PendingRestore::Phase::AwaitingScripts:
            finishPendingRestore(false);
            break;
        case PendingRestore::Phase::Live:
            pendingRestore.reset();
            break;
        }
    }

    void SaveService::onSceneLoadingCompleted(const std::string& scenePath, bool success)
    {
        if (!pendingRestore || scenePath != pendingRestore->scenePath)
        {
            return;
        }
        const PendingRestore::Phase phase = pendingRestore->phase;
        if (phase != PendingRestore::Phase::Queued && phase != PendingRestore::Phase::Loading)
        {
            return;
        }
        if (!success)
        {
            failPendingRestore("the slot's scene could not be loaded");
            return;
        }

        pendingRestore->phase = PendingRestore::Phase::AwaitingScripts;
        passCompletedSubscription = ::events::ScopedSubscription(
            ::events::EventDispatcher::instance().subscribe<::events::scripting::ScriptUpdatePassCompletedNotification>(
                [this](const ::events::scripting::ScriptUpdatePassCompletedNotification&)
                {
                    if (pendingRestore && pendingRestore->phase == PendingRestore::Phase::AwaitingScripts)
                    {
                        finishPendingRestore(true);
                    }
                }));
    }

    void SaveService::onScriptInstanceCreated(uint64_t instanceId, uint64_t entityUUID, const std::string& className)
    {
        if (!pendingRestore || pendingRestore->phase == PendingRestore::Phase::Queued || !scriptingProvider)
        {
            return;
        }

        auto& states = pendingRestore->states;
        const auto it = states.find({entityUUID, className});
        if (it == states.end())
        {
            return;
        }

        const std::string state = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty())
        {
            states.erase(it);
        }

        if (scriptingProvider->setInstanceState(instanceId, state))
        {
            ++pendingRestore->appliedCount;
        }
        else
        {
            vfLogWarning("[SaveService] Slot '{}': the saved state of {} could not be applied", pendingRestore->slotName,
                         className);
        }

        if (pendingRestore->phase == PendingRestore::Phase::Live && states.empty())
        {
            pendingRestore.reset();
        }
    }

    void SaveService::onPlayModeEnded()
    {
        if (!pendingRestore)
        {
            return;
        }
        if (pendingRestore->phase == PendingRestore::Phase::Live)
        {
            pendingRestore.reset();
            return;
        }
        failPendingRestore("Play mode ended before the load finished");
    }

    void SaveService::failPendingRestore(const std::string& reason)
    {
        if (!pendingRestore)
        {
            return;
        }
        const bool reported = pendingRestore->phase == PendingRestore::Phase::Live;
        ::events::save::LoadGameCompletedNotification notif;
        notif.slotName = pendingRestore->slotName;
        notif.success = false;
        notif.errorMessage = reason;

        passCompletedSubscription.unsubscribe();
        pendingRestore.reset();

        if (!reported)
        {
            vfLogError("[SaveService] Loading slot '{}' failed: {}", notif.slotName, reason);
            ::events::EventDispatcher::instance().publish(notif);
        }
    }

    void SaveService::finishPendingRestore(bool keepLeftovers)
    {
        if (!pendingRestore)
        {
            return;
        }

        std::size_t leftover = 0;
        for (const auto& [key, states] : pendingRestore->states)
        {
            leftover += states.size();
        }
        const std::size_t saved = pendingRestore->savedCount;
        const std::size_t applied = pendingRestore->appliedCount;

        ::events::save::LoadGameCompletedNotification notif;
        notif.slotName = pendingRestore->slotName;
        notif.success = saved == 0 || applied > 0;
        if (!notif.success)
        {
            notif.errorMessage = "none of the " + std::to_string(saved) +
                                 " saved script states matched a script in the loaded scene";
        }

        passCompletedSubscription.unsubscribe();
        if (keepLeftovers && leftover > 0)
        {
            pendingRestore->phase = PendingRestore::Phase::Live;
        }
        else
        {
            pendingRestore.reset();
        }

        if (!notif.success)
        {
            vfLogError("[SaveService] Loading slot '{}' failed: {}", notif.slotName, notif.errorMessage);
        }
        else
        {
            if (leftover > 0)
            {
                vfLogWarning("[SaveService] Slot '{}': {} of {} saved script states matched no script yet{}",
                             notif.slotName, leftover, saved,
                             keepLeftovers ? " (they go to matching scripts that start later in this scene)" : "");
            }
            vfLogInfo("[SaveService] Game loaded from slot '{}'", notif.slotName);
        }
        // Last: a subscriber may start another load.
        ::events::EventDispatcher::instance().publish(notif);
    }

    bool SaveService::deleteSlot(const std::string& slotName)
    {
        if (!isValidSlotName(slotName))
        {
            vfLogError("[SaveService] Invalid slot name: '{}'", slotName);
            return false;
        }

        std::string slotDir = getSlotDirectory(slotName);
        try
        {
            if (fs::exists(slotDir))
            {
                fs::remove_all(slotDir);
            }
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to delete save slot '{}': {}", slotName, e.what());
            return false;
        }
    }

    std::vector<std::string> SaveService::listSlots() const
    {
        std::vector<std::string> slots;
        std::string savesDir = getSavesDirectory();

        if (!fs::exists(savesDir)) return slots;

        try
        {
            for (const auto& entry : fs::directory_iterator(savesDir))
            {
                if (entry.is_directory())
                {
                    slots.push_back(entry.path().filename().string());
                }
            }
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to list save slots: {}", e.what());
        }

        return slots;
    }

    SaveSlotMetadata SaveService::getMetadata(const std::string& slotName) const
    {
        SaveSlotMetadata metadata;
        metadata.slotName = slotName;

        if (!isValidSlotName(slotName)) return metadata;

        std::string metaPath = getSlotDirectory(slotName) + "/metadata.json";
        if (!fs::exists(metaPath)) return metadata;

        try
        {
            json j = resource::readJsonFile(metaPath);

            if (j.contains("timestamp")) metadata.timestamp = j["timestamp"].get<std::string>();
            if (j.contains("playtimeSeconds")) metadata.playtimeSeconds = j["playtimeSeconds"].get<float>();
            if (j.contains("customData")) metadata.customData = j["customData"].get<std::string>();
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[SaveService] Failed to read metadata for slot '{}': {}", slotName, e.what());
        }

        return metadata;
    }

    std::string SaveService::getSavesDirectory() const
    {
        // VK-1726: an absolute state folder (the Runtime's) holds the saves whether a project is open or not.
        if (stateFolder.is_absolute())
        {
            return (stateFolder / "saves").string();
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::project::GetProjectPathQuery pathQuery;
        auto projectPath = dispatcher.query(pathQuery);

        if (projectPath.has_value() && !projectPath->empty())
        {
            // The query answers the .vfproj FILE path (as AssetDatabase reads it): saves/ lives in
            // its folder, under stateFolder.
            fs::path folder = fs::path(*projectPath).parent_path();
            if (!stateFolder.empty())
            {
                folder /= stateFolder;
            }
            return (folder / "saves").string();
        }
        // No project: the per-user folder an installed editor gave (VK-1732), else the working directory's.
        if (!noProjectFolder.empty())
        {
            return noProjectFolder.string();
        }
        return "saves";
    }

    std::string SaveService::getSlotDirectory(const std::string& slotName) const
    {
        return getSavesDirectory() + "/" + slotName;
    }

    bool SaveService::saveScriptStates(const std::string& slotDir)
    {
        if (!scriptingProvider) return false;

        try
        {
            json scriptStates;
            scriptStates["version"] = 1;
            json instancesArray = json::array();

            auto allIds = scriptingProvider->getAllInstanceIds();
            auto& registry = scene::EntityRegistry::getRegistry();

            for (uint64_t instanceId : allIds)
            {
                if (!scriptingProvider->isSaveableInstance(instanceId))
                    continue;

                auto entityHandle = scriptingProvider->getInstanceEntity(instanceId);
                std::string className = scriptingProvider->getInstanceClassName(instanceId);
                std::string scriptPath = scriptingProvider->getInstanceScriptPath(instanceId);
                std::string state = scriptingProvider->getInstanceState(instanceId);

                // Get entity UUID for stable matching on load
                uint64_t entityUUID = 0;
                if (entityHandle.isValid() && internal::isValidHandle(entityHandle, registry))
                {
                    auto entity = internal::fromHandle(entityHandle);
                    if (registry.all_of<components::UUIDComponent>(entity))
                    {
                        entityUUID = registry.get<components::UUIDComponent>(entity).id.getValue();
                    }
                }

                json instance;
                instance["entityUUID"] = entityUUID;
                instance["className"] = className;
                instance["scriptPath"] = scriptPath;
                instance["state"] = state;
                instancesArray.push_back(instance);
            }

            scriptStates["instances"] = instancesArray;

            std::string scriptsPath = slotDir + "/scripts.json";
            std::ofstream file(scriptsPath);
            file << scriptStates.dump(2);
            file.close();
            if (!file)
            {
                vfLogError("[SaveService] Failed to write {}", scriptsPath);
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to save script states: {}", e.what());
            return false;
        }
    }

    bool SaveService::readScriptStates(const std::string& slotDir, PendingRestore& restore)
    {
        std::string scriptsPath = slotDir + "/scripts.json";
        std::error_code existsError;
        if (!fs::exists(scriptsPath, existsError))
        {
            vfLogInfo("[SaveService] No script states to restore");
            return true;
        }

        try
        {
            json scriptStates = resource::readJsonFile(scriptsPath);

            if (!scriptStates.is_object() || !scriptStates.contains("instances") ||
                !scriptStates["instances"].is_array())
            {
                return true;
            }

            for (const auto& savedInstance : scriptStates["instances"])
            {
                if (!savedInstance.is_object()) continue;

                uint64_t savedUUID = savedInstance.value("entityUUID", uint64_t(0));
                std::string savedClassName = savedInstance.value("className", "");
                std::string savedState = savedInstance.value("state", "{}");

                if (savedClassName.empty() || savedUUID == 0) continue;

                restore.states[{savedUUID, std::move(savedClassName)}].push_back(std::move(savedState));
                ++restore.savedCount;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Save slot '{}' has an unreadable {}: {}", restore.slotName, scriptsPath, e.what());
            return false;
        }
    }

    void SaveService::writeMetadata(const std::string& slotDir, const std::string& slotName)
    {
        try
        {
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &timeT); // Windows-only; use localtime_r on POSIX

            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

            json metadata;
            metadata["slotName"] = slotName;
            metadata["timestamp"] = oss.str();
            metadata["playtimeSeconds"] = 0.0f;

            std::string metaPath = slotDir + "/metadata.json";
            std::ofstream file(metaPath);
            file << metadata.dump(2);
            file.close();
            if (!file)
            {
                vfLogWarning("[SaveService] Failed to write {}", metaPath);
            }
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[SaveService] Failed to write metadata: {}", e.what());
        }
    }
}
