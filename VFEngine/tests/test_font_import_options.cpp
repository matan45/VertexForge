#include <doctest.h>

#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <config/Config.hpp>
#include <controllers/Import.hpp>
#include <registry/AssetImporter.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <resource/FontResource.hpp>
#include <resource/Types.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// ============================================================
// VK-1629: the font importer exposes its FontImportConfig through the declarative
// option channel (AssetImporter::options() -> ImportConfig::customOptions), and the
// chosen values are persisted in the .vfmeta sidecar so a reimport can replay them.
//
// NOTE: this TU must NOT include import/types/Font.hpp or
// registry/builtin/FontImporter.hpp. Both pull in <ft2build.h>, and the Tests
// project has no FreeType include directory — the importer is only reachable here
// through controllers::Import. That is also why the descriptor defaults are pinned
// numerically instead of compared against a FontImportConfig instance; those
// numbers are the contract, because the shipped default font is baked by running
// the importer with an empty customOptions map.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Tests.exe runs from bin/Tests/<Config>/x64/ and resources/ is NOT copied
    // there, so the repo has to be reached from the source location.
    fs::path repoRoot()
    {
        return fs::path(__FILE__).parent_path() // VFEngine/tests
            .parent_path()                      // VFEngine
            .parent_path();                     // repo root
    }

    fs::path robotoSource()
    {
        return repoRoot() / "resources" / "editor" / "Roboto-Regular.ttf";
    }

    fs::path makeTempDir(const char* tag)
    {
        fs::path dir = fs::temp_directory_path() / (std::string("vf_vk1629_font_") + tag);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }

    std::vector<import::ImportOptionDesc> fontOptions()
    {
        import::builtin::ensureRegistered();
        return controllers::Import::optionsForExtension("ttf");
    }

    const import::ImportOptionDesc* findOption(const std::vector<import::ImportOptionDesc>& descs,
                                               std::string_view key)
    {
        const auto it = std::ranges::find_if(descs, [&](const auto& d) { return d.key == key; });
        return it == descs.end() ? nullptr : &*it;
    }

    // Fails the case rather than throwing out of std::get on the wrong alternative.
    bool boolDefault(const std::vector<import::ImportOptionDesc>& descs, std::string_view key)
    {
        const auto* desc = findOption(descs, key);
        REQUIRE_MESSAGE(desc != nullptr, "missing font import option: " << key);
        REQUIRE(std::holds_alternative<bool>(desc->defaultValue));
        return std::get<bool>(desc->defaultValue);
    }

    int32_t intDefault(const std::vector<import::ImportOptionDesc>& descs, std::string_view key)
    {
        const auto* desc = findOption(descs, key);
        REQUIRE_MESSAGE(desc != nullptr, "missing font import option: " << key);
        REQUIRE(std::holds_alternative<int32_t>(desc->defaultValue));
        return std::get<int32_t>(desc->defaultValue);
    }

    float floatDefault(const std::vector<import::ImportOptionDesc>& descs, std::string_view key)
    {
        const auto* desc = findOption(descs, key);
        REQUIRE_MESSAGE(desc != nullptr, "missing font import option: " << key);
        REQUIRE(std::holds_alternative<float>(desc->defaultValue));
        return std::get<float>(desc->defaultValue);
    }

    // Imports Roboto into `dir` under `stem`, with `options`. The importer names its
    // output from the source stem, so the .ttf is staged under the wanted name.
    // Returns the written .vfFont path (empty on failure).
    fs::path importRoboto(const fs::path& dir, const std::string& stem,
                          const std::map<std::string, importConfig::ImportOptionValue>& options)
    {
        std::error_code ec;
        const fs::path staged = dir / (stem + ".ttf");
        fs::copy_file(robotoSource(), staged, fs::copy_options::overwrite_existing, ec);
        if (ec) return {};

        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());

        importConfig::ImportConfig config;
        config.customOptions = options;

        const auto result = controllers::Import::importFiles(
            {importConfig::ImportFiles(staged.string(), config)});
        if (result.failureCount != 0) return {};

        const fs::path out = dir / (stem + "." + FileExtension::font);
        return fs::exists(out) ? out : fs::path{};
    }

    std::optional<asset::AssetMetadata> sidecarOf(const fs::path& assetPath)
    {
        return asset::AssetMetadataSerializer::load(
            asset::AssetMetadataSerializer::getMetaPath(assetPath));
    }
}

TEST_SUITE("FontImportOptions")
{
    TEST_CASE("the font importer declares options for both ttf and otf")
    {
        import::builtin::ensureRegistered();
        const auto ttf = controllers::Import::optionsForExtension("ttf");
        const auto otf = controllers::Import::optionsForExtension("otf");

        REQUIRE_FALSE(ttf.empty());
        REQUIRE(ttf.size() == otf.size());

        // One importer owns both formats, so the two lists must be identical.
        for (size_t i = 0; i < ttf.size(); ++i)
        {
            CHECK(ttf[i].key == otf[i].key);
            CHECK(ttf[i].type == otf[i].type);
        }

        // Every knob of FontImportConfig is reachable.
        const std::vector<std::string> expected = {
            "baseFontSize", "fieldMode", "includeKerning", "sdfSpread", "sdfPadding",
            "sdfOnEdgeValue", "mtsdfPxRange", "includeBasicLatin", "includeLatin1Supplement",
            "includeLatinExtendedA", "includeLatinExtendedB", "includeGreek", "includeCyrillic",
            "includeEmoji", "includeMiscSymbols", "includeDingbats", "atlasSize", "atlasPadding",
        };
        for (const auto& key : expected)
        {
            CHECK_MESSAGE(findOption(ttf, key) != nullptr, "missing option: " << key);
        }
        CHECK(ttf.size() == expected.size());

        // No duplicate keys: the dialog stores values in a map keyed by `key`, so a
        // duplicate would make two widgets fight over one entry.
        std::vector<std::string> keys;
        for (const auto& desc : ttf) keys.push_back(desc.key);
        std::ranges::sort(keys);
        CHECK(std::ranges::adjacent_find(keys) == keys.end());
    }

    TEST_CASE("every descriptor is self-consistent")
    {
        const auto descs = fontOptions();
        REQUIRE_FALSE(descs.empty());

        for (const auto& desc : descs)
        {
            CHECK_FALSE(desc.label.empty());
            CHECK_FALSE(desc.tooltip.empty());

            switch (desc.type)
            {
                case import::ImportOptionDesc::Type::Bool:
                    CHECK(std::holds_alternative<bool>(desc.defaultValue));
                    break;
                case import::ImportOptionDesc::Type::Int:
                case import::ImportOptionDesc::Type::Float:
                {
                    // The generic dialog feeds min/max straight to a slider, so an
                    // empty or inverted range would pin the widget.
                    CHECK(desc.maxValue > desc.minValue);
                    if (desc.type == import::ImportOptionDesc::Type::Int)
                    {
                        REQUIRE(std::holds_alternative<int32_t>(desc.defaultValue));
                        const auto value = static_cast<float>(std::get<int32_t>(desc.defaultValue));
                        CHECK(value >= desc.minValue);
                        CHECK(value <= desc.maxValue);
                    }
                    else
                    {
                        REQUIRE(std::holds_alternative<float>(desc.defaultValue));
                        const auto value = std::get<float>(desc.defaultValue);
                        CHECK(value >= desc.minValue);
                        CHECK(value <= desc.maxValue);
                    }
                    break;
                }
                case import::ImportOptionDesc::Type::Enum:
                {
                    REQUIRE(std::holds_alternative<int32_t>(desc.defaultValue));
                    const auto index = std::get<int32_t>(desc.defaultValue);
                    CHECK_FALSE(desc.enumNames.empty());
                    CHECK(index >= 0);
                    CHECK(index < static_cast<int32_t>(desc.enumNames.size()));
                    break;
                }
            }
        }
    }

    // This case is the guard on the shipped default font: it is baked by running the
    // importer with NO options, which resolves to exactly these values. Changing one
    // of them silently rebakes resources/fonts/DefaultFont.vfFont differently.
    TEST_CASE("descriptor defaults match the FontImportConfig defaults")
    {
        const auto descs = fontOptions();

        CHECK(intDefault(descs, "baseFontSize") == 32);
        CHECK(intDefault(descs, "fieldMode") == 1);
        CHECK(boolDefault(descs, "includeKerning") == true);
        CHECK(floatDefault(descs, "sdfSpread") == doctest::Approx(4.0f));
        CHECK(intDefault(descs, "sdfPadding") == 4);
        CHECK(intDefault(descs, "sdfOnEdgeValue") == 128);
        CHECK(floatDefault(descs, "mtsdfPxRange") == doctest::Approx(4.0f));

        const auto* fieldMode = findOption(descs, "fieldMode");
        REQUIRE(fieldMode != nullptr);
        CHECK(fieldMode->type == import::ImportOptionDesc::Type::Enum);
        REQUIRE(fieldMode->enumNames.size() == 3);
        CHECK(fieldMode->enumNames[0] == "Grayscale (raster)");
        CHECK(fieldMode->enumNames[1] == "SDF (legacy)");
        CHECK(fieldMode->enumNames[2] == "MTSDF");

        CHECK(boolDefault(descs, "includeBasicLatin") == true);
        CHECK(boolDefault(descs, "includeLatin1Supplement") == true);
        CHECK(boolDefault(descs, "includeLatinExtendedA") == false);
        CHECK(boolDefault(descs, "includeLatinExtendedB") == false);
        CHECK(boolDefault(descs, "includeGreek") == false);
        CHECK(boolDefault(descs, "includeCyrillic") == false);
        CHECK(boolDefault(descs, "includeEmoji") == false);
        CHECK(boolDefault(descs, "includeMiscSymbols") == false);
        CHECK(boolDefault(descs, "includeDingbats") == false);

        // Atlas size is one square enum over {512, 1024, 2048, 4096}; the default
        // index must select 1024, which is what FontImportConfig starts at.
        const auto* atlas = findOption(descs, "atlasSize");
        REQUIRE(atlas != nullptr);
        REQUIRE(atlas->type == import::ImportOptionDesc::Type::Enum);
        REQUIRE(atlas->enumNames.size() == 4);
        CHECK(atlas->enumNames[0] == "512");
        CHECK(atlas->enumNames[1] == "1024");
        CHECK(atlas->enumNames[2] == "2048");
        CHECK(atlas->enumNames[3] == "4096");
        CHECK(intDefault(descs, "atlasSize") == 1);

        CHECK(intDefault(descs, "atlasPadding") == 1);
    }

    TEST_CASE("option values round-trip through ImportConfig::customOptions")
    {
        importConfig::ImportConfig config;
        config.customOptions["flag"] = true;
        config.customOptions["count"] = int32_t{48};
        config.customOptions["ratio"] = 2.5f;
        config.customOptions["name"] = std::string("roboto");

        const importConfig::ImportConfig copy = config;

        CHECK(std::get<bool>(copy.customOptions.at("flag")) == true);
        CHECK(std::get<int32_t>(copy.customOptions.at("count")) == 48);
        CHECK(std::get<float>(copy.customOptions.at("ratio")) == doctest::Approx(2.5f));
        CHECK(std::get<std::string>(copy.customOptions.at("name")) == "roboto");
    }

    TEST_CASE("importOptions round-trip through the .vfmeta sidecar")
    {
        const fs::path dir = makeTempDir("sidecar");
        const fs::path assetPath = dir / ("Sidecar." + FileExtension::font);
        const fs::path metaPath = asset::AssetMetadataSerializer::getMetaPath(assetPath);

        asset::AssetMetadata metadata;
        metadata.guid = asset::AssetGUID::generate();
        metadata.type = resource::AssetType::Font;
        metadata.importSourcePath = "C:/fonts/Sidecar.ttf";
        metadata.importOptions["fieldMode"] = int32_t{2};
        metadata.importOptions["baseFontSize"] = int32_t{64};
        metadata.importOptions["sdfSpread"] = 6.5f;
        metadata.importOptions["mtsdfPxRange"] = 4.0f;

        REQUIRE(asset::AssetMetadataSerializer::save(metadata, metaPath));

        const auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->guid == metadata.guid);
        CHECK(loaded->formatVersion == asset::AssetMetadata::kCurrentFormatVersion);
        CHECK(loaded->formatVersion == 4);
        REQUIRE(loaded->importOptions.size() == 4);

        // The JSON type carries the variant alternative back — a bool must not come
        // back as an integer, and a whole-valued float must not come back as one.
        CHECK(std::get<int32_t>(loaded->importOptions.at("fieldMode")) == 2);
        CHECK(std::get<int32_t>(loaded->importOptions.at("baseFontSize")) == 64);
        CHECK(std::get<float>(loaded->importOptions.at("sdfSpread")) == doctest::Approx(6.5f));
        CHECK(std::get<float>(loaded->importOptions.at("mtsdfPxRange")) == doctest::Approx(4.0f));
    }

    TEST_CASE("a whole-valued float option survives the JSON round-trip as a float")
    {
        const fs::path dir = makeTempDir("floatfidelity");
        const fs::path metaPath =
            asset::AssetMetadataSerializer::getMetaPath(dir / ("Whole." + FileExtension::font));

        asset::AssetMetadata metadata;
        metadata.guid = asset::AssetGUID::generate();
        metadata.type = resource::AssetType::Font;
        metadata.importOptions["sdfSpread"] = 4.0f; // dumps as "4.0", not "4"

        REQUIRE(asset::AssetMetadataSerializer::save(metadata, metaPath));
        const auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->importOptions.contains("sdfSpread"));
        CHECK(std::holds_alternative<float>(loaded->importOptions.at("sdfSpread")));
        CHECK(std::get<float>(loaded->importOptions.at("sdfSpread")) == doctest::Approx(4.0f));
    }

    TEST_CASE("a pre-VK-1629 sidecar loads with no import options")
    {
        const fs::path dir = makeTempDir("v3");
        const fs::path metaPath =
            asset::AssetMetadataSerializer::getMetaPath(dir / ("Legacy." + FileExtension::font));

        const auto guid = asset::AssetGUID::generate();
        {
            std::ofstream out(metaPath);
            REQUIRE(out.is_open());
            out << "{\n"
                << "  \"guid\": \"" << guid.toString() << "\",\n"
                << "  \"type\": \"Font\",\n"
                << "  \"importSource\": \"C:/fonts/Legacy.ttf\",\n"
                << "  \"importTimestamp\": \"2026-01-01 00:00:00\",\n"
                << "  \"formatVersion\": 3\n"
                << "}\n";
        }

        const auto loaded = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(loaded.has_value());
        CHECK(loaded->guid == guid);
        CHECK(loaded->formatVersion == 3);
        CHECK(loaded->importOptions.empty());
        CHECK(loaded->importSourcePath == "C:/fonts/Legacy.ttf");
    }

    TEST_CASE("a real import honours the chosen options and records them")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("honour");
        std::map<std::string, importConfig::ImportOptionValue> options;
        options["baseFontSize"] = int32_t{48};
        options["includeGreek"] = true;
        options["atlasSize"] = int32_t{2}; // 2048

        const fs::path out = importRoboto(dir, "Honour", options);
        REQUIRE_FALSE(out.empty());

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());

        CHECK(font.metadata.baseFontSize == 48u);

        // The packer shrinks the atlas to the tightest power of two that holds the
        // glyphs and clamps that to the configured size, so the option is a CAP, not
        // an exact output size (see FontAtlasGenerator::packGlyphRects).
        CHECK(font.atlas.width <= 2048u);
        CHECK(font.atlas.height <= 2048u);
        CHECK(font.atlas.width >= 64u);

        // Greek was requested, so the range must be covered; Cyrillic was not.
        CHECK(font.findGlyph(0x03B1) != nullptr); // GREEK SMALL LETTER ALPHA
        CHECK(font.findGlyph(0x0416) == nullptr); // CYRILLIC CAPITAL LETTER ZHE
        CHECK(font.findGlyph('A') != nullptr);    // Basic Latin stays on by default

        const auto meta = sidecarOf(out);
        REQUIRE(meta.has_value());
        CHECK(meta->type == resource::AssetType::Font);
        CHECK(meta->importSourcePath == (dir / "Honour.ttf").string());
        CHECK(std::get<int32_t>(meta->importOptions.at("baseFontSize")) == 48);
        CHECK(std::get<bool>(meta->importOptions.at("includeGreek")) == true);
        CHECK(std::get<int32_t>(meta->importOptions.at("atlasSize")) == 2);
    }

    TEST_CASE("an empty option map reproduces the FontImportConfig defaults")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("defaults");
        const fs::path out = importRoboto(dir, "Defaults", {});
        REQUIRE_FALSE(out.empty());

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());

        // The path the shipped default font is baked through.
        CHECK(font.metadata.baseFontSize == 32u);
        CHECK(font.findGlyph('A') != nullptr);
        CHECK(font.findGlyph(0x00E9) != nullptr); // Latin-1 e-acute, on by default
        CHECK(font.findGlyph(0x2026) != nullptr); // ellipsis is always available for truncation
        CHECK(font.findGlyph(0x03B1) == nullptr); // Greek off by default

        // Nothing was chosen, so nothing is recorded — a reimport then falls back to
        // the declared descriptor defaults, which are these same values.
        const auto meta = sidecarOf(out);
        REQUIRE(meta.has_value());
        CHECK(meta->importOptions.empty());
    }

    TEST_CASE("reimport keeps the GUID and refreshes the recorded options")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("reimport");

        std::map<std::string, importConfig::ImportOptionValue> first;
        first["baseFontSize"] = int32_t{32};
        const fs::path out = importRoboto(dir, "Reimport", first);
        REQUIRE_FALSE(out.empty());

        const auto before = sidecarOf(out);
        REQUIRE(before.has_value());
        const auto originalGuid = before->guid;
        REQUIRE(originalGuid.isValid());
        CHECK(std::get<int32_t>(before->importOptions.at("baseFontSize")) == 32);

        std::map<std::string, importConfig::ImportOptionValue> second;
        second["baseFontSize"] = int32_t{64};
        second["includeCyrillic"] = true;
        const fs::path reimported = importRoboto(dir, "Reimport", second);
        REQUIRE(reimported == out);

        const auto after = sidecarOf(out);
        REQUIRE(after.has_value());

        // The GUID is what every AssetRef resolves through — it must survive.
        CHECK(after->guid == originalGuid);
        CHECK(std::get<int32_t>(after->importOptions.at("baseFontSize")) == 64);
        CHECK(std::get<bool>(after->importOptions.at("includeCyrillic")) == true);

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());
        CHECK(font.metadata.baseFontSize == 64u);
        CHECK(font.findGlyph(0x0416) != nullptr); // Cyrillic now covered
    }

    TEST_CASE("reimport preserves sidecar fields it does not own")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("preserve");
        const fs::path out = importRoboto(dir, "Preserve", {});
        REQUIRE_FALSE(out.empty());

        const fs::path metaPath = asset::AssetMetadataSerializer::getMetaPath(out);

        // Stand in for what the dependency scanner and the mesh importer write into
        // an existing sidecar after the initial import.
        auto seeded = sidecarOf(out);
        REQUIRE(seeded.has_value());
        const auto originalGuid = seeded->guid;
        const auto dependency = asset::AssetGUID::generate();
        seeded->dependencies.push_back(dependency);
        asset::FractureMetadata fracture;
        fracture.fragmentCount = 7;
        fracture.randomSeed = 1234;
        seeded->fractureData = fracture;
        REQUIRE(asset::AssetMetadataSerializer::save(*seeded, metaPath));

        std::map<std::string, importConfig::ImportOptionValue> options;
        options["baseFontSize"] = int32_t{40};
        REQUIRE_FALSE(importRoboto(dir, "Preserve", options).empty());

        const auto after = sidecarOf(out);
        REQUIRE(after.has_value());
        CHECK(after->guid == originalGuid);
        REQUIRE(after->dependencies.size() == 1);
        CHECK(after->dependencies[0] == dependency);
        REQUIRE(after->fractureData.has_value());
        CHECK(after->fractureData->fragmentCount == 7u);
        CHECK(after->fractureData->randomSeed == 1234u);
        CHECK(std::get<int32_t>(after->importOptions.at("baseFontSize")) == 40);
    }

    TEST_CASE("out-of-range option values are clamped, not trusted")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("clamp");

        // A hand-edited .vfmeta (or a mischievous plugin) can supply anything; the
        // slider ranges are also the importer's clamps.
        std::map<std::string, importConfig::ImportOptionValue> options;
        options["baseFontSize"] = int32_t{100000};
        options["atlasSize"] = int32_t{99};

        const fs::path out = importRoboto(dir, "Clamp", options);
        REQUIRE_FALSE(out.empty());

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());
        CHECK(font.metadata.baseFontSize == 128u); // maxFontSize
        // atlasSize 99 clamps to the last entry (4096), which is only a cap — the
        // "atlas size caps the atlas" case below is what proves the knob is live.
        CHECK(font.atlas.width <= 4096u);
    }

    TEST_CASE("the atlas size option caps the packed atlas")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        // A large base size over the default ranges needs well over 512x512, so the
        // cap becomes observable: the small budget clamps, the large one does not.
        // Asserted comparatively — the exact tight-fit size is a packer detail.
        std::map<std::string, importConfig::ImportOptionValue> capped;
        capped["baseFontSize"] = int32_t{128};
        capped["atlasSize"] = int32_t{0}; // 512

        std::map<std::string, importConfig::ImportOptionValue> roomy;
        roomy["baseFontSize"] = int32_t{128};
        roomy["atlasSize"] = int32_t{3}; // 4096

        const fs::path cappedOut = importRoboto(makeTempDir("cap512"), "Capped", capped);
        const fs::path roomyOut = importRoboto(makeTempDir("cap4096"), "Roomy", roomy);
        REQUIRE_FALSE(cappedOut.empty());
        REQUIRE_FALSE(roomyOut.empty());

        const resource::FontData cappedFont = resource::FontResource::loadFont(cappedOut.string());
        const resource::FontData roomyFont = resource::FontResource::loadFont(roomyOut.string());
        REQUIRE_FALSE(cappedFont.glyphs.empty());
        REQUIRE_FALSE(roomyFont.glyphs.empty());

        CHECK(cappedFont.atlas.width <= 512u);
        CHECK(cappedFont.atlas.height <= 512u);
        CHECK(roomyFont.atlas.width > cappedFont.atlas.width);
    }

    TEST_CASE("an option holding the wrong variant type falls back to the default")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("wrongtype");

        std::map<std::string, importConfig::ImportOptionValue> options;
        options["baseFontSize"] = std::string("huge"); // declared Int
        options["fieldMode"] = std::string("mtsdf");  // declared Enum (int32_t)

        const fs::path out = importRoboto(dir, "WrongType", options);
        REQUIRE_FALSE(out.empty());

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());
        CHECK(font.metadata.baseFontSize == 32u); // the declared default
        CHECK(resource::hasFlag(font.formatFlags, resource::FontFormatFlags::SDF_ENABLED));
    }

    TEST_CASE("the removed generateSDF key is deliberately ignored")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()), "missing fixture: " << robotoSource().string());

        const fs::path dir = makeTempDir("legacykey");
        std::map<std::string, importConfig::ImportOptionValue> options;
        options["generateSDF"] = false;

        const fs::path out = importRoboto(dir, "LegacyKey", options);
        REQUIRE_FALSE(out.empty());

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        REQUIRE_FALSE(font.glyphs.empty());
        CHECK(font.atlas.format == resource::FontAtlasFormat::SDF_8);
        CHECK(font.isSDF());
        CHECK_FALSE(font.isMSDF());
    }
}
