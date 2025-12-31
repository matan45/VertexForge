#include "AsyncIBLLoader.hpp"
#include "../render/IBL.hpp"
#include "../core/Device.hpp"
#include "../core/Texture.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"
#include <chrono>

namespace loaders
{
    AsyncIBLLoader::AsyncIBLLoader(core::Device& device)
        : device(device)
    {
    }

    void AsyncIBLLoader::startLoad(const std::string& hdrPath)
    {
        std::lock_guard lock(mutex);

        // Create new load state
        loadState = std::make_unique<IBLLoadState>();
        loadState->hdrPath = hdrPath;
        loadState->state = services::LoadingState::Loading;
        loadState->currentStage = Stage::HDRFile;
        loadState->progress = 0.0f;
        loadState->statusMessage = services::IBLLoadingProgress::getStageName(Stage::HDRFile);

        // Start async HDR file loading
        loadState->hdrFuture = resource::ResourceManager::loadHDRAsync(hdrPath);

        loggerInfo("Started async IBL load: {}", hdrPath);
    }

    void AsyncIBLLoader::cancelLoad()
    {
        std::lock_guard lock(mutex);

        if (loadState)
        {
            loadState->cancelled = true;
            loadState->state = services::LoadingState::Cancelled;
            loadState->statusMessage = "Cancelled";
            loggerInfo("Cancelled IBL load");
        }
    }

    bool AsyncIBLLoader::hasActiveLoad() const
    {
        std::lock_guard lock(mutex);
        return loadState != nullptr && loadState->state != services::LoadingState::Complete &&
               loadState->state != services::LoadingState::Error &&
               loadState->state != services::LoadingState::Cancelled;
    }

    bool AsyncIBLLoader::processFrame(render::IBL* ibl)
    {
        std::lock_guard lock(mutex);

        if (!loadState)
        {
            return true;  // No active load, done
        }

        if (loadState->cancelled)
        {
            return true;  // Cancelled, done
        }

        if (loadState->state == services::LoadingState::Complete ||
            loadState->state == services::LoadingState::Error)
        {
            return true;  // Already finished
        }

        try
        {
            switch (loadState->currentStage)
            {
                case Stage::HDRFile:
                    if (processHDRFileStage())
                    {
                        loadState->currentStage = Stage::EnvironmentCubemap;
                        updateProgress();
                    }
                    break;

                case Stage::EnvironmentCubemap:
                    if (processEnvironmentCubemapStage(ibl))
                    {
                        loadState->currentStage = Stage::Irradiance;
                        updateProgress();
                    }
                    break;

                case Stage::Irradiance:
                    if (processIrradianceStage(ibl))
                    {
                        loadState->currentStage = Stage::BRDF;
                        updateProgress();
                    }
                    break;

                case Stage::BRDF:
                    if (processBRDFStage(ibl))
                    {
                        loadState->currentStage = Stage::Prefiltered;
                        updateProgress();
                    }
                    break;

                case Stage::Prefiltered:
                    if (processPrefilteredStage(ibl))
                    {
                        loadState->currentStage = Stage::Skybox;
                        updateProgress();
                    }
                    break;

                case Stage::Skybox:
                    if (processSkyboxStage(ibl))
                    {
                        loadState->currentStage = Stage::Done;
                        loadState->state = services::LoadingState::Complete;
                        loadState->progress = 1.0f;
                        loadState->statusMessage = "Complete";
                        loggerInfo("IBL loading complete: {}", loadState->hdrPath);
                        return true;
                    }
                    break;

                case Stage::Done:
                    return true;
            }
        }
        catch (const std::exception& e)
        {
            loadState->state = services::LoadingState::Error;
            loadState->errorMessage = e.what();
            loadState->statusMessage = "Error: " + std::string(e.what());
            loggerError("IBL loading error: {}", e.what());
            return true;
        }

        return false;  // Still loading
    }

    bool AsyncIBLLoader::processHDRFileStage()
    {
        if (!loadState->hdrFuture.valid())
        {
            loadState->state = services::LoadingState::Error;
            loadState->errorMessage = "HDR future is invalid";
            return false;
        }

        // Non-blocking check if future is ready
        auto status = loadState->hdrFuture.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready)
        {
            // Still loading, update progress estimate
            loadState->progress = std::min(0.18f, loadState->progress + 0.01f);
            return false;
        }

        // Future is ready, get the data
        loadState->hdrData = loadState->hdrFuture.get();

        if (!loadState->hdrData || loadState->hdrData->pixels.empty())
        {
            loadState->state = services::LoadingState::Error;
            loadState->errorMessage = "Failed to load HDR file";
            return false;
        }

        loggerInfo("HDR file loaded, creating GPU texture");
        return true;
    }

    bool AsyncIBLLoader::processEnvironmentCubemapStage(render::IBL* ibl)
    {
        // First, create the HDR texture from the loaded data
        if (!ibl->getHDRTexture())
        {
            ibl->initHDRTexture(*loadState->hdrData);
            // Release CPU data after creating texture
            loadState->hdrData.reset();
        }

        // Generate environment cubemap
        ibl->generateEnvironmentCubemap();
        loggerInfo("Environment cubemap generated");
        return true;
    }

    bool AsyncIBLLoader::processIrradianceStage(render::IBL* ibl)
    {
        ibl->generateIrradiance();
        loggerInfo("Irradiance map generated");
        return true;
    }

    bool AsyncIBLLoader::processBRDFStage(render::IBL* ibl)
    {
        ibl->generateBRDFLUT();
        loggerInfo("BRDF LUT generated");
        return true;
    }

    bool AsyncIBLLoader::processPrefilteredStage(render::IBL* ibl)
    {
        ibl->generatePrefiltered();
        loggerInfo("Prefiltered environment generated");
        return true;
    }

    bool AsyncIBLLoader::processSkyboxStage(render::IBL* ibl)
    {
        ibl->initSkybox();
        loggerInfo("Skybox initialized");
        return true;
    }

    void AsyncIBLLoader::updateProgress()
    {
        if (!loadState) return;

        loadState->progress = services::IBLLoadingProgress::getStageProgress(
            loadState->currentStage, 0);
        loadState->statusMessage = services::IBLLoadingProgress::getStageName(
            loadState->currentStage);
    }

    services::IBLLoadingProgress AsyncIBLLoader::getProgress() const
    {
        std::lock_guard lock(mutex);

        services::IBLLoadingProgress progress;

        if (!loadState)
        {
            progress.state = services::LoadingState::Idle;
            return progress;
        }

        progress.state = loadState->state;
        progress.progress = loadState->progress;
        progress.statusMessage = loadState->statusMessage;
        progress.errorMessage = loadState->errorMessage;
        progress.currentStage = loadState->currentStage;
        progress.prefilteredMipLevel = 0;  // Not tracking per-mip for now

        return progress;
    }

    bool AsyncIBLLoader::isComplete() const
    {
        std::lock_guard lock(mutex);
        return loadState && loadState->state == services::LoadingState::Complete;
    }

    bool AsyncIBLLoader::hasError() const
    {
        std::lock_guard lock(mutex);
        return loadState && loadState->state == services::LoadingState::Error;
    }

    std::string AsyncIBLLoader::getErrorMessage() const
    {
        std::lock_guard lock(mutex);
        if (loadState)
        {
            return loadState->errorMessage;
        }
        return "";
    }

    void AsyncIBLLoader::clearState()
    {
        std::lock_guard lock(mutex);
        loadState.reset();
    }
}
