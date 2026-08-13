#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <map>
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

    struct CancelPendingSceneLoadsCommand : ICommand<void> {
        std::string_view getName() const override { return "CancelPendingSceneLoads"; }
    };

    // Play/Stop in-memory snapshot (Phase 3). On Play the editor captures the
    // pristine, possibly-unsaved scene into an in-memory snapshot; on Stop it
    // restores from that snapshot instead of re-reading the .vfScene from disk —
    // faster, and it preserves unsaved editor edits that a disk reload would drop.
    struct CaptureSceneSnapshotCommand : ICommand<bool> {
        std::string_view getName() const override { return "CaptureSceneSnapshot"; }
    };

    // Returns true if a snapshot was armed for restore; false if none was held, so
    // the caller can fall back to a disk reload instead of leaving the scene live.
    struct RestoreSceneSnapshotCommand : ICommand<bool> {
        std::string_view getName() const override { return "RestoreSceneSnapshot"; }
    };

    struct DiscardSceneSnapshotCommand : ICommand<void> {
        std::string_view getName() const override { return "DiscardSceneSnapshot"; }
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

    // "Update Prefab": returns the source .vfPrefab an entity was instantiated from
    // (or saved as), or "" if the entity is not a prefab instance.
    struct GetPrefabSourcePathQuery : IQuery<std::string> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetPrefabSourcePath"; }
    };

    // Editor copy/paste: serialize an entity subtree to prefab-format JSON text.
    // Returns an empty string for invalid entities or the scene root.
    struct CopyEntityToJsonQuery : IQuery<std::string> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "CopyEntityToJson"; }
    };

    // Editor copy/paste: instantiate an entity subtree from prefab-format JSON text
    // (fresh UUIDs, assets acquired — same post-load path as prefab instantiation).
    struct InstantiateEntityFromJsonCommand : ICommand<std::optional<services::EntityHandle>> {
        std::string jsonText;
        std::optional<services::EntityHandle> parent;  // nullopt = add to scene root

        std::string_view getName() const override { return "InstantiateEntityFromJson"; }
    };

    // ============================================
    // Scene Queries
    // ============================================

    struct GetCurrentScenePathQuery : IQuery<std::string> {
        std::string_view getName() const override { return "GetCurrentScenePath"; }
    };

    // VK-1365: per-scene plugin enable overrides (plugin name -> enabled).
    // Only explicit overrides are present; unlisted plugins inherit the global
    // .vfplugin flag. Persisted in the scene's .vfSettings on SaveScene.
    struct GetScenePluginSettingsQuery : IQuery<std::map<std::string, bool>> {
        std::string_view getName() const override { return "GetScenePluginSettings"; }
    };

    struct SetScenePluginSettingsCommand : ICommand<bool> {
        std::map<std::string, bool> settings;

        std::string_view getName() const override { return "SetScenePluginSettings"; }
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

    // VK-1597: published only on a SUCCESSFUL save. World Sector streaming needs it because an
    // always-loaded entity is persisted by the scene file, not by Save World - so this is the only
    // signal that tells the editor its "Save Scene still owed" warning can be cleared.
    struct SceneSavedNotification : INotification {
        std::string scenePath;

        std::string_view getName() const override { return "SceneSaved"; }
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
