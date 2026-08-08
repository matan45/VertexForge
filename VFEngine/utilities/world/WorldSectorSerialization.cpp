#include "WorldSectorSerialization.hpp"
#include "../serialization/SceneSerialization.hpp"
#include "../scene/Entity.hpp"
#include "../scene/EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../resource/EndianUtils.hpp"
#include "../serialization/SerializationFileAccess.hpp"
#include "../print/Log.hpp"
#include <array>
#include <cstring>
#include <fstream>

namespace world
{
    namespace
    {
        // Bounds-checked little-endian cursor over an in-memory sector file. Every accessor
        // reports failure instead of over-reading, which is what the std::ifstream failbit used
        // to do for us before the reads moved off the filesystem.
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

                T raw{};
                std::memcpy(&raw, data.data() + offset, sizeof(T));
                offset += sizeof(T);
                outValue = resource::endian::fromLittleEndian(raw);
                return true;
            }

            bool take(uint64_t count, std::span<const uint8_t>& outBytes) noexcept
            {
                if (count > remaining())
                    return false;

                outBytes = data.subspan(offset, static_cast<size_t>(count));
                offset += static_cast<size_t>(count);
                return true;
            }

            std::span<const uint8_t> rest() const noexcept { return data.subspan(offset); }

        private:
            std::span<const uint8_t> data;
            size_t offset = 0;
        };
    }

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

    bool WorldSectorSerialization::parseHeader(std::span<const uint8_t> bytes,
                                               SectorFileHeader& outHeader)
    {
        if (bytes.size() < SECTOR_HEADER_SIZE)
            return false;

        ByteCursor cursor(bytes);

        std::span<const uint8_t> magic;
        if (!cursor.take(4, magic))
            return false;
        std::memcpy(outHeader.magic.data(), magic.data(), 4);

        if (outHeader.magic != SECTOR_MAGIC)
            return false;

        return cursor.read(outHeader.version)
            && cursor.read(outHeader.entityCount)
            && cursor.read(outHeader.aabbMinX)
            && cursor.read(outHeader.aabbMinY)
            && cursor.read(outHeader.aabbMinZ)
            && cursor.read(outHeader.aabbMaxX)
            && cursor.read(outHeader.aabbMaxY)
            && cursor.read(outHeader.aabbMaxZ)
            && cursor.read(outHeader.totalFileSize);
    }

    // ── Binary save ──────────────────────────────────────────────────────

    bool WorldSectorSerialization::saveSectorBinary(WorldSector& sector,
                                                     const std::string& filePath)
    {
        try
        {
            json sectorJson = buildSectorJson(sector);
            math::AABB aabb = computeSectorAABB(sector);

            auto msgpackData = json::to_msgpack(sectorJson);

            // v3 sections (TLV after the entity blob)
            std::vector<uint8_t> dataLayerBlob;
            if (!sector.dataLayers.empty())
            {
                json layersJson;
                for (const auto& [name, bytes] : sector.dataLayers)
                    layersJson[name] = json::binary(bytes);
                dataLayerBlob = json::to_msgpack(layersJson);
            }
            uint32_t sectionCount = dataLayerBlob.empty() ? 0u : 1u;

            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                vfLogError("Failed to open sector file for binary writing: {}", filePath);
                return false;
            }

            // v3 layout: header | u64 entityBlobSize | entity msgpack |
            //            u32 sectionCount | per section: u32 id, u64 size, bytes
            uint64_t totalSize = SECTOR_HEADER_SIZE + sizeof(uint64_t) + msgpackData.size()
                               + sizeof(uint32_t);
            if (sectionCount > 0)
                totalSize += sizeof(uint32_t) + sizeof(uint64_t) + dataLayerBlob.size();

            SectorFileHeader header;
            header.version = SECTOR_FORMAT_VERSION;
            header.entityCount = static_cast<uint32_t>(sector.entityUUIDs.size());
            header.aabbMinX = aabb.min.x;
            header.aabbMinY = aabb.min.y;
            header.aabbMinZ = aabb.min.z;
            header.aabbMaxX = aabb.max.x;
            header.aabbMaxY = aabb.max.y;
            header.aabbMaxZ = aabb.max.z;
            header.totalFileSize = totalSize;

            writeHeader(file, header);
            resource::endian::writeLE<uint64_t>(file, msgpackData.size());
            file.write(reinterpret_cast<const char*>(msgpackData.data()), msgpackData.size());
            resource::endian::writeLE<uint32_t>(file, sectionCount);
            if (sectionCount > 0)
            {
                resource::endian::writeLE<uint32_t>(file, SECTOR_SECTION_DATA_LAYERS);
                resource::endian::writeLE<uint64_t>(file, dataLayerBlob.size());
                file.write(reinterpret_cast<const char*>(dataLayerBlob.data()), dataLayerBlob.size());
            }
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
                                               const std::string& filePath)
    {
        return saveSectorBinary(sector, filePath);
    }

    // ── JSON save (debug fallback) ───────────────────────────────────────

    bool WorldSectorSerialization::saveSectorJson(WorldSector& sector,
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

    // ── Binary load (header already parsed by the caller) ────────────────

    bool WorldSectorSerialization::parseSectorPayload(std::span<const uint8_t> payload,
                                                      uint32_t version,
                                                      std::vector<json>& outEntityData,
                                                      SectorDataLayers* outDataLayers)
    {
        try
        {
            ByteCursor cursor(payload);

            std::span<const uint8_t> msgpackData;
            if (version >= 3)
            {
                uint64_t entityBlobSize = 0;
                if (!cursor.read(entityBlobSize) || !cursor.take(entityBlobSize, msgpackData))
                {
                    vfLogError("Truncated v3 sector file: entity blob short read");
                    return false;
                }
            }
            else
            {
                // v2: the rest of the file is the entity MessagePack blob
                msgpackData = cursor.rest();
            }

            json sectorJson = json::from_msgpack(msgpackData.data(),
                                                 msgpackData.data() + msgpackData.size());

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

            // v3 section table. A payload that stops right after the entity blob carries no
            // sections — the same outcome the stream reader reached by failing the read.
            uint32_t sectionCount = 0;
            if (version >= 3 && cursor.read(sectionCount))
            {
                for (uint32_t i = 0; i < sectionCount; ++i)
                {
                    uint32_t sectionId = 0;
                    uint64_t sectionSize = 0;
                    if (!cursor.read(sectionId) || !cursor.read(sectionSize))
                        break;

                    // Bounds-check before touching the bytes: a corrupted size used to reach
                    // std::vector::resize and rely on catching bad_alloc.
                    std::span<const uint8_t> sectionBytes;
                    if (!cursor.take(sectionSize, sectionBytes))
                    {
                        vfLogError("Truncated v3 sector file: section {} declares {} bytes",
                                   sectionId, sectionSize);
                        return false;
                    }

                    // Unknown (or unwanted) sections are simply stepped over
                    if (sectionId == SECTOR_SECTION_DATA_LAYERS && outDataLayers)
                    {
                        json layersJson = json::from_msgpack(
                            sectionBytes.data(), sectionBytes.data() + sectionBytes.size());
                        for (const auto& [name, value] : layersJson.items())
                        {
                            if (value.is_binary())
                                (*outDataLayers)[name] = value.get_binary();
                        }
                    }
                }
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

    bool WorldSectorSerialization::parseSectorJson(std::span<const uint8_t> bytes,
                                                   std::vector<json>& outEntityData)
    {
        try
        {
            json sectorJson = json::parse(bytes.data(), bytes.data() + bytes.size());

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

    // ── Auto-detecting load (single read) ────────────────────────────────

    bool WorldSectorSerialization::loadSector(const std::string& filePath,
                                               std::vector<json>& outEntityData,
                                               SectorDataLayers* outDataLayers)
    {
        if (outDataLayers)
            outDataLayers->clear();

        // Read once through the bridge: a loose file in dev mode, a .vfpak entry in a shipped
        // game. The VFS lives in the exe, so a DLL must never reach for it directly.
        const auto bytes = serialization::readSerializationFileBytes(filePath);
        if (bytes.empty())
        {
            vfLogError("Failed to open sector file: {}", filePath);
            return false;
        }

        return loadSectorFromMemory(bytes, outEntityData, outDataLayers, filePath);
    }

    bool WorldSectorSerialization::loadSectorFromMemory(std::span<const uint8_t> bytes,
                                                        std::vector<json>& outEntityData,
                                                        SectorDataLayers* outDataLayers,
                                                        std::string_view sourceLabel)
    {
        if (outDataLayers)
            outDataLayers->clear();

        SectorFileHeader header;
        if (parseHeader(bytes, header))
        {
            if (header.version < SECTOR_MIN_SUPPORTED_VERSION ||
                header.version > SECTOR_FORMAT_VERSION)
            {
                vfLogError("Unsupported sector format version {} in: {}", header.version, sourceLabel);
                return false;
            }

            if (header.totalFileSize < SECTOR_HEADER_SIZE)
            {
                vfLogError("Corrupted sector header (totalFileSize < header size): {}", sourceLabel);
                return false;
            }

            static constexpr uint64_t MAX_SECTOR_FILE_SIZE = 256ULL * 1024 * 1024; // 256 MB
            if (header.totalFileSize > MAX_SECTOR_FILE_SIZE)
            {
                vfLogError("Sector file exceeds maximum size ({} bytes): {}",
                           header.totalFileSize, sourceLabel);
                return false;
            }

            return parseSectorPayload(bytes.subspan(SECTOR_HEADER_SIZE), header.version,
                                      outEntityData, outDataLayers);
        }

        // Not binary — the same bytes are the JSON debug format
        return parseSectorJson(bytes, outEntityData);
    }

    // ── Metadata read (header only where the bytes are seekable) ─────────

    bool WorldSectorSerialization::readSectorMetadata(const std::string& filePath,
                                                       SectorMetadata& outMetadata)
    {
        // Loose files and uncompressed archive entries have a physical location, so the 44-byte
        // header is all that has to be read — loadWorld does this once per sector. A compressed
        // .vfpak entry has none and must be inflated whole.
        if (const auto location = serialization::locateSerializationFile(filePath))
        {
            if (location->size < SECTOR_HEADER_SIZE)
                return false;

            std::ifstream file(location->filePath, std::ios::binary);
            if (!file.is_open())
                return false;

            file.seekg(static_cast<std::streamoff>(location->baseOffset));
            std::array<uint8_t, SECTOR_HEADER_SIZE> headerBytes{};
            file.read(reinterpret_cast<char*>(headerBytes.data()),
                      static_cast<std::streamsize>(SECTOR_HEADER_SIZE));
            if (file.gcount() != static_cast<std::streamsize>(SECTOR_HEADER_SIZE))
                return false;

            return readSectorMetadataFromMemory(headerBytes, outMetadata);
        }

        const auto bytes = serialization::readSerializationFileBytes(filePath);
        if (bytes.empty())
            return false;

        return readSectorMetadataFromMemory(bytes, outMetadata);
    }

    bool WorldSectorSerialization::readSectorMetadataFromMemory(std::span<const uint8_t> bytes,
                                                                SectorMetadata& outMetadata)
    {
        SectorFileHeader header;
        if (!parseHeader(bytes, header))
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
