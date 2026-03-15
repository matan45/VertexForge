#pragma once
#include "../EventTypes.hpp"
#include <string>
#include <vector>

namespace events::audio {

    struct CreateBusCommand : ::events::ICommand<void> {
        std::string busName;
        std::string parentName = "Master";
        std::string_view getName() const override { return "CreateBus"; }
    };

    struct SetBusVolumeCommand : ::events::ICommand<void> {
        std::string busName;
        float volume = 1.0f;
        std::string_view getName() const override { return "SetBusVolume"; }
    };

    struct SetBusMutedCommand : ::events::ICommand<void> {
        std::string busName;
        bool muted = false;
        std::string_view getName() const override { return "SetBusMuted"; }
    };

    struct SetBusSoloedCommand : ::events::ICommand<void> {
        std::string busName;
        bool soloed = false;
        std::string_view getName() const override { return "SetBusSoloed"; }
    };

    struct GetBusVolumeQuery : ::events::IQuery<float> {
        std::string busName;
        std::string_view getName() const override { return "GetBusVolume"; }
    };

    struct IsBusMutedQuery : ::events::IQuery<bool> {
        std::string busName;
        std::string_view getName() const override { return "IsBusMuted"; }
    };

    struct GetBusNamesQuery : ::events::IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetBusNames"; }
    };

    struct SaveMixSnapshotCommand : ::events::ICommand<void> {
        std::string name;
        std::string_view getName() const override { return "SaveMixSnapshot"; }
    };

    struct LoadMixSnapshotCommand : ::events::ICommand<void> {
        std::string name;
        std::string_view getName() const override { return "LoadMixSnapshot"; }
    };

    struct DeleteMixSnapshotCommand : ::events::ICommand<void> {
        std::string name;
        std::string_view getName() const override { return "DeleteMixSnapshot"; }
    };

    struct GetSnapshotNamesQuery : ::events::IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetSnapshotNames"; }
    };

}
