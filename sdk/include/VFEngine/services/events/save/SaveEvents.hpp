#pragma once
#include "../EventTypes.hpp"
#include "../../data/SaveTypes.hpp"
#include <string>
#include <vector>

namespace events::save {

    // ============================================
    // Save Slot Commands
    // ============================================

    struct CreateSaveSlotCommand : ICommand<bool> {
        std::string slotName;

        std::string_view getName() const override { return "CreateSaveSlot"; }
    };

    struct SaveGameCommand : ICommand<bool> {
        std::string slotName;

        std::string_view getName() const override { return "SaveGame"; }
    };

    struct LoadGameCommand : ICommand<bool> {
        std::string slotName;

        std::string_view getName() const override { return "LoadGame"; }
    };

    struct DeleteSaveSlotCommand : ICommand<bool> {
        std::string slotName;

        std::string_view getName() const override { return "DeleteSaveSlot"; }
    };

    // ============================================
    // Save Slot Queries
    // ============================================

    struct ListSaveSlotsQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "ListSaveSlots"; }
    };

    struct GetSaveMetadataQuery : IQuery<services::SaveSlotMetadata> {
        std::string slotName;

        std::string_view getName() const override { return "GetSaveMetadata"; }
    };

    // ============================================
    // Save Notifications
    // ============================================

    struct SaveGameCompletedNotification : INotification {
        std::string slotName;
        bool success;
        std::string errorMessage;

        std::string_view getName() const override { return "SaveGameCompleted"; }
    };

    struct LoadGameCompletedNotification : INotification {
        std::string slotName;
        bool success;
        std::string errorMessage;

        std::string_view getName() const override { return "LoadGameCompleted"; }
    };

}
