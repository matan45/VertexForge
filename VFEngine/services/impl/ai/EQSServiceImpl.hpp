#pragma once
#include "../../interfaces/ai/IEQSService.hpp"
#include "../../providers/ai/IEQSProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace services
{
    // The EQS events (services/events/ai/EQSEvents.hpp) over the provider, the EQS frame task (update) and
    // the debug overlay (VK-1660). Saving a .vfEQSQuery (AssetSavedNotification) drops its cached template;
    // Play -> Edit releases every query.
    class EQSServiceImpl : public IEQSService
    {
    public:
        // The folder ListEQSQueriesQuery lists; default: the project's assets/eqs.
        using TemplateFolderProvider = std::function<std::filesystem::path()>;

        // Queued queries get this many milliseconds of the frame (whole queries, so one may overrun it).
        static constexpr float kFrameBudgetMs = 2.0f;

        explicit EQSServiceImpl(IEQSProvider* provider, TemplateFolderProvider folderProvider = {});
        ~EQSServiceImpl() override;

        EQSServiceImpl(const EQSServiceImpl&) = delete;
        EQSServiceImpl& operator=(const EQSServiceImpl&) = delete;

        void registerEventHandlers() override;
        void update(float deltaTime, bool simulate) override;

        // Every valid template name in the folder, sorted, with the provider's view of it.
        std::vector<EQSQueryInfo> listQueries() const;

    private:
        void unregisterEventHandlers();
        void showOverlay(const eqs::EQSDebugResult& result, float seconds);
        void drawOverlay(float deltaTime);

        IEQSProvider* provider;
        TemplateFolderProvider templateFolder;
        bool handlersRegistered = false;
        std::vector<::events::ScopedSubscription> subscriptions;

        // ShowEQSDebugCommand arrives on the main thread, update() runs in the EQS task.
        std::mutex overlayMutex;
        std::optional<eqs::EQSDebugResult> overlay;
        float overlaySecondsLeft = 0.0f;
        bool overlayUnavailable = false; // the debug-draw commands have no handler in this binary
    };
}
