#pragma once
#include "../../data/SaveTypes.hpp"
#include "../../events/EventDispatcher.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace services
{
    class IScriptingProvider;

    class SaveService
    {
    private:
        // A LoadGame in flight (VK-1726). The slot's scene is loaded through LoadSceneCommand, and its
        // script instances are created later, one by one, by ScriptingServiceImpl::updateScripts (the
        // Runtime's incremental load even creates some while the scene is still loading). Each saved state
        // is applied when its instance is created, before its onStart (ScriptInstanceCreatedNotification).
        struct PendingRestore
        {
            enum class Phase
            {
                Queued,           // LoadSceneCommand accepted; the old scene is still there
                Loading,          // the slot's scene load cleared the old scene
                AwaitingScripts,  // the scene loaded; LoadGameCompleted goes out after the next script pass
                Live              // reported; leftover states still go to scripts that start later in this scene
            };

            std::string slotName;
            std::string scenePath;
            Phase phase = Phase::Queued;
            // (entity UUID, script class) -> the saved states, in the order they were saved.
            std::map<std::pair<uint64_t, std::string>, std::deque<std::string>> states;
            std::size_t savedCount = 0;
            std::size_t appliedCount = 0;
        };

        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        IScriptingProvider* scriptingProvider;
        std::filesystem::path stateFolder;
        std::filesystem::path noProjectFolder;

        bool handlersRegistered = false;
        std::optional<PendingRestore> pendingRestore;
        ::events::ScopedSubscription loadingStartedSubscription;
        ::events::ScopedSubscription sceneClearedSubscription;
        ::events::ScopedSubscription loadingCompletedSubscription;
        ::events::ScopedSubscription instanceCreatedSubscription;
        ::events::ScopedSubscription modeChangedSubscription;
        // Held only while a restore waits for the script pass after its scene loaded.
        ::events::ScopedSubscription passCompletedSubscription;

    public:
        // Save slots live in <stateFolder>/saves/<slot>.
        // - An absolute stateFolder is used as it is, with or without a project (VK-1726: the Runtime passes
        //   its per-user %LOCALAPPDATA%\<Publisher>\<Game>, or the game's own folder for a portable build;
        //   see launch/GameStateLocation.hpp).
        // - A relative one is under the project folder, the .vfproj's folder; empty means the project folder
        //   itself. The editor passes ".vertexforge" (launch::editorStateDirName), so Play-mode saves stay
        //   out of the content browser and of exported games (VK-1684). Without a project the slots live in
        //   `noProjectFolder` (VK-1732: an installed editor passes ~/.vertexforge/editor/saves,
        //   launch::UserStateLocation::savesFallbackDir); an empty one means "saves" under the working
        //   directory, as before.
        SaveService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                    IScriptingProvider* scriptingProvider,
                    std::filesystem::path stateFolder,
                    std::filesystem::path noProjectFolder = {});
        ~SaveService();

        SaveService(const SaveService&) = delete;
        SaveService& operator=(const SaveService&) = delete;

        // Registers the save commands and queries, and subscribes to the scene and script notifications a
        // LoadGame restore follows. The destructor unregisters them again. One SaveService per binary.
        void registerEventHandlers(::events::EventDispatcher& dispatcher);

        bool createSlot(const std::string& slotName);
        bool saveGame(const std::string& slotName);
        // Queues the slot's scene load and returns whether it was accepted (false: an invalid slot name, no
        // scene in the slot, or an unreadable scripts.json; the current scene is left alone). The outcome is
        // LoadGameCompletedNotification, published after the first script pass that follows the load.
        bool loadGame(const std::string& slotName);
        bool deleteSlot(const std::string& slotName);
        std::vector<std::string> listSlots() const;
        SaveSlotMetadata getMetadata(const std::string& slotName) const;

    private:
        std::string getSavesDirectory() const;
        std::string getSlotDirectory(const std::string& slotName) const;
        static bool isValidSlotName(const std::string& slotName);
        bool saveScriptStates(const std::string& slotDir);
        // Reads <slotDir>/scripts.json into `restore`; false when it exists but cannot be parsed.
        static bool readScriptStates(const std::string& slotDir, PendingRestore& restore);
        void writeMetadata(const std::string& slotDir, const std::string& slotName);

        void subscribeToRestoreEvents(::events::EventDispatcher& dispatcher);
        void onSceneLoadingStarted(const std::string& scenePath);
        void onSceneCleared();
        void onSceneLoadingCompleted(const std::string& scenePath, bool success);
        void onScriptInstanceCreated(uint64_t instanceId, uint64_t entityUUID, const std::string& className);
        void onPlayModeEnded();
        // Reports the pending restore as failed (unless it was already reported) and drops it.
        void failPendingRestore(const std::string& reason);
        // Reports the pending restore; leftovers stay Live when `keepLeftovers`, else it is dropped.
        void finishPendingRestore(bool keepLeftovers);
    };
}
