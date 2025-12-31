#include "AsyncSceneLoader.hpp"
#include "SceneSerialization.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>

namespace serialization
{
    AsyncSceneLoader::~AsyncSceneLoader()
    {
        cancel();
        // Wait for background thread to finish
        if (jsonParseFuture.valid())
        {
            try
            {
                jsonParseFuture.wait();
            }
            catch (...)
            {
                // Ignore exceptions during cleanup
            }
        }
    }

    void AsyncSceneLoader::startLoad(const std::string& path,
                                      std::shared_ptr<scene::SceneGraphSystem> graph,
                                      ProgressCallback progressCb,
                                      CompletionCallback completionCb)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (phase != Phase::Idle && phase != Phase::Complete && phase != Phase::Error && phase != Phase::Cancelled)
        {
            vfLogWarning("AsyncSceneLoader: Already loading a scene, ignoring new request");
            return;
        }

        cancelled.store(false);
        scenePath = path;
        sceneGraph = graph;
        progressCallback = std::move(progressCb);
        completionCallback = std::move(completionCb);
        phase = Phase::ParsingJSON;
        errorMessage.clear();
        iblPath.clear();
        entitiesLoaded = 0;
        totalEntities = 0;
        currentEntityName.clear();
        sceneJson.reset();

        // Start background JSON parsing
        jsonParseFuture = std::async(std::launch::async, [this]() {
            return parseJSONFile();
        });

        vfLogInfo("AsyncSceneLoader: Started loading scene: {}", path);
    }

    void AsyncSceneLoader::cancel()
    {
        cancelled.store(true);

        std::lock_guard<std::mutex> lock(mutex);
        if (phase != Phase::Idle && phase != Phase::Complete && phase != Phase::Error)
        {
            phase = Phase::Cancelled;
            vfLogInfo("AsyncSceneLoader: Cancelled loading scene");
        }
    }

    bool AsyncSceneLoader::isLoading() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return phase != Phase::Idle &&
               phase != Phase::Complete &&
               phase != Phase::Error &&
               phase != Phase::Cancelled;
    }

    bool AsyncSceneLoader::update()
    {
        Phase currentPhase;
        bool isCancelled;

        {
            std::lock_guard<std::mutex> lock(mutex);
            currentPhase = phase;
            isCancelled = cancelled.load();
        }

        if (isCancelled)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (phase != Phase::Cancelled && phase != Phase::Idle)
            {
                finishLoading(false, "Loading cancelled");
                phase = Phase::Cancelled;
            }
            return false;
        }

        switch (currentPhase)
        {
            case Phase::ParsingJSON:
            {
                // Check if JSON parsing is complete (non-blocking)
                bool futureReady = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    futureReady = jsonParseFuture.valid() &&
                        jsonParseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
                }

                if (futureReady)
                {
                    bool success = false;
                    std::string error;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        try
                        {
                            success = jsonParseFuture.get();
                            error = errorMessage;
                        }
                        catch (const std::exception& e)
                        {
                            error = e.what();
                            errorMessage = error;
                        }
                    }

                    if (!success)
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        finishLoading(false, error);
                        return false;
                    }

                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        phase = Phase::ClearingScene;
                    }
                }
                return true;
            }

            case Phase::ClearingScene:
            {
                // loadSceneInto already clears the scene, so just move to next phase
                std::lock_guard<std::mutex> lock(mutex);
                phase = Phase::DeserializingScene;
                return true;
            }

            case Phase::DeserializingScene:
            {
                // deserializeScene is called WITHOUT holding the lock
                // because it has callbacks that need to update state
                bool success = deserializeScene();

                std::lock_guard<std::mutex> lock(mutex);
                if (!success)
                {
                    finishLoading(false, errorMessage);
                    return false;
                }
                phase = Phase::Complete;
                finishLoading(true);
                return false;
            }

            case Phase::Complete:
            case Phase::Error:
            case Phase::Cancelled:
            case Phase::Idle:
                return false;

            default:
                return false;
        }
    }

    AsyncSceneLoader::LoadingState AsyncSceneLoader::getState() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        LoadingState state;
        state.phase = phase;
        state.errorMessage = errorMessage;
        state.currentEntityName = currentEntityName;
        state.entitiesLoaded = entitiesLoaded;
        state.totalEntities = totalEntities;

        switch (phase)
        {
            case Phase::Idle:
                state.progress = 0.0f;
                state.statusMessage = "Idle";
                break;
            case Phase::ParsingJSON:
                state.progress = 0.1f;
                state.statusMessage = "Parsing scene file...";
                break;
            case Phase::ClearingScene:
                state.progress = 0.2f;
                state.statusMessage = "Clearing scene...";
                break;
            case Phase::DeserializingScene:
                state.progress = 0.2f + 0.7f * (totalEntities > 0 ?
                    static_cast<float>(entitiesLoaded) / static_cast<float>(totalEntities) : 0.0f);
                state.statusMessage = "Loading entity: " + currentEntityName;
                break;
            case Phase::LoadingIBL:
                state.progress = 0.9f;
                state.statusMessage = "Loading IBL...";
                break;
            case Phase::Complete:
                state.progress = 1.0f;
                state.statusMessage = "Complete";
                break;
            case Phase::Error:
                state.progress = 0.0f;
                state.statusMessage = "Error: " + errorMessage;
                break;
            case Phase::Cancelled:
                state.progress = 0.0f;
                state.statusMessage = "Cancelled";
                break;
        }

        return state;
    }

    std::string AsyncSceneLoader::getIBLPath() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return iblPath;
    }

    bool AsyncSceneLoader::parseJSONFile()
    {
        try
        {
            std::ifstream file(scenePath);
            if (!file.is_open())
            {
                std::lock_guard<std::mutex> lock(mutex);
                errorMessage = "Failed to open file: " + scenePath;
                return false;
            }

            auto parsedJson = std::make_shared<json>(json::parse(file));
            file.close();

            // Validate structure
            if (!parsedJson->is_object())
            {
                std::lock_guard<std::mutex> lock(mutex);
                errorMessage = "Invalid scene file: root is not a JSON object";
                return false;
            }

            if (!parsedJson->contains("root") || !(*parsedJson)["root"].is_object())
            {
                std::lock_guard<std::mutex> lock(mutex);
                errorMessage = "Invalid scene file: missing or invalid 'root' object";
                return false;
            }

            // Store the parsed JSON
            {
                std::lock_guard<std::mutex> lock(mutex);
                sceneJson = parsedJson;
                totalEntities = SceneSerialization::countEntities((*sceneJson)["root"]);
            }

            return true;
        }
        catch (const json::parse_error& e)
        {
            std::lock_guard<std::mutex> lock(mutex);
            errorMessage = std::string("JSON parse error: ") + e.what();
            return false;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> lock(mutex);
            errorMessage = std::string("Failed to read scene file: ") + e.what();
            return false;
        }
    }

    void AsyncSceneLoader::clearScene()
    {
        if (sceneGraph)
        {
            sceneGraph->clearScene();
        }
    }

    bool AsyncSceneLoader::deserializeScene()
    {
        if (!sceneGraph)
        {
            errorMessage = "Scene graph is null";
            return false;
        }

        try
        {
            // Create progress callback wrapper
            auto wrappedProgressCallback = [this](const std::string& entityName, size_t loaded, size_t total) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    currentEntityName = entityName;
                    entitiesLoaded = loaded;
                    totalEntities = total;
                }

                if (progressCallback)
                {
                    progressCallback(entityName, loaded, total);
                }
            };

            // Use the public API to load the scene
            // Note: This re-reads the file but keeps entity deserialization on main thread
            bool success = SceneSerialization::loadSceneInto(scenePath, *sceneGraph, wrappedProgressCallback);

            if (!success)
            {
                errorMessage = "Failed to load scene";
                return false;
            }

            // Extract IBL path if present
            scene::Entity& root = sceneGraph->GetRoot();
            if (root.hasComponent<components::IBLComponent>())
            {
                const auto& ibl = root.getComponent<components::IBLComponent>();
                if (!ibl.fileName.empty())
                {
                    iblPath = ibl.fileName;
                }
            }

            vfLogInfo("AsyncSceneLoader: Scene loaded successfully from: {}", scenePath);
            return true;
        }
        catch (const std::exception& e)
        {
            errorMessage = std::string("Failed to deserialize scene: ") + e.what();
            vfLogError("AsyncSceneLoader: {}", errorMessage);
            sceneGraph->clearScene();
            return false;
        }
    }

    void AsyncSceneLoader::finishLoading(bool success, const std::string& error)
    {
        if (!success)
        {
            phase = Phase::Error;
            errorMessage = error;
        }
        else
        {
            phase = Phase::Complete;
        }

        sceneJson.reset();

        if (completionCallback)
        {
            completionCallback(success, errorMessage);
        }
    }
}
