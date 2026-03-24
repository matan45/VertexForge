#pragma once
#include "../../data/SaveTypes.hpp"
#include "../../events/EventDispatcher.hpp"
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

        // Pending load state (deferred until scene loads)
        std::string pendingLoadSlot;
        bool waitingForSceneLoad = false;
        ::events::SubscriptionToken sceneLoadToken;

    public:
        SaveService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                     IScriptingProvider* scriptingProvider);
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
        bool saveScriptStates(const std::string& slotDir);
        bool restoreScriptStates(const std::string& slotDir);
        void writeMetadata(const std::string& slotDir, const std::string& slotName);
        void onSceneLoadCompleted(const std::string& slotDir);
    };
}
