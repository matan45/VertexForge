#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace events::scene {

    // ============================================
    // Additive Scene Commands
    // ============================================

    struct LoadSceneAdditiveCommand : ICommand<bool> {
        std::string scenePath;
        std::string sceneName;

        std::string_view getName() const override { return "LoadSceneAdditive"; }
    };

    struct UnloadAdditiveSceneCommand : ICommand<bool> {
        std::string sceneName;

        std::string_view getName() const override { return "UnloadAdditiveScene"; }
    };

    struct SetActiveSceneCommand : ICommand<bool> {
        std::string sceneName;

        std::string_view getName() const override { return "SetActiveScene"; }
    };

    struct PreloadSceneCommand : ICommand<bool> {
        std::string scenePath;

        std::string_view getName() const override { return "PreloadScene"; }
    };

    // ============================================
    // Additive Scene Queries
    // ============================================

    struct GetActiveSceneQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetActiveScene"; }
    };

    struct GetLoadedScenesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetLoadedScenes"; }
    };

    struct IsSceneLoadedQuery : IQuery<bool> {
        std::string sceneName;

        std::string_view getName() const override { return "IsSceneLoaded"; }
    };

    // ============================================
    // Additive Scene Notifications
    // ============================================

    struct AdditiveSceneLoadedNotification : INotification {
        std::string sceneName;
        std::string scenePath;
        services::EntityHandle rootEntity;

        std::string_view getName() const override { return "AdditiveSceneLoaded"; }
    };

    struct AdditiveSceneUnloadedNotification : INotification {
        std::string sceneName;

        std::string_view getName() const override { return "AdditiveSceneUnloaded"; }
    };

}
