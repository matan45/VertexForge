#include "print/Log.hpp"
#include "HeightmapGenerationHandler.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/terrain/HeightmapGenerationEvents.hpp"
#include "asset/AssetMetadata.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "resource/AtomicFileReplace.hpp"
#include "generator/HeightmapGenerator.hpp"
#include "generator/HeightmapPresets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace handlers
{
    namespace
    {
        namespace fs = std::filesystem;

        // HeightmapGenerationRequest::preset value meaning "no preset": generator defaults + overrides.
        constexpr std::string_view customPreset = "custom";

        constexpr uint32_t minResolution = 64;
        constexpr uint32_t maxResolution = 8192;

        // generate() reports [0, 1]; writing the file and building the preview take the rest.
        constexpr float generateProgressShare = 0.9f;

        // HeightmapNoiseSettings documents the procedural enums by integer value.
        static_assert(static_cast<uint8_t>(procedural::NoiseType::Perlin) == 0 &&
                      static_cast<uint8_t>(procedural::NoiseType::Simplex) == 1);
        static_assert(static_cast<uint8_t>(procedural::FractalType::None) == 0 &&
                      static_cast<uint8_t>(procedural::FractalType::FBM) == 1 &&
                      static_cast<uint8_t>(procedural::FractalType::Ridged) == 2 &&
                      static_cast<uint8_t>(procedural::FractalType::Billowy) == 3);

        struct HeightmapJob
        {
            procedural::HeightmapParams params;
            fs::path target;
            bool overwrite = false;
            asset::AssetGUID guid;
            services::HeightmapJobStatus status; // Running, with everything known up front
        };

        // Request paths are UTF-8, and narrow std::filesystem::path construction uses the ANSI code
        // page on Windows, so go through u8string (as mcp/tools/PathSandbox.hpp does).
        fs::path pathFromUtf8(std::string_view utf8)
        {
            return fs::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string pathToUtf8(const fs::path& path)
        {
            const std::u8string utf8 = path.u8string();
            return std::string(utf8.begin(), utf8.end());
        }

        bool hasVfImageExtension(const fs::path& path)
        {
            const std::wstring extension = path.extension().wstring();
            constexpr std::wstring_view expected = L".vfimage";
            return std::equal(extension.begin(), extension.end(), expected.begin(), expected.end(),
                              [](wchar_t actual, wchar_t lower) { return std::towlower(actual) == lower; });
        }

        // Local time, in the format HeightmapGeneratorWindow stamps on its exports.
        std::string currentTimestamp()
        {
            auto time = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            return buf;
        }

        // Applies one optional numeric override after checking it against the range of the
        // matching HeightmapGeneratorWindow widget. NaN fails the range test.
        template <typename T>
        bool applyOverride(const std::optional<T>& value, T minValue, T maxValue, std::string_view name,
                           T& target, std::string& error)
        {
            if (!value)
                return true;

            if (!(*value >= minValue && *value <= maxValue))
            {
                error = std::format("{} must be {}..{} (got {})", name, minValue, maxValue, *value);
                return false;
            }

            target = *value;
            return true;
        }

        // The generator parameters for `request`: defaults -> preset (unless "custom") -> overrides
        // -> resolution and featureScale rescaling. Returns why the request is invalid, or "".
        std::string buildParams(const services::HeightmapGenerationRequest& request,
                                procedural::HeightmapParams& params)
        {
            if (request.resolution < minResolution || request.resolution > maxResolution)
            {
                return std::format("resolution must be {}..{} (got {})",
                                   minResolution, maxResolution, request.resolution);
            }

            if (!std::isfinite(request.featureScale) || request.featureScale <= 0.0f)
                return std::format("featureScale must be a finite number > 0 (got {})", request.featureScale);

            if (request.preset != customPreset)
            {
                const auto preset = procedural::findHeightmapPreset(request.preset);
                if (!preset)
                {
                    std::string choices(customPreset);
                    for (const auto& info : procedural::heightmapPresets())
                    {
                        choices += ", ";
                        choices += info.id;
                    }
                    return std::format("unknown preset '{}' (expected one of: {})", request.preset, choices);
                }
                procedural::applyHeightmapPreset(*preset, params);
            }

            if (request.noiseType)
            {
                if (*request.noiseType > 1)
                {
                    return std::format("noiseType must be 0 (Perlin) or 1 (Simplex) (got {})",
                                       static_cast<int>(*request.noiseType));
                }
                params.noiseType = static_cast<procedural::NoiseType>(*request.noiseType);
            }

            if (request.fractalType)
            {
                if (*request.fractalType > 3)
                {
                    return std::format("fractalType must be 0 (None), 1 (FBM), 2 (Ridged) or 3 (Billowy) (got {})",
                                       static_cast<int>(*request.fractalType));
                }
                params.fractalType = static_cast<procedural::FractalType>(*request.fractalType);
            }

            std::string error;
            if (!applyOverride(request.octaves, 1, 16, "octaves", params.octaves, error) ||
                !applyOverride(request.frequency, 0.001f, 0.1f, "frequency", params.frequency, error) ||
                !applyOverride(request.lacunarity, 1.0f, 4.0f, "lacunarity", params.lacunarity, error) ||
                !applyOverride(request.persistence, 0.0f, 1.0f, "persistence", params.persistence, error) ||
                !applyOverride(request.heightExponent, 0.1f, 5.0f, "heightExponent", params.heightExponent, error) ||
                !applyOverride(request.warpAmplitude, 0.0f, 200.0f, "warpAmplitude",
                               params.domainWarp.amplitude, error) ||
                !applyOverride(request.warpFrequency, 0.001f, 0.05f, "warpFrequency",
                               params.domainWarp.frequency, error) ||
                !applyOverride(request.terraceSteps, 2, 64, "terraceSteps", params.terraceSteps, error))
            {
                return error;
            }

            if (request.domainWarp)
                params.domainWarp.enabled = *request.domainWarp;
            if (request.invert)
                params.invert = *request.invert;
            if (request.terracing)
                params.terracing = *request.terracing;

            procedural::scaleHeightmapFeatures(params, request.resolution, request.resolutionIndependent,
                                               request.featureScale);
            params.width = request.resolution;
            params.height = request.resolution;
            params.seed = request.seed;
            return {};
        }

        services::HeightmapNoiseSettings toNoiseSettings(const procedural::HeightmapParams& params)
        {
            services::HeightmapNoiseSettings settings;
            settings.noiseType = static_cast<uint8_t>(params.noiseType);
            settings.fractalType = static_cast<uint8_t>(params.fractalType);
            settings.octaves = params.octaves;
            settings.frequency = params.frequency;
            settings.lacunarity = params.lacunarity;
            settings.persistence = params.persistence;
            settings.heightExponent = params.heightExponent;
            settings.domainWarp = params.domainWarp.enabled;
            settings.warpAmplitude = params.domainWarp.amplitude;
            settings.warpFrequency = params.domainWarp.frequency;
            settings.invert = params.invert;
            settings.terracing = params.terracing;
            settings.terraceSteps = params.terraceSteps;
            return settings;
        }

        // Generates the heightmap into `tempPath`, swaps it over the target and writes the .vfmeta.
        // Returns why it failed, or "". The target is only ever replaced whole.
        std::string writeHeightmap(const HeightmapJob& job, const fs::path& tempPath,
                                   std::atomic<float>& progress)
        {
            const auto result = procedural::HeightmapGenerator::generate(job.params,
                [&progress](float p) { progress.store(p * generateProgressShare); });
            if (!result.valid())
                return "the generator produced no pixels";

            // saveAsVFImage opens a narrow path, which Windows reads in the ANSI code page --
            // path::string() is that encoding (and throws for a name it cannot express).
            if (!procedural::HeightmapGenerator::saveAsVFImage(result, tempPath.string()))
                return std::format("could not write {}", pathToUtf8(tempPath));

            // Begin refused an existing file without overwrite; don't clobber one that appeared since.
            std::error_code ec;
            if (!job.overwrite && fs::exists(job.target, ec))
                return std::format("{} appeared while generating (pass overwrite to replace it)", pathToUtf8(job.target));

            // One MoveFileExW(REPLACE_EXISTING) of a flushed sibling, whether or not the target
            // exists: it ends up the complete old file or the complete new one, never half-written.
            if (!resource::replaceFileAtomically(tempPath, job.target))
                return std::format("could not replace {} (is it open in another program?)", pathToUtf8(job.target));

            // Same fields HeightmapGeneratorWindow writes, but under the GUID Begin resolved.
            asset::AssetMetadata metadata;
            metadata.guid = job.guid;
            metadata.type = resource::AssetType::Texture;
            metadata.importSourcePath = "procedural://heightmap";
            metadata.formatVersion = 1;
            metadata.importTimestamp = currentTimestamp();
            if (!asset::AssetMetadataSerializer::save(metadata, asset::AssetMetadataSerializer::getMetaPath(job.target)))
            {
                // Not a failure: the image is complete and usable, and the AssetSavedNotification
                // still registers it (the asset database writes a .vfmeta for a file it lacks one for).
                vfLogWarning("[Heightmap] Wrote {} but not its .vfmeta", pathToUtf8(job.target));
            }

            return {};
        }

        // The job body, on the worker thread; failures come back in the status. Never touches the
        // dispatcher.
        services::HeightmapJobStatus runHeightmapJob(HeightmapJob job, std::atomic<float>& progress)
        {
            const auto started = std::chrono::steady_clock::now();
            services::HeightmapJobStatus status = std::move(job.status);

            fs::path tempPath = job.target;
            tempPath += ".tmp";

            try
            {
                status.error = writeHeightmap(job, tempPath, progress);
            }
            catch (const std::exception& e)
            {
                status.error = std::format("heightmap generation failed: {}", e.what());
            }

            // A successful swap consumed the temp; a failure must not leave it behind.
            std::error_code ignored;
            fs::remove(tempPath, ignored);

            if (status.error.empty())
            {
                // The file is final; a preview failure only costs the preview.
                try
                {
                    auto preview = procedural::HeightmapGenerator::generatePreview(job.params);
                    if (preview.valid())
                    {
                        status.previewSize = preview.width;
                        status.previewRgba = std::move(preview.rgbaData);
                    }
                }
                catch (const std::exception& e)
                {
                    vfLogWarning("[Heightmap] Preview of {} failed: {}", status.outputPath, e.what());
                }

                status.state = services::HeightmapJobState::Done;
                progress.store(1.0f);
            }
            else
            {
                status.state = services::HeightmapJobState::Failed;
            }

            status.progress = progress.load();
            status.durationMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            return status;
        }
    }

    HeightmapGenerationHandler::~HeightmapGenerationHandler()
    {
        unregisterEventHandlers();

        // The worker writes `progress`, so it must not outlive the handler -- the same wait as
        // HeightmapGeneratorWindow's destructor.
        if (runningJob.valid())
        {
            runningJob.wait();
        }
    }

    void HeightmapGenerationHandler::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::heightmapGeneration::BeginHeightmapGenerationCommand>(
            [this](const events::heightmapGeneration::BeginHeightmapGenerationCommand& cmd) -> services::HeightmapJobStart {
                return handleBegin(cmd.request);
            });

        dispatcher.registerCommandHandler<events::heightmapGeneration::PollHeightmapGenerationCommand>(
            [this](const events::heightmapGeneration::PollHeightmapGenerationCommand& cmd) -> services::HeightmapJobStatus {
                return handlePoll(cmd.jobId);
            });
    }

    void HeightmapGenerationHandler::unregisterEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::heightmapGeneration::BeginHeightmapGenerationCommand>();
        dispatcher.unregisterCommandHandler<events::heightmapGeneration::PollHeightmapGenerationCommand>();
    }

    services::HeightmapJobStart HeightmapGenerationHandler::handleBegin(const services::HeightmapGenerationRequest& request)
    {
        services::HeightmapJobStart start;

        // A job that finished but was never polled no longer blocks the next one.
        collectFinishedJob();
        if (runningJobId != 0)
        {
            start.error = "a heightmap is already being generated";
            return start;
        }

        procedural::HeightmapParams params;
        start.error = buildParams(request, params);
        if (!start.error.empty())
        {
            return start;
        }

        const fs::path target = pathFromUtf8(request.outputPath);
        if (request.outputPath.empty() || !target.is_absolute())
        {
            start.error = std::format("outputPath must be an absolute path (got '{}')", request.outputPath);
            return start;
        }
        if (!hasVfImageExtension(target))
        {
            start.error = std::format("outputPath must end in .vfImage (got '{}')", request.outputPath);
            return start;
        }

        std::error_code ec;
        const bool targetExists = fs::exists(target, ec);
        if (ec)
        {
            start.error = std::format("cannot access {}: {}", request.outputPath, ec.message());
            return start;
        }
        if (targetExists && fs::is_directory(target, ec))
        {
            start.error = std::format("{} is a folder", request.outputPath);
            return start;
        }
        if (targetExists && !request.overwrite)
        {
            start.error = std::format("{} already exists (pass overwrite to replace it)", request.outputPath);
            return start;
        }

        fs::create_directories(target.parent_path(), ec);
        if (ec)
        {
            start.error = std::format("cannot create the folder for {}: {}", request.outputPath, ec.message());
            return start;
        }

        HeightmapJob job;
        job.params = params;
        job.target = target;
        job.overwrite = request.overwrite;

        // Regenerating keeps the asset's identity: a fresh GUID (what the window does on every
        // export) would leave the asset database's entry, and anything referencing it, pointing at
        // a GUID the .vfmeta no longer has.
        const auto existingMeta =
            asset::AssetMetadataSerializer::load(asset::AssetMetadataSerializer::getMetaPath(target));
        job.guid = existingMeta ? existingMeta->guid : asset::AssetGUID::generate();

        job.status.state = services::HeightmapJobState::Running;
        job.status.outputPath = request.outputPath;
        job.status.width = request.resolution;
        job.status.height = request.resolution;
        job.status.effective = toNoiseSettings(params);

        runningStatus = job.status;
        progress.store(0.0f);

        try
        {
            runningJob = std::async(std::launch::async,
                [job = std::move(job), progressPtr = &progress]() mutable
                {
                    return runHeightmapJob(std::move(job), *progressPtr);
                });
        }
        catch (const std::system_error& e)
        {
            start.error = std::format("could not start the generator thread: {}", e.what());
            return start;
        }

        runningJobId = nextJobId++;
        vfLogInfo("[Heightmap] Job {}: generating {} ({}x{}, preset '{}', seed {})", runningJobId,
                  request.outputPath, request.resolution, request.resolution, request.preset, request.seed);

        start.accepted = true;
        start.jobId = runningJobId;
        return start;
    }

    services::HeightmapJobStatus HeightmapGenerationHandler::handlePoll(uint64_t jobId)
    {
        collectFinishedJob();

        if (jobId != 0 && jobId == runningJobId)
        {
            services::HeightmapJobStatus status = runningStatus;
            status.progress = progress.load();
            return status;
        }

        for (const auto& finished : finishedJobs)
        {
            if (finished.jobId == jobId)
            {
                return finished.status;
            }
        }

        return {}; // Unknown: never started, or evicted from the history
    }

    void HeightmapGenerationHandler::collectFinishedJob()
    {
        if (runningJobId == 0 || !runningJob.valid() ||
            runningJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return;
        }

        FinishedJob finished;
        finished.jobId = runningJobId;
        try
        {
            finished.status = runningJob.get();
        }
        catch (const std::exception& e)
        {
            // runHeightmapJob catches its own failures; this only guards the plumbing.
            finished.status = runningStatus;
            finished.status.state = services::HeightmapJobState::Failed;
            finished.status.error = std::format("heightmap generation failed: {}", e.what());
        }
        runningJobId = 0;

        const bool done = finished.status.state == services::HeightmapJobState::Done;
        const std::string outputPath = finished.status.outputPath;
        if (done)
        {
            vfLogInfo("[Heightmap] Job {}: wrote {} in {:.0f} ms", finished.jobId, outputPath,
                      finished.status.durationMs);
        }
        else
        {
            vfLogError("[Heightmap] Job {} failed: {}", finished.jobId, finished.status.error);
        }

        finishedJobs.push_back(std::move(finished));
        if (finishedJobs.size() > maxFinishedJobs)
        {
            finishedJobs.pop_front();
        }

        if (done)
        {
            // Main thread, once per job, after the .vfImage and its .vfmeta are complete: the asset
            // database registers the file under the .vfmeta GUID and the content browser refreshes.
            events::resource::AssetSavedNotification saved;
            saved.filePath = outputPath;
            events::EventDispatcher::instance().publish(saved);
        }
    }
}
