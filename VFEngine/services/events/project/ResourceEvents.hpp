#pragma once
#include "../EventTypes.hpp"
#include "../../data/DTOs.hpp"
#include <vector>
#include <string>

namespace events::resource {

    // ============================================
    // NOTIFICATIONS - Import state broadcasts (pub/sub)
    // Editor calls Import directly; these notifications allow
    // decoupled UI components (e.g., ImportProgressWindow) to react.
    // ============================================

    struct ImportStartedNotification : INotification {
        std::vector<std::string> files;

        std::string_view getName() const override { return "ImportStarted"; }
    };

    struct ImportProgressNotification : INotification {
        std::string currentFile;
        float progress;  // 0.0 - 1.0

        std::string_view getName() const override { return "ImportProgress"; }
    };

    struct ImportCompletedNotification : INotification {
        std::vector<services::ImportResult> results;

        std::string_view getName() const override { return "ImportCompleted"; }
    };

    struct AssetSavedNotification : INotification {
        std::string filePath;

        std::string_view getName() const override { return "AssetSaved"; }
    };

}
