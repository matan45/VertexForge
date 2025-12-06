#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
#include <vector>
#include <string>

namespace events::resource {

    // ============================================
    // COMMANDS - Operations that trigger resource operations
    // ============================================

    struct ImportFilesCommand : ICommand<> {
        std::vector<services::ImportFileRequest> files;

        std::string_view getName() const override { return "ImportFiles"; }
    };

    struct SetImportLocationCommand : ICommand<> {
        std::string path;

        std::string_view getName() const override { return "SetImportLocation"; }
    };

    struct CancelImportCommand : ICommand<> {
        std::string_view getName() const override { return "CancelImport"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetImportLocationQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetImportLocation"; }
    };

    struct IsImportingQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsImporting"; }
    };

    struct GetImportProgressQuery : IQuery<float> {
        std::string_view getName() const override { return "GetImportProgress"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
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

    struct ImportLocationChangedNotification : INotification {
        std::string newLocation;

        std::string_view getName() const override { return "ImportLocationChanged"; }
    };

}
