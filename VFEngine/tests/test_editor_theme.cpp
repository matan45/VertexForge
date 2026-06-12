#include <doctest.h>
#include <config/EditorTheme.hpp>
#include <config/EditorThemeSerializer.hpp>
#include <config/EditorThemeStorage.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace
{
    struct TempThemeDir
    {
        std::string path;

        TempThemeDir()
        {
            path = (std::filesystem::temp_directory_path() / "vf_theme_test").string();
            std::filesystem::remove_all(path);
        }

        ~TempThemeDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    config::EditorTheme makeSampleTheme()
    {
        config::EditorTheme theme;
        theme.name = "Ocean";
        theme.basedOn = "Light";
        theme.colors["Text"] = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
        theme.colors["WindowBg"] = glm::vec4(0.0f, 0.05f, 0.1f, 1.0f);
        theme.colors["Button"] = glm::vec4(0.2f, 0.4f, 0.8f, 0.5f);
        return theme;
    }

    void checkColor(const config::EditorTheme& theme, const std::string& key, const glm::vec4& expected)
    {
        REQUIRE(theme.colors.count(key) == 1);
        const glm::vec4& c = theme.colors.at(key);
        CHECK(c.x == doctest::Approx(expected.x));
        CHECK(c.y == doctest::Approx(expected.y));
        CHECK(c.z == doctest::Approx(expected.z));
        CHECK(c.w == doctest::Approx(expected.w));
    }
}

TEST_SUITE("EditorTheme") {

TEST_CASE("serializer: roundtrip preserves name, base and colors")
{
    config::EditorTheme original = makeSampleTheme();

    nlohmann::json j = original;
    auto restored = j.get<config::EditorTheme>();

    CHECK(restored.name == original.name);
    CHECK(restored.basedOn == original.basedOn);
    CHECK(restored.colors.size() == original.colors.size());
    for (const auto& [key, color] : original.colors)
        checkColor(restored, key, color);
}

TEST_CASE("serializer: writes schema version")
{
    nlohmann::json j = makeSampleTheme();
    REQUIRE(j.contains("schemaVersion"));
    CHECK(j["schemaVersion"]["major"].get<uint32_t>() == config::EditorThemeSchemaVersion::major);
}

TEST_CASE("serializer: missing fields fall back to defaults")
{
    auto theme = nlohmann::json::object().get<config::EditorTheme>();
    CHECK(theme.name.empty());
    CHECK(theme.basedOn == "Dark");
    CHECK(theme.colors.empty());
}

TEST_CASE("serializer: malformed color entries are skipped without throwing")
{
    nlohmann::json j = {
        {"name", "Broken"},
        {"basedOn", "Purple"}, // invalid base -> default
        {"colors", {
            {"Text", nlohmann::json::array({0.5f, 0.5f, 0.5f, 1.0f})},
            {"TooShort", nlohmann::json::array({0.1f, 0.2f, 0.3f})},
            {"NotArray", "red"},
            {"NotNumbers", nlohmann::json::array({"a", "b", "c", "d"})}
        }}
    };

    auto theme = j.get<config::EditorTheme>();
    CHECK(theme.basedOn == "Dark");
    CHECK(theme.colors.size() == 1);
    checkColor(theme, "Text", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
}

TEST_CASE("storage: save, list, load, remove")
{
    TempThemeDir dir;
    config::EditorTheme theme = makeSampleTheme();

    CHECK(config::EditorThemeStorage::listThemes(dir.path).empty());
    REQUIRE(config::EditorThemeStorage::save(dir.path, theme));

    auto names = config::EditorThemeStorage::listThemes(dir.path);
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "Ocean");

    auto loaded = config::EditorThemeStorage::load(dir.path, "Ocean");
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == theme.name);
    CHECK(loaded->basedOn == theme.basedOn);
    for (const auto& [key, color] : theme.colors)
        checkColor(*loaded, key, color);

    CHECK(config::EditorThemeStorage::remove(dir.path, "Ocean"));
    CHECK(config::EditorThemeStorage::listThemes(dir.path).empty());
    CHECK_FALSE(config::EditorThemeStorage::load(dir.path, "Ocean").has_value());
}

TEST_CASE("storage: load of corrupt file returns nullopt")
{
    TempThemeDir dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(std::filesystem::path(dir.path) / "Bad.json") << "{ not json";

    CHECK_FALSE(config::EditorThemeStorage::load(dir.path, "Bad").has_value());
}

TEST_CASE("storage: missing directory is handled gracefully")
{
    std::string missing = (std::filesystem::temp_directory_path() / "vf_theme_test_missing").string();
    CHECK(config::EditorThemeStorage::listThemes(missing).empty());
    CHECK_FALSE(config::EditorThemeStorage::load(missing, "Anything").has_value());
    CHECK_FALSE(config::EditorThemeStorage::remove(missing, "Anything"));
}

TEST_CASE("sanitizeName: rejects empty, reserved and strips path characters")
{
    CHECK(config::EditorThemeStorage::sanitizeName("") == "");
    CHECK(config::EditorThemeStorage::sanitizeName("   ") == "");
    CHECK(config::EditorThemeStorage::sanitizeName("Dark") == "");
    CHECK(config::EditorThemeStorage::sanitizeName("Light") == "");
    CHECK(config::EditorThemeStorage::sanitizeName("../../evil") == "evil");
    CHECK(config::EditorThemeStorage::sanitizeName("My Theme-2_final") == "My Theme-2_final");
    CHECK(config::EditorThemeStorage::sanitizeName("  trimmed  ") == "trimmed");
    CHECK(config::EditorThemeStorage::sanitizeName("a/b\\c:d*e") == "abcde");
}

TEST_CASE("storage: save rejects reserved names")
{
    TempThemeDir dir;
    config::EditorTheme theme;
    theme.name = "Dark";
    CHECK_FALSE(config::EditorThemeStorage::save(dir.path, theme));
}

}
