#include "EQSServiceImpl.hpp"
#include "../../events/ai/EQSEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "asset/AssetDatabase.hpp"
#include "eqs/EQSQuerySpec.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <exception>
#include <string>
#include <system_error>
#include <utility>

namespace services
{
    namespace
    {
        namespace fs = std::filesystem;

        bool isTemplateFile(const fs::path& path)
        {
            std::string extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".vfeqsquery";
        }

        fs::path projectTemplateFolder()
        {
            return fs::path(asset::AssetDatabase::instance().resolveAssetPath("assets/eqs"));
        }

        // Overlay look: dropped candidates small and gray, kept ones from red (low total) to green (high), the
        // best one large.
        constexpr float kDroppedRadius = 0.15f;
        constexpr float kKeptRadius = 0.25f;
        constexpr float kBestRadius = 0.5f;
        constexpr float kLift = 0.1f; // above the ground the candidates usually lie on
        const glm::vec4 kDroppedColor{0.5f, 0.5f, 0.5f, 0.6f};
        const glm::vec4 kBestColor{0.2f, 0.9f, 1.0f, 1.0f};
    }

    EQSServiceImpl::EQSServiceImpl(IEQSProvider* provider, TemplateFolderProvider folderProvider)
        : provider(provider),
          templateFolder(folderProvider ? std::move(folderProvider) : TemplateFolderProvider(projectTemplateFolder))
    {
        assert(provider && "IEQSProvider must not be null");
    }

    EQSServiceImpl::~EQSServiceImpl()
    {
        subscriptions.clear();
        unregisterEventHandlers();
    }

    void EQSServiceImpl::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::ai::SubmitEQSQueryCommand>(
            [this](const events::ai::SubmitEQSQueryCommand& cmd)
            {
                return provider->submitQuery(cmd.queryName, cmd.context);
            });

        dispatcher.registerQueryHandler<events::ai::GetEQSQueryResultQuery>(
            [this](const events::ai::GetEQSQueryResultQuery& query)
            {
                return provider->getResult(query.handle);
            });

        dispatcher.registerCommandHandler<events::ai::CancelEQSQueryCommand>(
            [this](const events::ai::CancelEQSQueryCommand& cmd)
            {
                provider->releaseQuery(cmd.handle);
            });

        dispatcher.registerQueryHandler<events::ai::RunEQSQueryNowQuery>(
            [this](const events::ai::RunEQSQueryNowQuery& query)
            {
                return provider->runQueryNow(query.queryName, query.context);
            });

        dispatcher.registerQueryHandler<events::ai::ListEQSQueriesQuery>(
            [this](const events::ai::ListEQSQueriesQuery&)
            {
                return listQueries();
            });

        dispatcher.registerQueryHandler<events::ai::GetEQSQueryInfoQuery>(
            [this](const events::ai::GetEQSQueryInfoQuery& query)
            {
                return provider->lookupQuery(query.queryName);
            });

        dispatcher.registerCommandHandler<events::ai::ShowEQSDebugCommand>(
            [this](const events::ai::ShowEQSDebugCommand& cmd)
            {
                showOverlay(cmd.result, cmd.seconds);
            });

        // A saved template replaces the cached one (the next query reads the file again).
        subscriptions.emplace_back(dispatcher.subscribe<::events::resource::AssetSavedNotification>(
            [this](const ::events::resource::AssetSavedNotification& notification)
            {
                const fs::path path(notification.filePath);
                if (isTemplateFile(path))
                    provider->invalidateQuery(path.stem().string());
            }));

        // A finished Play session's queries and results are of no use in Edit mode.
        subscriptions.emplace_back(dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                if (notification.previousMode == EditorMode::Play && notification.currentMode == EditorMode::Edit)
                    provider->releaseAll();
            }));
    }

    void EQSServiceImpl::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::ai::SubmitEQSQueryCommand>();
        dispatcher.unregisterQueryHandler<events::ai::GetEQSQueryResultQuery>();
        dispatcher.unregisterCommandHandler<events::ai::CancelEQSQueryCommand>();
        dispatcher.unregisterQueryHandler<events::ai::RunEQSQueryNowQuery>();
        dispatcher.unregisterQueryHandler<events::ai::ListEQSQueriesQuery>();
        dispatcher.unregisterQueryHandler<events::ai::GetEQSQueryInfoQuery>();
        dispatcher.unregisterCommandHandler<events::ai::ShowEQSDebugCommand>();
    }

    void EQSServiceImpl::update(float deltaTime, bool simulate)
    {
        // Never throw out of the frame task: a throw there terminates the process.
        if (simulate && provider)
        {
            try
            {
                provider->update(kFrameBudgetMs);
            }
            catch (const std::exception& e)
            {
                vfLogError("[EQS] running queued queries threw: {}", e.what());
            }
        }
        drawOverlay(deltaTime);
    }

    std::vector<EQSQueryInfo> EQSServiceImpl::listQueries() const
    {
        std::vector<EQSQueryInfo> infos;
        if (!provider)
            return infos;

        std::error_code ec;
        const fs::path folder = templateFolder();
        if (folder.empty() || !fs::is_directory(folder, ec))
            return infos;

        std::vector<std::string> names;
        for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            std::error_code fileError;
            if (!it->is_regular_file(fileError) || !isTemplateFile(it->path()))
                continue;
            std::string name = it->path().stem().string();
            if (eqs::isValidQueryName(name))
                names.push_back(std::move(name));
        }

        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        infos.reserve(names.size());
        for (const std::string& name : names)
            infos.push_back(provider->lookupQuery(name));
        return infos;
    }

    void EQSServiceImpl::showOverlay(const eqs::EQSDebugResult& result, float seconds)
    {
        std::lock_guard lock(overlayMutex);
        if (seconds <= 0.0f)
        {
            overlay.reset();
            return;
        }
        overlay = result;
        overlaySecondsLeft = seconds;
    }

    void EQSServiceImpl::drawOverlay(float deltaTime)
    {
        std::lock_guard lock(overlayMutex);
        if (!overlay || overlayUnavailable)
            return;

        overlaySecondsLeft -= deltaTime;
        if (overlaySecondsLeft <= 0.0f)
        {
            overlay.reset();
            return;
        }

        float bestTotal = 0.0f;
        for (const auto& candidate : overlay->candidates)
        {
            if (candidate.filteredByTest < 0)
                bestTotal = std::max(bestTotal, candidate.total);
        }

        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            for (const auto& candidate : overlay->candidates)
            {
                events::debugdraw::DrawSphereCommand sphere;
                sphere.center = candidate.position + glm::vec3(0.0f, kLift, 0.0f);
                if (candidate.filteredByTest >= 0)
                {
                    sphere.radius = kDroppedRadius;
                    sphere.color = kDroppedColor;
                }
                else
                {
                    const float t = bestTotal > 0.0f ? std::clamp(candidate.total / bestTotal, 0.0f, 1.0f) : 1.0f;
                    sphere.radius = kKeptRadius;
                    sphere.color = glm::vec4(1.0f - t, t, 0.2f, 0.9f);
                }
                dispatcher.execute(sphere);
            }

            if (overlay->result.hasResults())
            {
                events::debugdraw::DrawSphereCommand best;
                best.center = overlay->result.getBestPosition() + glm::vec3(0.0f, kLift, 0.0f);
                best.radius = kBestRadius;
                best.color = kBestColor;
                dispatcher.execute(best);
            }
        }
        catch (const std::exception& e)
        {
            overlayUnavailable = true;
            overlay.reset();
            vfLogWarning("[EQS] the debug overlay is unavailable: {}", e.what());
        }
    }
}
