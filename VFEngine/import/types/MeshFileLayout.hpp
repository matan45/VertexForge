#pragma once

#include "print/Log.hpp"
#include "config/Config.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/Types.hpp"
#include "resource/VertexQuantization.hpp"

#include <cstdint>
#include <fstream>
#include <string_view>

// The .vfMesh file/submesh header layout, in ONE place.
//
// These lived in Mesh.cpp's anonymous namespace, which meant every other writer had to re-derive
// the byte order by hand — the fractured-mesh path (Mesh.cpp:1089-1102) already open-codes it a
// second time. VK-1621 adds a third writer (ProceduralMeshWriter), so the layout moves here rather
// than being copied again and left to drift from what MeshStreamHandle expects to read.
//
// Internal to Import.dll: no export macro, header-only, callers are all inside the DLL.
namespace types::meshlayout
{
    inline void writeFileHeader(std::ofstream& outFile, uint32_t numMeshes)
    {
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);
        resource::endian::writeLE<uint32_t>(outFile, numMeshes);
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(resource::MeshCompressionFlags::ALL));
    }

    inline void writeSubmeshHeader(std::ofstream& outFile, std::string_view meshName,
                                   size_t vertexCount, size_t triangleCount)
    {
        vfLogDebug("Processing submesh '{}' ({} vertices, {} triangles)...",
                   meshName, vertexCount, triangleCount);

        uint32_t nameLength = static_cast<uint32_t>(meshName.length());
        resource::endian::writeLE<uint32_t>(outFile, nameLength);
        if (nameLength > 0)
        {
            outFile.write(meshName.data(), nameLength);
        }

        resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);
    }
}
