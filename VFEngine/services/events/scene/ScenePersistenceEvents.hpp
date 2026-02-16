#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <optional>
#include <string>

namespace events::scene {

    // ============================================
    // Scene Management Commands
    // ============================================

    struct NewSceneCommand : ICommand<bool> {
        std::string_view getName() const override { return "NewScene"; }
    };

    struct SaveSceneCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "SaveScene"; }
    };

    struct LoadSceneCommand : ICommand<bool> {
        std::string filePath;

        std::string_view getName() const override { return "LoadScene"; }
    };

    struct SavePrefabCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string filePath;

        std::string_view getName() const override { return "SavePrefab"; }
    };

    struct LoadPrefabCommand : ICommand<std::optional<services::EntityHandle>> {
        std::string filePath;
        std::optional<services::EntityHandle> parent;  // nullopt = add to scene root

        std::string_view getName() const override { return "LoadPrefab"; }
    };

    // ============================================
    // Scene Notifications
    // ============================================

    struct SceneLoadedNotification : INotification {
        std::string scenePath;

        std::string_view getName() const override { return "SceneLoaded"; }
    };

    struct SceneClearedNotification : INotification {
        std::string_view getName() const override { return "SceneCleared"; }
    };

    struct SceneLoadingStartedNotification : INotification {
        std::string scenePath;

        std::string_view getName() const override { return "SceneLoadingStarted"; }
    };

    struct SceneLoadingProgressUpdatedNotification : INotification {
        std::string currentEntityName;
        float progress;  // 0.0 - 1.0

        std::string_view getName() const override { return "SceneLoadingProgressUpdated"; }
    };

    struct SceneLoadingCompletedNotification : INotification {
        std::string scenePath;
        bool success;
        std::string errorMessage;  // Only set if success is false

        std::string_view getName() const override { return "SceneLoadingCompleted"; }
    };

    struct PrefabCreatedNotification : INotification {
        std::string filePath;
        services::EntityHandle sourceEntity;

        std::string_view getName() const override { return "PrefabCreated"; }
    };

    struct PrefabInstantiatedNotification : INotification {
        std::string filePath;
        services::EntityHandle rootEntity;

        std::string_view getName() const override { return "PrefabInstantiated"; }
    };

}
