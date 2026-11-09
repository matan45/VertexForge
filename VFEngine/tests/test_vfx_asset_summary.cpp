#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <impl/vfx/VFXAssetSummaryService.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXAssetSummaryEvents.hpp>
#include <asset/AssetDatabase.hpp>

#include <glm/glm.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_repo_scan_helpers.hpp"

// VK-1657 — VFXAssetSummaryService reports the AUTHORED values of effects and combos for the MCP
// vfx tools. The library files are copied into a temporary project (combo steps reference
// "assets/vfx/library/..." and resolving them registers .vfmeta files next to the effects, which
// must not land in the repo).

namespace
{
    namespace fs = std::filesystem;

    struct LibraryProject
    {
        fs::path root;

        LibraryProject()
            : root(fs::temp_directory_path() / "vf_vfx_asset_summary_project")
        {
            const auto repo = repo_scan::findRepoRoot();
            REQUIRE_MESSAGE(repo.has_value(), "repo root not found from the Tests.exe path or the working directory");
            const fs::path library = *repo / "assets" / "vfx" / "library";

            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root / "assets" / "vfx" / "library");
            for (const char* file : {"fire_loop.vfVFX", "magic_heal.vfVFX", "example_cast_combo.vfVFXSequence"})
            {
                fs::copy_file(library / file, root / "assets" / "vfx" / "library" / file,
                              fs::copy_options::overwrite_existing);
            }
            std::ofstream(root / "assets" / "vfx" / "broken.vfVFX") << "this is not json";
            REQUIRE(asset::AssetDatabase::instance().rebuildFromMetaFiles(root.string()));
        }

        ~LibraryProject()
        {
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        std::string path(const char* relative) const { return (root / relative).generic_string(); }
    };

    void checkColor(const glm::vec4& actual, const glm::vec4& expected)
    {
        CHECK(actual.x == doctest::Approx(expected.x));
        CHECK(actual.y == doctest::Approx(expected.y));
        CHECK(actual.z == doctest::Approx(expected.z));
        CHECK(actual.w == doctest::Approx(expected.w));
    }
}

TEST_SUITE("VFXAssetSummary")
{
    TEST_CASE("fire_loop reports its authored emitter values and the gradient's birth colour")
    {
        LibraryProject project;
        const services::VFXAssetSummary summary =
            services::VFXAssetSummaryService::summarizeAsset(project.path("assets/vfx/library/fire_loop.vfVFX"));

        REQUIRE(summary.valid);
        CHECK(summary.error.empty());
        CHECK_FALSE(summary.isCombo);
        CHECK(summary.name == "Fire Loop");
        REQUIRE(summary.effect.has_value());
        CHECK_FALSE(summary.combo.has_value());

        const services::VFXEffectSummary& effect = *summary.effect;
        CHECK(effect.spawnRate == doctest::Approx(40.0f));
        CHECK(effect.lifetime == doctest::Approx(0.9f));
        CHECK(effect.startSize == doctest::Approx(0.8f));
        CHECK(effect.startSpeed == doctest::Approx(1.8f));
        checkColor(effect.startColor, glm::vec4(1.0f, 0.5f, 0.1f, 1.0f));
        CHECK(effect.looping);
        CHECK(effect.blendMode == vfx::VFXBlendMode::Additive); // legacy additiveBlend: true
        CHECK(effect.sortOrder == 0);
        CHECK(effect.renderMode == 0);
        CHECK(effect.emitsLight);
        CHECK(effect.texture.empty());
        CHECK(effect.events.empty());

        // The ColorOverLifetime gradient, not startColor, is what the particles show.
        CHECK(effect.colorOverLifetime);
        checkColor(effect.birthColor, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
        checkColor(effect.deathColor, glm::vec4(0.2f, 0.05f, 0.01f, 0.0f));
    }

    TEST_CASE("example_cast_combo reports its steps and cue names")
    {
        LibraryProject project;
        const services::VFXAssetSummary summary = services::VFXAssetSummaryService::summarizeAsset(
            project.path("assets/vfx/library/example_cast_combo.vfVFXSequence"));

        REQUIRE(summary.valid);
        CHECK(summary.isCombo);
        CHECK(summary.name == "Example Cast Combo");
        REQUIRE(summary.combo.has_value());
        CHECK_FALSE(summary.effect.has_value());

        const services::VFXComboSummary& combo = *summary.combo;
        REQUIRE(combo.steps.size() == 2);
        CHECK(combo.steps[0].label == "charge");
        CHECK(combo.steps[0].kind == "vfx");
        CHECK(combo.steps[0].cue.empty());
        CHECK(combo.steps[0].loop);
        CHECK(combo.steps[0].effectPath.find("fire_loop.vfVFX") != std::string::npos);
        CHECK(combo.steps[1].label == "release");
        CHECK(combo.steps[1].cue == "release");
        CHECK_FALSE(combo.steps[1].loop);
        CHECK(combo.cues == std::vector<std::string>{"release"});
        CHECK(combo.playbackRate == doctest::Approx(1.0f));
    }

    TEST_CASE("unreadable and missing files come back invalid with a reason")
    {
        LibraryProject project;

        const auto broken = services::VFXAssetSummaryService::summarizeAsset(project.path("assets/vfx/broken.vfVFX"));
        CHECK_FALSE(broken.valid);
        CHECK_FALSE(broken.error.empty());
        CHECK_FALSE(broken.effect.has_value());

        const auto missing = services::VFXAssetSummaryService::summarizeAsset(project.path("assets/vfx/missing.vfVFXSequence"));
        CHECK_FALSE(missing.valid);
        CHECK(missing.isCombo); // classified by extension even when the file is gone
        CHECK(missing.error.rfind("File not found", 0) == 0);

        const auto empty = services::VFXAssetSummaryService::summarizeAsset("");
        CHECK_FALSE(empty.valid);
        CHECK_FALSE(empty.error.empty());
    }

    TEST_CASE("the query answers one summary per path, in order, for project-relative paths too")
    {
        LibraryProject project;
        services::VFXAssetSummaryService service;
        service.registerEventHandlers();

        services::events::vfxassets::GetVFXAssetSummariesQuery query;
        query.paths = {"assets/vfx/library/magic_heal.vfVFX", "assets/vfx/library/fire_loop.vfVFX", "nope.vfVFX"};
        const std::vector<services::VFXAssetSummary> summaries = ::events::EventDispatcher::instance().query(query);

        REQUIRE(summaries.size() == 3);
        CHECK(summaries[0].path == "assets/vfx/library/magic_heal.vfVFX");
        CHECK(summaries[0].valid);
        CHECK(summaries[1].valid);
        CHECK(summaries[1].name == "Fire Loop");
        CHECK_FALSE(summaries[2].valid);
    }
}
