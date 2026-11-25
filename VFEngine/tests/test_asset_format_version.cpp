#include <doctest.h>

#include <config/Config.hpp>
#include <print/Log.hpp>
#include <resource/AnimationResource.hpp>
#include <resource/AudioResource.hpp>
#include <resource/EndianUtils.hpp>
#include <resource/MeshStreamHandle.hpp>
#include <resource/TextureResource.hpp>
#include <resource/TextureStreamHandle.hpp>
#include <resource/Types.hpp>
#include <resource/VfAudioHeader.hpp>
#include <types/MeshFileLayout.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "test_repo_scan_helpers.hpp" // pulls <windows.h>: keep it last

// ============================================================
// VK-1680: binary assets carry AssetFormatVersion (config/Config.hpp), decoupled
// from the engine Version that the repo-root VERSION file drives. Before this
// change every .vfImage/.vfHdr/.vfMesh/.vfAnim/.vfAudio was stamped with Version
// and the readers required an exact match, so bumping the engine version forced
// a re-import of every project.
//
// The "legacy" fixtures write their version bytes as LITERALS (1, 0, 0): the
// bytes every asset written before VK-1680 carries. Never build them from a
// constant, or they stop describing the files that are already on disk.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    namespace endian = resource::endian;

    struct StampedVersion
    {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
    };

    // What every asset written before VK-1680 carries (the engine version was 1.0.0).
    constexpr StampedVersion kWrittenBeforeVk1680{1u, 0u, 0u};
    // What an asset stamped with the ENGINE version would carry after VERSION = 1.1.0.
    constexpr StampedVersion kEngineVersion110{1u, 1u, 0u};

    struct ScratchDir
    {
        fs::path path;

        explicit ScratchDir(const char* name) : path(fs::temp_directory_path() / name)
        {
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path, ec);
        }

        ~ScratchDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    std::string writeFile(const fs::path& path, const std::string& bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        REQUIRE(out.good());
        return path.string();
    }

    void writeHeaderPrefix(std::ostream& out, resource::FileType type, StampedVersion version)
    {
        endian::writeLE<uint8_t>(out, static_cast<uint8_t>(type));
        endian::writeLE<uint32_t>(out, version.major);
        endian::writeLE<uint32_t>(out, version.minor);
        endian::writeLE<uint32_t>(out, version.patch);
    }

    void writeString(std::ostream& out, std::string_view text)
    {
        endian::writeLE<uint32_t>(out, static_cast<uint32_t>(text.size()));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // .vfImage, 1x1, one BC7 mip (a single 16-byte block, read raw: no TGA swizzle).
    std::string textureBytes(StampedVersion version)
    {
        std::ostringstream out(std::ios::binary);
        writeHeaderPrefix(out, resource::FileType::TEXTURE, version);
        endian::writeLE<uint32_t>(out, 1u); // width
        endian::writeLE<uint32_t>(out, 1u); // height
        endian::writeLE<uint32_t>(out, 4u); // channels
        endian::writeLE<uint32_t>(out, 1u); // mip levels
        endian::writeLE<uint8_t>(out, static_cast<uint8_t>(resource::TextureCompressionFormat::BC7));
        endian::writeLE<uint32_t>(out, 1u);  // mip width
        endian::writeLE<uint32_t>(out, 1u);  // mip height
        endian::writeLE<uint32_t>(out, 16u); // mip data size
        out.write(std::string(16, '\x5A').data(), 16);
        return out.str();
    }

    // .vfHdr, 1x1, one uncompressed RGBA32F mip (16 bytes).
    std::string hdrBytes(StampedVersion version)
    {
        std::ostringstream out(std::ios::binary);
        writeHeaderPrefix(out, resource::FileType::HDR, version);
        endian::writeLE<uint32_t>(out, 1u); // width
        endian::writeLE<uint32_t>(out, 1u); // height
        endian::writeLE<uint32_t>(out, 4u); // channels
        endian::writeLE<uint32_t>(out, 1u); // mip levels
        endian::writeLE<uint8_t>(out, static_cast<uint8_t>(resource::TextureCompressionFormat::Uncompressed));
        endian::writeLE<uint32_t>(out, 1u);  // mip width
        endian::writeLE<uint32_t>(out, 1u);  // mip height
        endian::writeLE<uint32_t>(out, 16u); // mip data size
        for (int channel = 0; channel < 4; ++channel)
            endian::writeLE<float>(out, 0.25f * static_cast<float>(channel + 1));
        return out.str();
    }

    // .vfMesh, static, no submeshes: header, submesh count, compression flags, hasSkinning = 0
    // (ends there, like a static mesh without sockets; see test_static_mesh_socket.cpp).
    std::string meshBytes(StampedVersion version)
    {
        std::ostringstream out(std::ios::binary);
        writeHeaderPrefix(out, resource::FileType::MESH, version);
        endian::writeLE<uint32_t>(out, 0u); // submesh count
        endian::writeLE<uint32_t>(out, 0u); // compression flags
        endian::writeLE<uint8_t>(out, 0u);  // hasSkinning
        return out.str();
    }

    // .vfAnim named "Legacy": no channels and an empty event section.
    std::string animationBytes(StampedVersion version)
    {
        std::ostringstream out(std::ios::binary);
        writeHeaderPrefix(out, resource::FileType::ANIMATION, version);
        writeString(out, "Legacy");
        endian::writeLE<float>(out, 48.0f); // duration (ticks)
        endian::writeLE<float>(out, 24.0f); // ticks per second
        endian::writeLE<uint32_t>(out, 0u); // channel count
        endian::writeLE<uint32_t>(out, 0u); // event count
        return out.str();
    }

    // .vfAudio, 16-bit PCM mono, 4 samples.
    std::string audioBytes(StampedVersion version)
    {
        resource::VfAudioHeader header;
        header.fileType = static_cast<uint8_t>(resource::FileType::AUDIO);
        header.versionMajor = version.major;
        header.versionMinor = version.minor;
        header.versionPatch = version.patch;
        header.compressionFormat = static_cast<uint8_t>(resource::AudioCompressionFormat::PCM);
        header.loadType = static_cast<uint8_t>(resource::AudioLoadType::DecompressOnLoad);
        header.sampleRate = 8000;
        header.channels = 1;
        header.frames = 4;
        header.totalDurationSeconds = 0;
        header.dataSize = 4 * sizeof(int16_t);

        std::ostringstream out(std::ios::binary);
        resource::writeVfAudioHeader(out, header);
        for (int16_t sample : {int16_t{100}, int16_t{-100}, int16_t{200}, int16_t{-200}})
            endian::writeLE<int16_t>(out, sample);
        return out.str();
    }

    // Reports the version complaints ("Incompatible ..." / "... Re-import required.")
    // that readers log after construction. Every vfLogError reaches the console buffer.
    class VersionComplaintWatch
    {
    public:
        VersionComplaintWatch()
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            firstNewSequence = util::logSequenceCounter.load();
        }

        std::vector<std::string> complaints() const
        {
            std::vector<std::string> found;
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            for (const auto& entry : util::imguiConsoleBuffer)
            {
                if (entry.sequenceNumber < firstNewSequence)
                    continue;
                const std::string& message = entry.message;
                if (message.find("ncompatible") != std::string::npos ||
                    message.find("e-import") != std::string::npos)
                {
                    found.push_back(message);
                }
            }
            return found;
        }

        // The complaints as one newline-separated string, for failure messages.
        std::string listed() const
        {
            std::string list;
            for (const auto& message : complaints())
                list += "\n  " + message;
            return list;
        }

    private:
        uint64_t firstNewSequence = 0;
    };

    bool isIdentifierChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    bool isBlank(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    // Does the "Version" at `pos` read the ENGINE version's numbers? It must be the whole
    // identifier (so AssetFormatVersion / ProjectSchemaVersion don't count), unqualified or
    // global-qualified (::Version counts, sl::Version doesn't), and be followed by `::` and
    // major / minor / patch as a whole word. Whitespace around `::` is allowed.
    bool readsEngineVersionNumberAt(std::string_view text, size_t pos)
    {
        constexpr std::string_view name = "Version";
        if (pos > 0 && isIdentifierChar(text[pos - 1]))
            return false;

        size_t before = pos;
        while (before > 0 && isBlank(text[before - 1]))
            --before;
        if (before >= 2 && text[before - 1] == ':' && text[before - 2] == ':')
        {
            size_t qualifier = before - 2;
            while (qualifier > 0 && isBlank(text[qualifier - 1]))
                --qualifier;
            if (qualifier > 0 && isIdentifierChar(text[qualifier - 1]))
                return false; // some_namespace::Version
        }

        size_t at = pos + name.size();
        while (at < text.size() && isBlank(text[at]))
            ++at;
        if (text.substr(at, 2) != "::")
            return false;
        at += 2;
        while (at < text.size() && isBlank(text[at]))
            ++at;
        for (std::string_view member : {std::string_view("major"), std::string_view("minor"), std::string_view("patch")})
        {
            if (text.substr(at, member.size()) == member)
            {
                const size_t end = at + member.size();
                return end == text.size() || !isIdentifierChar(text[end]);
            }
        }
        return false;
    }

    bool readsEngineVersionNumbers(std::string_view text)
    {
        constexpr std::string_view name = "Version";
        for (size_t pos = text.find(name); pos != std::string_view::npos; pos = text.find(name, pos + 1))
        {
            if (readsEngineVersionNumberAt(text, pos))
                return true;
        }
        return false;
    }

    bool isSourceFile(const fs::path& path)
    {
        const std::string ext = path.extension().string();
        return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".inl";
    }
}

TEST_SUITE("AssetFormatVersion")
{
    TEST_CASE("AssetFormatVersion is 1.0.0 and only an exact match is current")
    {
        // Tripwire: bumping AssetFormatVersion invalidates every asset on disk. If this
        // fails, the bump must be deliberate: the binary layout changed and every
        // project re-imports.
        CHECK(AssetFormatVersion::major == 1u);
        CHECK(AssetFormatVersion::minor == 0u);
        CHECK(AssetFormatVersion::patch == 0u);

        static_assert(AssetFormatVersion::matches(1u, 0u, 0u));
        CHECK(AssetFormatVersion::matches(1u, 0u, 0u));
        CHECK_FALSE(AssetFormatVersion::matches(1u, 1u, 0u));
        CHECK_FALSE(AssetFormatVersion::matches(2u, 0u, 0u));
        CHECK_FALSE(AssetFormatVersion::matches(1u, 0u, 1u));
        CHECK_FALSE(AssetFormatVersion::matches(0u, 0u, 0u));

        // FileVersion is embedded in SDK-exported asset structs: the layout must not change.
        static_assert(sizeof(FileVersion) == 3 * sizeof(uint32_t));
        const FileVersion defaults;
        CHECK(defaults.major == AssetFormatVersion::major);
        CHECK(defaults.minor == AssetFormatVersion::minor);
        CHECK(defaults.patch == AssetFormatVersion::patch);
    }

    TEST_CASE(".vfImage written before VK-1680 loads; an engine-version stamp is rejected")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_Texture");

        SUBCASE("legacy file loads without a version complaint")
        {
            const std::string path = writeFile(dir.path / "Legacy.vfImage", textureBytes(kWrittenBeforeVk1680));
            VersionComplaintWatch watch;

            const resource::TextureData texture = resource::TextureResource::loadTexture(path);
            CHECK(texture.width == 1u);
            CHECK(texture.height == 1u);
            REQUIRE(texture.mipData.size() == 1u);
            CHECK(texture.mipData[0].data.size() == 16u);

            const auto stream = resource::TextureStreamResource::openStream(path);
            REQUIRE(stream != nullptr);
            CHECK(stream->getHeader().width == 1u);
            CHECK(stream->getHeader().mipLevels == 1u);

            CHECK_MESSAGE(watch.complaints().empty(), "unexpected:" << watch.listed());
        }

        SUBCASE("a file stamped 1.1.0 is rejected")
        {
            const std::string path = writeFile(dir.path / "Bumped.vfImage", textureBytes(kEngineVersion110));
            VersionComplaintWatch watch;

            CHECK(resource::TextureResource::loadTexture(path).mipData.empty());
            CHECK(resource::TextureStreamResource::openStream(path) == nullptr);
            CHECK(watch.complaints().size() == 2u);
        }
    }

    TEST_CASE(".vfHdr written before VK-1680 loads; an engine-version stamp is rejected")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_Hdr");

        SUBCASE("legacy file loads without a version complaint")
        {
            const std::string path = writeFile(dir.path / "Legacy.vfHdr", hdrBytes(kWrittenBeforeVk1680));
            VersionComplaintWatch watch;

            const resource::HDRData hdr = resource::TextureResource::loadHDR(path);
            CHECK(hdr.width == 1u);
            REQUIRE(hdr.mipData.size() == 1u);
            CHECK(hdr.mipData[0].data.size() == 16u);

            CHECK_MESSAGE(watch.complaints().empty(), "unexpected:" << watch.listed());
        }

        SUBCASE("a file stamped 1.1.0 is rejected")
        {
            const std::string path = writeFile(dir.path / "Bumped.vfHdr", hdrBytes(kEngineVersion110));
            VersionComplaintWatch watch;

            const resource::HDRData hdr = resource::TextureResource::loadHDR(path);
            CHECK(hdr.width == 0u);
            CHECK(hdr.mipData.empty());
            CHECK(watch.complaints().size() == 1u);
        }
    }

    TEST_CASE(".vfMesh written before VK-1680 opens; an engine-version stamp is rejected")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_Mesh");

        SUBCASE("legacy file opens without a version complaint")
        {
            const std::string path = writeFile(dir.path / "Legacy.vfMesh", meshBytes(kWrittenBeforeVk1680));
            VersionComplaintWatch watch;

            const auto stream = resource::MeshStreamResource::openStream(path);
            REQUIRE(stream != nullptr);
            CHECK(stream->getHeader().version.major == 1u);
            CHECK(stream->getHeader().version.minor == 0u);
            CHECK(stream->getHeader().version.patch == 0u);
            CHECK(stream->getHeader().numSubmeshes == 0u);

            CHECK_MESSAGE(watch.complaints().empty(), "unexpected:" << watch.listed());
        }

        SUBCASE("a file stamped 1.1.0 is rejected")
        {
            const std::string path = writeFile(dir.path / "Bumped.vfMesh", meshBytes(kEngineVersion110));
            VersionComplaintWatch watch;

            CHECK(resource::MeshStreamResource::openStream(path) == nullptr);
            CHECK(watch.complaints().size() == 1u);
        }
    }

    TEST_CASE(".vfAnim written before VK-1680 loads; an engine-version stamp is rejected")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_Animation");

        SUBCASE("legacy file loads without a version complaint")
        {
            const std::string path = writeFile(dir.path / "Legacy.vfAnim", animationBytes(kWrittenBeforeVk1680));
            VersionComplaintWatch watch;

            // loadAnimation signals failure with a default AnimationData, whose headerFileType
            // is already ANIMATION: only the name tells success apart.
            CHECK(resource::AnimationResource::loadAnimation(path).name == "Legacy");

            const resource::AnimationClipHeader summary = resource::AnimationResource::readClipSummary(path);
            CHECK_MESSAGE(summary.success, summary.error);
            CHECK(summary.name == "Legacy");

            CHECK(static_cast<std::streamoff>(resource::AnimationResource::getEventDataOffset(path)) > 0);

            CHECK_MESSAGE(watch.complaints().empty(), "unexpected:" << watch.listed());
        }

        SUBCASE("a file stamped 1.1.0 is rejected")
        {
            const std::string path = writeFile(dir.path / "Bumped.vfAnim", animationBytes(kEngineVersion110));
            VersionComplaintWatch watch;

            CHECK(resource::AnimationResource::loadAnimation(path).name.empty());
            CHECK(watch.complaints().size() == 1u);

            const resource::AnimationClipHeader summary = resource::AnimationResource::readClipSummary(path);
            CHECK_FALSE(summary.success);
            CHECK(summary.error.find("version") != std::string::npos);

            CHECK(static_cast<std::streamoff>(resource::AnimationResource::getEventDataOffset(path)) == 0);
        }
    }

    TEST_CASE(".vfAudio written before VK-1680 loads; an engine-version stamp is rejected")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_Audio");

        SUBCASE("legacy file loads without a version complaint")
        {
            const std::string path = writeFile(dir.path / "Legacy.vfAudio", audioBytes(kWrittenBeforeVk1680));
            VersionComplaintWatch watch;

            const resource::AudioData audio = resource::AudioResource::loadAudio(path);
            CHECK(audio.channels == 1u);
            REQUIRE(audio.data.size() == 4u);
            CHECK(audio.data[0] == 100);
            CHECK(audio.data[3] == -200);

            CHECK(resource::AudioResource::openStream(path) != nullptr);

            CHECK_MESSAGE(watch.complaints().empty(), "unexpected:" << watch.listed());
        }

        SUBCASE("a file stamped 1.1.0 is rejected")
        {
            const std::string path = writeFile(dir.path / "Bumped.vfAudio", audioBytes(kEngineVersion110));
            VersionComplaintWatch watch;

            CHECK(resource::AudioResource::loadAudio(path).data.empty());
            CHECK(resource::AudioResource::openStream(path) == nullptr);
            CHECK(watch.complaints().size() == 2u);
        }
    }

    TEST_CASE("the .vfMesh header writer stamps the version the reader accepts")
    {
        ScratchDir dir("VertexForge_AssetFormatVersion_MeshWriter");
        const fs::path path = dir.path / "Written.vfMesh";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            types::meshlayout::writeFileHeader(out, 0u);
            endian::writeLE<uint8_t>(out, 0u); // hasSkinning = 0
        }

        const auto stream = resource::MeshStreamResource::openStream(path.string());
        REQUIRE(stream != nullptr);
        CHECK(AssetFormatVersion::matches(stream->getHeader().version.major, stream->getHeader().version.minor,
                                          stream->getHeader().version.patch));
    }

    TEST_CASE("the engine-version matcher only flags Version::major/minor/patch")
    {
        CHECK(readsEngineVersionNumbers("uint32_t v = Version::major;"));
        CHECK(readsEngineVersionNumbers("write(out, Version::minor)"));
        CHECK(readsEngineVersionNumbers("Version :: patch"));
        CHECK(readsEngineVersionNumbers("::Version::minor"));
        CHECK(readsEngineVersionNumbers("( :: Version::patch )"));
        CHECK(readsEngineVersionNumbers("Version::\n    major"));

        CHECK_FALSE(readsEngineVersionNumbers("AssetFormatVersion::major"));
        CHECK_FALSE(readsEngineVersionNumbers("config::ProjectSchemaVersion::major"));
        CHECK_FALSE(readsEngineVersionNumbers("EditorThemeSchemaVersion::minor"));
        CHECK_FALSE(readsEngineVersionNumbers("sl::Version::major"));
        CHECK_FALSE(readsEngineVersionNumbers("Version::majorVersion"));
        CHECK_FALSE(readsEngineVersionNumbers("Version::text"));
        CHECK_FALSE(readsEngineVersionNumbers("Version::displayName"));
        CHECK_FALSE(readsEngineVersionNumbers("struct Version {"));
    }

    TEST_CASE("no engine or plugin source reads the engine Version's numbers")
    {
        // Asset files must carry AssetFormatVersion. The engine Version is identity only and
        // is consumed as Version::text / Version::displayName; its numbers appear only in the
        // tests that check them against the VERSION file.
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");

        const std::vector<std::string> allowed = {
            "VFEngine/tests/test_engine_version.cpp",
            "VFEngine/tests/test_asset_format_version.cpp", // the matcher's own sample strings
        };

        std::vector<std::string> offenders;
        size_t scanned = 0;
        for (const char* top : {"VFEngine", "plugins"})
        {
            std::error_code ec;
            fs::recursive_directory_iterator it(*root / top, fs::directory_options::skip_permission_denied, ec);
            REQUIRE_MESSAGE(!ec, "cannot scan " << (*root / top).string() << ": " << ec.message());
            for (const fs::recursive_directory_iterator end; it != end; it.increment(ec))
            {
                if (ec)
                    break;
                if (!it->is_regular_file(ec) || !isSourceFile(it->path()))
                    continue;

                const std::string relative = fs::relative(it->path(), *root, ec).generic_string();
                if (std::find(allowed.begin(), allowed.end(), relative) != allowed.end())
                    continue;

                ++scanned;
                if (readsEngineVersionNumbers(repo_scan::stripComments(repo_scan::readFile(it->path()))))
                    offenders.push_back(relative);
            }
            CHECK_MESSAGE(!ec, "scan of " << top << " stopped early: " << ec.message());
        }

        CHECK(scanned > 1000u);
        std::string list;
        for (const auto& offender : offenders)
            list += "\n  " + offender;
        CHECK_MESSAGE(offenders.empty(), "stamp/check AssetFormatVersion, not Version, in:" << list);
    }
}
