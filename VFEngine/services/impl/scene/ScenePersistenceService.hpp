#pragma once
#include "../../data/EntityHandle.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioTypes.hpp"
#include "types/RenderSettings.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>

namespace scene
{
    class SceneGraphSystem;
    class Entity;
}

namespace events
{
    class EventDispatcher;
}

namespace serialization
{
    struct IncrementalLoadState;
}

namespace services
{
    class EntityStateService;
    class StreamingZoneManager;

    // Wrapper that keeps nlohmann::json out of this header (defined in the .cpp,
    // mirroring how IncrementalLoadState is forward-declared above). Holds the
    // Play-time scene snapshot used by the Play/Stop in-memory restore (Phase 3).
    struct PieSnapshotState;

    class ScenePersistenceService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        EntityStateService* entityStateService;
        std::optional<std::string> pendingLoadPath;

        // Additive scene management
        std::string activeSceneName = "Main";
        std::unordered_map<std::string, EntityHandle> loadedAdditiveScenes;

        // Async additive load queue (supports multiple per frame)
        struct PendingAdditiveLoad
        {
            std::string scenePath;
            std::string sceneName;
        };
        std::queue<PendingAdditiveLoad> pendingAdditiveLoads;

        // Streaming zone manager
        std::unique_ptr<StreamingZoneManager> streamingZoneManager;

        // Frame-budgeted scene load (VK-1268). budget <= 0 keeps the original
        // single-frame (blocking) load; > 0 spreads entity spawning across frames
        // so a loading screen can animate. Runtime opts in; the editor stays 0.
        int incrementalLoadBudget = 0;
        bool incrementalActive = false;
        std::string incrementalFilePath;
        std::unique_ptr<serialization::IncrementalLoadState> incrementalState;

        // Play/Stop in-memory snapshot (Phase 3). captureSnapshot() stores the
        // pristine pre-play scene here; restoreSnapshot() arms pendingSnapshotRestore
        // so the heavy restore runs in update() (off the frame where the render
        // thread may be reading the registry), mirroring pendingLoadPath.
        std::unique_ptr<PieSnapshotState> pieSnapshot;
        bool pendingSnapshotRestore = false;

    public:
        explicit ScenePersistenceService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                         EntityStateService* entityStateService);
        ~ScenePersistenceService();

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        void update();

        // Entities spawned per frame during a deferred scene load. 0 (default)
        // = original synchronous one-frame load; > 0 = incremental load.
        void setIncrementalLoadBudget(int entitiesPerFrame);

        void cancelPendingLoads();

        // Play/Stop in-memory snapshot (Phase 3).
        bool captureSnapshot();   // capture current scene; false if nothing captured
        bool restoreSnapshot();   // arm a deferred restore; false if no snapshot held
        void discardSnapshot();   // drop the snapshot

        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);
        bool savePrefab(EntityHandle entity, const std::string& filePath);
        std::optional<EntityHandle> loadPrefab(const std::string& filePath,
                                               std::optional<EntityHandle> parent = std::nullopt);

        // Returns the source .vfPrefab path an entity was instantiated from (or saved
        // as), or "" if it has no PrefabInstanceComponent.
        std::string getPrefabSourcePath(EntityHandle entity) const;

        // Editor copy/paste of entity subtrees (prefab-format JSON, in-memory)
        std::string copyEntityToJson(EntityHandle entity) const;
        std::optional<EntityHandle> instantiateEntityFromJson(const std::string& jsonText,
                                                              std::optional<EntityHandle> parent);

        // Additive scene management
        bool loadSceneAdditive(const std::string& scenePath, const std::string& sceneName);
        bool unloadAdditiveScene(const std::string& sceneName);
        std::string getActiveScene() const;
        bool setActiveScene(const std::string& sceneName);
        std::vector<std::string> getLoadedScenes() const;
        bool isSceneLoaded(const std::string& sceneName) const;

        types::PhysicsSettings getPhysicsSettings() const;
        bool setPhysicsSettings(const types::PhysicsSettings& settings);

        types::AudioSettings getAudioSettings() const;
        bool setAudioSettings(const types::AudioSettings& settings);

        types::RenderSettings getRenderSettings() const;
        bool setRenderSettings(const types::RenderSettings& settings);

    private:
        void performDeferredLoad(const std::string& filePath);
        // Deferred Play/Stop snapshot restore (Phase 3). Runs the same prologue as
        // performDeferredLoad, deserializes the in-memory snapshot, then calls
        // finishLoad() so all post-load subsystem wiring fires identically.
        void performDeferredSnapshotRestore();
        // Post-deserialization finalization (completed notification + IBL/navmesh/
        // terrain/settings application + clears sceneTransitioning). Shared by the
        // synchronous and incremental load paths.
        void finishLoad(const std::string& filePath, bool success);
        void performDeferredAdditiveLoad(const PendingAdditiveLoad& load);
        void triggerResourceLoadingForEntity(scene::Entity& entity) const;
        void acquireAndNotifyResources(scene::Entity& entity) const;
    };
}
