#include <doctest.h>
#include <windows/contentbrowser/ContentBrowserTypes.hpp>
#include <unordered_set>

// ============================================================
// Content browser canonical type table — every AssetType must
// have exactly one entry, and the resource-enum mapping must be
// total. Guards against the old hand-maintained filter list
// drifting out of sync with the enum again.
// ============================================================

TEST_CASE("assetTypeTable covers every AssetType exactly once")
{
    const auto& table = windows::assetTypeTable();

    std::unordered_set<int> seen;
    for (const auto& info : table)
    {
        CHECK(seen.insert(static_cast<int>(info.type)).second);
        CHECK(info.label != nullptr);
        CHECK(info.label[0] != '\0');
    }

    // AssetType is contiguous from Texture (0) through Other; the table size
    // pins the count so adding an enum value without a table entry fails here.
    const int typeCount = static_cast<int>(windows::AssetType::Other) + 1;
    CHECK(static_cast<int>(table.size()) == typeCount);
    for (int i = 0; i < typeCount; ++i)
        CHECK(seen.count(i) == 1);
}

TEST_CASE("assetTypeInfo returns the matching entry")
{
    using windows::AssetType;
    CHECK(windows::assetTypeInfo(AssetType::Texture).type == AssetType::Texture);
    CHECK(windows::assetTypeInfo(AssetType::Other).type == AssetType::Other);
    CHECK(std::string(windows::assetTypeInfo(AssetType::MaterialInstance).label) == "Material Instance");
}

TEST_CASE("VFX Sequence type is registered in the table")
{
    using windows::AssetType;
    // The VFXSequence row has its own label/type and a dedicated atlas icon (slot 23).
    CHECK(windows::assetTypeInfo(AssetType::VFXSequence).type == AssetType::VFXSequence);
    CHECK(std::string(windows::assetTypeInfo(AssetType::VFXSequence).label) == "VFX Sequence");
    CHECK(windows::assetTypeInfo(AssetType::VFXSequence).icon == windows::AtlasIcon::VFXSequence);
}

TEST_CASE("formatFileSize uses binary units")
{
    CHECK(windows::formatFileSize(0) == "0 B");
    CHECK(windows::formatFileSize(1023) == "1023 B");
    CHECK(windows::formatFileSize(1024) == "1.0 KB");
    CHECK(windows::formatFileSize(1536) == "1.5 KB");
    CHECK(windows::formatFileSize(1024ull * 1024ull) == "1.0 MB");
    CHECK(windows::formatFileSize(5ull * 1024ull * 1024ull * 1024ull * 1024ull) == "5.0 TB");
}

TEST_CASE("fromResourceType is total and maps known pairs")
{
    using windows::AssetType;
    using windows::fromResourceType;

    SUBCASE("known mappings")
    {
        CHECK(fromResourceType(resource::AssetType::Texture) == AssetType::Texture);
        CHECK(fromResourceType(resource::AssetType::Mesh) == AssetType::Model);
        CHECK(fromResourceType(resource::AssetType::PhysicsShape) == AssetType::PhysAnim);
        CHECK(fromResourceType(resource::AssetType::HDR) == AssetType::HDR);
        CHECK(fromResourceType(resource::AssetType::Scene) == AssetType::Scene);
        CHECK(fromResourceType(resource::AssetType::Prefab) == AssetType::Prefab);
        CHECK(fromResourceType(resource::AssetType::MaterialInstance) == AssetType::MaterialInstance);
        CHECK(fromResourceType(resource::AssetType::VFX) == AssetType::VFX);
        CHECK(fromResourceType(resource::AssetType::VFXSequence) == AssetType::VFXSequence);
    }

    SUBCASE("types without a browser counterpart fall back to Other")
    {
        CHECK(fromResourceType(resource::AssetType::Skeleton) == AssetType::Other);
        CHECK(fromResourceType(resource::AssetType::World) == AssetType::Other);
        CHECK(fromResourceType(resource::AssetType::Theme) == AssetType::Other);
        CHECK(fromResourceType(resource::AssetType::COUNT) == AssetType::Other);
    }

    SUBCASE("every resource type maps to a valid browser type")
    {
        for (int i = 0; i <= static_cast<int>(resource::AssetType::COUNT); ++i)
        {
            auto mapped = fromResourceType(static_cast<resource::AssetType>(i));
            CHECK(static_cast<int>(mapped) >= 0);
            CHECK(static_cast<int>(mapped) <= static_cast<int>(AssetType::Other));
        }
    }
}
