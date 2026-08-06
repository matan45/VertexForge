#include <doctest.h>
#include <config/EditorPreferencesSerializer.hpp>

#include <nlohmann/json.hpp>

TEST_SUITE("EditorPreferences")
{
    TEST_CASE("global editor audio starts unmuted")
    {
        const auto preferences = config::EditorPreferences::createDefault();
        CHECK_FALSE(preferences.audio.globalMuted);
    }

    TEST_CASE("global editor audio mute survives JSON roundtrip")
    {
        auto preferences = config::EditorPreferences::createDefault();
        preferences.audio.globalMuted = true;
        preferences.debug.logLevel = "Warning";

        const nlohmann::json serialized = preferences;
        const auto restored = serialized.get<config::EditorPreferences>();

        CHECK(restored.audio.globalMuted);
        CHECK(restored.debug.logLevel == "Warning");
    }

    TEST_CASE("legacy JSON without audio settings remains unmuted")
    {
        const nlohmann::json legacy = {
            {"debug", {{"logLevel", "Error"}}},
            {"memory", {{"cpuMemoryBudgetBytes", 1024u}}}
        };

        const auto restored = legacy.get<config::EditorPreferences>();
        CHECK_FALSE(restored.audio.globalMuted);
        CHECK(restored.debug.logLevel == "Error");
        CHECK(restored.memory.cpuMemoryBudgetBytes == 1024u);
    }

    TEST_CASE("serializer writes editor settings schema 1.4")
    {
        const nlohmann::json serialized = config::EditorPreferences::createDefault();
        REQUIRE(serialized.contains("schemaVersion"));
        CHECK(serialized["schemaVersion"]["major"].get<uint32_t>() == 1u);
        CHECK(serialized["schemaVersion"]["minor"].get<uint32_t>() == 4u);
    }

    // VK-1615
    TEST_CASE("undo history limits default to 50 entries and a 512 MiB ceiling")
    {
        const auto preferences = config::EditorPreferences::createDefault();
        CHECK(preferences.undo.maxHistoryDepth == 50u);
        CHECK(preferences.undo.maxHistoryBytes == 512ull * 1024ull * 1024ull);
    }

    TEST_CASE("undo history limits survive JSON roundtrip")
    {
        auto preferences = config::EditorPreferences::createDefault();
        preferences.undo.maxHistoryDepth = 0;  // unlimited
        preferences.undo.maxHistoryBytes = 64ull * 1024ull * 1024ull;

        const nlohmann::json serialized = preferences;
        const auto restored = serialized.get<config::EditorPreferences>();

        CHECK(restored.undo.maxHistoryDepth == 0u);
        CHECK(restored.undo.maxHistoryBytes == 64ull * 1024ull * 1024ull);
    }

    TEST_CASE("legacy JSON without undo settings gets the defaults")
    {
        const nlohmann::json legacy = {
            {"debug", {{"logLevel", "Error"}}},
            {"memory", {{"cpuMemoryBudgetBytes", 1024u}}}
        };

        const auto restored = legacy.get<config::EditorPreferences>();
        CHECK(restored.undo.maxHistoryDepth == 50u);
        CHECK(restored.undo.maxHistoryBytes == 512ull * 1024ull * 1024ull);
    }
}
