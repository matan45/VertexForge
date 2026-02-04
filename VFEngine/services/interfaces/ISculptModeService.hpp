#pragma once
#include "../data/EntityHandle.hpp"
#include <optional>

namespace services {

    class ISculptModeService {
    public:
        virtual ~ISculptModeService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Sculpt Mode Control
        // ============================================

        virtual bool activate() = 0;
        virtual void deactivate() = 0;
        virtual bool isActive() const = 0;

        // ============================================
        // Target Terrain
        // ============================================

        virtual std::optional<EntityHandle> getTargetEntity() const = 0;
    };

}
