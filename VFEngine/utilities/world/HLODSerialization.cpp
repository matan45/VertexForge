#include "HLODSerialization.hpp"
#include "../print/Log.hpp"
#include <fstream>
#include <cstring>

namespace world
{
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

        static_assert(sizeof(HLODFileHeader) == HLOD_HEADER_SIZE,
            "HLODFileHeader size must match HLOD_HEADER_SIZE");

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
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("HLODSerialization: Failed to open file: {}", filePath);
            return false;
        }

        // Read header
        file.read(reinterpret_cast<char*>(&outData.header), sizeof(HLODFileHeader));
        if (outData.header.magic != HLOD_MAGIC)
        {
            vfLogError("HLODSerialization: Invalid magic in {}", filePath);
            return false;
        }
        if (outData.header.version != HLOD_FORMAT_VERSION)
        {
            vfLogError("HLODSerialization: Unsupported version {} in {}", outData.header.version, filePath);
            return false;
        }

        // Read submesh info
        outData.submeshes.resize(outData.header.submeshCount);
        for (auto& submesh : outData.submeshes)
        {
            uint32_t pathLen = 0;
            file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
            if (pathLen > 4096)
            {
                vfLogError("HLODSerialization: Invalid material path length in {}", filePath);
                return false;
            }
            submesh.materialPath.resize(pathLen);
            file.read(submesh.materialPath.data(), pathLen);

            for (uint32_t lod = 0; lod < HLOD_LOD_LEVELS; ++lod)
            {
                file.read(reinterpret_cast<char*>(&submesh.lods[lod]), sizeof(HLODSubmeshLOD));
            }
        }

        // Read vertex data
        outData.vertices.resize(outData.header.totalVertexCount);
        if (outData.header.totalVertexCount > 0)
        {
            file.read(reinterpret_cast<char*>(outData.vertices.data()),
                      static_cast<std::streamsize>(outData.header.totalVertexCount * sizeof(resource::Vertex)));
        }

        // Read index data
        outData.indices.resize(outData.header.totalIndexCount);
        if (outData.header.totalIndexCount > 0)
        {
            file.read(reinterpret_cast<char*>(outData.indices.data()),
                      static_cast<std::streamsize>(outData.header.totalIndexCount * sizeof(uint32_t)));
        }

        // Read meshlet data
        uint32_t meshletBlobSize = 0, meshletVertexCount = 0, meshletPrimCount = 0;
        file.read(reinterpret_cast<char*>(&meshletBlobSize), sizeof(meshletBlobSize));
        file.read(reinterpret_cast<char*>(&meshletVertexCount), sizeof(meshletVertexCount));
        file.read(reinterpret_cast<char*>(&meshletPrimCount), sizeof(meshletPrimCount));

        if (meshletBlobSize > 0)
        {
            outData.meshletData.meshletBlob.resize(meshletBlobSize);
            file.read(reinterpret_cast<char*>(outData.meshletData.meshletBlob.data()), meshletBlobSize);
        }
        if (meshletVertexCount > 0)
        {
            outData.meshletData.meshletVertices.resize(meshletVertexCount);
            file.read(reinterpret_cast<char*>(outData.meshletData.meshletVertices.data()),
                      static_cast<std::streamsize>(meshletVertexCount * sizeof(uint32_t)));
        }
        if (meshletPrimCount > 0)
        {
            outData.meshletData.meshletPrimitives.resize(meshletPrimCount);
            file.read(reinterpret_cast<char*>(outData.meshletData.meshletPrimitives.data()), meshletPrimCount);
        }

        return file.good();
    }

    bool HLODSerialization::readHeader(const std::string& filePath, HLODFileHeader& outHeader)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&outHeader), sizeof(HLODFileHeader));
        return file.good() && outHeader.magic == HLOD_MAGIC;
    }

} // namespace world
