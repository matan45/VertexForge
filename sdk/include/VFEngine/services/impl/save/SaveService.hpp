#pragma once
#include "../../data/SaveTypes.hpp"
#include "../../events/EventDispatcher.hpp"
#include <filesystem>
#include <memory>
#include <string>
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
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        IScriptingProvider* scriptingProvider;
        std::filesystem::path stateFolder;
        std::filesystem::path noProjectFolder;

        // Pending load state (deferred until scene loads)
        std::string pendingLoadSlotName;
        std::string pendingLoadSlotDir;
        bool waitingForSceneLoad = false;
        ::events::SubscriptionToken sceneLoadToken;

    public:
        // Save slots live in <project folder>/<stateFolder>/saves/<slot>, the project folder being the
        // .vfproj's folder; an empty stateFolder means the project folder itself. The editor passes
        // ".vertexforge" (launch::editorStateDirName), so Play-mode saves stay out of the content
        // browser and of exported games; the Runtime keeps them beside its game's .vfproj.
        // Without a project the slots live in `noProjectFolder` (VK-1732: an installed editor passes
        // ~/.vertexforge/editor/saves, launch::UserStateLocation::savesFallbackDir); an empty one means
        // "saves" under the working directory, as before.
        SaveService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                    IScriptingProvider* scriptingProvider,
                    std::filesystem::path stateFolder,
                    std::filesystem::path noProjectFolder = {});
        ~SaveService();

        void registerEventHandlers(::events::EventDispatcher& dispatcher);

        bool createSlot(const std::string& slotName);
        bool saveGame(const std::string& slotName);
        bool loadGame(const std::string& slotName);
        bool deleteSlot(const std::string& slotName);
        std::vector<std::string> listSlots() const;
        SaveSlotMetadata getMetadata(const std::string& slotName) const;

    private:
        std::string getSavesDirectory() const;
        std::string getSlotDirectory(const std::string& slotName) const;
        static bool isValidSlotName(const std::string& slotName);
        bool saveScriptStates(const std::string& slotDir);
        bool restoreScriptStates(const std::string& slotDir);
        void writeMetadata(const std::string& slotDir, const std::string& slotName);
        void onSceneLoadCompleted();
    };
}
