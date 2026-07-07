#pragma once
#include "../EventTypes.hpp"
#include "asset/AssetGUID.hpp"
#include "resource/AssetTypes.hpp"
#include <optional>
#include <string>
#include <vector>

namespace events::assetdb {

    // ============================================
    // COMMANDS
    // ============================================

    struct RegisterAssetCommand : ICommand<asset::AssetGUID> {
        std::string path;
        ::resource::AssetType type = ::resource::AssetType::COUNT;
        std::string importSource;

        std::string_view getName() const override { return "RegisterAsset"; }
    };

    struct UnregisterAssetCommand : ICommand<void> {
        asset::AssetGUID guid;

        std::string_view getName() const override { return "UnregisterAsset"; }
    };

    struct RebuildDatabaseCommand : ICommand<bool> {
        std::string_view getName() const override { return "RebuildDatabase"; }
    };

    struct RegenerateMetadataResult {
        uint32_t assetsScanned = 0;
        uint32_t metaFilesCreated = 0;
        std::vector<std::string> failures;
    };

    struct RegenerateMissingMetadataCommand : ICommand<RegenerateMetadataResult> {
        std::string_view getName() const override { return "RegenerateMissingMetadata"; }
    };

    // ============================================
    // QUERIES
    // ============================================

    struct GetAssetPathQuery : IQuery<std::optional<std::string>> {
        asset::AssetGUID guid;

        std::string_view getName() const override { return "GetAssetPath"; }
    };

    struct GetAssetGUIDQuery : IQuery<std::optional<asset::AssetGUID>> {
        std::string path;

        std::string_view getName() const override { return "GetAssetGUID"; }
    };

    struct GetAssetDependentsQuery : IQuery<std::vector<asset::AssetGUID>> {
        asset::AssetGUID guid;

        std::string_view getName() const override { return "GetAssetDependents"; }
    };

    struct GetAssetDependenciesQuery : IQuery<std::vector<asset::AssetGUID>> {
        asset::AssetGUID guid;

        std::string_view getName() const override { return "GetAssetDependencies"; }
    };

    struct GetAssetCountQuery : IQuery<size_t> {
        std::string_view getName() const override { return "GetAssetCount"; }
    };

    // Registered type for a path (absolute or project-relative); nullopt when
    // the asset is not in the database. Lets the content browser resolve types
    // without opening files.
    struct GetAssetTypeQuery : IQuery<std::optional<::resource::AssetType>> {
        std::string path;

        std::string_view getName() const override { return "GetAssetType"; }
    };

    // Lightweight projection of a database entry for enumeration queries.
    struct AssetEntryData {
        asset::AssetGUID guid;
        std::string path;
        ::resource::AssetType type = ::resource::AssetType::COUNT;
    };

    struct GetAllAssetsQuery : IQuery<std::vector<AssetEntryData>> {
        std::string_view getName() const override { return "GetAllAssets"; }
    };

    struct GetAssetsByTypeQuery : IQuery<std::vector<AssetEntryData>> {
        ::resource::AssetType type = ::resource::AssetType::COUNT;

        std::string_view getName() const override { return "GetAssetsByType"; }
    };

    // ============================================
    // NOTIFICATIONS
    // ============================================

    struct AssetRegisteredNotification : INotification {
        asset::AssetGUID guid;
        std::string path;
        ::resource::AssetType type = ::resource::AssetType::COUNT;

        std::string_view getName() const override { return "AssetRegistered"; }
    };

    struct AssetPathChangedNotification : INotification {
        asset::AssetGUID guid;
        std::string oldPath;
        std::string newPath;

        std::string_view getName() const override { return "AssetPathChanged"; }
    };

    struct AssetDatabaseRebuiltNotification : INotification {
        uint32_t assetCount = 0;

        std::string_view getName() const override { return "AssetDatabaseRebuilt"; }
    };

}
