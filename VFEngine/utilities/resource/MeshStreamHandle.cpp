#include "MeshStreamHandle.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

namespace resource {

    MeshStreamHandle::~MeshStreamHandle() {
        close();
    }

    MeshStreamHandle::MeshStreamHandle(MeshStreamHandle&& other) noexcept
        : file(std::move(other.file))
        , header(std::move(other.header))
        , filePath(std::move(other.filePath)) {
    }

    MeshStreamHandle& MeshStreamHandle::operator=(MeshStreamHandle&& other) noexcept {
        if (this != &other) {
            close();
            file = std::move(other.file);
            header = std::move(other.header);
            filePath = std::move(other.filePath);
        }
        return *this;
    }

    bool MeshStreamHandle::openStream(std::string_view path) {
        close();

        filePath = std::string(path);
        file.open(filePath, std::ios::binary);

        if (!file) {
            vfLogError("MeshStreamHandle: Failed to open mesh file: {}", path);
            return false;
        }

        if (!parseHeader()) {
            vfLogError("MeshStreamHandle: Failed to parse header: {}", path);
            close();
            return false;
        }

        vfLogInfo("MeshStreamHandle: Opened stream for {} with {} submeshes", path, header.numSubmeshes);
        return true;
    }

    void MeshStreamHandle::close() {
        if (file.is_open()) {
            file.close();
        }
        header = MeshStreamHeader{};
        filePath.clear();
    }

    bool MeshStreamHandle::parseHeader() {
        if (!file.is_open()) return false;

        // Read file header
        uint8_t headerFileType = endian::readLE<uint8_t>(file);
        header.headerFileType = static_cast<FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(file);
        uint32_t minorVersion = endian::readLE<uint32_t>(file);
        uint32_t patchVersion = endian::readLE<uint32_t>(file);

        header.version.major = majorVersion;
        header.version.minor = minorVersion;
        header.version.patch = patchVersion;

        // Check format version (must be LOD format v0.0.3+)
        bool isLODFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
        if (!isLODFormat) {
            vfLogError("MeshStreamHandle: Incompatible mesh file version: {}.{}.{}",
                       majorVersion, minorVersion, patchVersion);
            return false;
        }

        // Check for meshlet support (v0.0.4+)
        hasMeshlets = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 4);

        header.numSubmeshes = endian::readLE<uint32_t>(file);

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read header");
            return false;
        }

        if (header.numSubmeshes > maxSubmeshCount) {
            vfLogError("MeshStreamHandle: Submesh count {} exceeds limit {}",
                       header.numSubmeshes, maxSubmeshCount);
            return false;
        }

        header.submeshes.resize(header.numSubmeshes);

        // Parse each submesh to record file offsets
        for (uint32_t meshIdx = 0; meshIdx < header.numSubmeshes; ++meshIdx) {
            auto& submeshInfo = header.submeshes[meshIdx];

            // Read submesh name
            uint32_t nameLength = endian::readLE<uint32_t>(file);
            if (nameLength > 0 && nameLength < 1024) {
                submeshInfo.name.resize(nameLength);
                file.read(submeshInfo.name.data(), nameLength);
            } else if (nameLength == 0) {
                submeshInfo.name = "SubMesh_" + std::to_string(meshIdx);
            } else {
                vfLogError("MeshStreamHandle: Invalid name length {} for submesh {}",
                           nameLength, meshIdx);
                return false;
            }

            // Read LOD level count
            uint32_t lodLevelCount = endian::readLE<uint32_t>(file);
            if (lodLevelCount == 0 || lodLevelCount > 8) {
                vfLogError("MeshStreamHandle: Invalid LOD level count {} in submesh {}",
                           lodLevelCount, meshIdx);
                return false;
            }

            // Parse each LOD level and record file offsets
            for (uint32_t lodIdx = 0; lodIdx < LOD_LEVEL_COUNT; ++lodIdx) {
                auto& lodInfo = submeshInfo.lods[lodIdx];

                if (lodIdx < lodLevelCount) {
                    // Record current file position as start of this LOD
                    lodInfo.fileOffset = file.tellg();

                    // Read vertex count
                    lodInfo.vertexCount = endian::readLE<uint32_t>(file);
                    if (lodInfo.vertexCount > maxVertexCount) {
                        vfLogError("MeshStreamHandle: Vertex count {} exceeds limit in submesh {} LOD {}",
                                   lodInfo.vertexCount, meshIdx, lodIdx);
                        return false;
                    }

                    // Skip vertex data (8 floats per vertex = 32 bytes)
                    file.seekg(lodInfo.vertexCount * sizeof(Vertex), std::ios::cur);

                    // Read index count
                    lodInfo.indexCount = endian::readLE<uint32_t>(file);
                    if (lodInfo.indexCount > maxIndexCount) {
                        vfLogError("MeshStreamHandle: Index count {} exceeds limit in submesh {} LOD {}",
                                   lodInfo.indexCount, meshIdx, lodIdx);
                        return false;
                    }

                    // Skip index data
                    file.seekg(lodInfo.indexCount * sizeof(uint32_t), std::ios::cur);

                    // If this LOD is empty (simplified away), copy from previous LOD
                    if (lodInfo.vertexCount == 0 && lodIdx > 0) {
                        lodInfo = submeshInfo.lods[lodIdx - 1];
                    }
                } else {
                    // Duplicate last available LOD for missing levels
                    uint32_t lastLod = lodLevelCount - 1;
                    lodInfo = submeshInfo.lods[lastLod];
                }

                if (file.fail()) {
                    vfLogError("MeshStreamHandle: Failed to parse LOD {} of submesh {}",
                               lodIdx, meshIdx);
                    return false;
                }
            }

            // Parse meshlet headers if available (v0.0.4+)
            if (hasMeshlets) {
                if (!parseMeshletHeaders(meshIdx)) {
                    return false;
                }
            }
        }

        return true;
    }

    bool MeshStreamHandle::parseMeshletHeaders(uint32_t meshIdx) {
        auto& submeshInfo = header.submeshes[meshIdx];

        // Record where meshlet data starts for this submesh
        submeshInfo.meshletDataOffset = file.tellg();
        submeshInfo.hasMeshletData = true;

        // Read per-LOD meshlet counts
        uint32_t totalMeshlets = 0;
        uint32_t totalVertexIndices = 0;
        uint32_t totalPrimitives = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
            auto& meshletInfo = submeshInfo.meshletLods[lod];
            meshletInfo.meshletCount = endian::readLE<uint32_t>(file);
            meshletInfo.vertexIndexCount = endian::readLE<uint32_t>(file);
            meshletInfo.primitiveCount = endian::readLE<uint32_t>(file);

            if (meshletInfo.meshletCount > maxMeshletCount) {
                vfLogError("MeshStreamHandle: Meshlet count {} exceeds limit {} in submesh {} LOD {}",
                           meshletInfo.meshletCount, maxMeshletCount, meshIdx, lod);
                return false;
            }

            totalMeshlets += meshletInfo.meshletCount;
            totalVertexIndices += meshletInfo.vertexIndexCount;
            totalPrimitives += meshletInfo.primitiveCount;
        }

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read meshlet headers for submesh {}", meshIdx);
            return false;
        }

        // Skip meshlet data (we only parse headers for streaming)
        // Meshlet descriptors + bounds: 44 bytes each
        file.seekg(totalMeshlets * sizeof(Meshlet), std::ios::cur);
        // Meshlet vertex indices: 4 bytes each
        file.seekg(totalVertexIndices * sizeof(uint32_t), std::ios::cur);
        // Meshlet primitive data: 4 bytes each
        file.seekg(totalPrimitives * sizeof(uint32_t), std::ios::cur);

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to skip meshlet data for submesh {}", meshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::readLODLevel(uint32_t submeshIdx, uint32_t lodLevel,
                                         std::vector<Vertex>& outVertices,
                                         std::vector<uint32_t>& outIndices) {
        // Lock mutex to prevent concurrent file access from multiple async threads
        std::lock_guard<std::mutex> lock(fileMutex);

        if (!file.is_open()) {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (submeshIdx >= header.numSubmeshes) {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        if (lodLevel >= LOD_LEVEL_COUNT) {
            vfLogError("MeshStreamHandle: Invalid LOD level {}", lodLevel);
            return false;
        }

        const auto& lodInfo = header.submeshes[submeshIdx].lods[lodLevel];

        // Seek to LOD data position
        file.seekg(lodInfo.fileOffset);
        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to seek to LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        // Read vertex count (we already know it, but it's part of the file format)
        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount) {
            vfLogError("MeshStreamHandle: Vertex count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        // Read vertices
        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v) {
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            if (file.fail()) {
                vfLogError("MeshStreamHandle: Failed to read vertex {} of LOD {} submesh {}",
                           v, lodLevel, submeshIdx);
                return false;
            }
        }

        // Read index count
        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount) {
            vfLogError("MeshStreamHandle: Index count mismatch at LOD {} of submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        // Read indices
        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read indices of LOD {} submesh {}",
                       lodLevel, submeshIdx);
            return false;
        }

        return true;
    }

    bool MeshStreamHandle::readMeshletData(uint32_t submeshIdx, SubmeshMeshletData& outMeshletData) {
        std::lock_guard<std::mutex> lock(fileMutex);

        if (!file.is_open()) {
            vfLogError("MeshStreamHandle: File not open");
            return false;
        }

        if (!hasMeshlets) {
            vfLogError("MeshStreamHandle: File does not contain meshlet data");
            return false;
        }

        if (submeshIdx >= header.numSubmeshes) {
            vfLogError("MeshStreamHandle: Invalid submesh index {} (max {})",
                       submeshIdx, header.numSubmeshes);
            return false;
        }

        const auto& submeshInfo = header.submeshes[submeshIdx];
        if (!submeshInfo.hasMeshletData) {
            vfLogError("MeshStreamHandle: Submesh {} does not have meshlet data", submeshIdx);
            return false;
        }

        // Seek to meshlet data position
        file.seekg(submeshInfo.meshletDataOffset);
        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to seek to meshlet data for submesh {}", submeshIdx);
            return false;
        }

        outMeshletData.name = submeshInfo.name;

        // Read per-LOD headers (already parsed, but need to read them again to get to actual data)
        uint32_t totalMeshlets = 0;
        uint32_t totalVertexIndices = 0;
        uint32_t totalPrimitives = 0;

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
            uint32_t meshletCount = endian::readLE<uint32_t>(file);
            uint32_t vertexCount = endian::readLE<uint32_t>(file);
            uint32_t primitiveCount = endian::readLE<uint32_t>(file);

            outMeshletData.lodLevels[lod].meshletOffset = totalMeshlets;
            outMeshletData.lodLevels[lod].meshletCount = meshletCount;
            outMeshletData.lodLevels[lod].vertexDataOffset = totalVertexIndices;
            outMeshletData.lodLevels[lod].vertexDataCount = vertexCount;
            outMeshletData.lodLevels[lod].primitiveDataOffset = totalPrimitives;
            outMeshletData.lodLevels[lod].primitiveDataCount = primitiveCount;

            totalMeshlets += meshletCount;
            totalVertexIndices += vertexCount;
            totalPrimitives += primitiveCount;
        }

        // Read all meshlet descriptors and bounds
        outMeshletData.meshlets.resize(totalMeshlets);
        for (uint32_t i = 0; i < totalMeshlets; ++i) {
            auto& meshlet = outMeshletData.meshlets[i];

            // Read descriptor
            meshlet.descriptor.vertexOffset = endian::readLE<uint32_t>(file);
            meshlet.descriptor.primitiveOffset = endian::readLE<uint32_t>(file);
            meshlet.descriptor.vertexCount = endian::readLE<uint8_t>(file);
            meshlet.descriptor.primitiveCount = endian::readLE<uint8_t>(file);
            meshlet.descriptor.padding = endian::readLE<uint16_t>(file);

            // Read bounds
            meshlet.bounds.boundingSphere.x = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.y = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.z = endian::readLE<float>(file);
            meshlet.bounds.boundingSphere.w = endian::readLE<float>(file);
            meshlet.bounds.cone.x = endian::readLE<float>(file);
            meshlet.bounds.cone.y = endian::readLE<float>(file);
            meshlet.bounds.cone.z = endian::readLE<float>(file);
            meshlet.bounds.cone.w = endian::readLE<float>(file);
        }

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read meshlet descriptors for submesh {}", submeshIdx);
            return false;
        }

        // Read all meshlet vertex indices
        outMeshletData.meshletVertices.resize(totalVertexIndices);
        for (uint32_t i = 0; i < totalVertexIndices; ++i) {
            outMeshletData.meshletVertices[i] = endian::readLE<uint32_t>(file);
        }

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read meshlet vertices for submesh {}", submeshIdx);
            return false;
        }

        // Read all meshlet primitive data
        outMeshletData.meshletPrimitives.resize(totalPrimitives);
        for (uint32_t i = 0; i < totalPrimitives; ++i) {
            outMeshletData.meshletPrimitives[i] = endian::readLE<uint32_t>(file);
        }

        if (file.fail()) {
            vfLogError("MeshStreamHandle: Failed to read meshlet primitives for submesh {}", submeshIdx);
            return false;
        }

        return true;
    }

    size_t MeshStreamHandle::getLODMemorySize(uint32_t submeshIdx, uint32_t lodLevel) const {
        if (submeshIdx >= header.numSubmeshes || lodLevel >= LOD_LEVEL_COUNT) {
            return 0;
        }
        return header.submeshes[submeshIdx].lods[lodLevel].getMemorySize();
    }

    size_t MeshStreamHandle::getMeshletMemorySize(uint32_t submeshIdx, uint32_t lodLevel) const {
        if (submeshIdx >= header.numSubmeshes || lodLevel >= LOD_LEVEL_COUNT) {
            return 0;
        }
        if (!hasMeshlets || !header.submeshes[submeshIdx].hasMeshletData) {
            return 0;
        }
        return header.submeshes[submeshIdx].meshletLods[lodLevel].getMeshletMemorySize();
    }

    uint32_t MeshStreamHandle::getTotalVertexCount(uint32_t lodLevel) const {
        if (lodLevel >= LOD_LEVEL_COUNT) return 0;
        uint32_t total = 0;
        for (const auto& submesh : header.submeshes) {
            total += submesh.lods[lodLevel].vertexCount;
        }
        return total;
    }

    uint32_t MeshStreamHandle::getTotalIndexCount(uint32_t lodLevel) const {
        if (lodLevel >= LOD_LEVEL_COUNT) return 0;
        uint32_t total = 0;
        for (const auto& submesh : header.submeshes) {
            total += submesh.lods[lodLevel].indexCount;
        }
        return total;
    }

    // MeshStreamResource implementation
    std::unique_ptr<MeshStreamHandle> MeshStreamResource::openStream(std::string_view path) {
        auto handle = std::make_unique<MeshStreamHandle>();
        if (!handle->openStream(path)) {
            return nullptr;
        }
        return handle;
    }

    bool MeshStreamResource::supportsStreaming(std::string_view path) {
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file) return false;

        // Read header
        uint8_t headerFileType = endian::readLE<uint8_t>(file);
        if (static_cast<FileType>(headerFileType) != FileType::MESH) {
            return false;
        }

        uint32_t majorVersion = endian::readLE<uint32_t>(file);
        uint32_t minorVersion = endian::readLE<uint32_t>(file);
        uint32_t patchVersion = endian::readLE<uint32_t>(file);

        // Must be v0.0.3+ for LOD format
        return (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
    }

    bool MeshStreamResource::readLODFromFile(std::string_view path,
                                              const LODFileInfo& lodInfo,
                                              std::vector<Vertex>& outVertices,
                                              std::vector<uint32_t>& outIndices) {
        // Open our own file handle for thread-safe reading
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file) {
            vfLogError("MeshStreamResource: Failed to open file for LOD read: {}", path);
            return false;
        }

        // Seek to the LOD data position
        file.seekg(lodInfo.fileOffset);
        if (file.fail()) {
            vfLogError("MeshStreamResource: Failed to seek to LOD offset in {}", path);
            return false;
        }

        // Read and validate vertex count
        uint32_t vertexCount = endian::readLE<uint32_t>(file);
        if (vertexCount != lodInfo.vertexCount) {
            vfLogError("MeshStreamResource: Vertex count mismatch: expected {}, got {} in {}",
                       lodInfo.vertexCount, vertexCount, path);
            return false;
        }

        // Read vertices
        outVertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v) {
            outVertices[v].position.x = endian::readLE<float>(file);
            outVertices[v].position.y = endian::readLE<float>(file);
            outVertices[v].position.z = endian::readLE<float>(file);
            outVertices[v].normal.x = endian::readLE<float>(file);
            outVertices[v].normal.y = endian::readLE<float>(file);
            outVertices[v].normal.z = endian::readLE<float>(file);
            outVertices[v].texCoords.x = endian::readLE<float>(file);
            outVertices[v].texCoords.y = endian::readLE<float>(file);

            if (file.fail()) {
                vfLogError("MeshStreamResource: Failed to read vertex {} in {}", v, path);
                return false;
            }
        }

        // Read and validate index count
        uint32_t indexCount = endian::readLE<uint32_t>(file);
        if (indexCount != lodInfo.indexCount) {
            vfLogError("MeshStreamResource: Index count mismatch: expected {}, got {} in {}",
                       lodInfo.indexCount, indexCount, path);
            return false;
        }

        // Read indices
        endian::readVectorLE<uint32_t>(file, outIndices, indexCount);

        if (file.fail()) {
            vfLogError("MeshStreamResource: Failed to read indices in {}", path);
            return false;
        }

        return true;
    }

}
