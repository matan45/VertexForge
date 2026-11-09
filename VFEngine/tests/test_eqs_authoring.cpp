#include <doctest.h>
#include "impl/ai/EQSAuthoringService.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/EQSAuthoringEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "archive/VFPakFormat.hpp"
#include "asset/AssetDatabase.hpp"
#include "eqs/EQSQuerySpec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// ============================================================
// VK-1660: EQSAuthoringService - LoadEQSQueryAssetQuery / SaveEQSQueryAssetCommand, the validated
// .vfEQSQuery write path of the MCP eqs_* tools.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path authoringRoot()
    {
        return fs::temp_directory_path() / "vf_eqs_authoring_tests";
    }

    fs::path templatesRoot()
    {
        return authoringRoot() / "assets" / "eqs";
    }

    std::string readAll(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void writeAll(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

    uint64_t hashOfFile(const fs::path& path)
    {
        const std::string bytes = readAll(path);
        return archive::hashBytes(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    }

    // Every param at its catalogue default: the nearest point of a ring around the querier.
    constexpr const char* kValidTemplate = R"({
        "version": 1,
        "description": "Nearest point of a ring",
        "generator": {"type": "Ring", "params": {}},
        "tests": [{"type": "Distance", "params": {}, "score": {"mode": "InverseLinear"}}],
        "maxResults": 1
    })";

    eqs::EQSQuerySpec validSpec()
    {
        eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(kValidTemplate);
        REQUIRE_MESSAGE(parsed.spec.has_value(), "the test template must parse; first error: ",
                        parsed.errors.empty() ? std::string() : parsed.errors.front().path + ": " +
                                                                 parsed.errors.front().message);
        return *parsed.spec;
    }

    // A clean dispatcher, asset database and temp folder per case; records what a save publishes.
    struct AuthoringFixture
    {
        services::EQSAuthoringService service;
        std::vector<std::string> savedPaths;

        AuthoringFixture()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(authoringRoot(), ec);
            fs::create_directories(templatesRoot(), ec);

            service.registerEventHandlers();
            dispatcher.subscribe<events::resource::AssetSavedNotification>(
                [this](const events::resource::AssetSavedNotification& notification)
                {
                    savedPaths.push_back(notification.filePath);
                });
        }

        ~AuthoringFixture()
        {
            service.unregisterEventHandlers();
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
            std::error_code ec;
            fs::remove_all(authoringRoot(), ec);
        }

        AuthoringFixture(const AuthoringFixture&) = delete;
        AuthoringFixture& operator=(const AuthoringFixture&) = delete;

        services::EQSAssetSaveResult save(const fs::path& path, const eqs::EQSQuerySpec& spec,
                                          std::optional<uint64_t> expectedContentHash = std::nullopt,
                                          bool createNew = false)
        {
            events::ai::SaveEQSQueryAssetCommand cmd;
            cmd.path = path.string();
            cmd.spec = spec;
            cmd.expectedContentHash = expectedContentHash;
            cmd.createNew = createNew;
            return events::EventDispatcher::instance().execute(cmd);
        }

        services::EQSAssetLoadResult load(const fs::path& path)
        {
            events::ai::LoadEQSQueryAssetQuery query;
            query.path = path.string();
            return events::EventDispatcher::instance().query(query);
        }
    };
}

TEST_SUITE("EQSAuthoringService")
{
    TEST_CASE_FIXTURE(AuthoringFixture, "a new template is written in canonical form, hashed and announced")
    {
        const fs::path path = templatesRoot() / "NearestPoint.vfEQSQuery";
        const eqs::EQSQuerySpec spec = validSpec();

        const auto result = save(path, spec, std::nullopt, true);
        REQUIRE(result.success);
        CHECK(result.error.empty());
        CHECK(result.errors.empty());
        CHECK(result.created);
        CHECK(readAll(path) == eqs::querySpecToText(spec));
        CHECK(result.contentHash == hashOfFile(path));
        CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
        REQUIRE(savedPaths.size() == 1);
        CHECK(savedPaths[0] == path.string());

        const auto loaded = load(path);
        REQUIRE(loaded.success);
        CHECK(loaded.text == readAll(path));
        CHECK(loaded.contentHash == result.contentHash);

        // The canonical text parses back to the same spec.
        const eqs::EQSSpecParseResult reparsed = eqs::parseQuerySpecText(loaded.text);
        REQUIRE(reparsed.spec.has_value());
        CHECK(*reparsed.spec == spec);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "only absolute .vfEQSQuery paths named like a query are accepted")
    {
        const eqs::EQSQuerySpec spec = validSpec();

        CHECK_FALSE(save(fs::path("assets/eqs/Relative.vfEQSQuery"), spec).success);
        CHECK_FALSE(save(templatesRoot() / "Wrong.json", spec).success);
        CHECK_FALSE(save(templatesRoot() / "9Lives.vfEQSQuery", spec).success);
        CHECK_FALSE(save(templatesRoot() / "has space.vfEQSQuery", spec).success);
        CHECK_FALSE(fs::exists(templatesRoot() / "Wrong.json"));
        CHECK_FALSE(fs::exists(templatesRoot() / "9Lives.vfEQSQuery"));
        CHECK(savedPaths.empty());

        // The extension in any case.
        CHECK(save(templatesRoot() / "UpperCase.VFEQSQUERY", spec).success);
        CHECK(savedPaths.size() == 1);
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "createNew refuses an existing file")
    {
        const fs::path path = templatesRoot() / "Existing.vfEQSQuery";
        writeAll(path, "{}");

        const auto result = save(path, validSpec(), std::nullopt, true);
        CHECK_FALSE(result.success);
        CHECK(result.error.find("already exists") != std::string::npos);
        CHECK(readAll(path) == "{}");
        CHECK(savedPaths.empty());
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "expectedContentHash guards against edits made elsewhere")
    {
        const fs::path path = templatesRoot() / "Guarded.vfEQSQuery";
        const auto first = save(path, validSpec());
        REQUIRE(first.success);

        writeAll(path, readAll(path) + "\n"); // edited elsewhere since it was read
        const auto stale = save(path, validSpec(), first.contentHash);
        CHECK_FALSE(stale.success);
        CHECK(stale.error.find("changed on disk") != std::string::npos);

        const auto fresh = save(path, validSpec(), hashOfFile(path));
        CHECK(fresh.success);
        CHECK_FALSE(fresh.created);

        const auto missing = save(templatesRoot() / "Missing.vfEQSQuery", validSpec(), uint64_t{42});
        CHECK_FALSE(missing.success);
        CHECK(missing.error.find("no longer exists") != std::string::npos);
        CHECK_FALSE(fs::exists(templatesRoot() / "Missing.vfEQSQuery"));
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "a spec the parser refuses is not saved and nothing is written")
    {
        eqs::EQSQuerySpec spec = validSpec();
        spec.maxResults = 0; // outside 1..64

        const fs::path path = templatesRoot() / "Broken.vfEQSQuery";
        const auto result = save(path, spec);
        CHECK_FALSE(result.success);
        CHECK_FALSE(result.errors.empty());
        CHECK(result.error.find("not saved") != std::string::npos);
        CHECK_FALSE(fs::exists(path));
        CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
        CHECK(savedPaths.empty());
    }

    TEST_CASE_FIXTURE(AuthoringFixture, "load returns the file's text and hash, or why it cannot")
    {
        CHECK_FALSE(load(templatesRoot() / "Nope.vfEQSQuery").success);
        CHECK_FALSE(load(fs::path("relative.vfEQSQuery")).success);

        // Load does not parse: an invalid template still reads, so a tool can show and fix it.
        const fs::path path = templatesRoot() / "Text.vfEQSQuery";
        writeAll(path, "not json");
        const auto loaded = load(path);
        REQUIRE(loaded.success);
        CHECK(loaded.text == "not json");
        CHECK(loaded.contentHash == hashOfFile(path));
    }
}
