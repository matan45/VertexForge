#include "VolumetricSerializer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>

namespace volumetric
{
    namespace
    {
        // Header layout (little-endian, 60 bytes):
        //   0 magic "VNVO" | 4 version u32 | 8 origin 3 x f32 | 20 voxelSize f32 | 24 dims 3 x i32 |
        //  36 clearance f32 | 40 connectivity u32 | 44 navigable count u64 | 52 payload byte count u64
        constexpr size_t kHeaderSize = 60;
        constexpr uint8_t kMagic[4] = {'V', 'N', 'V', 'O'};

        void writeU32(std::vector<uint8_t>& out, uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
        }

        void writeU64(std::vector<uint8_t>& out, uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
                out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
        }

        void writeF32(std::vector<uint8_t>& out, float value)
        {
            writeU32(out, std::bit_cast<uint32_t>(value));
        }

        void writeVarint(std::vector<uint8_t>& out, uint64_t value)
        {
            while (value >= 0x80u)
            {
                out.push_back(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
                value >>= 7;
            }
            out.push_back(static_cast<uint8_t>(value));
        }

        uint32_t readU32(std::span<const uint8_t> bytes, size_t offset)
        {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
                value |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (8 * i);
            return value;
        }

        uint64_t readU64(std::span<const uint8_t> bytes, size_t offset)
        {
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
                value |= static_cast<uint64_t>(bytes[offset + static_cast<size_t>(i)]) << (8 * i);
            return value;
        }

        float readF32(std::span<const uint8_t> bytes, size_t offset)
        {
            return std::bit_cast<float>(readU32(bytes, offset));
        }

        // LEB128; false on a truncated or over-long (more than 64 bits) value.
        bool readVarint(std::span<const uint8_t> bytes, size_t& position, uint64_t& value)
        {
            value = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                if (position >= bytes.size())
                    return false;
                const uint8_t byte = bytes[position++];
                const uint64_t bits = static_cast<uint64_t>(byte & 0x7Fu);
                if (shift == 63 && bits > 1u)
                    return false; // would overflow 64 bits
                value |= bits << shift;
                if ((byte & 0x80u) == 0)
                    return true;
            }
            return false;
        }
    }

    std::vector<uint8_t> encodeVolume(const VoxelGrid& grid)
    {
        std::vector<uint8_t> payload;
        uint8_t current = 0; // runs alternate, starting with blocked
        uint64_t run = 0;
        uint64_t navigable = 0;
        for (const uint8_t cell : grid.data())
        {
            const uint8_t value = cell != 0 ? uint8_t{1} : uint8_t{0};
            navigable += value;
            if (value == current)
            {
                ++run;
                continue;
            }
            writeVarint(payload, run);
            current = value;
            run = 1;
        }
        writeVarint(payload, run);

        std::vector<uint8_t> out;
        out.reserve(kHeaderSize + payload.size());
        out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
        writeU32(out, kVolumeFileVersion);
        writeF32(out, grid.origin().x);
        writeF32(out, grid.origin().y);
        writeF32(out, grid.origin().z);
        writeF32(out, grid.voxelSize());
        writeU32(out, static_cast<uint32_t>(grid.dims().x));
        writeU32(out, static_cast<uint32_t>(grid.dims().y));
        writeU32(out, static_cast<uint32_t>(grid.dims().z));
        writeF32(out, grid.settings().clearance);
        writeU32(out, grid.settings().connectivity);
        writeU64(out, navigable);
        writeU64(out, static_cast<uint64_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    std::optional<VoxelGrid> decodeVolume(std::span<const uint8_t> bytes, std::string& error)
    {
        if (bytes.size() < kHeaderSize)
        {
            error = std::format("the file is too short for a volume header ({} bytes)", bytes.size());
            return std::nullopt;
        }
        for (size_t i = 0; i < 4; ++i)
        {
            if (bytes[i] != kMagic[i])
            {
                error = "not a volumetric navigation file (bad magic)";
                return std::nullopt;
            }
        }
        const uint32_t version = readU32(bytes, 4);
        if (version != kVolumeFileVersion)
        {
            error = std::format("unsupported volume file version {} (expected {})", version, kVolumeFileVersion);
            return std::nullopt;
        }

        const glm::vec3 origin(readF32(bytes, 8), readF32(bytes, 12), readF32(bytes, 16));
        VolumetricBakeSettings settings;
        settings.voxelSize = readF32(bytes, 20);
        const int32_t dimX = static_cast<int32_t>(readU32(bytes, 24));
        const int32_t dimY = static_cast<int32_t>(readU32(bytes, 28));
        const int32_t dimZ = static_cast<int32_t>(readU32(bytes, 32));
        settings.clearance = readF32(bytes, 36);
        const uint32_t connectivity = readU32(bytes, 40);
        const uint64_t navigable = readU64(bytes, 44);
        const uint64_t payloadSize = readU64(bytes, 52);

        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
        {
            error = "the volume's origin is not finite";
            return std::nullopt;
        }
        if (!std::isfinite(settings.voxelSize) || !(settings.voxelSize > 0.0f))
        {
            error = "the volume's voxel size is not a positive number";
            return std::nullopt;
        }
        if (!std::isfinite(settings.clearance) || settings.clearance < 0.0f)
        {
            error = "the volume's clearance is not valid";
            return std::nullopt;
        }
        if (connectivity != 6 && connectivity != 26)
        {
            error = std::format("the volume's connectivity is {} (expected 6 or 26)", connectivity);
            return std::nullopt;
        }
        settings.connectivity = static_cast<uint8_t>(connectivity);
        if (dimX <= 0 || dimY <= 0 || dimZ <= 0)
        {
            error = std::format("the volume's dimensions {}x{}x{} are not positive", dimX, dimY, dimZ);
            return std::nullopt;
        }

        // Checked step by step so the product never overflows, and before anything is allocated.
        const int64_t plane = static_cast<int64_t>(dimX) * static_cast<int64_t>(dimY);
        if (plane > kMaxVoxels || plane * static_cast<int64_t>(dimZ) > kMaxVoxels)
        {
            error = std::format("the volume's {}x{}x{} voxels exceed the limit of {}", dimX, dimY, dimZ, kMaxVoxels);
            return std::nullopt;
        }
        const int64_t count = plane * static_cast<int64_t>(dimZ);
        if (payloadSize != static_cast<uint64_t>(bytes.size() - kHeaderSize))
        {
            error = std::format("the payload is {} bytes but the header says {}", bytes.size() - kHeaderSize, payloadSize);
            return std::nullopt;
        }
        if (navigable > static_cast<uint64_t>(count))
        {
            error = "the header's navigable count exceeds the voxel count";
            return std::nullopt;
        }

        VoxelGrid grid(origin, glm::ivec3(dimX, dimY, dimZ), settings);
        std::vector<uint8_t>& cells = grid.data();

        size_t position = kHeaderSize;
        uint64_t filled = 0;
        uint64_t navigableSeen = 0;
        uint8_t value = 0;
        while (position < bytes.size())
        {
            uint64_t run = 0;
            if (!readVarint(bytes, position, run))
            {
                error = "the run-length data is truncated or malformed";
                return std::nullopt;
            }
            if (run > static_cast<uint64_t>(count) - filled)
            {
                error = "the run-length data overruns the voxel count";
                return std::nullopt;
            }
            if (value != 0)
            {
                std::fill(cells.begin() + static_cast<std::ptrdiff_t>(filled),
                          cells.begin() + static_cast<std::ptrdiff_t>(filled + run), uint8_t{1});
                navigableSeen += run;
            }
            filled += run;
            value = value == 0 ? uint8_t{1} : uint8_t{0};
        }
        if (filled != static_cast<uint64_t>(count))
        {
            error = std::format("the runs cover {} voxels but the volume has {}", filled, count);
            return std::nullopt;
        }
        if (navigableSeen != navigable)
        {
            error = std::format("the runs hold {} navigable voxels but the header says {}", navigableSeen, navigable);
            return std::nullopt;
        }
        return grid;
    }
}
