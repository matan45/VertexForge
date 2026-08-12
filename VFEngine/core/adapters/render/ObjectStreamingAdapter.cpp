#include "ObjectStreamingAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../../graphics/render/mesh/MeshStreamManager.hpp"
#include "world/HLODSerialization.hpp"

namespace core::adapters
{
    void ObjectStreamingAdapter::setObjectStreamingEnabled(bool enabled)
    {
        if (offScreen)
        {
            offScreen->setObjectStreamingEnabled(enabled);
        }
    }

    void ObjectStreamingAdapter::setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config)
    {
        if (offScreen)
        {
            offScreen->setObjectStreamingConfig(config);
        }
    }

    render::gpudriven::ObjectStreamConfig ObjectStreamingAdapter::getObjectStreamingConfig() const
    {
        if (offScreen)
        {
            return offScreen->getObjectStreamingConfig();
        }
        return {};
    }

    render::gpudriven::ObjectStreamingStats ObjectStreamingAdapter::getObjectStreamingStats() const
    {
        if (offScreen)
        {
            return offScreen->getObjectStreamingStats();
        }
        return {};
    }

    void ObjectStreamingAdapter::registerSectorObjects(
        uint32_t sectorId,
        const std::vector<std::pair<uint64_t, entt::entity>>& entities)
    {
        if (offScreen)
        {
            offScreen->registerSectorObjects(sectorId, entities);
        }
    }

    void ObjectStreamingAdapter::unregisterSectorObjects(uint32_t sectorId)
    {
        if (offScreen)
        {
            offScreen->unregisterSectorObjects(sectorId);
        }
    }

    bool ObjectStreamingAdapter::registerHLODMesh(const std::string& meshKey,
                                                  const ::world::HLODFileData& data)
    {
        if (!offScreen)
            return false;

        // .vfHLOD stores every submesh's every LOD back to back in two flat arrays; the per-LOD
        // offsets are slice positions into them and the indices are LOD-LOCAL, which is exactly
        // what MergedMeshBuffer::uploadLOD expects. So this is a view, not a copy.
        ::render::mesh::InMemoryMeshData mesh;
        mesh.meshKey = meshKey;
        mesh.submeshes.reserve(data.submeshes.size());

        for (size_t subIdx = 0; subIdx < data.submeshes.size(); ++subIdx)
        {
            const auto& src = data.submeshes[subIdx];

            ::render::mesh::InMemorySubmeshData submesh;
            // HLODSubmeshInfo carries only a material path, so the submesh name - which
            // MergedMeshBuffer keys its slots on - has to be synthesised. It only needs to be
            // stable and unique within this mesh.
            submesh.name = "hlod_" + std::to_string(subIdx);

            for (uint32_t lod = 0; lod < ::render::gpudriven::LOD_LEVEL_COUNT; ++lod)
            {
                if (lod >= world::HLOD_LOD_LEVELS)
                    break;

                const auto& lodInfo = src.lods[lod];
                if (lodInfo.vertexCount == 0 || lodInfo.indexCount == 0)
                    continue;

                // Guard against a corrupt/truncated bake: a bad offset here would hand the GPU
                // upload a pointer past the end of the parsed buffer.
                const size_t vertexEnd = static_cast<size_t>(lodInfo.vertexOffset) + lodInfo.vertexCount;
                const size_t indexEnd = static_cast<size_t>(lodInfo.indexOffset) + lodInfo.indexCount;
                if (vertexEnd > data.vertices.size() || indexEnd > data.indices.size())
                    return false;

                submesh.lods[lod].vertices = data.vertices.data() + lodInfo.vertexOffset;
                submesh.lods[lod].vertexCount = lodInfo.vertexCount;
                submesh.lods[lod].indices = data.indices.data() + lodInfo.indexOffset;
                submesh.lods[lod].indexCount = lodInfo.indexCount;
            }

            mesh.submeshes.push_back(std::move(submesh));
        }

        if (mesh.submeshes.empty())
            return false;

        return offScreen->registerHLODMesh(mesh);
    }

    void ObjectStreamingAdapter::releaseHLODMesh(const std::string& meshKey)
    {
        if (offScreen)
        {
            offScreen->releaseHLODMesh(meshKey);
        }
    }
}
