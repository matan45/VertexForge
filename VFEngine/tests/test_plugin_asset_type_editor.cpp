#include <doctest.h>
#include <windows/contentbrowser/ContentBrowserTypes.hpp>
#include <export/AssetClosureResolver.hpp>
#include <asset/AssetTypeRegistry.hpp>
#include <resource/AssetTypes.hpp>
#include <string>

// ============================================================
// VK-1449 — editor + export integration for plugin-registered
// asset types. Each case registers a type under a unique owning
// plugin and unregisters it so the process-wide registry returns
// to built-ins only.
// ============================================================

namespace
{
    asset::AssetTypeHandle registerSample()
    {
        asset::AssetTypeRecord rec;
        rec.typeId = "test.editorAsset";
        rec.displayName = "Editor Test Asset";
        rec.category = "Testing";
        rec.extensions = {".vfEditorTest"};
        rec.jsonContainer = true;
        rec.createMenuEntry = true;
        rec.badgeColor = 0xFF1234AB;
        rec.alwaysIncludeGlobs = {"*.vfeditortest"};
        rec.owningPlugin = "EditorTestPlugin";
        return asset::AssetTypeRegistry::instance().registerType(rec);
    }
}

TEST_CASE("VK-1449: resource PluginAsset maps to the windows Plugin type")
{
    CHECK(windows::fromResourceType(resource::AssetType::PluginAsset) == windows::AssetType::Plugin);
}

TEST_CASE("VK-1449: displayInfoForExtension carries plugin-provided metadata")
{
    auto handle = registerSample();
    REQUIRE(static_cast<bool>(handle));

    SUBCASE("registered plugin extension uses the plugin's display name + badge")
    {
        auto info = windows::displayInfoForExtension(".vfeditortest", windows::AssetType::Plugin);
        CHECK(info.label == "Editor Test Asset");
        CHECK(info.badgeColor == 0xFF1234AB);
        CHECK(info.icon == windows::AtlasIcon::Plugin);
    }

    SUBCASE("built-in extension falls back to the canonical table entry")
    {
        auto info = windows::displayInfoForExtension(".vfmesh", windows::AssetType::Model);
        CHECK(info.label == std::string("Model"));
    }

    asset::AssetTypeRegistry::instance().unregisterType(handle);
}

TEST_CASE("VK-1449: export always-include honors a registered type's globs")
{
    using gameExport::AssetClosureResolver;

    // Not always-included before the type is registered.
    CHECK_FALSE(AssetClosureResolver::isAlwaysIncluded("abilities/fireball.vfeditortest", {}));

    auto handle = registerSample();
    REQUIRE(static_cast<bool>(handle));

    CHECK(AssetClosureResolver::isAlwaysIncluded("abilities/fireball.vfeditortest", {}));
    // A built-in always-include pattern still works alongside.
    CHECK(AssetClosureResolver::isAlwaysIncluded("project.vfsettings", {}));

    asset::AssetTypeRegistry::instance().unregisterType(handle);
    CHECK_FALSE(AssetClosureResolver::isAlwaysIncluded("abilities/fireball.vfeditortest", {}));
}
