#include "ConvexDecompositionSidecar.hpp"

#include "AtomicFileReplace.hpp"
#include "EndianUtils.hpp"
#include "VirtualFileSystem.hpp"
#include "../print/Log.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace resource
{
    namespace
    {
        constexpr uint32_t sidecarMagic = 0x58434656; // 'VFCX' little-endian
        constexpr uint32_t sidecarVersion = 1;
        constexpr uint32_t maxEntryCount = 10'000;
        constexpr uint32_t maxConvexHullCount = 256;
        constexpr uint32_t maxHullVertexCount = 256;
        constexpr uint32_t maxHullIndexCount = 4096;

        void writeDecomposition(std::ostream& file, const ConvexDecompositionData& data)
        {
            endian::writeLE<uint8_t>(file, data.hasDecomposition ? 1 : 0);
            if (!data.hasDecomposition)
                return;

            endian::writeLE<uint32_t>(file, data.params.maxConvexHulls);
            endian::writeLE<uint32_t>(file, data.params.resolution);
            endian::writeLE<uint32_t>(file, data.params.maxVerticesPerHull);
            endian::writeLE<float>(file, data.params.minVolumePercentError);
            endian::writeLE<uint32_t>(file, data.params.maxRecursionDepth);
            endian::writeLE<uint8_t>(file, data.params.shrinkWrap ? 1 : 0);

            endian::writeLE<uint32_t>(file, static_cast<uint32_t>(data.hulls.size()));
            for (const auto& hull : data.hulls)
            {
                endian::writeLE<uint32_t>(file, static_cast<uint32_t>(hull.vertices.size()));
                for (const auto& vertex : hull.vertices)
                {
                    endian::writeLE<float>(file, vertex.x);
                    endian::writeLE<float>(file, vertex.y);
                    endian::writeLE<float>(file, vertex.z);
                }

                endian::writeLE<uint32_t>(file, static_cast<uint32_t>(hull.indices.size()));
                for (uint32_t index : hull.indices)
                {
                    endian::writeLE<uint32_t>(file, index);
                }

                endian::writeLE<float>(file, hull.center.x);
                endian::writeLE<float>(file, hull.center.y);
                endian::writeLE<float>(file, hull.center.z);
                endian::writeLE<float>(file, hull.volume);
            }
        }

        bool readDecomposition(std::istream& file, ConvexDecompositionData& outData)
        {
            outData = ConvexDecompositionData{};

            const uint8_t hasDecomposition = endian::readLE<uint8_t>(file);
            outData.hasDecomposition = hasDecomposition != 0;
            if (!outData.hasDecomposition)
                return !file.fail();

            outData.params.maxConvexHulls = endian::readLE<uint32_t>(file);
            outData.params.resolution = endian::readLE<uint32_t>(file);
            outData.params.maxVerticesPerHull = endian::readLE<uint32_t>(file);
            outData.params.minVolumePercentError = endian::readLE<float>(file);
            outData.params.maxRecursionDepth = endian::readLE<uint32_t>(file);
            outData.params.shrinkWrap = endian::readLE<uint8_t>(file) != 0;

            const uint32_t hullCount = endian::readLE<uint32_t>(file);
            if (hullCount > maxConvexHullCount)
            {
                vfLogError("ConvexDecompositionSidecar: Hull count {} exceeds limit {}", hullCount, maxConvexHullCount);
                return false;
            }

            outData.hulls.resize(hullCount);
            for (uint32_t h = 0; h < hullCount; ++h)
            {
                auto& hull = outData.hulls[h];

                const uint32_t vertexCount = endian::readLE<uint32_t>(file);
                if (vertexCount > maxHullVertexCount)
                {
                    vfLogError("ConvexDecompositionSidecar: Hull {} vertex count {} exceeds limit {}",
                               h, vertexCount, maxHullVertexCount);
                    return false;
                }

                hull.vertices.resize(vertexCount);
                for (auto& vertex : hull.vertices)
                {
                    vertex.x = endian::readLE<float>(file);
                    vertex.y = endian::readLE<float>(file);
                    vertex.z = endian::readLE<float>(file);
                }

                const uint32_t indexCount = endian::readLE<uint32_t>(file);
                if (indexCount > maxHullIndexCount)
                {
                    vfLogError("ConvexDecompositionSidecar: Hull {} index count {} exceeds limit {}",
                               h, indexCount, maxHullIndexCount);
                    return false;
                }

                hull.indices.resize(indexCount);
                for (uint32_t& index : hull.indices)
                    index = endian::readLE<uint32_t>(file);

                hull.center.x = endian::readLE<float>(file);
                hull.center.y = endian::readLE<float>(file);
                hull.center.z = endian::readLE<float>(file);
                hull.volume = endian::readLE<float>(file);
            }

            return !file.fail();
        }

    }

    std::filesystem::path ConvexDecompositionSidecar::sidecarPathForMesh(std::string_view meshPath)
    {
        return std::filesystem::path(std::string(meshPath) + ".vfCollider");
    }

    bool ConvexDecompositionSidecar::load(
        std::string_view meshPath,
        std::vector<ConvexDecompositionSidecarEntry>& outEntries)
    {
        outEntries.clear();

        const auto path = sidecarPathForMesh(meshPath);
        if (!VirtualFileSystem::instance().exists(path.string()))
            return false;

        const auto bytes = VirtualFileSystem::instance().readFile(path.string());
        if (bytes.empty())
        {
            vfLogError("ConvexDecompositionSidecar: Cannot open {}", path.string());
            return false;
        }

        const std::string buffer(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream file(buffer, std::ios::in | std::ios::binary);

        const uint32_t magic = endian::readLE<uint32_t>(file);
        const uint32_t version = endian::readLE<uint32_t>(file);
        if (magic != sidecarMagic || version != sidecarVersion)
        {
            vfLogError("ConvexDecompositionSidecar: Unsupported sidecar {} (magic={}, version={})",
                       path.string(), magic, version);
            return false;
        }

        const uint32_t entryCount = endian::readLE<uint32_t>(file);
        if (entryCount > maxEntryCount)
        {
            vfLogError("ConvexDecompositionSidecar: Entry count {} exceeds limit {}", entryCount, maxEntryCount);
            return false;
        }

        outEntries.resize(entryCount);
        for (auto& entry : outEntries)
        {
            entry.submeshIndex = endian::readLE<uint32_t>(file);
            if (!readDecomposition(file, entry.data))
                return false;
        }

        if (file.fail())
        {
            vfLogError("ConvexDecompositionSidecar: Failed to read {}", path.string());
            outEntries.clear();
            return false;
        }

        return true;
    }

    bool ConvexDecompositionSidecar::loadForSubmesh(
        std::string_view meshPath,
        uint32_t submeshIndex,
        ConvexDecompositionData& outData)
    {
        std::vector<ConvexDecompositionSidecarEntry> entries;
        if (!load(meshPath, entries))
            return false;

        const auto it = std::find_if(entries.begin(), entries.end(),
            [submeshIndex](const auto& entry) { return entry.submeshIndex == submeshIndex; });
        if (it == entries.end() || !it->data.isValid())
            return false;

        outData = it->data;
        return true;
    }

    bool ConvexDecompositionSidecar::save(
        std::string_view meshPath,
        const std::vector<ConvexDecompositionSidecarEntry>& entries)
    {
        if (entries.size() > maxEntryCount)
        {
            vfLogError("ConvexDecompositionSidecar: Entry count {} exceeds limit {}",
                       entries.size(), maxEntryCount);
            return false;
        }

        const auto path = sidecarPathForMesh(meshPath);
        const auto tempPath = path.string() + ".tmp";

        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            vfLogError("ConvexDecompositionSidecar: Cannot open {} for writing", tempPath);
            return false;
        }

        endian::writeLE<uint32_t>(file, sidecarMagic);
        endian::writeLE<uint32_t>(file, sidecarVersion);
        endian::writeLE<uint32_t>(file, static_cast<uint32_t>(entries.size()));

        for (const auto& entry : entries)
        {
            endian::writeLE<uint32_t>(file, entry.submeshIndex);
            writeDecomposition(file, entry.data);
        }

        file.close();
        if (file.fail())
        {
            vfLogError("ConvexDecompositionSidecar: Failed to write {}", tempPath);
            return false;
        }

        if (!replaceFileAtomically(tempPath, path))
        {
            vfLogError("ConvexDecompositionSidecar: Failed to replace {}", path.string());
            return false;
        }

        return true;
    }

    bool ConvexDecompositionSidecar::upsert(
        std::string_view meshPath,
        const std::vector<ConvexDecompositionSidecarEntry>& entries)
    {
        std::vector<ConvexDecompositionSidecarEntry> merged;
        load(meshPath, merged);

        for (const auto& entry : entries)
        {
            const auto it = std::find_if(merged.begin(), merged.end(),
                [submeshIndex = entry.submeshIndex](const auto& existing) {
                    return existing.submeshIndex == submeshIndex;
                });

            if (it == merged.end())
                merged.push_back(entry);
            else
                *it = entry;
        }

        std::sort(merged.begin(), merged.end(), [](const auto& a, const auto& b) {
            return a.submeshIndex < b.submeshIndex;
        });

        return save(meshPath, merged);
    }
}
