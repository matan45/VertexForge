#pragma once
#include "Types.hpp"
#include "EndianUtils.hpp"
#include "../config/Config.hpp"
#include <cstdint>
#include <istream>
#include <ostream>

// Single source of truth for the .vfAudio on-disk header (everything before the
// data blob). Hand-coded magic offsets used to be duplicated across the writer
// (import/types/Audio.cpp), the full readers (utilities/resource/AudioResource.cpp)
// and the pre-decode estimate reader (utilities/resource/ResourceLoadEstimate.hpp).
//
// IMPORTANT: this is a serialized format — do NOT reorder fields, change types, or
// add a version bump here. The layout below is exactly what already ships:
//
//   off  size  field
//   ----  ----  --------------------------------------------------
//     0    u8   fileType            (resource::FileType, AUDIO == 4)
//     1   u32   version.major
//     5   u32   version.minor
//     9   u32   version.patch
//    13    u8   compressionFormat   (resource::AudioCompressionFormat)
//    14    u8   loadType            (resource::AudioLoadType)
//    15   u32   sampleRate
//    19   u32   channels
//    23   u32   frames
//    27   u32   totalDurationSeconds
//    31   u32   dataSize            (byte count of the blob that follows)
//    35   ...   data blob
//
// All multi-byte fields are little-endian (see EndianUtils.hpp).

namespace resource
{
    namespace vfaudio
    {
        // Byte offsets of each header field. Offset-based readers (e.g. the
        // pre-decode estimate) index these instead of re-deriving the layout.
        inline constexpr std::streamoff kFileTypeOffset = 0;
        inline constexpr std::streamoff kVersionMajorOffset = 1;
        inline constexpr std::streamoff kVersionMinorOffset = 5;
        inline constexpr std::streamoff kVersionPatchOffset = 9;
        inline constexpr std::streamoff kCompressionOffset = 13;
        inline constexpr std::streamoff kLoadTypeOffset = 14;
        inline constexpr std::streamoff kSampleRateOffset = 15;
        inline constexpr std::streamoff kChannelsOffset = 19;
        inline constexpr std::streamoff kFramesOffset = 23;
        inline constexpr std::streamoff kTotalDurationOffset = 27;
        inline constexpr std::streamoff kDataSizeOffset = 31;
        inline constexpr std::streamoff kHeaderSize = 35; // first byte of the data blob
    }

    // Parsed header fields, in on-disk order. Raw values: callers still validate
    // fileType / version / dataSize and interpret the enum bytes themselves, so
    // this mirrors the existing inline read/write logic without changing it.
    struct VfAudioHeader
    {
        uint8_t fileType = static_cast<uint8_t>(FileType::AUDIO);
        uint32_t versionMajor = Version::major;
        uint32_t versionMinor = Version::minor;
        uint32_t versionPatch = Version::patch;
        uint8_t compressionFormat = 0;
        uint8_t loadType = 0;
        uint32_t sampleRate = 0;
        uint32_t channels = 0;
        uint32_t frames = 0;
        uint32_t totalDurationSeconds = 0;
        uint32_t dataSize = 0;
    };

    // Writes the header in exactly the layout above. Does NOT write the data blob.
    inline void writeVfAudioHeader(std::ostream& out, const VfAudioHeader& h)
    {
        endian::writeLE<uint8_t>(out, h.fileType);
        endian::writeLE<uint32_t>(out, h.versionMajor);
        endian::writeLE<uint32_t>(out, h.versionMinor);
        endian::writeLE<uint32_t>(out, h.versionPatch);
        endian::writeLE<uint8_t>(out, h.compressionFormat);
        endian::writeLE<uint8_t>(out, h.loadType);
        endian::writeLE<uint32_t>(out, h.sampleRate);
        endian::writeLE<uint32_t>(out, h.channels);
        endian::writeLE<uint32_t>(out, h.frames);
        endian::writeLE<uint32_t>(out, h.totalDurationSeconds);
        endian::writeLE<uint32_t>(out, h.dataSize);
    }

    // Reads the header sequentially into `h`. Performs no validation — the caller
    // checks fileType / version / dataSize as before. The stream is left positioned
    // at the first byte of the data blob, matching the previous inline reads.
    inline void readVfAudioHeader(std::istream& in, VfAudioHeader& h)
    {
        h.fileType = endian::readLE<uint8_t>(in);
        h.versionMajor = endian::readLE<uint32_t>(in);
        h.versionMinor = endian::readLE<uint32_t>(in);
        h.versionPatch = endian::readLE<uint32_t>(in);
        h.compressionFormat = endian::readLE<uint8_t>(in);
        h.loadType = endian::readLE<uint8_t>(in);
        h.sampleRate = endian::readLE<uint32_t>(in);
        h.channels = endian::readLE<uint32_t>(in);
        h.frames = endian::readLE<uint32_t>(in);
        h.totalDurationSeconds = endian::readLE<uint32_t>(in);
        h.dataSize = endian::readLE<uint32_t>(in);
    }
}
