#include <doctest.h>
#include <uuid/UUID.hpp>
#include <asset/AssetGUID.hpp>
#include <string/StringUtil.hpp>
#include <unordered_set>

// ============================================================
// VK-1094: Core Utilities unit tests
// ============================================================

TEST_SUITE("CoreUtilities") {

// ---- UUID ----

TEST_CASE("UUID: default constructor generates valid UUID") {
    uuid::UUID id;
    CHECK(id.isValid());
    CHECK(id.getValue() != 0);
}

TEST_CASE("UUID: invalid() returns invalid UUID") {
    auto id = uuid::UUID::invalid();
    CHECK_FALSE(id.isValid());
    CHECK(id.getValue() == 0);
}

TEST_CASE("UUID: construct from value") {
    uuid::UUID id(42);
    CHECK(id.isValid());
    CHECK(id.getValue() == 42);
}

TEST_CASE("UUID: uniqueness") {
    // UUID uses thread_local mt19937_64 seeded from random_device.
    // Collision probability for 1000 64-bit values is negligible (~2.7e-14).
    constexpr int N = 1000;
    std::unordered_set<uint64_t> values;
    for (int i = 0; i < N; ++i) {
        uuid::UUID id;
        values.insert(id.getValue());
    }
    CHECK(values.size() == N);
}

TEST_CASE("UUID: equality operators") {
    uuid::UUID a(100);
    uuid::UUID b(100);
    uuid::UUID c(200);

    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("UUID: less-than operator") {
    uuid::UUID a(10);
    uuid::UUID b(20);
    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("UUID: Hash functor works in unordered containers") {
    std::unordered_set<uuid::UUID, uuid::UUID::Hash> set;
    uuid::UUID a(1);
    uuid::UUID b(2);
    set.insert(a);
    set.insert(b);
    set.insert(a); // duplicate
    CHECK(set.size() == 2);
}

// ---- AssetGUID ----

TEST_CASE("AssetGUID: generate produces valid GUID") {
    auto guid = asset::AssetGUID::generate();
    CHECK(guid.isValid());
}

TEST_CASE("AssetGUID: invalid returns invalid GUID") {
    auto guid = asset::AssetGUID::invalid();
    CHECK_FALSE(guid.isValid());
}

TEST_CASE("AssetGUID: fromValue roundtrip") {
    auto guid = asset::AssetGUID::fromValue(0xDEADBEEF);
    CHECK(guid.isValid());
    CHECK(guid.getValue() == 0xDEADBEEF);
}

TEST_CASE("AssetGUID: toString produces 16-char hex string") {
    auto guid = asset::AssetGUID::fromValue(0xFF);
    auto str = guid.toString();
    CHECK(str.length() == 16);
}

TEST_CASE("AssetGUID: fromString/toString roundtrip") {
    auto original = asset::AssetGUID::generate();
    auto str = original.toString();
    auto restored = asset::AssetGUID::fromString(str);
    CHECK(original == restored);
}

TEST_CASE("AssetGUID: equality and inequality") {
    auto a = asset::AssetGUID::fromValue(42);
    auto b = asset::AssetGUID::fromValue(42);
    auto c = asset::AssetGUID::fromValue(99);
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("AssetGUID: hashing works in unordered containers") {
    std::unordered_set<asset::AssetGUID, asset::AssetGUID::Hash> set;
    auto a = asset::AssetGUID::fromValue(1);
    auto b = asset::AssetGUID::fromValue(2);
    set.insert(a);
    set.insert(b);
    set.insert(a); // duplicate
    CHECK(set.size() == 2);
}

// ---- StringUtil ----

TEST_CASE("StringUtil: toLower converts uppercase") {
    CHECK(StringUtil::toLower("HELLO") == "hello");
}

TEST_CASE("StringUtil: toLower preserves lowercase") {
    CHECK(StringUtil::toLower("hello") == "hello");
}

TEST_CASE("StringUtil: toLower handles empty string") {
    CHECK(StringUtil::toLower("") == "");
}

TEST_CASE("StringUtil: toLower handles mixed case") {
    CHECK(StringUtil::toLower("Hello World 123!") == "hello world 123!");
}

} // TEST_SUITE
