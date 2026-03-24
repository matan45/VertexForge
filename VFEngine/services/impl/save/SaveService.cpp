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
#include "../../providers/scripting/IScriptingProvider.hpp"
#include "print/Log.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace services
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    SaveService::SaveService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                             IScriptingProvider* scriptingProvider)
        : sceneGraph(sceneGraph)
        , scriptingProvider(scriptingProvider)
    {
    }

    SaveService::~SaveService()
    {
        if (sceneLoadToken.isValid())
        {
            ::events::EventDispatcher::instance().unsubscribe(sceneLoadToken);
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

        vfLogInfo("[SaveService] Game saved to slot '{}'", slotName);
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

        if (!fs::exists(scenePath))
        {
            vfLogError("[SaveService] Save slot '{}' does not contain a scene file", slotName);
            return false;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();

        // Store both name and directory for later restoration
        pendingLoadSlotName = slotName;
        pendingLoadSlotDir = slotDir;
        waitingForSceneLoad = true;

        // Subscribe to scene load completion to restore script states
        if (sceneLoadToken.isValid())
        {
            dispatcher.unsubscribe(sceneLoadToken);
        }

        sceneLoadToken = dispatcher.subscribe<::events::scene::SceneLoadingCompletedNotification>(
            [this](const ::events::scene::SceneLoadingCompletedNotification& notif)
            {
                if (!waitingForSceneLoad) return;
                waitingForSceneLoad = false;

                if (notif.success)
                {
                    onSceneLoadCompleted();
                }
                else
                {
                    auto& disp = ::events::EventDispatcher::instance();
                    ::events::save::LoadGameCompletedNotification loadNotif;
                    loadNotif.slotName = pendingLoadSlotName;
                    loadNotif.success = false;
                    loadNotif.errorMessage = "Scene load failed";
                    disp.publish(loadNotif);
                }
            });

        // Trigger scene load (deferred — runs on next update() tick, not background-threaded)
        ::events::scene::LoadSceneCommand loadCmd;
        loadCmd.filePath = scenePath;
        dispatcher.execute(loadCmd);

        return true;
    }

    void SaveService::onSceneLoadCompleted()
    {
        bool success = restoreScriptStates(pendingLoadSlotDir);

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::save::LoadGameCompletedNotification notif;
        notif.slotName = pendingLoadSlotName;
        notif.success = success;
        if (!success)
        {
            notif.errorMessage = "Failed to restore script states";
        }
        dispatcher.publish(notif);

        vfLogInfo("[SaveService] Game loaded from slot '{}'", pendingLoadSlotName);
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
            std::ifstream file(metaPath);
            json j = json::parse(file);

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
        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::project::GetProjectPathQuery pathQuery;
        auto projectPath = dispatcher.query(pathQuery);

        if (projectPath.has_value())
        {
            return projectPath.value() + "/saves";
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

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to save script states: {}", e.what());
            return false;
        }
    }

    bool SaveService::restoreScriptStates(const std::string& slotDir)
    {
        if (!scriptingProvider) return false;

        std::string scriptsPath = slotDir + "/scripts.json";
        if (!fs::exists(scriptsPath))
        {
            vfLogInfo("[SaveService] No script states to restore");
            return true;
        }

        try
        {
            std::ifstream file(scriptsPath);
            json scriptStates = json::parse(file);
            file.close();

            if (!scriptStates.contains("instances") || !scriptStates["instances"].is_array())
                return true;

            auto& registry = scene::EntityRegistry::getRegistry();
            auto allIds = scriptingProvider->getAllInstanceIds();

            for (const auto& savedInstance : scriptStates["instances"])
            {
                uint64_t savedUUID = savedInstance.value("entityUUID", uint64_t(0));
                std::string savedClassName = savedInstance.value("className", "");
                std::string savedState = savedInstance.value("state", "{}");

                if (savedClassName.empty() || savedUUID == 0) continue;

                // Find matching live instance by entityUUID + className
                for (uint64_t liveId : allIds)
                {
                    std::string liveClassName = scriptingProvider->getInstanceClassName(liveId);
                    if (liveClassName != savedClassName) continue;

                    auto liveEntity = scriptingProvider->getInstanceEntity(liveId);
                    if (!liveEntity.isValid()) continue;

                    auto enttEntity = internal::fromHandle(liveEntity);
                    if (!registry.valid(enttEntity)) continue;
                    if (!registry.all_of<components::UUIDComponent>(enttEntity)) continue;

                    uint64_t liveUUID = registry.get<components::UUIDComponent>(enttEntity).id.getValue();
                    if (liveUUID == savedUUID)
                    {
                        scriptingProvider->setInstanceState(liveId, savedState);
                        break;
                    }
                }
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("[SaveService] Failed to restore script states: {}", e.what());
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
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[SaveService] Failed to write metadata: {}", e.what());
        }
    }
}
