#include "HLODSerialization.hpp"
#include "../print/Log.hpp"
#include "../serialization/SerializationFileAccess.hpp"
#include <array>
#include <fstream>
#include <cstring>

namespace world
{
    static_assert(sizeof(HLODFileHeader) == HLOD_HEADER_SIZE,
        "HLODFileHeader size must match HLOD_HEADER_SIZE");

    namespace
    {
        // Bounds-checked cursor over an in-memory .vfHLOD. The stream reader relied on the
        // ifstream failbit to stop at EOF; parsing from a buffer has to say no explicitly —
        // and reject a corrupted count before it reaches a resize.
        class ByteCursor
        {
        public:
            explicit ByteCursor(std::span<const uint8_t> bytes) noexcept : data(bytes) {}

            uint64_t remaining() const noexcept
            {
                return static_cast<uint64_t>(data.size() - offset);
            }

            template<typename T>
            bool read(T& outValue) noexcept
            {
                if (remaining() < sizeof(T))
                    return false;

                std::memcpy(&outValue, data.data() + offset, sizeof(T));
                offset += sizeof(T);
                return true;
            }

            bool readBytes(void* dest, uint64_t count) noexcept
            {
                if (count > remaining())
                    return false;

                if (count > 0)
                    std::memcpy(dest, data.data() + offset, static_cast<size_t>(count));
                offset += static_cast<size_t>(count);
                return true;
            }

            template<typename T>
            bool readArray(std::vector<T>& outValues, uint64_t count)
            {
                // Division, not multiplication: a corrupted count cannot overflow into a pass
                if (count > remaining() / sizeof(T))
                    return false;

                outValues.resize(static_cast<size_t>(count));
                return readBytes(outValues.data(), count * sizeof(T));
            }

        private:
            std::span<const uint8_t> data;
            size_t offset = 0;
        };
    }

    bool HLODSerialization::save(const std::string& filePath, const HLODFileData& data)
    {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("HLODSerialization: Failed to open file for writing: {}", filePath);
            return false;
        }

        // Write header (64 bytes)
        file.write(reinterpret_cast<const char*>(&data.header), sizeof(HLODFileHeader));

        // Write per-submesh info
        for (const auto& submesh : data.submeshes)
        {
            auto pathLen = static_cast<uint32_t>(submesh.materialPath.size());
            file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
            file.write(submesh.materialPath.data(), pathLen);

            for (uint32_t lod = 0; lod < HLOD_LOD_LEVELS; ++lod)
            {
                file.write(reinterpret_cast<const char*>(&submesh.lods[lod]), sizeof(HLODSubmeshLOD));
            }
        }

        // Write vertex data
        if (!data.vertices.empty())
        {
            file.write(reinterpret_cast<const char*>(data.vertices.data()),
                       static_cast<std::streamsize>(data.vertices.size() * sizeof(resource::Vertex)));
        }

        // Write index data
        if (!data.indices.empty())
        {
            file.write(reinterpret_cast<const char*>(data.indices.data()),
                       static_cast<std::streamsize>(data.indices.size() * sizeof(uint32_t)));
        }

        // Write meshlet data
        auto meshletBlobSize = static_cast<uint32_t>(data.meshletData.meshletBlob.size());
        auto meshletVertexCount = static_cast<uint32_t>(data.meshletData.meshletVertices.size());
        auto meshletPrimCount = static_cast<uint32_t>(data.meshletData.meshletPrimitives.size());

        file.write(reinterpret_cast<const char*>(&meshletBlobSize), sizeof(meshletBlobSize));
        file.write(reinterpret_cast<const char*>(&meshletVertexCount), sizeof(meshletVertexCount));
        file.write(reinterpret_cast<const char*>(&meshletPrimCount), sizeof(meshletPrimCount));

        if (meshletBlobSize > 0)
            file.write(reinterpret_cast<const char*>(data.meshletData.meshletBlob.data()), meshletBlobSize);
        if (meshletVertexCount > 0)
            file.write(reinterpret_cast<const char*>(data.meshletData.meshletVertices.data()),
                       static_cast<std::streamsize>(meshletVertexCount * sizeof(uint32_t)));
        if (meshletPrimCount > 0)
            file.write(reinterpret_cast<const char*>(data.meshletData.meshletPrimitives.data()), meshletPrimCount);

        return file.good();
    }

    bool HLODSerialization::load(const std::string& filePath, HLODFileData& outData)
    {
        const auto bytes = serialization::readSerializationFileBytes(filePath);
        if (bytes.empty())
        {
            vfLogError("HLODSerialization: Failed to open file: {}", filePath);
            return false;
        }

        return loadFromMemory(bytes, outData, filePath);
    }

    bool HLODSerialization::loadFromMemory(std::span<const uint8_t> bytes, HLODFileData& outData,
                                           std::string_view sourceLabel)
    {
        if (!parseHeader(bytes, outData.header))
        {
            vfLogError("HLODSerialization: Invalid magic in {}", sourceLabel);
            return false;
        }
        if (outData.header.version != HLOD_FORMAT_VERSION)
        {
            vfLogError("HLODSerialization: Unsupported version {} in {}", outData.header.version, sourceLabel);
            return false;
        }

        ByteCursor cursor(bytes.subspan(HLOD_HEADER_SIZE));

        // Read submesh info. Every entry costs at least a length field and its LOD table, so a
        // count the remaining bytes cannot possibly cover is corruption, not a huge model.
        static constexpr uint64_t MIN_SUBMESH_BYTES =
            sizeof(uint32_t) + HLOD_LOD_LEVELS * sizeof(HLODSubmeshLOD);
        if (outData.header.submeshCount > cursor.remaining() / MIN_SUBMESH_BYTES)
        {
            vfLogError("HLODSerialization: submesh count {} exceeds file size in {}",
                       outData.header.submeshCount, sourceLabel);
            return false;
        }

        outData.submeshes.resize(outData.header.submeshCount);
        for (auto& submesh : outData.submeshes)
        {
            uint32_t pathLen = 0;
            if (!cursor.read(pathLen) || pathLen > 4096)
            {
                vfLogError("HLODSerialization: Invalid material path length in {}", sourceLabel);
                return false;
            }
            submesh.materialPath.resize(pathLen);
            if (!cursor.readBytes(submesh.materialPath.data(), pathLen))
            {
                vfLogError("HLODSerialization: Truncated submesh table in {}", sourceLabel);
                return false;
            }

            for (uint32_t lod = 0; lod < HLOD_LOD_LEVELS; ++lod)
            {
                if (!cursor.read(submesh.lods[lod]))
                {
                    vfLogError("HLODSerialization: Truncated submesh table in {}", sourceLabel);
                    return false;
                }
            }
        }

        // Read vertex and index data
        if (!cursor.readArray(outData.vertices, outData.header.totalVertexCount) ||
            !cursor.readArray(outData.indices, outData.header.totalIndexCount))
        {
            vfLogError("HLODSerialization: Truncated geometry in {}", sourceLabel);
            return false;
        }

        // Read meshlet data
        uint32_t meshletBlobSize = 0, meshletVertexCount = 0, meshletPrimCount = 0;
        if (!cursor.read(meshletBlobSize) || !cursor.read(meshletVertexCount) ||
            !cursor.read(meshletPrimCount))
        {
            vfLogError("HLODSerialization: Truncated meshlet header in {}", sourceLabel);
            return false;
        }

        if (!cursor.readArray(outData.meshletData.meshletBlob, meshletBlobSize) ||
            !cursor.readArray(outData.meshletData.meshletVertices, meshletVertexCount) ||
            !cursor.readArray(outData.meshletData.meshletPrimitives, meshletPrimCount))
        {
            vfLogError("HLODSerialization: Truncated meshlet data in {}", sourceLabel);
            return false;
        }

        return true;
    }

    bool HLODSerialization::readHeader(const std::string& filePath, HLODFileHeader& outHeader)
    {
        // Loose files and uncompressed archive entries expose a physical location, so only the
        // 64-byte header has to be read; a compressed .vfpak entry has none.
        if (const auto location = serialization::locateSerializationFile(filePath))
        {
            if (location->size < HLOD_HEADER_SIZE)
                return false;

            std::ifstream file(location->filePath, std::ios::binary);
            if (!file.is_open()) return false;

            file.seekg(static_cast<std::streamoff>(location->baseOffset));
            std::array<uint8_t, HLOD_HEADER_SIZE> headerBytes{};
            file.read(reinterpret_cast<char*>(headerBytes.data()),
                      static_cast<std::streamsize>(HLOD_HEADER_SIZE));
            if (file.gcount() != static_cast<std::streamsize>(HLOD_HEADER_SIZE))
                return false;

            return parseHeader(headerBytes, outHeader);
        }

        const auto bytes = serialization::readSerializationFileBytes(filePath);
        if (bytes.empty()) return false;

        return parseHeader(bytes, outHeader);
    }

    bool HLODSerialization::parseHeader(std::span<const uint8_t> bytes, HLODFileHeader& outHeader)
    {
        if (bytes.size() < HLOD_HEADER_SIZE)
            return false;

        std::memcpy(&outHeader, bytes.data(), HLOD_HEADER_SIZE);
        return outHeader.magic == HLOD_MAGIC;
    }

} // namespace world
