#pragma once
#include "../EventTypes.hpp"
#include <string>
#include <vector>

namespace events::input {

    // ============================================
    // COMMANDS - Context management
    // ============================================

    struct CreateContextCommand : ICommand<void> {
        std::string contextName;
        bool blocking = true;

        std::string_view getName() const override { return "CreateContext"; }
    };

    struct RemoveContextCommand : ICommand<void> {
        std::string contextName;

        std::string_view getName() const override { return "RemoveContext"; }
    };

    struct PushContextCommand : ICommand<void> {
        std::string contextName;

        std::string_view getName() const override { return "PushContext"; }
    };

    struct PopContextCommand : ICommand<void> {
        std::string contextName;

        std::string_view getName() const override { return "PopContext"; }
    };

    struct SetContextBlockingCommand : ICommand<void> {
        std::string contextName;
        bool blocking = true;

        std::string_view getName() const override { return "SetContextBlocking"; }
    };

    // ============================================
    // QUERIES - Context state
    // ============================================

    struct GetActiveContextsQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetActiveContexts"; }
    };

    struct GetAllContextNamesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetAllContextNames"; }
    };

    struct IsContextActiveQuery : IQuery<bool> {
        std::string contextName;

        std::string_view getName() const override { return "IsContextActive"; }
    };

    struct GetContextActionsQuery : IQuery<std::vector<std::string>> {
        std::string contextName;

        std::string_view getName() const override { return "GetContextActions"; }
    };

    struct GetActionContextQuery : IQuery<std::string> {
        std::string actionName;

        std::string_view getName() const override { return "GetActionContext"; }
    };

}
