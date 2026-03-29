#include <doctest.h>
#include <resource/EndianUtils.hpp>
#include <sstream>
#include <cstring>

// ============================================================
// VK-1060: Endian utilities unit tests
// ============================================================

using namespace resource::endian;

TEST_SUITE("EndianUtils") {

// ---- byteswap16 ----

TEST_CASE("byteswap16: known value") {
    CHECK(byteswap16(0x0102) == 0x0201);
}

TEST_CASE("byteswap16: zero unchanged") {
    CHECK(byteswap16(0x0000) == 0x0000);
}

TEST_CASE("byteswap16: swap twice is identity") {
    uint16_t val = 0xABCD;
    CHECK(byteswap16(byteswap16(val)) == val);
}

// ---- byteswap32 ----

TEST_CASE("byteswap32: known value") {
    CHECK(byteswap32(0x01020304) == 0x04030201);
}

TEST_CASE("byteswap32: zero unchanged") {
    CHECK(byteswap32(0x00000000) == 0x00000000);
}

TEST_CASE("byteswap32: swap twice is identity") {
    uint32_t val = 0xDEADBEEF;
    CHECK(byteswap32(byteswap32(val)) == val);
}

// ---- byteswap64 ----

TEST_CASE("byteswap64: swap twice is identity") {
    uint64_t val = 0x0102030405060708ULL;
    CHECK(byteswap64(byteswap64(val)) == val);
}

TEST_CASE("byteswap64: known value") {
    uint64_t val = 0x0102030405060708ULL;
    uint64_t expected = 0x0807060504030201ULL;
    CHECK(byteswap64(val) == expected);
}

TEST_CASE("byteswap64: zero unchanged") {
    CHECK(byteswap64(0ULL) == 0ULL);
}

// ---- toLittleEndian / fromLittleEndian roundtrip ----

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip uint16_t") {
    uint16_t val = 0x1234;
    CHECK(fromLittleEndian(toLittleEndian(val)) == val);
}

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip uint32_t") {
    uint32_t val = 0x12345678;
    CHECK(fromLittleEndian(toLittleEndian(val)) == val);
}

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip uint64_t") {
    uint64_t val = 0x123456789ABCDEF0ULL;
    CHECK(fromLittleEndian(toLittleEndian(val)) == val);
}

// ---- Float specialization roundtrip ----

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip float") {
    float val = 3.14159f;
    float result = fromLittleEndian(toLittleEndian(val));
    CHECK(result == doctest::Approx(val));
}

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip float negative") {
    float val = -42.5f;
    float result = fromLittleEndian(toLittleEndian(val));
    CHECK(result == doctest::Approx(val));
}

TEST_CASE("toLittleEndian/fromLittleEndian roundtrip float zero") {
    float val = 0.0f;
    float result = fromLittleEndian(toLittleEndian(val));
    CHECK(result == doctest::Approx(val));
}

// ---- writeLE / readLE roundtrip through stringstream ----

TEST_CASE("writeLE/readLE roundtrip uint32_t through stringstream") {
    std::stringstream ss;
    uint32_t original = 0xCAFEBABE;
    writeLE(ss, original);

    ss.seekg(0);
    uint32_t restored = readLE<uint32_t>(ss);
    CHECK(restored == original);
}

TEST_CASE("writeLE/readLE roundtrip uint16_t through stringstream") {
    std::stringstream ss;
    uint16_t original = 0xBEEF;
    writeLE(ss, original);

    ss.seekg(0);
    uint16_t restored = readLE<uint16_t>(ss);
    CHECK(restored == original);
}

TEST_CASE("writeLE/readLE roundtrip uint64_t through stringstream") {
    std::stringstream ss;
    uint64_t original = 0xDEADC0DEBEEFCAFEULL;
    writeLE(ss, original);

    ss.seekg(0);
    uint64_t restored = readLE<uint64_t>(ss);
    CHECK(restored == original);
}

TEST_CASE("writeLE/readLE roundtrip float through stringstream") {
    std::stringstream ss;
    float original = 2.71828f;
    writeLE(ss, original);

    ss.seekg(0);
    float restored = readLE<float>(ss);
    CHECK(restored == doctest::Approx(original));
}

TEST_CASE("writeLE/readLE multiple values in sequence") {
    std::stringstream ss;
    uint32_t a = 100;
    uint16_t b = 200;
    uint64_t c = 300;

    writeLE(ss, a);
    writeLE(ss, b);
    writeLE(ss, c);

    ss.seekg(0);
    CHECK(readLE<uint32_t>(ss) == a);
    CHECK(readLE<uint16_t>(ss) == b);
    CHECK(readLE<uint64_t>(ss) == c);
}

} // TEST_SUITE
