#include <doctest.h>

#include "export/ExeIconEmbedder.hpp"
#include "export/ExeVersionInfo.hpp"
#include "export/GameExporter.hpp"
#include "export/PeAuthenticode.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

#pragma comment(lib, "version.lib")

// ============================================================
// VK-1696: a release package ships a signed Runtime.exe. GameExporter copies it under the game's
// name and edits its resources, so the copy loses the engine's Authenticode signature first
// (PeAuthenticode), and the resource writers refuse an exe that still carries one.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using gameExport::PeAuthenticode;
    using gameExport::PeSecurityDirectory;

    void putU16(std::vector<uint8_t>& bytes, size_t at, uint16_t value)
    {
        bytes[at] = static_cast<uint8_t>(value & 0xFF);
        bytes[at + 1] = static_cast<uint8_t>(value >> 8);
    }

    void putU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
    {
        putU16(bytes, at, static_cast<uint16_t>(value & 0xFFFF));
        putU16(bytes, at + 2, static_cast<uint16_t>(value >> 16));
    }

    uint32_t u32At(const std::vector<uint8_t>& bytes, size_t at)
    {
        return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    // The optional header's CheckSum (offset 64 in PE32 and PE32+), which signtool sets on the files it signs.
    size_t checksumOf(const std::vector<uint8_t>& image)
    {
        return u32At(image, 0x3C) + 4 + 20 + 64;
    }

    // Where data directory 4 of a well-formed image lies, found without PeAuthenticode.
    size_t securityEntryOf(const std::vector<uint8_t>& image)
    {
        const size_t optional = u32At(image, 0x3C) + 4 + 20;
        const uint16_t magic = static_cast<uint16_t>(image[optional] | (image[optional + 1] << 8));
        REQUIRE((magic == 0x10B || magic == 0x20B));
        return optional + (magic == 0x20B ? 112 : 96) + 4 * 8;
    }

    // A minimal PE image only the parser reads: a DOS header (e_lfanew 0x40), "PE\0\0", a file
    // header with one section, an optional header with `directories` data directories, and the
    // section's 0x200 bytes of raw data at 0x200.
    std::vector<uint8_t> makePe(bool plus, uint32_t directories = 16)
    {
        std::vector<uint8_t> image(0x400, 0);
        image[0] = 'M';
        image[1] = 'Z';
        putU32(image, 0x3C, 0x40);
        image[0x40] = 'P';
        image[0x41] = 'E';
        const size_t fileHeader = 0x44;
        const size_t optionalSize = (plus ? 112 : 96) + size_t{directories} * 8;
        putU16(image, fileHeader, plus ? 0x8664 : 0x014C);
        putU16(image, fileHeader + 2, 1);
        putU16(image, fileHeader + 16, static_cast<uint16_t>(optionalSize));
        const size_t optional = fileHeader + 20;
        putU16(image, optional, plus ? 0x20B : 0x10B);
        putU32(image, optional + (plus ? 108 : 92), directories);
        const size_t section = optional + optionalSize;
        REQUIRE(section + 40 <= 0x200);
        putU32(image, section + 16, 0x200); // SizeOfRawData
        putU32(image, section + 20, 0x200); // PointerToRawData
        return image;
    }

    // Appends a WIN_CERTIFICATE (revision 2.0, PKCS#7) the way signtool does: pad the file to
    // 8 bytes, append the table (its length a multiple of 8), point data directory 4 at it.
    PeSecurityDirectory appendCertificate(std::vector<uint8_t>& image)
    {
        while (image.size() % 8 != 0)
        {
            image.push_back(0);
        }
        const size_t offset = image.size();
        const uint32_t length = 8 + 13;
        image.resize(offset + 8, 0);
        putU32(image, offset, length);
        putU16(image, offset + 4, 0x0200);
        putU16(image, offset + 6, 0x0002);
        image.insert(image.end(), 13, uint8_t{0xAB});
        while (image.size() % 8 != 0)
        {
            image.push_back(0);
        }
        const PeSecurityDirectory directory{static_cast<uint32_t>(offset), static_cast<uint32_t>(image.size() - offset)};
        const size_t entry = securityEntryOf(image);
        putU32(image, entry, directory.offset);
        putU32(image, entry + 4, directory.size);
        return directory;
    }

    PeSecurityDirectory directoryOf(const std::vector<uint8_t>& image)
    {
        PeSecurityDirectory directory;
        std::string error;
        REQUIRE_MESSAGE(PeAuthenticode::readSecurityDirectory(image, directory, error), error);
        return directory;
    }

    std::vector<uint8_t> readBytes(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        REQUIRE(in.good());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    void writeBytes(const fs::path& file, const std::vector<uint8_t>& bytes)
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }

    // "פרויקט" (project).
    const std::string kHebrewUtf8 = "\xD7\xA4\xD7\xA8\xD7\x95\xD7\x99\xD7\xA7\xD7\x98";
    const std::wstring kHebrewWide = L"פרויקט";

    struct TempDir
    {
        fs::path path;

        explicit TempDir(std::wstring_view name)
        {
            path = fs::temp_directory_path() / (L"VertexForge_PeAuthenticode_" + std::wstring(name) + L"_" + kHebrewWide);
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    constexpr unsigned char kTinyIcon[] = {0, 0, 1, 0, 1, 0,
                                           1, 1, 0, 0, 1, 0, 32, 0, 8, 0, 0, 0, 22, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0, 0};

    std::optional<fs::path> builtRuntime()
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found");
        const fs::path exe = *root / "bin" / "Runtime" / repo_scan::thisConfiguration() / "x64" / "Runtime.exe";
        if (!fs::exists(exe))
        {
            MESSAGE("skipped: " << exe.string() << " not found (build Runtime in " << repo_scan::thisConfiguration() << ")");
            return std::nullopt;
        }
        return exe;
    }

    std::optional<std::wstring> versionString(const fs::path& exe, const wchar_t* key)
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, exe.c_str(), &ignored);
        if (size == 0)
        {
            return std::nullopt;
        }
        std::vector<uint8_t> block(size);
        if (!GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, exe.c_str(), 0, size, block.data()))
        {
            return std::nullopt;
        }
        void* value = nullptr;
        UINT length = 0;
        const std::wstring query = std::wstring(L"\\StringFileInfo\\040904B0\\") + key;
        if (!VerQueryValueW(block.data(), query.c_str(), &value, &length) || value == nullptr)
        {
            return std::nullopt;
        }
        return std::wstring(static_cast<const wchar_t*>(value));
    }
}

TEST_SUITE("PeAuthenticode")
{
    TEST_CASE("readSecurityDirectory parses PE32 and PE32+ headers")
    {
        for (const bool plus : {false, true})
        {
            CAPTURE(plus);
            std::vector<uint8_t> image = makePe(plus);
            CHECK_FALSE(directoryOf(image).present());

            const PeSecurityDirectory appended = appendCertificate(image);
            const PeSecurityDirectory read = directoryOf(image);
            CHECK(read.offset == appended.offset);
            CHECK(read.size == appended.size);
            CHECK(read.offset == 0x400);
            CHECK(read.size == 24);
        }

        SUBCASE("fewer than 5 data directories: no security entry")
        {
            const std::vector<uint8_t> image = makePe(false, 4);
            CHECK_FALSE(directoryOf(image).present());
        }
    }

    TEST_CASE("readSecurityDirectory refuses what is not a PE image")
    {
        PeSecurityDirectory directory;
        std::string error;
        std::vector<uint8_t> image = makePe(false);

        SUBCASE("no MZ")
        {
            image[0] = 'X';
        }
        SUBCASE("truncated DOS header")
        {
            image.resize(0x30);
        }
        SUBCASE("e_lfanew outside the file")
        {
            putU32(image, 0x3C, 0x10000);
        }
        SUBCASE("no PE signature")
        {
            image[0x41] = 'X';
        }
        SUBCASE("unknown optional-header magic")
        {
            putU16(image, 0x58, 0x0107);
        }
        SUBCASE("optional header longer than the file")
        {
            putU16(image, 0x44 + 16, 0xFFFF);
        }
        SUBCASE("cut inside the headers")
        {
            image.resize(0x80);
        }
        CHECK_FALSE(PeAuthenticode::readSecurityDirectory(image, directory, error));
        CHECK_FALSE(error.empty());
    }

    TEST_CASE("stripSignature removes a certificate table at the end of the file")
    {
        for (const bool plus : {false, true})
        {
            CAPTURE(plus);
            const std::vector<uint8_t> plain = makePe(plus);
            std::vector<uint8_t> image = plain;
            appendCertificate(image);
            putU32(image, checksumOf(image), 0x01092D39); // as signtool leaves it

            bool removed = false;
            std::string error;
            REQUIRE_MESSAGE(PeAuthenticode::stripSignature(image, removed, error), error);
            CHECK(removed);
            CHECK(u32At(image, checksumOf(image)) == 0u);
            CHECK(image == plain); // 0x400 is already 8-aligned: no padding stays
            CHECK_FALSE(directoryOf(image).present());

            // Nothing left to remove.
            REQUIRE(PeAuthenticode::stripSignature(image, removed, error));
            CHECK_FALSE(removed);
            CHECK(image == plain);
        }

        SUBCASE("the alignment padding before the table stays")
        {
            std::vector<uint8_t> image = makePe(false);
            image.push_back(0x11); // 0x401 bytes: signtool pads to 0x408
            std::vector<uint8_t> expected = image;
            appendCertificate(image);
            bool removed = false;
            std::string error;
            REQUIRE(PeAuthenticode::stripSignature(image, removed, error));
            expected.resize(0x408, 0);
            CHECK(image == expected);
        }

        SUBCASE("a directory with a size of 0 is zeroed")
        {
            std::vector<uint8_t> image = makePe(false);
            putU32(image, securityEntryOf(image), 0x400);
            putU32(image, checksumOf(image), 0x1234); // no table was removed: the checksum stays
            const size_t size = image.size();
            bool removed = false;
            std::string error;
            REQUIRE(PeAuthenticode::stripSignature(image, removed, error));
            CHECK(removed);
            CHECK(image.size() == size);
            CHECK(u32At(image, checksumOf(image)) == 0x1234u);
            CHECK_FALSE(directoryOf(image).present());
        }
    }

    TEST_CASE("stripSignature refuses a table it cannot cut off safely")
    {
        std::vector<uint8_t> image = makePe(false);
        const PeSecurityDirectory appended = appendCertificate(image);
        const size_t entry = securityEntryOf(image);

        SUBCASE("past the end of the file")
        {
            putU32(image, entry + 4, appended.size + 8);
        }
        SUBCASE("not at the end of the file")
        {
            image.insert(image.end(), 8, uint8_t{0});
        }
        SUBCASE("not 8-byte aligned")
        {
            putU32(image, entry, appended.offset + 4);
            putU32(image, entry + 4, appended.size - 4);
        }
        SUBCASE("inside a section's raw data")
        {
            putU32(image, entry, 0x200);
            putU32(image, entry + 4, static_cast<uint32_t>(image.size() - 0x200));
        }
        SUBCASE("inside the headers")
        {
            putU32(image, entry, 0x100);
            putU32(image, entry + 4, static_cast<uint32_t>(image.size() - 0x100));
        }

        const std::vector<uint8_t> before = image;
        bool removed = true;
        std::string error;
        CHECK_FALSE(PeAuthenticode::stripSignature(image, removed, error));
        CHECK_FALSE(removed);
        CHECK_FALSE(error.empty());
        CHECK(image == before);
    }

    TEST_CASE("a signed copy of Runtime.exe: the resource writers refuse it, the strip restores it")
    {
        const std::optional<fs::path> runtime = builtRuntime();
        if (!runtime)
        {
            return;
        }
        TempDir dir(L"Strip");
        const std::vector<uint8_t> original = readBytes(*runtime);
        REQUIRE_FALSE(directoryOf(original).present());

        std::vector<uint8_t> signedImage = original;
        appendCertificate(signedImage);
        const fs::path game = dir.path / (kHebrewWide + L".exe");
        writeBytes(game, signedImage);

        PeSecurityDirectory directory;
        std::string error;
        REQUIRE_MESSAGE(PeAuthenticode::readSecurityDirectoryFile(game, directory, error), error);
        CHECK(directory.present());

        const fs::path icon = dir.path / L"icon.ico";
        {
            std::ofstream out(icon, std::ios::binary);
            out.write(reinterpret_cast<const char*>(kTinyIcon), sizeof(kTinyIcon));
        }
        CHECK_FALSE(gameExport::ExeIconEmbedder::embedIcon(game, icon, error));
        CHECK(error.find("signature") != std::string::npos);

        gameExport::ExportConfig config;
        config.gameName = "Game";
        config.gameVersion = "1.0";
        std::vector<std::string> warnings;
        std::vector<uint8_t> resource;
        REQUIRE(gameExport::ExeVersionInfo::buildResource(gameExport::ExeVersionInfo::gameFields(config, warnings),
                                                          resource, error));
        error.clear();
        CHECK_FALSE(gameExport::ExeVersionInfo::writeResource(game, resource, error));
        CHECK(error.find("signature") != std::string::npos);
        CHECK(readBytes(game) == signedImage); // neither writer touched it

        bool removed = false;
        REQUIRE_MESSAGE(PeAuthenticode::stripSignatureFile(game, removed, error), error);
        CHECK(removed);
        std::vector<uint8_t> expected = original;
        while (expected.size() % 8 != 0)
        {
            expected.push_back(0);
        }
        putU32(expected, checksumOf(expected), 0); // the strip zeroes the checksum ("none", as the linker writes)
        CHECK(readBytes(game) == expected);
        REQUIRE(PeAuthenticode::readSecurityDirectoryFile(game, directory, error));
        CHECK_FALSE(directory.present());
    }

    TEST_CASE("prepareGameExecutable turns a signed Runtime.exe copy into the game's exe")
    {
        const std::optional<fs::path> runtime = builtRuntime();
        if (!runtime)
        {
            return;
        }
        TempDir dir(L"Prepare");
        std::vector<uint8_t> signedImage = readBytes(*runtime);
        appendCertificate(signedImage);
        const fs::path game = dir.path / (kHebrewWide + L".exe");
        writeBytes(game, signedImage);

        gameExport::ExportConfig config;
        config.gameName = kHebrewUtf8;
        config.gameVersion = "2.5.1-beta";
        config.publisher = "Studio";
        gameExport::ExportResult result;
        REQUIRE_MESSAGE(gameExport::GameExporter::prepareGameExecutable(game, config, result), result.errorMessage);
        CHECK(result.warnings.empty());

        PeSecurityDirectory directory;
        std::string error;
        REQUIRE(PeAuthenticode::readSecurityDirectoryFile(game, directory, error));
        CHECK_FALSE(directory.present());
        CHECK(versionString(game, L"FileDescription") == kHebrewWide);
        CHECK(versionString(game, L"OriginalFilename") == kHebrewWide + L".exe");
        CHECK(versionString(game, L"CompanyName") == std::wstring(L"Studio"));
        CHECK(versionString(game, L"ProductVersion") == std::wstring(L"2.5.1-beta"));

        // The exporter's icon step runs on the prepared exe.
        const fs::path icon = dir.path / L"icon.ico";
        {
            std::ofstream out(icon, std::ios::binary);
            out.write(reinterpret_cast<const char*>(kTinyIcon), sizeof(kTinyIcon));
        }
        REQUIRE_MESSAGE(gameExport::ExeIconEmbedder::embedIcon(game, icon, error), error);
        CHECK(versionString(game, L"FileDescription") == kHebrewWide);

        SUBCASE("an unnumbered project version is only a warning")
        {
            config.gameVersion = "first release";
            gameExport::ExportResult again;
            REQUIRE_MESSAGE(gameExport::GameExporter::prepareGameExecutable(game, config, again), again.errorMessage);
            CHECK(again.warnings.size() == 1);
            CHECK(versionString(game, L"ProductVersion") == std::wstring(L"first release"));
        }
    }
}
