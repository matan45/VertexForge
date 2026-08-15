#include <doctest.h>
#include <string/FileNameSanitize.hpp>

#include <string>

// ============================================================
// utilities/string/FileNameSanitize: the shared "turn arbitrary text into a usable file name"
// helper. Four byte-identical copies of the character filter had accumulated (import/Mesh.cpp,
// import/MeshTextureImport.cpp, editor RoadMeshGenerator.cpp, editor WorldSectorWindowDataLayers.cpp)
// and none of them handled Win32 reserved device names. These cases pin the behaviour all four now
// share, so a future divergence fails here rather than at an export the user only notices later.
// ============================================================

TEST_SUITE("FileNameSanitize")
{
    TEST_CASE("every Win32-illegal character becomes an underscore")
    {
        CHECK(strutil::sanitizeFileStem("a/b") == "a_b");
        CHECK(strutil::sanitizeFileStem("a\\b") == "a_b");
        CHECK(strutil::sanitizeFileStem("C:name") == "C_name");
        CHECK(strutil::sanitizeFileStem("a*b?c") == "a_b_c");
        CHECK(strutil::sanitizeFileStem("a<b>c") == "a_b_c");
        CHECK(strutil::sanitizeFileStem("a\"b|c") == "a_b_c");
        CHECK(strutil::sanitizeFileStem(std::string("a\x01_b")) == "a__b");
    }

    TEST_CASE("a legal name is returned unchanged")
    {
        CHECK(strutil::sanitizeFileStem("fog_of_war") == "fog_of_war");
        CHECK(strutil::sanitizeFileStem("Layer 1 (final)") == "Layer 1 (final)");
    }

    TEST_CASE("leading and trailing dots and spaces are trimmed")
    {
        // Not cosmetic: Win32 silently drops a trailing dot or space, so "report." and "report"
        // address the SAME file - and a stem of "." or ".." names a directory, not a file.
        CHECK(strutil::sanitizeFileStem("  name  ") == "name");
        CHECK(strutil::sanitizeFileStem("name...") == "name");
        CHECK(strutil::sanitizeFileStem("...name") == "name");
        CHECK(strutil::sanitizeFileStem(".") == "");
        CHECK(strutil::sanitizeFileStem("..") == "");
        CHECK(strutil::sanitizeFileStem("   ") == "");
    }

    TEST_CASE("a name made entirely of illegal characters survives as underscores")
    {
        // '_' is not in the trim set, so this is deliberately NOT empty - the caller still gets a
        // usable, if ugly, stem rather than falling back.
        CHECK(strutil::sanitizeFileStem("///") == "___");
    }

    TEST_CASE("Win32 reserved device names are recognised")
    {
        CHECK(strutil::isReservedDeviceName("CON"));
        CHECK(strutil::isReservedDeviceName("PRN"));
        CHECK(strutil::isReservedDeviceName("AUX"));
        CHECK(strutil::isReservedDeviceName("NUL"));
        CHECK(strutil::isReservedDeviceName("COM1"));
        CHECK(strutil::isReservedDeviceName("LPT9"));

        // Case-insensitive, and matched against the text BEFORE the first period - which is how
        // the Win32 layer matches them, so "CON.txt" is the console device, not a file.
        CHECK(strutil::isReservedDeviceName("con"));
        CHECK(strutil::isReservedDeviceName("Com3"));
        CHECK(strutil::isReservedDeviceName("NUL.txt"));

        CHECK_FALSE(strutil::isReservedDeviceName("CONS"));
        CHECK_FALSE(strutil::isReservedDeviceName("COM0"));
        CHECK_FALSE(strutil::isReservedDeviceName("COM10"));
        CHECK_FALSE(strutil::isReservedDeviceName("LPT"));
        CHECK_FALSE(strutil::isReservedDeviceName(""));
        CHECK_FALSE(strutil::isReservedDeviceName("fog"));
    }

    TEST_CASE("toSafeFileStem always yields something creatable")
    {
        CHECK(strutil::toSafeFileStem("fog") == "fog");
        CHECK(strutil::toSafeFileStem("..", "layer") == "layer");
        CHECK(strutil::toSafeFileStem("", "layer") == "layer");
        CHECK(strutil::toSafeFileStem("a/b", "layer") == "a_b");

        // A reserved name is escaped rather than replaced, so the user can still recognise it.
        CHECK(strutil::toSafeFileStem("CON", "layer") == "CON_");
        CHECK(strutil::toSafeFileStem("com4", "layer") == "com4_");

        // The default fallback is used when the caller supplies none.
        CHECK(strutil::toSafeFileStem("...") == "unnamed");
    }
}
