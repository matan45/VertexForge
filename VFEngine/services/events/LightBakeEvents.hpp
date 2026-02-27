#pragma once
#include "EventTypes.hpp"
#include "../providers/ILightBakeProvider.hpp"
#include <string>

namespace services::events::lightbake
{
    // ============================================================
    // COMMANDS
    // ============================================================

    struct StartBakeCommand : ::events::ICommand<void>
    {
        services::LightBakeConfig config;
        std::string_view getName() const override { return "StartBake"; }
    };

    struct CancelBakeCommand : ::events::ICommand<void>
    {
        std::string_view getName() const override { return "CancelBake"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct GetBakeProgressQuery : ::events::IQuery<float>
    {
        std::string_view getName() const override { return "GetBakeProgress"; }
    };

    struct IsBakingQuery : ::events::IQuery<bool>
    {
        std::string_view getName() const override { return "IsBaking"; }
    };

    struct GetBakeResultQuery : ::events::IQuery<services::LightBakeResult>
    {
        std::string_view getName() const override { return "GetBakeResult"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    struct BakeCompletedNotification : ::events::INotification
    {
        services::LightBakeResult result;
        std::string_view getName() const override { return "BakeCompleted"; }
    };

    struct BakeFailedNotification : ::events::INotification
    {
        std::string errorMessage;
        std::string_view getName() const override { return "BakeFailed"; }
    };
}
