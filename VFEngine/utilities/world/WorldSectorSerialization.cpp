#include "WorldSectorSerialization.hpp"
#include "../serialization/SceneSerialization.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../scene/EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../resource/EndianUtils.hpp"
#include "../print/Log.hpp"
#include <fstream>

namespace world
{
    // ── Shared JSON builder ──────────────────────────────────────────────

    json WorldSectorSerialization::buildSectorJson(WorldSector& sector)
    {
        json sectorJson;
        sectorJson["version"] = "1.0";
        sectorJson["coord"] = {{"x", sector.coord.x}, {"z", sector.coord.z}};

        json entitiesJson = json::array();

        for (uint64_t uuid : sector.entityUUIDs)
        {
            auto entity = scene::EntityRegistry::findByUUID(uuid);
            if (entity != entt::null)
            {
                scene::Entity sceneEntity(entity);
                entitiesJson.push_back(serialization::SceneSerialization::serializeEntity(sceneEntity));
            }
        }

        sectorJson["entities"] = entitiesJson;
        return sectorJson;
    }

    // ── AABB computation ─────────────────────────────────────────────────

    math::AABB WorldSectorSerialization::computeSectorAABB(WorldSector& sector)
    {
        math::AABB aabb;
        bool hasPoints = false;
        auto& registry = scene::EntityRegistry::getRegistry();

        for (uint64_t uuid : sector.entityUUIDs)
        {
            auto entity = scene::EntityRegistry::findByUUID(uuid);
            if (entity != entt::null && registry.any_of<components::TransformComponent>(entity))
            {
                const auto& transform = registry.get<components::TransformComponent>(entity);
                if (!hasPoints)
                {
                    aabb.min = transform.position;
                    aabb.max = transform.position;
                    hasPoints = true;
                }
                else
                {
                    aabb.expand(transform.position);
                }
            }
        }

        return aabb;
    }

    // ── Header I/O ───────────────────────────────────────────────────────

    void WorldSectorSerialization::writeHeader(std::ostream& file, const SectorFileHeader& header)
    {
        file.write(header.magic.data(), 4);
        resource::endian::writeLE<uint32_t>(file, header.version);
        resource::endian::writeLE<uint32_t>(file, header.entityCount);
        resource::endian::writeLE<float>(file, header.aabbMinX);
        resource::endian::writeLE<float>(file, header.aabbMinY);
        resource::endian::writeLE<float>(file, header.aabbMinZ);
        resource::endian::writeLE<float>(file, header.aabbMaxX);
        resource::endian::writeLE<float>(file, header.aabbMaxY);
        resource::endian::writeLE<float>(file, header.aabbMaxZ);
        resource::endian::writeLE<uint64_t>(file, header.totalFileSize);
    }

    bool WorldSectorSerialization::readHeader(std::istream& file, SectorFileHeader& outHeader)
    {
        file.read(outHeader.magic.data(), 4);
        if (file.gcount() != 4)
            return false;

        if (outHeader.magic != SECTOR_MAGIC)
            return false;

        outHeader.version = resource::endian::readLE<uint32_t>(file);
        outHeader.entityCount = resource::endian::readLE<uint32_t>(file);
        outHeader.aabbMinX = resource::endian::readLE<float>(file);
        outHeader.aabbMinY = resource::endian::readLE<float>(file);
        outHeader.aabbMinZ = resource::endian::readLE<float>(file);
        outHeader.aabbMaxX = resource::endian::readLE<float>(file);
        outHeader.aabbMaxY = resource::endian::readLE<float>(file);
        outHeader.aabbMaxZ = resource::endian::readLE<float>(file);
        outHeader.totalFileSize = resource::endian::readLE<uint64_t>(file);

        return file.good();
    }

    // ── Format detection ─────────────────────────────────────────────────

    bool WorldSectorSerialization::isBinaryFormat(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
            return false;

        char magic[4] = {};
        file.read(magic, 4);
        return file.gcount() == 4
            && magic[0] == 'V' && magic[1] == 'F'
            && magic[2] == 'S' && magic[3] == 'C';
    }

    // ── Binary save ──────────────────────────────────────────────────────

    bool WorldSectorSerialization::saveSectorBinary(WorldSector& sector,
                                                     scene::SceneGraphSystem& sceneGraph,
                                                     const std::string& filePath)
    {
        try
        {
            json sectorJson = buildSectorJson(sector);
            math::AABB aabb = computeSectorAABB(sector);

            auto msgpackData = json::to_msgpack(sectorJson);

            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for binary writing: {}", filePath);
                return false;
            }

            SectorFileHeader header;
            header.entityCount = static_cast<uint32_t>(sector.entityUUIDs.size());
            header.aabbMinX = aabb.min.x;
            header.aabbMinY = aabb.min.y;
            header.aabbMinZ = aabb.min.z;
            header.aabbMaxX = aabb.max.x;
            header.aabbMaxY = aabb.max.y;
            header.aabbMaxZ = aabb.max.z;
            header.totalFileSize = SECTOR_HEADER_SIZE + msgpackData.size();

            writeHeader(file, header);
            file.write(reinterpret_cast<const char*>(msgpackData.data()), msgpackData.size());
            file.close();

            sector.dirty = false;
            sector.filePath = filePath;

            // Update cached metadata
            sector.metadata.entityCount = header.entityCount;
            sector.metadata.bounds = aabb;
            sector.metadata.estimatedMemory = header.totalFileSize;
            sector.metadata.valid = true;

            vfLogInfo("Sector ({},{}) saved as binary to: {}", sector.coord.x, sector.coord.z, filePath);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save sector as binary: {}", e.what());
            return false;
        }
    }

    bool WorldSectorSerialization::saveSector(WorldSector& sector,
                                               scene::SceneGraphSystem& sceneGraph,
                                               const std::string& filePath)
    {
        return saveSectorBinary(sector, sceneGraph, filePath);
    }

    // ── JSON save (debug fallback) ───────────────────────────────────────

    bool WorldSectorSerialization::saveSectorJson(WorldSector& sector,
                                                   scene::SceneGraphSystem& sceneGraph,
                                                   const std::string& filePath)
    {
        try
        {
            json sectorJson = buildSectorJson(sector);

            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for writing: {}", filePath);
                return false;
            }

            file << sectorJson.dump(2);
            file.close();

            sector.dirty = false;
            sector.filePath = filePath;

            vfLogInfo("Sector ({},{}) saved as JSON to: {}", sector.coord.x, sector.coord.z, filePath);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save sector as JSON: {}", e.what());
            return false;
        }
    }

    // ── Binary load ──────────────────────────────────────────────────────

    bool WorldSectorSerialization::loadSectorBinary(const std::string& filePath,
                                                     std::vector<json>& outEntityData)
    {
        try
        {
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for binary reading: {}", filePath);
                return false;
            }

            SectorFileHeader header;
            if (!readHeader(file, header))
            {
                vfLogError("Invalid binary sector header: {}", filePath);
                return false;
            }

            if (header.version != SECTOR_FORMAT_VERSION)
            {
                vfLogError("Unsupported sector format version {} in: {}", header.version, filePath);
                return false;
            }

            size_t payloadSize = header.totalFileSize - SECTOR_HEADER_SIZE;
            std::vector<uint8_t> msgpackData(payloadSize);
            file.read(reinterpret_cast<char*>(msgpackData.data()), payloadSize);

            if (static_cast<size_t>(file.gcount()) != payloadSize)
            {
                vfLogError("Truncated binary sector file: {}", filePath);
                return false;
            }

            json sectorJson = json::from_msgpack(msgpackData);

            if (!sectorJson.contains("entities") || !sectorJson["entities"].is_array())
            {
                vfLogError("Invalid binary sector file: missing entities array");
                return false;
            }

            outEntityData.clear();
            for (const auto& entityJson : sectorJson["entities"])
            {
                outEntityData.push_back(entityJson);
            }

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load binary sector file: {}", e.what());
            return false;
        }
    }

    // ── JSON load ────────────────────────────────────────────────────────

    bool WorldSectorSerialization::loadSectorJson(const std::string& filePath,
                                                   std::vector<json>& outEntityData)
    {
        try
        {
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for reading: {}", filePath);
                return false;
            }

            json sectorJson = json::parse(file);
            file.close();

            if (!sectorJson.contains("entities") || !sectorJson["entities"].is_array())
            {
                vfLogError("Invalid sector file: missing entities array");
                return false;
            }

            outEntityData.clear();
            for (const auto& entityJson : sectorJson["entities"])
            {
                outEntityData.push_back(entityJson);
            }

            return true;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error in sector file: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load sector file: {}", e.what());
            return false;
        }
    }

    // ── Auto-detecting load ──────────────────────────────────────────────

    bool WorldSectorSerialization::loadSector(const std::string& filePath,
                                               std::vector<json>& outEntityData)
    {
        if (isBinaryFormat(filePath))
            return loadSectorBinary(filePath, outEntityData);
        return loadSectorJson(filePath, outEntityData);
    }

    // ── Metadata read (header only) ──────────────────────────────────────

    bool WorldSectorSerialization::readSectorMetadata(const std::string& filePath,
                                                       SectorMetadata& outMetadata)
    {
        if (!isBinaryFormat(filePath))
            return false;

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
            return false;

        SectorFileHeader header;
        if (!readHeader(file, header))
            return false;

        outMetadata.entityCount = header.entityCount;
        outMetadata.bounds = math::AABB(
            glm::vec3(header.aabbMinX, header.aabbMinY, header.aabbMinZ),
            glm::vec3(header.aabbMaxX, header.aabbMaxY, header.aabbMaxZ));
        outMetadata.estimatedMemory = header.totalFileSize;
        outMetadata.valid = true;
        return true;
    }

    // ── Metadata JSON ────────────────────────────────────────────────────

    json WorldSectorSerialization::serializeSectorMetadata(const WorldSector& sector)
    {
        json meta;
        meta["coord"] = {{"x", sector.coord.x}, {"z", sector.coord.z}};
        meta["entityCount"] = sector.entityUUIDs.size();
        meta["filePath"] = sector.filePath;
        return meta;
    }

} // namespace world
