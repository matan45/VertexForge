#include "MeshStreamHandle.hpp"
#include "../print/Log.hpp"
#include "EndianUtils.hpp"
#include <filesystem>

namespace resource
{
    MeshStreamHandle::~MeshStreamHandle()
    {
        close();
    }

    MeshStreamHandle::MeshStreamHandle(MeshStreamHandle&& other) noexcept
        : file(std::move(other.file))
          , header(std::move(other.header))
          , filePath(std::move(other.filePath))
    {
    }

    MeshStreamHandle& MeshStreamHandle::operator=(MeshStreamHandle&& other) noexcept
    {
        if (this != &other)
        {
            close();
            file = std::move(other.file);
            header = std::move(other.header);
            filePath = std::move(other.filePath);
        }
        return *this;
    }

    bool MeshStreamHandle::openStream(std::string_view path)
    {
        close();

        filePath = std::string(path);
        file.open(filePath, std::ios::binary);

        if (!file)
        {
            vfLogError("MeshStreamHandle: Failed to open mesh file: {}", path);
            return false;
        }

        if (!parseHeader())
        {
            vfLogError("MeshStreamHandle: Failed to parse header: {}", path);
            close();
            return false;
        }

        return true;
    }

    void MeshStreamHandle::close()
    {
        if (file.is_open())
        {
            file.close();
        }
        header = MeshStreamHeader{};
        filePath.clear();
    }

    std::streampos MeshStreamHandle::getSocketDataOffset()
    {
        if (!hasSkeleton || socketDataOffset == std::streampos(0))
            return 0;
        return socketDataOffset;
    }

    std::streampos MeshStreamHandle::getIKChainDataOffset()
    {
        if (!hasSkeleton || ikChainDataOffset == std::streampos(0))
            return 0;
        return ikChainDataOffset;
    }

    bool MeshStreamHandle::readLODLevel(uint32_t submeshIdx, uint32_t lodLevel,
                                        std::vector<Vertex>& outVertices,
                                        std::vector<uint32_t>& outIndices)
    {
        std::lock_guard<std::mutex> lock(fileMutex);

        if (!file.is_open())
        {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (submeshIdx >= header.numSubmeshes)
        {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        if (lodLevel >= LOD_LEVEL_COUNT)
        {
            vfLogError("MeshStreamHandle: Invalid LOD level {}", lodLevel);
            return false;
        }

        const auto& lodInfo = header.submeshes[submeshIdx].lods[lodLevel];

        file.seekg(lodInfo.fileOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to seek to LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount)
        {
            vfLogError("MeshStreamHandle: Vertex count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            // Position
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            // Normal
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            // TexCoords
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            // Bone data for v0.0.7+ files
            if (has64ByteVertices)
            {
                outVertices[v].boneIndices.x = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.y = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.z = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.w = endian::readLE<int32_t>(file);
                outVertices[v].boneWeights.x = endian::readLE<float>(file);
                outVertices[v].boneWeights.y = endian::readLE<float>(file);
                outVertices[v].boneWeights.z = endian::readLE<float>(file);
                outVertices[v].boneWeights.w = endian::readLE<float>(file);
            }
            else
            {
                // Default bone data for older file versions
                outVertices[v].boneIndices = glm::ivec4(-1, -1, -1, -1);
                outVertices[v].boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (file.fail())
            {
                vfLogError("MeshStreamHandle: Failed to read vertex {} of LOD {} submesh {}",
                           v, lodLevel, submeshIdx);
                return false;
            }
        }

        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount)
        {
            vfLogError("MeshStreamHandle: Index count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail())
        {
            vfLogError("MeshStreamHandle: Failed to read indices of LOD {} submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        return true;
    }

    std::unique_ptr<MeshStreamHandle> MeshStreamResource::openStream(std::string_view path)
    {
        auto handle = std::make_unique<MeshStreamHandle>();
        if (!handle->openStream(path))
        {
            return nullptr;
        }
        return handle;
    }

    MeshesData MeshStreamResource::loadAll(std::string_view path)
    {
        MeshesData result;

        auto stream = openStream(path);
        if (!stream)
        {
            return result;
        }

        const auto& header = stream->getHeader();
        result.headerFileType = header.headerFileType;
        result.version = header.version;
        result.numberOfMeshes = header.numSubmeshes;
        result.meshes.resize(header.numSubmeshes);

        for (uint32_t i = 0; i < header.numSubmeshes; ++i)
        {
            auto& meshData = result.meshes[i];
            meshData.name = header.submeshes[i].name;
            meshData.lodLevels.resize(LOD_LEVEL_COUNT);

            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (!stream->readLODLevel(i, lod,
                                          meshData.lodLevels[lod].vertices,
                                          meshData.lodLevels[lod].indices))
                {
                    vfLogError("MeshStreamResource: Failed to read LOD {} of submesh {} from {}",
                               lod, i, path);
                    return MeshesData{};
                }
            }
        }

        // Read skeleton data (v0.0.7+)
        if (stream->hasSkeletonData())
        {
            if (!stream->readSkeleton(result.skeleton))
            {
                vfLogError("MeshStreamResource: Failed to read skeleton from {}", path);
                return MeshesData{};
            }
        }

        return result;
    }

    bool MeshStreamResource::readLODFromFile(std::string_view path,
                                             const LODFileInfo& lodInfo,
                                             std::vector<Vertex>& outVertices,
                                             std::vector<uint32_t>& outIndices,
                                             bool hasBoneData)
    {
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file)
        {
            vfLogError("MeshStreamResource: Failed to open file for LOD read: {}", path);
            return false;
        }

        file.seekg(lodInfo.fileOffset);
        if (file.fail())
        {
            vfLogError("MeshStreamResource: Failed to seek to LOD offset in {}", path);
            return false;
        }

        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount)
        {
            vfLogError("MeshStreamResource: Vertex count mismatch: expected {}, got {} in {}",
                       lodInfo.vertexCount, vertexCount, path);
            return false;
        }

        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            // Position
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            // Normal
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            // TexCoords
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            // Bone data (v0.0.6+)
            if (hasBoneData)
            {
                outVertices[v].boneIndices.x = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.y = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.z = endian::readLE<int32_t>(file);
                outVertices[v].boneIndices.w = endian::readLE<int32_t>(file);
                outVertices[v].boneWeights.x = endian::readLE<float>(file);
                outVertices[v].boneWeights.y = endian::readLE<float>(file);
                outVertices[v].boneWeights.z = endian::readLE<float>(file);
                outVertices[v].boneWeights.w = endian::readLE<float>(file);
            }
            else
            {
                // Initialize with defaults for older file versions
                outVertices[v].boneIndices = glm::ivec4(-1, -1, -1, -1);
                outVertices[v].boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (file.fail())
            {
                vfLogError("MeshStreamResource: Failed to read vertex {} in {}", v, path);
                return false;
            }
        }

        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount)
        {
            vfLogError("MeshStreamResource: Index count mismatch: expected {}, got {} in {}",
                       lodInfo.indexCount, indexCount, path);
            return false;
        }

        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail())
        {
            vfLogError("MeshStreamResource: Failed to read indices in {}", path);
            return false;
        }

        return true;
    }
}
