#include <doctest.h>

#include "config/EngineVersion.generated.hpp"
#include "export/ExeIconEmbedder.hpp"
#include "export/ExeVersionInfo.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

#pragma comment(lib, "version.lib")

// ============================================================
// VK-1696: version resources. Editor.exe, Runtime.exe and Tests.exe get theirs from
// VFEngine/utilities/config/VersionInfo.rc2 (numbers from VERSION); GameExport rewrites an exported
// game's copy of Runtime.exe with the game's own (ExeVersionInfo).
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using gameExport::ExeVersionFields;
    using gameExport::ExeVersionInfo;

    // "פרויקט" (project): an exported game may have any name.
    const std::string kHebrewUtf8 = "\xD7\xA4\xD7\xA8\xD7\x95\xD7\x99\xD7\xA7\xD7\x98";
    const std::wstring kHebrewWide = L"פרויקט";

    struct TempDir
    {
        fs::path path;

        explicit TempDir(std::wstring_view name)
        {
            path = fs::temp_directory_path() / (L"VertexForge_VersionInfo_" + std::wstring(name) + L"_" + kHebrewWide);
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

    std::wstring widen(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return {};
        }
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
        return wide;
    }

    // What GetFileVersionInfoExW reports about an exe.
    struct VersionRead
    {
        VS_FIXEDFILEINFO fixed{};
        std::vector<std::pair<WORD, WORD>> translations;
        std::vector<uint8_t> block;

        std::optional<std::wstring> string(const wchar_t* key) const
        {
            const std::wstring query = std::wstring(L"\\StringFileInfo\\040904B0\\") + key;
            void* value = nullptr;
            UINT length = 0;
            if (!VerQueryValueW(block.data(), query.c_str(), &value, &length) || value == nullptr)
            {
                return std::nullopt;
            }
            return std::wstring(static_cast<const wchar_t*>(value));
        }
    };

    std::optional<VersionRead> readVersion(const fs::path& exe)
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeExW(FILE_VER_GET_NEUTRAL, exe.c_str(), &ignored);
        if (size == 0)
        {
            return std::nullopt;
        }
        VersionRead read;
        read.block.resize(size);
        if (!GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL, exe.c_str(), 0, size, read.block.data()))
        {
            return std::nullopt;
        }
        void* fixed = nullptr;
        UINT fixedLength = 0;
        if (!VerQueryValueW(read.block.data(), L"\\", &fixed, &fixedLength) || fixedLength < sizeof(VS_FIXEDFILEINFO))
        {
            return std::nullopt;
        }
        read.fixed = *static_cast<const VS_FIXEDFILEINFO*>(fixed);
        void* translations = nullptr;
        UINT translationsLength = 0;
        if (VerQueryValueW(read.block.data(), L"\\VarFileInfo\\Translation", &translations, &translationsLength))
        {
            const auto* words = static_cast<const WORD*>(translations);
            for (UINT i = 0; i + 1 < translationsLength / sizeof(WORD); i += 2)
            {
                read.translations.emplace_back(words[i], words[i + 1]);
            }
        }
        return read;
    }

    // Every RT_VERSION resource of an exe as (integer id, language); a string name counts as id 0.
    std::vector<std::pair<WORD, WORD>> versionResources(const fs::path& exe)
    {
        std::vector<std::pair<WORD, WORD>> found;
        HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        REQUIRE(module != nullptr);
        EnumResourceNamesExW(
            module, MAKEINTRESOURCEW(16),
            [](HMODULE m, LPCWSTR type, LPWSTR name, LONG_PTR param) -> BOOL {
                EnumResourceLanguagesExW(
                    m, type, name,
                    [](HMODULE, LPCWSTR, LPCWSTR n, WORD language, LONG_PTR p) -> BOOL {
                        const WORD id = IS_INTRESOURCE(n) ? static_cast<WORD>(reinterpret_cast<ULONG_PTR>(n)) : WORD{0};
                        reinterpret_cast<std::vector<std::pair<WORD, WORD>>*>(p)->emplace_back(id, language);
                        return TRUE;
                    },
                    param, RESOURCE_ENUM_LN, 0);
                return TRUE;
            },
            reinterpret_cast<LONG_PTR>(&found), RESOURCE_ENUM_LN, 0);
        FreeLibrary(module);
        return found;
    }

    // The raw bytes of RT_VERSION #1 in a language, or nullopt.
    std::optional<std::vector<uint8_t>> versionResourceBytes(const fs::path& exe, WORD language)
    {
        HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == nullptr)
        {
            return std::nullopt;
        }
        std::optional<std::vector<uint8_t>> bytes;
        if (HRSRC resource = FindResourceExW(module, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), language))
        {
            HGLOBAL loaded = LoadResource(module, resource);
            const DWORD size = SizeofResource(module, resource);
            const auto* data = loaded != nullptr ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
            if (data != nullptr)
            {
                bytes.emplace(data, data + size);
            }
        }
        FreeLibrary(module);
        return bytes;
    }

    bool hasManifest(const fs::path& exe)
    {
        HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == nullptr)
        {
            return false;
        }
        const bool found = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24)) != nullptr;
        FreeLibrary(module);
        return found;
    }

    bool addRawVersionResource(const fs::path& exe, WORD language, const std::vector<uint8_t>& bytes)
    {
        HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
        if (update == nullptr)
        {
            return false;
        }
        if (!UpdateResourceW(update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(1), language,
                             bytes.empty() ? nullptr : const_cast<uint8_t*>(bytes.data()),
                             static_cast<DWORD>(bytes.size())))
        {
            EndUpdateResourceW(update, TRUE);
            return false;
        }
        return EndUpdateResourceW(update, FALSE) != FALSE;
    }

    // The fields VersionInfo.rc2 writes for an engine executable.
    ExeVersionFields engineFields(const std::string& app)
    {
        ExeVersionFields fields;
        fields.fileVersion = {VF_ENGINE_VERSION_MAJOR, VF_ENGINE_VERSION_MINOR, VF_ENGINE_VERSION_PATCH, 0};
        fields.productVersion = fields.fileVersion;
        fields.strings = {
            {"CompanyName", "VertexForge"},
            {"FileDescription", "VertexForge " + app},
            {"FileVersion", VF_ENGINE_VERSION_STRING},
            {"InternalName", app},
            {"OriginalFilename", app + ".exe"},
            {"ProductName", "VertexForge"},
            {"ProductVersion", VF_ENGINE_VERSION_STRING},
        };
        return fields;
    }

    // ---- a walker over VS_VERSIONINFO bytes, independent of the builder ----

    struct WalkedNode
    {
        size_t start = 0;
        uint16_t length = 0;
        uint16_t valueLength = 0;
        uint16_t type = 0;
        std::u16string key;
        std::vector<uint8_t> value;
        std::vector<WalkedNode> children;
    };

    uint16_t u16At(const std::vector<uint8_t>& bytes, size_t at)
    {
        return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
    }

    size_t align4(size_t offset)
    {
        return (offset + 3) & ~size_t{3};
    }

    // Parses the node at `start`; every rule of the format is a CHECK.
    WalkedNode walk(const std::vector<uint8_t>& bytes, size_t start)
    {
        WalkedNode node;
        node.start = start;
        CHECK(start % 4 == 0);
        REQUIRE(start + 6 <= bytes.size());
        node.length = u16At(bytes, start);
        node.valueLength = u16At(bytes, start + 2);
        node.type = u16At(bytes, start + 4);
        const size_t end = start + node.length;
        REQUIRE(end <= bytes.size());
        size_t pos = start + 6;
        while (true)
        {
            REQUIRE(pos + 2 <= end);
            const uint16_t c = u16At(bytes, pos);
            pos += 2;
            if (c == 0)
            {
                break;
            }
            node.key.push_back(static_cast<char16_t>(c));
        }
        pos = align4(pos);
        const size_t valueBytes = node.type == 1 ? size_t{node.valueLength} * 2 : size_t{node.valueLength};
        REQUIRE(pos + valueBytes <= end);
        node.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                          bytes.begin() + static_cast<std::ptrdiff_t>(pos + valueBytes));
        pos += valueBytes;
        while (align4(pos) < end)
        {
            WalkedNode child = walk(bytes, align4(pos));
            pos = child.start + child.length;
            node.children.push_back(std::move(child));
        }
        // wLength leaves out the padding after the last child: the last child ends exactly at `end`.
        CHECK(pos == end);
        return node;
    }

    std::u16string u16(const char* ascii)
    {
        std::u16string out;
        for (; *ascii != '\0'; ++ascii)
        {
            out.push_back(static_cast<char16_t>(*ascii));
        }
        return out;
    }

    // A text value (wType 1) as UTF-16 without its NUL.
    std::u16string textValue(const WalkedNode& node)
    {
        std::u16string text;
        for (size_t i = 0; i + 1 < node.value.size(); i += 2)
        {
            text.push_back(static_cast<char16_t>(node.value[i] | (node.value[i + 1] << 8)));
        }
        REQUIRE_FALSE(text.empty());
        CHECK(text.back() == u'\0');
        text.pop_back();
        return text;
    }

    // A one-image .ico, as in test_unicode_paths.cpp: ExeIconEmbedder only copies the bytes.
    constexpr unsigned char kTinyIcon[] = {0, 0, 1, 0, 1, 0,
                                           1, 1, 0, 0, 1, 0, 32, 0, 8, 0, 0, 0, 22, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0, 0};

    fs::path builtExe(const char* app)
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "repo root not found");
        return *root / "bin" / app / repo_scan::thisConfiguration() / "x64" / (std::string(app) + ".exe");
    }
}

TEST_SUITE("ExeVersionInfo")
{
    TEST_CASE("the built Editor, Runtime and Tests exes carry the engine version resource")
    {
        const std::wstring version = widen(VF_ENGINE_VERSION_STRING);
        for (const char* app : {"Editor", "Runtime", "Tests"})
        {
            CAPTURE(app);
            const fs::path exe = builtExe(app);
            if (!fs::exists(exe))
            {
                MESSAGE("skipped: " << exe.string() << " not found (build " << app << " in "
                                    << repo_scan::thisConfiguration() << ")");
                continue;
            }
            const std::optional<VersionRead> read = readVersion(exe);
            REQUIRE_MESSAGE(read.has_value(), "no version resource in " << exe.string() << " (rebuild " << app << ")");

            const VS_FIXEDFILEINFO& fixed = read->fixed;
            CHECK(fixed.dwSignature == 0xFEEF04BDu);
            CHECK(HIWORD(fixed.dwFileVersionMS) == VF_ENGINE_VERSION_MAJOR);
            CHECK(LOWORD(fixed.dwFileVersionMS) == VF_ENGINE_VERSION_MINOR);
            CHECK(HIWORD(fixed.dwFileVersionLS) == VF_ENGINE_VERSION_PATCH);
            CHECK(LOWORD(fixed.dwFileVersionLS) == 0);
            CHECK(fixed.dwProductVersionMS == fixed.dwFileVersionMS);
            CHECK(fixed.dwProductVersionLS == fixed.dwFileVersionLS);
            CHECK(fixed.dwFileOS == static_cast<DWORD>(VOS_NT_WINDOWS32));
            CHECK(fixed.dwFileType == static_cast<DWORD>(VFT_APP));
#if defined(DEBUG)
            CHECK((fixed.dwFileFlags & VS_FF_DEBUG) != 0);
#else
            CHECK((fixed.dwFileFlags & VS_FF_DEBUG) == 0);
#endif

            CHECK(read->string(L"CompanyName") == std::wstring(L"VertexForge"));
            CHECK(read->string(L"ProductName") == std::wstring(L"VertexForge"));
            CHECK(read->string(L"FileVersion") == version);
            CHECK(read->string(L"ProductVersion") == version);
            CHECK(read->string(L"FileDescription") == L"VertexForge " + widen(app));
            CHECK(read->string(L"InternalName") == widen(app));
            CHECK(read->string(L"OriginalFilename") == widen(app) + L".exe");

            REQUIRE(read->translations.size() == 1);
            CHECK(read->translations[0].first == ExeVersionInfo::language);
            CHECK(read->translations[0].second == ExeVersionInfo::codePage);

            const auto resources = versionResources(exe);
            REQUIRE(resources.size() == 1);
            CHECK(resources[0].first == ExeVersionInfo::resourceId);
            CHECK(resources[0].second == ExeVersionInfo::language);
        }
    }

    TEST_CASE("parseFixedVersion reads the leading MAJOR[.MINOR[.PATCH[.BUILD]]] numbers")
    {
        using Parts = std::array<uint16_t, 4>;
        CHECK(ExeVersionInfo::parseFixedVersion("1.2.3") == Parts{1, 2, 3, 0});
        CHECK(ExeVersionInfo::parseFixedVersion("2") == Parts{2, 0, 0, 0});
        CHECK(ExeVersionInfo::parseFixedVersion("1.2-beta") == Parts{1, 2, 0, 0});
        CHECK(ExeVersionInfo::parseFixedVersion("1.2.3.4") == Parts{1, 2, 3, 4});
        CHECK(ExeVersionInfo::parseFixedVersion("1.2.3.4.5") == Parts{1, 2, 3, 4});
        CHECK(ExeVersionInfo::parseFixedVersion("1.") == Parts{1, 0, 0, 0});
        CHECK(ExeVersionInfo::parseFixedVersion("0.9 preview") == Parts{0, 9, 0, 0});
        CHECK(ExeVersionInfo::parseFixedVersion("65535.65535.65535.65535") == Parts{65535, 65535, 65535, 65535});

        CHECK_FALSE(ExeVersionInfo::parseFixedVersion("").has_value());
        CHECK_FALSE(ExeVersionInfo::parseFixedVersion("v1.0").has_value());
        CHECK_FALSE(ExeVersionInfo::parseFixedVersion(" 1.0").has_value());
        CHECK_FALSE(ExeVersionInfo::parseFixedVersion("70000").has_value());
        CHECK_FALSE(ExeVersionInfo::parseFixedVersion("1.65536").has_value());
        CHECK_FALSE(ExeVersionInfo::parseFixedVersion("99999999999999999999").has_value());
    }

    TEST_CASE("gameFields describes the exported game")
    {
        gameExport::ExportConfig config;
        config.gameName = kHebrewUtf8;
        config.gameVersion = "2.5.1-beta";
        config.publisher = "Studio";
        std::vector<std::string> warnings;
        const ExeVersionFields fields = ExeVersionInfo::gameFields(config, warnings);
        CHECK(warnings.empty());
        CHECK(fields.fileVersion == std::array<uint16_t, 4>{2, 5, 1, 0});
        CHECK(fields.productVersion == fields.fileVersion);
        const std::vector<std::pair<std::string, std::string>> expected = {
            {"CompanyName", "Studio"},
            {"FileDescription", kHebrewUtf8},
            {"FileVersion", "2.5.1-beta"},
            {"InternalName", kHebrewUtf8},
            {"OriginalFilename", kHebrewUtf8 + ".exe"},
            {"ProductName", kHebrewUtf8},
            {"ProductVersion", "2.5.1-beta"},
        };
        CHECK(fields.strings == expected);

        config.gameVersion = "v3";
        warnings.clear();
        const ExeVersionFields unnumbered = ExeVersionInfo::gameFields(config, warnings);
        CHECK(warnings.size() == 1);
        CHECK(unnumbered.fileVersion == std::array<uint16_t, 4>{0, 0, 0, 0});
    }

    TEST_CASE("buildResource lays out a VS_VERSIONINFO")
    {
        ExeVersionFields fields;
        fields.fileVersion = {1, 2, 3, 4};
        fields.productVersion = {5, 6, 7, 8};
        fields.strings = {{"CompanyName", ""}, {"ProductName", kHebrewUtf8}, {"FileVersion", "1.2"}};
        std::vector<uint8_t> bytes;
        std::string error;
        REQUIRE_MESSAGE(ExeVersionInfo::buildResource(fields, bytes, error), error);

        const WalkedNode root = walk(bytes, 0);
        CHECK(root.length == bytes.size());
        CHECK(root.key == u"VS_VERSION_INFO");
        CHECK(root.type == 0);
        CHECK(root.valueLength == 52);
        REQUIRE(root.value.size() == 52);
        auto dword = [&](size_t index) {
            const size_t at = index * 4;
            return static_cast<uint32_t>(root.value[at]) | (static_cast<uint32_t>(root.value[at + 1]) << 8) |
                   (static_cast<uint32_t>(root.value[at + 2]) << 16) | (static_cast<uint32_t>(root.value[at + 3]) << 24);
        };
        CHECK(dword(0) == 0xFEEF04BDu);
        CHECK(dword(2) == 0x00010002u);
        CHECK(dword(3) == 0x00030004u);
        CHECK(dword(4) == 0x00050006u);
        CHECK(dword(5) == 0x00070008u);
        CHECK(dword(8) == static_cast<uint32_t>(VOS_NT_WINDOWS32));
        CHECK(dword(9) == static_cast<uint32_t>(VFT_APP));

        REQUIRE(root.children.size() == 2);
        const WalkedNode& stringFileInfo = root.children[0];
        CHECK(stringFileInfo.key == u"StringFileInfo");
        CHECK(stringFileInfo.type == 1);
        CHECK(stringFileInfo.valueLength == 0);
        REQUIRE(stringFileInfo.children.size() == 1);
        const WalkedNode& table = stringFileInfo.children[0];
        CHECK(table.key == u"040904B0");
        // The empty CompanyName is left out; the others keep their order.
        REQUIRE(table.children.size() == 2);
        CHECK(table.children[0].key == u"ProductName");
        CHECK(table.children[0].type == 1);
        CHECK(table.children[0].valueLength == kHebrewWide.size() + 1);
        CHECK(textValue(table.children[0]) == std::u16string(kHebrewWide.begin(), kHebrewWide.end()));
        CHECK(table.children[1].key == u"FileVersion");
        CHECK(textValue(table.children[1]) == u"1.2");

        const WalkedNode& varFileInfo = root.children[1];
        CHECK(varFileInfo.key == u"VarFileInfo");
        REQUIRE(varFileInfo.children.size() == 1);
        const WalkedNode& translation = varFileInfo.children[0];
        CHECK(translation.key == u"Translation");
        CHECK(translation.type == 0);
        CHECK(translation.valueLength == 4);
        CHECK(translation.value == std::vector<uint8_t>{0x09, 0x04, 0xB0, 0x04});

        fields.strings = {{"ProductName", "\xFF\xFE"}};
        CHECK_FALSE(ExeVersionInfo::buildResource(fields, bytes, error));
    }

    // The builder lays out the engine's fields byte for byte as rc.exe laid out VersionInfo.rc2 in the
    // built Runtime.exe (only dwFileFlags differs in Debug, where the .rc2 sets VS_FF_DEBUG).
    TEST_CASE("buildResource matches rc.exe's layout of the same fields")
    {
        const fs::path exe = builtExe("Runtime");
        if (!fs::exists(exe))
        {
            MESSAGE("skipped: " << exe.string() << " not found (build Runtime in " << repo_scan::thisConfiguration() << ")");
            return;
        }
        const auto compiled = versionResourceBytes(exe, ExeVersionInfo::language);
        REQUIRE_MESSAGE(compiled.has_value(), "no version resource in " << exe.string() << " (rebuild Runtime)");

        std::vector<uint8_t> built;
        std::string error;
        REQUIRE_MESSAGE(ExeVersionInfo::buildResource(engineFields("Runtime"), built, error), error);

        std::vector<uint8_t> expected = *compiled;
        // dwFileFlags: the 8th DWORD of VS_FIXEDFILEINFO, which starts at byte 40.
        constexpr size_t fileFlags = 40 + 7 * 4;
        REQUIRE(expected.size() > fileFlags + 4);
        for (size_t i = 0; i < 4; ++i)
        {
            expected[fileFlags + i] = 0;
        }
        CHECK(built == expected);
    }

    TEST_CASE("writeResource leaves exactly the game's version resource in a copy of Runtime.exe")
    {
        const fs::path runtime = builtExe("Runtime");
        if (!fs::exists(runtime))
        {
            MESSAGE("skipped: " << runtime.string() << " not found (build Runtime in " << repo_scan::thisConfiguration() << ")");
            return;
        }
        TempDir dir(L"Write");
        const fs::path game = dir.path / (kHebrewWide + L".exe");
        fs::copy_file(runtime, game);

        gameExport::ExportConfig config;
        config.gameName = kHebrewUtf8;
        config.gameVersion = "3.1";
        config.publisher = "Studio";
        std::vector<std::string> warnings;
        std::vector<uint8_t> resource;
        std::string error;
        REQUIRE_MESSAGE(ExeVersionInfo::buildResource(ExeVersionInfo::gameFields(config, warnings), resource, error), error);

        SUBCASE("a second language is removed")
        {
            // Hebrew (0x040D) beside the en-US one rc.exe wrote.
            REQUIRE(addRawVersionResource(game, 0x040D, resource));
            REQUIRE(versionResources(game).size() == 2);
        }
        SUBCASE("an exe without a version resource gets one")
        {
            for (const auto& [id, language] : versionResources(game))
            {
                REQUIRE(addRawVersionResource(game, language, {})); // empty data deletes it
            }
            REQUIRE(versionResources(game).empty());
        }

        REQUIRE_MESSAGE(ExeVersionInfo::writeResource(game, resource, error), error);
        const auto resources = versionResources(game);
        REQUIRE(resources.size() == 1);
        CHECK(resources[0] == std::pair<WORD, WORD>{ExeVersionInfo::resourceId, ExeVersionInfo::language});
        CHECK(hasManifest(game));

        auto checkGame = [&] {
            const std::optional<VersionRead> read = readVersion(game);
            REQUIRE(read.has_value());
            CHECK(HIWORD(read->fixed.dwFileVersionMS) == 3);
            CHECK(LOWORD(read->fixed.dwFileVersionMS) == 1);
            CHECK(read->fixed.dwFileVersionLS == 0);
            CHECK(read->string(L"FileDescription") == kHebrewWide);
            CHECK(read->string(L"ProductName") == kHebrewWide);
            CHECK(read->string(L"OriginalFilename") == kHebrewWide + L".exe");
            CHECK(read->string(L"CompanyName") == std::wstring(L"Studio"));
            CHECK(read->string(L"ProductVersion") == std::wstring(L"3.1"));
        };
        checkGame();

        // The icon swap that follows in the exporter keeps the version resource.
        const fs::path icon = dir.path / L"icon.ico";
        {
            std::ofstream out(icon, std::ios::binary);
            out.write(reinterpret_cast<const char*>(kTinyIcon), sizeof(kTinyIcon));
        }
        REQUIRE_MESSAGE(gameExport::ExeIconEmbedder::embedIcon(game, icon, error), error);
        checkGame();
        CHECK(versionResources(game).size() == 1);
    }
}
