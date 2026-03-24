#pragma once
#include "../EventTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace events::scene {

    // ============================================
    // Streaming Zone Commands
    // ============================================

    struct SetStreamingTriggerZoneCommand : ICommand<uint32_t> {
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        std::string scenePath;

        std::string_view getName() const override { return "SetStreamingTriggerZone"; }
    };

    struct RemoveStreamingTriggerZoneCommand : ICommand<bool> {
        uint32_t zoneId;

        std::string_view getName() const override { return "RemoveStreamingTriggerZone"; }
    };

    // ============================================
    // Streaming Zone Queries
    // ============================================

    struct IsStreamingSceneLoadedQuery : IQuery<bool> {
        std::string scenePath;

        std::string_view getName() const override { return "IsStreamingSceneLoaded"; }
    };

    // ============================================
    // Streaming Zone Notifications
    // ============================================

    struct StreamingZoneActivatedNotification : INotification {
        uint32_t zoneId;
        std::string scenePath;

        std::string_view getName() const override { return "StreamingZoneActivated"; }
    };

    struct StreamingZoneDeactivatedNotification : INotification {
        uint32_t zoneId;
        std::string scenePath;

        std::string_view getName() const override { return "StreamingZoneDeactivated"; }
    };

}
