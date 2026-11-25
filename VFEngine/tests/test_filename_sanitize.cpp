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
        CHECK_FALSE(strutil::isReservedDeviceName("COM10"));
        CHECK_FALSE(strutil::isReservedDeviceName("LPT"));
        CHECK_FALSE(strutil::isReservedDeviceName(""));
        CHECK_FALSE(strutil::isReservedDeviceName("fog"));
    }

    TEST_CASE("the reserved set matches the launcher's twin (ProjectNameRules.java)")
    {
        // VK-1726: COM0/LPT0, the superscript digits, CONIN$/CONOUT$, and trailing spaces before the
        // period, which Win32 drops before it compares ("nul .tar.gz" is the NUL device).
        CHECK(strutil::isReservedDeviceName("COM0"));
        CHECK(strutil::isReservedDeviceName("lpt0"));
        CHECK(strutil::isReservedDeviceName("COM\xC2\xB9"));  // COM¹
        CHECK(strutil::isReservedDeviceName("lpt\xC2\xB3"));  // lpt³
        CHECK(strutil::isReservedDeviceName("COM\xC2\xB2.log"));
        CHECK(strutil::isReservedDeviceName("CONIN$"));
        CHECK(strutil::isReservedDeviceName("conout$"));
        CHECK(strutil::isReservedDeviceName("NUL .txt"));
        CHECK(strutil::isReservedDeviceName("aux  "));

        CHECK_FALSE(strutil::isReservedDeviceName("COM\xC2\xB4"));  // COM´ (an acute accent, not a digit)
        CHECK_FALSE(strutil::isReservedDeviceName("CONIN"));
        CHECK_FALSE(strutil::isReservedDeviceName("CONOUT$$"));
        CHECK_FALSE(strutil::isReservedDeviceName(" NUL"));  // only TRAILING spaces are dropped
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

        // The '_' goes right after the device name, the only place where it changes what Win32
        // matches: "NUL.txt_" would still be the NUL device.
        CHECK(strutil::toSafeFileStem("NUL.txt", "layer") == "NUL_.txt");
        CHECK(strutil::toSafeFileStem("nul .tar.gz", "layer") == "nul_ .tar.gz");
        CHECK(strutil::toSafeFileStem("COM\xC2\xB9", "layer") == "COM\xC2\xB9_");
        CHECK_FALSE(strutil::isReservedDeviceName(strutil::toSafeFileStem("CONOUT$.x")));
    }

    TEST_CASE("toSafeFolderName fits a UTF-16 budget and cuts on a code point boundary")
    {
        // VK-1726: the shipped game's %LOCALAPPDATA%\<Publisher>\<Game> folder names.
        CHECK(strutil::toSafeFolderName("Acme Games", 64) == "Acme Games");
        CHECK(strutil::toSafeFolderName("a:b", 64) == "a_b");
        CHECK(strutil::toSafeFolderName("CON", 64) == "CON_");
        CHECK(strutil::toSafeFolderName("...", 64, "Game") == "Game");
        CHECK(strutil::toSafeFolderName("", 64, "") == "");

        // ASCII: one unit per byte.
        const std::string hundred(100, 'x');
        CHECK(strutil::toSafeFolderName(hundred, 64) == std::string(64, 'x'));

        // Hebrew letters are two UTF-8 bytes and one UTF-16 unit each.
        const std::string shin = "\xD7\xA9";
        std::string hebrew;
        for (int i = 0; i < 70; ++i)
            hebrew += shin;
        std::string hebrewCut;
        for (int i = 0; i < 64; ++i)
            hebrewCut += shin;
        CHECK(strutil::toSafeFolderName(hebrew, 64) == hebrewCut);

        // An emoji (four UTF-8 bytes, a surrogate pair) that would straddle the limit is left out
        // whole, never split: 63 units of 'x' + the emoji would be 65.
        const std::string emoji = "\xF0\x9F\x8E\xAE";  // U+1F3AE
        CHECK(strutil::toSafeFolderName(std::string(63, 'x') + emoji, 64) == std::string(63, 'x'));
        CHECK(strutil::toSafeFolderName(std::string(62, 'x') + emoji, 64) == std::string(62, 'x') + emoji);

        // A cut that ends in spaces or dots is trimmed again (Win32 would drop them).
        CHECK(strutil::toSafeFolderName(std::string(10, 'x') + "   " + std::string(60, 'y'), 12) == std::string(10, 'x'));
        CHECK(strutil::toSafeFolderName(std::string(10, 'x') + "..yyyy", 12) == std::string(10, 'x'));

        // A budget too small to escape a device name gives the fallback.
        CHECK(strutil::toSafeFolderName("CON", 3, "Game") == "Game");
    }
}
