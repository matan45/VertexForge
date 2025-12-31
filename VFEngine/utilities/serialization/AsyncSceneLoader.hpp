#pragma once
#include "../../services/data/AsyncLoadingTypes.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <future>
#include <mutex>
#include <atomic>
#include <functional>

namespace scene {
    class SceneGraphSystem;
}

namespace serialization
{
    using json = nlohmann::json;

    class AsyncSceneLoader
    {
    public:
        using ProgressCallback = std::function<void(const std::string& entityName, size_t loaded, size_t total)>;
        using CompletionCallback = std::function<void(bool success, const std::string& errorMessage)>;

        enum class Phase
        {
            Idle,
            ParsingJSON,        // Background thread - parsing JSON file
            ClearingScene,      // Main thread - clearing old scene
            DeserializingScene, // Main thread - creating entities (chunked)
            LoadingIBL,         // Main thread - async IBL loading
            Complete,
            Error,
            Cancelled
        };

        struct LoadingState
        {
            Phase phase = Phase::Idle;
            float progress = 0.0f;
            std::string statusMessage;
            std::string errorMessage;
            std::string currentEntityName;
            size_t entitiesLoaded = 0;
            size_t totalEntities = 0;
        };

        AsyncSceneLoader() = default;
        ~AsyncSceneLoader();

        // Non-copyable
        AsyncSceneLoader(const AsyncSceneLoader&) = delete;
        AsyncSceneLoader& operator=(const AsyncSceneLoader&) = delete;

        // Start async scene loading
        void startLoad(const std::string& scenePath,
                       std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                       ProgressCallback progressCallback = nullptr,
                       CompletionCallback completionCallback = nullptr);

        // Cancel loading
        void cancel();

        // Check if loading is in progress
        bool isLoading() const;

        // Update loading state (call each frame from main thread)
        // Returns true if loading is still in progress
        bool update();

        // Get current loading state
        LoadingState getState() const;

        // Get the IBL path if scene has one (after loading complete)
        std::string getIBLPath() const;

    private:
        mutable std::mutex mutex;
        std::atomic<bool> cancelled{false};

        // Loading state
        Phase phase = Phase::Idle;
        std::string scenePath;
        std::string errorMessage;
        std::string iblPath;

        // Background thread data
        std::future<bool> jsonParseFuture;
        std::shared_ptr<json> sceneJson;

        // Scene graph reference
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // Progress tracking
        size_t entitiesLoaded = 0;
        size_t totalEntities = 0;
        std::string currentEntityName;

        // Callbacks
        ProgressCallback progressCallback;
        CompletionCallback completionCallback;

        // Helper methods
        bool parseJSONFile();
        void clearScene();
        bool deserializeScene();
        void finishLoading(bool success, const std::string& error = "");
    };
}
