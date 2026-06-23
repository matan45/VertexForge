#pragma once
#include "AssetTypes.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// VK-1434: conservative pre-decode CPU-RAM estimate for the asset-load gate.
//
// THROTTLE-ONLY: the authoritative decoded size is recorded by
// AssetLifecycleManager after the real decode (see ResourceManager.hpp). This
// estimate exists only so the gate can reserve headroom for a load that has not
// decoded yet. It deliberately errs HIGH (never under-estimates) so the gate
// never admits a load that would then blow the budget. Returns 0 when the size
// cannot be determined (type-less load / missing file) — the gate treats 0 as
// "ungated by design".
//
// Hybrid strategy (per the design decision):
//   * Texture / HDR (.vfImage / .vfHdr): mip data is stored inline, so the held
//     CPU bytes ~= file size; file size IS the accurate answer (small headroom).
//   * Audio (.vfAudio): a Vorbis file is ~10x smaller than its decoded PCM, so
//     file size badly under-estimates. Read the tiny fixed header for the frame
//     and channel counts and compute the exact PCM size; fall back to a
//     conservative expansion factor if the header read fails.
//   * Mesh / Animation / others: file size x a conservative per-type multiplier.

namespace resource
{
    namespace estimate_detail
    {
        inline uint64_t fileSizeOf(const std::string& path)
        {
            std::error_code ec;
            auto n = std::filesystem::file_size(path, ec);
            return ec ? 0ull : static_cast<uint64_t>(n);
        }

        inline bool readLE32At(std::ifstream& f, std::streamoff offset, uint32_t& out)
        {
            f.clear();
            f.seekg(offset, std::ios::beg);
            unsigned char b[4];
            if (!f.read(reinterpret_cast<char*>(b), 4))
                return false;
            out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                  (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
            return true;
        }

        // .vfAudio header layout (see AudioResource::loadAudio): after the 1-byte
        // file type + 3 x u32 version + 1-byte compression + 1-byte loadType +
        // u32 sampleRate, the channel count sits at byte 19 and the per-channel
        // frame count at byte 23. Decoded PCM = frames * channels * sizeof(int16).
        inline uint64_t estimateAudioPcmBytes(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return 0;
            uint32_t channels = 0, frames = 0;
            if (!readLE32At(f, 19, channels) || !readLE32At(f, 23, frames))
                return 0;
            if (channels == 0 || channels > 8 || frames == 0)
                return 0; // implausible -> let the caller fall back
            return static_cast<uint64_t>(frames) * channels * sizeof(short);
        }
    }

    inline uint64_t estimatePreDecodeBytes(AssetType type, const std::string& path)
    {
        const uint64_t fileBytes = estimate_detail::fileSizeOf(path);
        if (fileBytes == 0)
            return 0; // unknown / missing -> ungated by design

        switch (type)
        {
        case AssetType::Texture:
        case AssetType::HDR:
            // .vfImage / .vfHdr store mip data inline; held CPU bytes ~= file size.
            return fileBytes + fileBytes / 16; // ~1.06x for struct/container overhead

        case AssetType::Audio:
        {
            // Header-accurate PCM size, with a conservative Vorbis->PCM fallback.
            uint64_t pcm = estimate_detail::estimateAudioPcmBytes(path);
            return pcm != 0 ? pcm : fileBytes * 12ull;
        }

        case AssetType::Mesh:
            return fileBytes * 3ull; // possible meshopt decompress + Vertex/index struct overhead

        case AssetType::Animation:
            // Keyframe tracks stored ~inline; small structural overhead.
            return fileBytes * 2ull;

        default:
            return fileBytes * 2ull; // Font / etc. — conservative
        }
    }
}
