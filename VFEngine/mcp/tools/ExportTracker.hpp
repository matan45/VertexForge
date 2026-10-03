#pragma once

#include "PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/project/ExportEvents.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mcp::tools
{
    // Mirrors the editor's game export from its notifications so game_export_status
    // can be polled. ExportHandler publishes Started (and a pre-thread failure's
    // Completed) on the main thread, but Progress and the final Completed come from
    // its export jthread, and the dispatcher invokes callbacks outside its lock - a
    // callback can still arrive just after unsubscribe. Every callback therefore
    // holds only a weak_ptr, and all state is guarded by the tracker's own mutex.
    class ExportTracker
    {
    public:
        enum class State
        {
            Idle,       // no export seen since the tracker was created
            Preparing,  // Started seen; scripts build / materials recompile on the main thread
            Exporting,  // export thread running; progress + step are live
            Done        // Completed seen (success or failure)
        };

        struct Snapshot
        {
            State state = State::Idle;
            float progress = 0.0f;
            std::string step;
            std::string outputDirectory;  // from Started
            bool success = false;
            std::string error;
            std::vector<std::string> warnings;
            std::string outputPath;
            uint64_t startCount = 0;  // Started notifications seen; lets a caller tell whether its execute() published one
        };

        // Subscribes the new tracker to the three export notifications.
        static std::shared_ptr<ExportTracker> create()
        {
            std::shared_ptr<ExportTracker> tracker(new ExportTracker());
            const std::weak_ptr<ExportTracker> weak = tracker;
            auto& dispatcher = events::EventDispatcher::instance();

            tracker->startedSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::gameExport::ExportStartedNotification>(
                    [weak](const events::gameExport::ExportStartedNotification& notification)
                    {
                        if (auto self = weak.lock())
                        {
                            self->onStarted(notification);
                        }
                    }));
            tracker->progressSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::gameExport::ExportProgressNotification>(
                    [weak](const events::gameExport::ExportProgressNotification& notification)
                    {
                        if (auto self = weak.lock())
                        {
                            self->onProgress(notification);
                        }
                    }));
            tracker->completedSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::gameExport::ExportCompletedNotification>(
                    [weak](const events::gameExport::ExportCompletedNotification& notification)
                    {
                        if (auto self = weak.lock())
                        {
                            self->onCompleted(notification);
                        }
                    }));
            return tracker;
        }

        ExportTracker(const ExportTracker&) = delete;
        ExportTracker& operator=(const ExportTracker&) = delete;

        Snapshot snapshot() const
        {
            std::lock_guard lock(mutex);
            return current;
        }

        void onStarted(const events::gameExport::ExportStartedNotification& notification)
        {
            std::lock_guard lock(mutex);
            const uint64_t startCount = current.startCount + 1;
            current = Snapshot{};
            current.state = State::Preparing;
            current.step = "Preparing (building scripts, recompiling stale materials)";
            current.outputDirectory = notification.outputDirectory;
            current.startCount = startCount;
        }

        void onProgress(const events::gameExport::ExportProgressNotification& notification)
        {
            std::lock_guard lock(mutex);
            current.state = State::Exporting;
            current.progress = notification.progress;
            current.step = notification.currentStep;
        }

        void onCompleted(const events::gameExport::ExportCompletedNotification& notification)
        {
            std::lock_guard lock(mutex);
            current.state = State::Done;
            current.success = notification.success;
            current.error = notification.errorMessage;
            current.warnings = notification.warnings;
            current.outputPath = notification.outputPath;
            if (notification.success)
            {
                current.progress = 1.0f;
            }
        }

    private:
        ExportTracker() = default;

        mutable std::mutex mutex;
        Snapshot current;

        // Declared last so they unsubscribe first on destruction.
        events::ScopedSubscription startedSubscription;
        events::ScopedSubscription progressSubscription;
        events::ScopedSubscription completedSubscription;
    };

    inline const char* exportStateName(ExportTracker::State state)
    {
        switch (state)
        {
        case ExportTracker::State::Preparing: return "preparing";
        case ExportTracker::State::Exporting: return "exporting";
        case ExportTracker::State::Done: return "done";
        case ExportTracker::State::Idle: break;
        }
        return "idle";
    }

    // game_export_status payload. success / error / warnings / outputPath only once done.
    // The engine's paths are narrow strings; the agent gets UTF-8.
    inline nlohmann::json exportStatusToJson(const ExportTracker::Snapshot& snapshot)
    {
        nlohmann::json out{
            {"state", exportStateName(snapshot.state)},
            {"progress", snapshot.progress},
            {"step", snapshot.step}
        };
        if (snapshot.state != ExportTracker::State::Idle)
        {
            out["outputDirectory"] = pathToUtf8(std::filesystem::path(snapshot.outputDirectory));
        }
        if (snapshot.state == ExportTracker::State::Done)
        {
            out["success"] = snapshot.success;
            out["error"] = snapshot.error;
            out["warnings"] = snapshot.warnings;
            out["outputPath"] = pathToUtf8(std::filesystem::path(snapshot.outputPath));
        }
        return out;
    }
}
