#include <doctest.h>
#include <archive/VFPakFormat.hpp>
#include <cstdint>
#include <string>

// ============================================================
// VK-1060: Archive format (VFPak) unit tests
// ============================================================

using namespace archive;

TEST_SUITE("ArchiveFormat") {

// ---- hashPath: normalization ----

TEST_CASE("hashPath: backslash and forward slash produce same hash") {
    std::string forward = "assets/textures/grass.vfImage";
    std::string back = "assets\\textures\\grass.vfImage";
    CHECK(hashPath(forward) == hashPath(back));
}

TEST_CASE("hashPath: mixed separators normalized to same hash") {
    std::string mixed = "assets\\textures/sub\\file.vfImage";
    std::string forward = "assets/textures/sub/file.vfImage";
    CHECK(hashPath(mixed) == hashPath(forward));
}

// ---- hashPath: deterministic ----

TEST_CASE("hashPath: same input always produces same output") {
    std::string path = "models/hero/hero.vfMesh";
    uint64_t h1 = hashPath(path);
    uint64_t h2 = hashPath(path);
    CHECK(h1 == h2);
}

// ---- hashPath: different paths produce different hashes ----

TEST_CASE("hashPath: different paths produce different hashes") {
    uint64_t h1 = hashPath("assets/a.txt");
    uint64_t h2 = hashPath("assets/b.txt");
    CHECK(h1 != h2);
}

TEST_CASE("hashPath: similar but distinct paths produce different hashes") {
    uint64_t h1 = hashPath("data/file1");
    uint64_t h2 = hashPath("data/file2");
    CHECK(h1 != h2);
}

// ---- hashBytes: known FNV-1a outputs ----

TEST_CASE("hashBytes: empty data produces FNV offset basis") {
    // FNV-1a with zero bytes processed should return the offset basis
    uint64_t result = hashBytes(nullptr, 0);
    CHECK(result == 14695981039346656037ULL);
}

TEST_CASE("hashBytes: known output for 'hello'") {
    // Pre-computed FNV-1a 64-bit for "hello"
    const uint8_t data[] = {'h', 'e', 'l', 'l', 'o'};
    uint64_t result = hashBytes(data, 5);

    // FNV-1a 64 for "hello" = 0xa430d84680aabd0b = 11831194018420276491
    CHECK(result == 11831194018420276491ULL);
}

TEST_CASE("hashBytes: different data produces different hashes") {
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5, 6};
    CHECK(hashBytes(a, 3) != hashBytes(b, 3));
}

// ---- alignTo ----

TEST_CASE("alignTo: zero value stays zero") {
    CHECK(alignTo(0, 16) == 0);
}

TEST_CASE("alignTo: value 1 rounds up to alignment") {
    CHECK(alignTo(1, 16) == 16);
}

TEST_CASE("alignTo: already aligned value stays unchanged") {
    CHECK(alignTo(16, 16) == 16);
}

TEST_CASE("alignTo: value just past alignment rounds to next multiple") {
    CHECK(alignTo(17, 16) == 32);
}

TEST_CASE("alignTo: various alignment values") {
    CHECK(alignTo(5, 4) == 8);
    CHECK(alignTo(8, 4) == 8);
    CHECK(alignTo(9, 4) == 12);
    CHECK(alignTo(0, 4) == 0);
    CHECK(alignTo(63, 64) == 64);
    CHECK(alignTo(65, 64) == 128);
}

// ---- VFPakHeader defaults ----

TEST_CASE("VFPakHeader: default magic equals VFPAK_MAGIC") {
    VFPakHeader header{};
    CHECK(header.magic == VFPAK_MAGIC);
}

TEST_CASE("VFPakHeader: default version equals VFPAK_VERSION") {
    VFPakHeader header{};
    CHECK(header.version == VFPAK_VERSION);
}

TEST_CASE("VFPakHeader: default fields are zeroed") {
    VFPakHeader header{};
    CHECK(header.flags == 0);
    CHECK(header.entryCount == 0);
    CHECK(header.tocOffset == 0);
    CHECK(header.tocSize == 0);
}

// ---- sizeof(VFPakHeader) == VFPAK_HEADER_SIZE ----

TEST_CASE("VFPakHeader: size matches VFPAK_HEADER_SIZE") {
    // Runtime equivalent of the static_assert in the header
    CHECK(sizeof(VFPakHeader) == VFPAK_HEADER_SIZE);
    CHECK(sizeof(VFPakHeader) == 64);
}

} // TEST_SUITE
