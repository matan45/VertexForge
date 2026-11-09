#include <doctest.h>
#include <resource/AnimationResource.hpp>
#include <resource/EndianUtils.hpp>
#include <config/Config.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// .vfAnim clip summaries (VK-1656, Utilities)
//
// AnimationResource::readClipSummary reads a clip's header, name, duration, channel
// names and event count without its keys (the MCP animation_list_clips tool). Fixtures
// use the byte layout of types::Animation::saveToFile (import/types/Animation.cpp) plus
// the event block AnimationEventWriter appends; the full loader is the reference.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    namespace endian = resource::endian;

    struct FixtureChannel
    {
        std::string name;
        uint32_t positionKeys = 0;
        uint32_t rotationKeys = 0;
        uint32_t scaleKeys = 0;
    };

    struct FixtureEvent
    {
        std::string name;
        float normalizedTime = 0.0f;
        std::string payload;
    };

    struct ClipFixture
    {
        uint8_t fileType = static_cast<uint8_t>(resource::FileType::ANIMATION);
        uint32_t major = Version::major;
        uint32_t minor = Version::minor;
        uint32_t patch = Version::patch;
        std::string name = "Run";
        float duration = 48.0f; // ticks
        float ticksPerSecond = 24.0f;
        std::vector<FixtureChannel> channels;
        bool writeEventSection = true;
        std::vector<FixtureEvent> events;
    };

    fs::path makeScratchDir(const char* name)
    {
        fs::path dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }

    void writeString(std::ostream& out, const std::string& str)
    {
        endian::writeLE<uint32_t>(out, static_cast<uint32_t>(str.size()));
        out.write(str.data(), static_cast<std::streamsize>(str.size()));
    }

    // Writes `count` keys of `floatsPerKey` floats. Non-zero values, so a wrong key stride
    // would land inside key data and read garbage names/counts.
    void writeKeys(std::ostream& out, uint32_t count, int floatsPerKey)
    {
        endian::writeLE<uint32_t>(out, count);
        for (uint32_t k = 0; k < count; ++k)
        {
            for (int f = 0; f < floatsPerKey; ++f)
                endian::writeLE<float>(out, 1.5f + static_cast<float>(k) * 10.0f + static_cast<float>(f));
        }
    }

    std::string clipBytes(const ClipFixture& clip)
    {
        std::ostringstream out(std::ios::binary);
        endian::writeLE<uint8_t>(out, clip.fileType);
        endian::writeLE<uint32_t>(out, clip.major);
        endian::writeLE<uint32_t>(out, clip.minor);
        endian::writeLE<uint32_t>(out, clip.patch);

        writeString(out, clip.name);
        endian::writeLE<float>(out, clip.duration);
        endian::writeLE<float>(out, clip.ticksPerSecond);

        endian::writeLE<uint32_t>(out, static_cast<uint32_t>(clip.channels.size()));
        for (const auto& channel : clip.channels)
        {
            writeString(out, channel.name);
            writeKeys(out, channel.positionKeys, 4); // time + xyz
            writeKeys(out, channel.rotationKeys, 5); // time + xyzw
            writeKeys(out, channel.scaleKeys, 4);    // time + xyz
        }

        if (clip.writeEventSection)
        {
            endian::writeLE<uint32_t>(out, static_cast<uint32_t>(clip.events.size()));
            for (const auto& event : clip.events)
            {
                writeString(out, event.name);
                endian::writeLE<float>(out, event.normalizedTime);
                writeString(out, event.payload);
            }
        }
        return out.str();
    }

    fs::path writeFile(const fs::path& path, const std::string& bytes)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

    ClipFixture threeChannelClip()
    {
        ClipFixture clip;
        clip.channels = {{"Hips", 2, 3, 1}, {"Spine", 0, 1, 0}, {"Head", 4, 0, 2}};
        clip.events = {{"FootL", 0.25f, ""}, {"FootR", 0.75f, "right"}};
        return clip;
    }
}

TEST_SUITE("AnimationClipSummary")
{
    TEST_CASE("clip summary reads the header and channel names without the keys")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary");
        const fs::path path = writeFile(dir / "Hero_Run.vfAnim", clipBytes(threeChannelClip()));

        const resource::AnimationClipHeader header = resource::AnimationResource::readClipSummary(path.string());

        REQUIRE(header.success);
        CHECK(header.error.empty());
        CHECK(header.name == "Run");
        CHECK(header.durationTicks == doctest::Approx(48.0f));
        CHECK(header.ticksPerSecond == doctest::Approx(24.0f));
        CHECK(header.durationSeconds == doctest::Approx(2.0f));
        REQUIRE(header.channelNames.size() == 3);
        CHECK(header.channelNames[0] == "Hips");
        CHECK(header.channelNames[1] == "Spine");
        CHECK(header.channelNames[2] == "Head");
        CHECK(header.eventCount == 2u);

        // The full loader is the reference: same name, channel order and event count.
        const resource::AnimationData full = resource::AnimationResource::loadAnimation(path.string());
        CHECK(full.name == header.name);
        REQUIRE(full.channels.size() == header.channelNames.size());
        for (size_t i = 0; i < full.channels.size(); ++i)
            CHECK(full.channels[i].boneName == header.channelNames[i]);
        CHECK(full.events.size() == header.eventCount);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("clip summary falls back to 24 ticks per second like the runtime")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary_tps");
        ClipFixture clip;
        clip.duration = 12.0f;
        clip.ticksPerSecond = 0.0f;
        clip.channels = {{"Root", 1, 1, 1}};
        const fs::path path = writeFile(dir / "zero_tps.vfAnim", clipBytes(clip));

        const auto header = resource::AnimationResource::readClipSummary(path.string());

        REQUIRE(header.success);
        CHECK(header.ticksPerSecond == doctest::Approx(24.0f));
        CHECK(header.durationSeconds == doctest::Approx(0.5f));

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("clip summary counts only the events the loader would load")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary_events");
        ClipFixture clip;
        clip.channels = {{"Root", 1, 1, 1}};

        SUBCASE("no event section at all")
        {
            clip.writeEventSection = false;
            const fs::path path = writeFile(dir / "no_events.vfAnim", clipBytes(clip));

            const auto header = resource::AnimationResource::readClipSummary(path.string());
            REQUIRE(header.success);
            CHECK(header.eventCount == 0u);
            REQUIRE(header.channelNames.size() == 1);
            CHECK(header.channelNames[0] == "Root");
        }

        SUBCASE("an event count of 256 or more")
        {
            clip.writeEventSection = false;
            std::string bytes = clipBytes(clip);
            std::ostringstream count(std::ios::binary);
            endian::writeLE<uint32_t>(count, 300u);
            bytes += count.str();
            const fs::path path = writeFile(dir / "too_many_events.vfAnim", bytes);

            const auto header = resource::AnimationResource::readClipSummary(path.string());
            REQUIRE(header.success);
            CHECK(header.eventCount == 0u);
            CHECK(resource::AnimationResource::loadAnimation(path.string()).events.empty());
        }

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("clip summary rejects files the loader rejects")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary_reject");
        ClipFixture clip;
        clip.channels = {{"Root", 1, 1, 1}};
        fs::path path = dir / "bad.vfAnim";

        SUBCASE("missing file")
        {
            path = dir / "missing.vfAnim";
        }
        SUBCASE("empty file")
        {
            writeFile(path, "");
        }
        SUBCASE("not an animation file")
        {
            clip.fileType = static_cast<uint8_t>(resource::FileType::MESH);
            writeFile(path, clipBytes(clip));
        }
        SUBCASE("incompatible version")
        {
            clip.major = Version::major + 1;
            writeFile(path, clipBytes(clip));
        }
        SUBCASE("channel count above the loader's limit")
        {
            // No channels and no event section: the channel count is the last field.
            clip.channels.clear();
            clip.writeEventSection = false;
            std::string bytes = clipBytes(clip);
            bytes.resize(bytes.size() - sizeof(uint32_t));
            std::ostringstream count(std::ios::binary);
            endian::writeLE<uint32_t>(count, 1001u);
            bytes += count.str();
            writeFile(path, bytes);
        }
        SUBCASE("channel name longer than the loader's string limit")
        {
            clip.channels = {{std::string(10001, 'x'), 1, 1, 1}};
            writeFile(path, clipBytes(clip));
        }

        const auto header = resource::AnimationResource::readClipSummary(path.string());
        CHECK_FALSE(header.success);
        CHECK_FALSE(header.error.empty());
        CHECK(header.channelNames.empty());

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("clip summary reports a version mismatch by name")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary_version");
        ClipFixture clip;
        clip.minor = Version::minor + 3;
        const fs::path path = writeFile(dir / "old.vfAnim", clipBytes(clip));

        const auto header = resource::AnimationResource::readClipSummary(path.string());
        CHECK_FALSE(header.success);
        CHECK(header.error.find("version") != std::string::npos);

        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("clip summary reports truncated data instead of partial results")
    {
        const fs::path dir = makeScratchDir("vf_vk1656_clip_summary_truncated");
        ClipFixture clip;
        clip.writeEventSection = false;
        // File tail: Hips' 4 position keys (64 B), its rotation count (4 B), its scale count (4 B).
        clip.channels = {{"Hips", 4, 0, 0}};
        std::string bytes = clipBytes(clip);

        SUBCASE("cut inside the key data")
        {
            // Drop both counts and half of the position keys: the seek past the keys
            // lands beyond the end of the file.
            bytes.resize(bytes.size() - (4 + 4 + 32));
        }
        SUBCASE("cut inside a channel name")
        {
            // Keep the header, the channel count, the name length and 2 of Hips' 4 chars.
            const size_t nameStart = 1 + 3 * sizeof(uint32_t) + sizeof(uint32_t) + clip.name.size() +
                                     2 * sizeof(float) + sizeof(uint32_t);
            bytes.resize(nameStart + sizeof(uint32_t) + 2);
        }

        const fs::path path = writeFile(dir / "cut.vfAnim", bytes);
        const auto header = resource::AnimationResource::readClipSummary(path.string());
        CHECK_FALSE(header.success);
        CHECK_FALSE(header.error.empty());

        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
