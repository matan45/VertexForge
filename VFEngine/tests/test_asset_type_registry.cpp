#include <doctest.h>
#include <asset/AssetTypeRegistry.hpp>
#include <asset/AssetExtensions.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <resource/AssetTypes.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

// ============================================================
// VK-1449 — AssetTypeRegistry: plugin-registered asset types.
//
// The registry is a process-wide singleton living in AssetDB.dll.
// Built-in extension classification was MOVED from the old inline
// tables in AssetExtensions.hpp into the registry seed; the GOLDEN
// PARITY case guards that the move preserved behaviour byte-for-byte.
// Registration cases register under a unique owning-plugin and clean
// up with unregisterByPlugin so the singleton returns to built-ins.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using resource::AssetType;
    using asset::AssetTypeRegistry;
    using asset::AssetTypeRecord;

    // The exact built-in extension -> AssetType mapping as it existed before
    // the refactor (the "golden" reference). Any drift in the registry seed
    // makes the parity case fail.
    const std::unordered_map<std::string, AssetType>& goldenExtToType()
    {
        static const std::unordered_map<std::string, AssetType> m = {
            {".vfimage", AssetType::Texture},
            {".vfhdr", AssetType::HDR},
            {".vfmesh", AssetType::Mesh},
            {".vfaudio", AssetType::Audio},
            {".vfanim", AssetType::Animation},
            {".vfmat", AssetType::Material},
            {".vfmatinstance", AssetType::MaterialInstance},
            {".vfanimator", AssetType::Animator},
            {".vfvfx", AssetType::VFX},
            {".vfvfxsequence", AssetType::VFXSequence},
            {".vffont", AssetType::Font},
            {".vfnavmesh", AssetType::Navmesh},
            {".vfnavindex", AssetType::Navmesh},
            {".vfinputmapping", AssetType::InputMapping},
            {".vfterrain", AssetType::Terrain},
            {".vfterrainmat", AssetType::TerrainMaterial},
            {".vfbehaviortree", AssetType::BehaviorTree},
            {".vfphysanim", AssetType::PhysicsShape},
            {".vfscene", AssetType::Scene},
            {".vfsettings", AssetType::Scene},
            {".vftheme", AssetType::Theme},
            {".vfprefab", AssetType::Prefab},
            {".vfrig", AssetType::HumanoidRig},
            {".vfretarget", AssetType::RetargetMap},
            {".mt", AssetType::Script},
        };
        return m;
    }

    const std::unordered_set<std::string>& goldenAllExtensions()
    {
        static const std::unordered_set<std::string> s = {
            ".vfimage", ".vfhdr", ".vfmesh", ".vfaudio", ".vfanim",
            ".vfmat", ".vfmatinstance", ".vfanimator", ".vfvfx",
            ".vfvfxsequence",
            ".vffont", ".vfscene", ".vfsettings", ".vfprefab", ".vftheme",
            ".vfterrain", ".vfterrainmat", ".vfwater", ".vfnavmesh",
            ".vfnavindex", ".vfimposter", ".vfinputmapping",
            ".vfbehaviortree", ".vfphysanim", ".vfrig", ".vfretarget", ".mt"
        };
        return s;
    }

    const std::unordered_set<std::string>& goldenJsonExtensions()
    {
        static const std::unordered_set<std::string> s = {
            ".vfscene", ".vfsettings", ".vfprefab", ".vfmat",
            ".vfmatinstance", ".vfanimator", ".vfvfx", ".vfvfxsequence",
            ".vfterrainmat",
            ".vftheme", ".vfbehaviortree", ".vfinputmapping",
            ".vfrig", ".vfretarget"
        };
        return s;
    }
}

TEST_CASE("AssetTypeRegistry: golden parity with pre-refactor built-in tables")
{
    using namespace asset::extensions;

    SUBCASE("typeForExtension matches the golden map for every built-in")
    {
        for (const auto& [ext, type] : goldenExtToType())
        {
            CHECK(typeForExtension(ext) == type);
        }
        // Typeless-but-registrable built-ins classify to COUNT yet are assets.
        CHECK(typeForExtension(".vfwater") == AssetType::COUNT);
        CHECK(typeForExtension(".vfimposter") == AssetType::COUNT);
        CHECK(isAssetExtension(".vfwater"));
        CHECK(isAssetExtension(".vfimposter"));
    }

    SUBCASE("case-insensitive")
    {
        CHECK(typeForExtension(".VFMesh") == AssetType::Mesh);
        CHECK(typeForExtension(".VfScene") == AssetType::Scene);
        CHECK(isJsonContainerExtension(".VFScene"));
    }

    SUBCASE("allAssetExtensions equals the golden set")
    {
        CHECK(allAssetExtensions() == goldenAllExtensions());
    }

    SUBCASE("jsonContainerExtensions equals the golden set")
    {
        CHECK(jsonContainerExtensions() == goldenJsonExtensions());
        // Spot-check a non-JSON built-in stays excluded.
        CHECK_FALSE(isJsonContainerExtension(".vfmesh"));
        CHECK_FALSE(isJsonContainerExtension(".vfimage"));
    }

    SUBCASE("unknown extension is not an asset and classifies to COUNT")
    {
        CHECK(typeForExtension(".unknownext") == AssetType::COUNT);
        CHECK_FALSE(isAssetExtension(".unknownext"));
        CHECK_FALSE(isJsonContainerExtension(".unknownext"));
    }
}

TEST_CASE("AssetTypeRegistry: register / unregister round-trip")
{
    auto& reg = AssetTypeRegistry::instance();
    const uint64_t rev0 = reg.revision();

    AssetTypeRecord rec;
    rec.typeId = "test.widget";
    rec.displayName = "Test Widget";
    rec.category = "Testing";
    rec.extensions = {".vfTestWidget"};   // mixed case on input — lowercased internally
    rec.jsonContainer = true;
    rec.createMenuEntry = true;
    rec.owningPlugin = "TestRegistryPlugin";

    auto handle = reg.registerType(rec);
    CHECK(static_cast<bool>(handle));
    CHECK(reg.revision() > rev0);

    SUBCASE("classification reflects the registered type")
    {
        using namespace asset::extensions;
        CHECK(typeForExtension(".vftestwidget") == AssetType::PluginAsset);
        CHECK(isAssetExtension(".vftestwidget"));
        CHECK(isJsonContainerExtension(".vftestwidget"));
        // Case-insensitive lookup of a mixed-case registered extension.
        CHECK(typeForExtension(".vfTestWidget") == AssetType::PluginAsset);
        CHECK(allAssetExtensions().count(".vftestwidget") == 1);
        CHECK(jsonContainerExtensions().count(".vftestwidget") == 1);
    }

    SUBCASE("findByExtension / findByTypeId return the record")
    {
        AssetTypeRecord out;
        CHECK(reg.findByExtension(".vftestwidget", out));
        CHECK(out.typeId == "test.widget");
        CHECK(out.displayName == "Test Widget");
        CHECK(out.type == AssetType::PluginAsset);

        AssetTypeRecord out2;
        CHECK(reg.findByTypeId("test.widget", out2));
        CHECK(out2.extensions.size() == 1);
        CHECK(out2.extensions[0] == ".vftestwidget");   // stored lowercased
    }

    // Cleanup -> registry returns to built-ins only.
    reg.unregisterType(handle);
    CHECK(asset::extensions::typeForExtension(".vftestwidget") == AssetType::COUNT);
    CHECK_FALSE(asset::extensions::isAssetExtension(".vftestwidget"));
    AssetTypeRecord gone;
    CHECK_FALSE(reg.findByTypeId("test.widget", gone));
}

TEST_CASE("AssetTypeRegistry: high-bit extension bytes and badge color lookup")
{
    auto& reg = AssetTypeRegistry::instance();

    AssetTypeRecord rec;
    rec.typeId = "test.highbit";
    rec.displayName = "High Bit";
    rec.extensions = {std::string(".vf") + static_cast<char>(0xC0) + "Widget"};
    rec.badgeColor = 0xFF123456u;
    rec.owningPlugin = "TestHighBitPlugin";

    auto handle = reg.registerType(rec);
    REQUIRE(static_cast<bool>(handle));

    AssetTypeRecord out;
    CHECK(reg.findByExtension(rec.extensions[0], out));
    CHECK(out.typeId == "test.highbit");
    CHECK(reg.badgeColorForExtension(rec.extensions[0]) == 0xFF123456u);
    CHECK(reg.badgeColorForExtension(".unknown", 0xFFABCDEFu) == 0xFFABCDEFu);

    reg.unregisterType(handle);
}

TEST_CASE("AssetTypeRegistry: collisions are rejected (first registrant wins)")
{
    auto& reg = AssetTypeRegistry::instance();

    AssetTypeRecord a;
    a.typeId = "test.collA";
    a.extensions = {".vfCollide"};
    a.owningPlugin = "TestRegistryPlugin";
    auto ha = reg.registerType(a);
    CHECK(static_cast<bool>(ha));

    SUBCASE("duplicate typeId rejected")
    {
        AssetTypeRecord dup;
        dup.typeId = "test.collA";          // same id
        dup.extensions = {".vfOther"};
        dup.owningPlugin = "TestRegistryPlugin";
        CHECK_FALSE(static_cast<bool>(reg.registerType(dup)));
    }

    SUBCASE("extension already used by another plugin type rejected")
    {
        AssetTypeRecord b;
        b.typeId = "test.collB";
        b.extensions = {".vfCollide"};      // same extension
        b.owningPlugin = "TestRegistryPlugin";
        CHECK_FALSE(static_cast<bool>(reg.registerType(b)));
    }

    SUBCASE("extension colliding with a built-in rejected")
    {
        AssetTypeRecord c;
        c.typeId = "test.collC";
        c.extensions = {".vfmesh"};         // built-in
        c.owningPlugin = "TestRegistryPlugin";
        CHECK_FALSE(static_cast<bool>(reg.registerType(c)));
    }

    SUBCASE("invalid extension (no leading dot) rejected")
    {
        AssetTypeRecord d;
        d.typeId = "test.collD";
        d.extensions = {"vfNoDot"};
        d.owningPlugin = "TestRegistryPlugin";
        CHECK_FALSE(static_cast<bool>(reg.registerType(d)));
    }

    reg.unregisterByPlugin("TestRegistryPlugin");
    CHECK(asset::extensions::typeForExtension(".vfcollide") == AssetType::COUNT);
}

TEST_CASE("AssetTypeRegistry: unregisterByPlugin removes all of a plugin's types")
{
    auto& reg = AssetTypeRegistry::instance();

    AssetTypeRecord a; a.typeId = "p.one"; a.extensions = {".vfPone"}; a.owningPlugin = "P";
    AssetTypeRecord b; b.typeId = "p.two"; b.extensions = {".vfPtwo"}; b.owningPlugin = "P";
    CHECK(static_cast<bool>(reg.registerType(a)));
    CHECK(static_cast<bool>(reg.registerType(b)));
    CHECK(asset::extensions::isAssetExtension(".vfpone"));
    CHECK(asset::extensions::isAssetExtension(".vfptwo"));

    reg.unregisterByPlugin("P");
    CHECK_FALSE(asset::extensions::isAssetExtension(".vfpone"));
    CHECK_FALSE(asset::extensions::isAssetExtension(".vfptwo"));
}

TEST_CASE("VK-1449: PluginAsset enum round-trips by name (serialization-safe)")
{
    // Name-based round-trip works even with an empty registry — the enum value
    // is fixed; only the precise typeId needs the registry.
    CHECK(std::string(resource::assetTypeName(AssetType::PluginAsset)) == "PluginAsset");
    CHECK(asset::AssetMetadataSerializer::stringToAssetType("PluginAsset") == AssetType::PluginAsset);
}

TEST_CASE("VK-1449: pluginTypeId persists through .vfmeta and the asset DB index")
{
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / "vf_gas_registry_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    SUBCASE(".vfmeta round-trip")
    {
        asset::AssetMetadata meta;
        meta.guid = asset::AssetGUID::generate();
        meta.type = AssetType::PluginAsset;
        meta.pluginTypeId = "gas.ability";

        fs::path assetPath = tmp / "fireball.vfability";
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(assetPath);
        REQUIRE(asset::AssetMetadataSerializer::save(meta, metaPath));

        auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->type == AssetType::PluginAsset);
        CHECK(loaded->pluginTypeId == "gas.ability");
        CHECK(loaded->formatVersion == asset::AssetMetadata::kCurrentFormatVersion);
    }

    SUBCASE("AssetDatabase index round-trip")
    {
        auto& db = asset::AssetDatabase::instance();
        db.clear();

        auto guid = db.registerAsset((tmp / "stun.vfgameplayeffect").string(),
                                     AssetType::PluginAsset, "", "gas.effect");

        REQUIRE(db.saveIndex(tmp.string()));
        db.clear();
        REQUIRE(db.loadIndex(tmp.string()));

        auto entry = db.getEntry(guid);
        REQUIRE(entry.has_value());
        CHECK(entry->type == AssetType::PluginAsset);
        CHECK(entry->pluginTypeId == "gas.effect");

        db.clear();
    }

    fs::remove_all(tmp, ec);
}
