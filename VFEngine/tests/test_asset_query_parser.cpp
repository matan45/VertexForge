#include <doctest.h>
#include <windows/contentbrowser/AssetQueryParser.hpp>

// ============================================================
// Content browser search-box query syntax: plain terms plus
// type:/ext:/guid:/ref: tokens, quoting, degradation rules.
// ============================================================

using windows::parseAssetQuery;
using windows::normalizePathForCompare;

TEST_CASE("plain terms are lowercased and split on whitespace")
{
    auto q = parseAssetQuery("Wood  FLOOR\tstone");
    REQUIRE(q.terms.size() == 3);
    CHECK(q.terms[0] == "wood");
    CHECK(q.terms[1] == "floor");
    CHECK(q.terms[2] == "stone");
    CHECK_FALSE(q.hasStructuredTokens());
}

TEST_CASE("empty and whitespace-only queries parse to nothing")
{
    CHECK(parseAssetQuery("").terms.empty());
    auto q = parseAssetQuery("   \t ");
    CHECK(q.terms.empty());
    CHECK_FALSE(q.hasStructuredTokens());
}

TEST_CASE("type token combines with plain terms")
{
    auto q = parseAssetQuery("type:Texture wood");
    CHECK(q.typeToken == "texture");
    REQUIRE(q.terms.size() == 1);
    CHECK(q.terms[0] == "wood");
    CHECK(q.hasStructuredTokens());
}

TEST_CASE("quoted values keep spaces")
{
    auto q = parseAssetQuery("type:\"Material Instance\"");
    CHECK(q.typeToken == "material instance");

    auto q2 = parseAssetQuery("\"two words\"");
    REQUIRE(q2.terms.size() == 1);
    CHECK(q2.terms[0] == "two words");
}

TEST_CASE("ext token gets a leading dot and is lowercased")
{
    CHECK(parseAssetQuery("ext:vfMesh").extToken == ".vfmesh");
    CHECK(parseAssetQuery("ext:.vfMesh").extToken == ".vfmesh");
}

TEST_CASE("guid and ref tokens are captured")
{
    auto q = parseAssetQuery("guid:00FF00FF00FF00FF ref:assets/wood.vfMat");
    CHECK(q.guidToken == "00ff00ff00ff00ff");
    CHECK(q.refToken == "assets/wood.vfMat"); // ref keeps its case (path)
}

TEST_CASE("unknown keys degrade to plain terms")
{
    auto q = parseAssetQuery("foo:bar baz");
    CHECK(q.typeToken.empty());
    REQUIRE(q.terms.size() == 2);
    CHECK(q.terms[0] == "foo:bar");
    CHECK(q.terms[1] == "baz");
}

TEST_CASE("known key with empty value degrades to a plain term")
{
    auto q = parseAssetQuery("type:");
    CHECK(q.typeToken.empty());
    REQUIRE(q.terms.size() == 1);
    CHECK(q.terms[0] == "type:");
}

TEST_CASE("last occurrence of a key wins")
{
    auto q = parseAssetQuery("type:texture type:model");
    CHECK(q.typeToken == "model");
}

TEST_CASE("leading colon is not a key")
{
    auto q = parseAssetQuery(":weird");
    REQUIRE(q.terms.size() == 1);
    CHECK(q.terms[0] == ":weird");
}

TEST_CASE("normalizePathForCompare unifies separators and case")
{
    CHECK(normalizePathForCompare("C:\\Assets\\Wood.vfMat") == "c:/assets/wood.vfmat");
    CHECK(normalizePathForCompare("c:/assets/wood.vfmat") == "c:/assets/wood.vfmat");
}
