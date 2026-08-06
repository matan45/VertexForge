#include "TerrainLayerSidecar.hpp"

#include "TerrainSaveFaultInjection.hpp"
#include "../print/Log.hpp"
#include "../resource/AtomicFileReplace.hpp"
#include "../resource/Crc32.hpp"
#include "../resource/EndianUtils.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <string_view>
#include <unordered_set>

namespace terrain
{
    namespace fs = std::filesystem;

    namespace
    {
        using namespace resource::endian;

        // Base blocks are checksummed and written as raw little-endian float bytes straight out of
        // the source vector, which is only the same thing on a little-endian host. Every supported
        // target is x64; saying so here means a port fails to compile rather than silently writing
        // a sidecar whose CRCs no reader can reproduce.
        static_assert(std::endian::native == std::endian::little,
                      "VFTL assumes a little-endian host for its raw float blocks");

        void appendBytes(std::vector<uint8_t>& out, const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            out.insert(out.end(), bytes, bytes + size);
        }

        template<typename T>
        void appendLE(std::vector<uint8_t>& out, T value)
        {
            const T converted = toLittleEndian(value);
            appendBytes(out, &converted, sizeof(T));
        }

        // A cursor over an already-loaded buffer. Every read is bounds-checked against the buffer
        // end and sets `ok` on failure, so a truncated file degrades into "this parse failed"
        // instead of into a read past the end.
        class ByteReader
        {
        public:
            ByteReader(const uint8_t* data, size_t size) : cursor(data), end(data + size) {}

            template<typename T>
            T read()
            {
                T value{};
                if (!ok || static_cast<size_t>(end - cursor) < sizeof(T))
                {
                    ok = false;
                    return value;
                }
                std::memcpy(&value, cursor, sizeof(T));
                cursor += sizeof(T);
                return fromLittleEndian(value);
            }

            bool skip(size_t size)
            {
                if (!ok || static_cast<size_t>(end - cursor) < size)
                {
                    ok = false;
                    return false;
                }
                cursor += size;
                return true;
            }

            [[nodiscard]] bool good() const { return ok; }
            [[nodiscard]] const uint8_t* position() const { return cursor; }

        private:
            const uint8_t* cursor;
            const uint8_t* end;
            bool ok = true;
        };

        uint64_t baseBlockByteLength(uint32_t vertexCount)
        {
            return static_cast<uint64_t>(vertexCount) * vertexCount * sizeof(float);
        }

        const uint8_t* rawBytes(const std::vector<float>& heights)
        {
            return reinterpret_cast<const uint8_t*>(heights.data());
        }

        // Serialises one layer record, CRC included. Records are small (an authored polyline), so
        // buffering one is cheap — and the checksum has to cover bytes that already exist.
        bool encodeLayerRecord(const HeightLayerRecord& layer, uint32_t order,
                               std::vector<uint8_t>& out)
        {
            if (layer.type != HeightLayerType::SplineCorridor)
                return false;

            std::vector<uint8_t> params;
            appendLE<float>(params, layer.spline.corridor.corridorWidth);
            appendLE<float>(params, layer.spline.corridor.falloffWidth);
            appendLE<float>(params, layer.spline.corridor.embankmentHeight);
            appendLE<uint32_t>(params, static_cast<uint32_t>(layer.spline.samples.size()));
            for (const glm::vec3& sample : layer.spline.samples)
            {
                appendLE<float>(params, sample.x);
                appendLE<float>(params, sample.y);
                appendLE<float>(params, sample.z);
            }

            // Sorted, so two saves of the same stack produce byte-identical records. An
            // unordered_set's iteration order is not a property of its contents.
            std::vector<TileCoord> affected(layer.affected.begin(), layer.affected.end());
            std::sort(affected.begin(), affected.end(),
                      [](const TileCoord& a, const TileCoord& b)
                      { return a.z != b.z ? a.z < b.z : a.x < b.x; });

            // VK-1648. Truncated, never refused: losing a whole save because a name is long is a
            // far worse outcome than losing the tail of the name. std::string_view over the
            // original avoids a copy for the overwhelmingly common short case.
            std::string_view name(layer.name);
            if (name.size() > MAX_LAYER_NAME_LENGTH)
            {
                vfLogWarning("TerrainLayerSidecar: Truncating layer {}'s name from {} to {} bytes",
                             layer.id, name.size(), MAX_LAYER_NAME_LENGTH);
                name = name.substr(0, MAX_LAYER_NAME_LENGTH);
            }

            const uint64_t bodySize =
                sizeof(uint32_t) +                                  // recordByteLength
                sizeof(uint64_t) +                                  // id
                sizeof(uint32_t) +                                  // type
                sizeof(uint8_t) +                                   // visible
                sizeof(uint32_t) +                                  // order
                sizeof(uint32_t) +                                  // affectedCount
                affected.size() * 2 * sizeof(int32_t) +
                sizeof(uint32_t) +                                  // VK-1648: nameLength
                name.size() +
                sizeof(uint32_t) +                                  // paramByteLength
                params.size();

            std::vector<uint8_t> record;
            record.reserve(static_cast<size_t>(bodySize) + sizeof(uint32_t));
            // Counts itself and the trailing CRC, so a reader can step over an unrecognised record
            // by adding one number to its start offset.
            appendLE<uint32_t>(record, static_cast<uint32_t>(bodySize + sizeof(uint32_t)));
            appendLE<uint64_t>(record, layer.id);
            appendLE<uint32_t>(record, static_cast<uint32_t>(layer.type));
            appendLE<uint8_t>(record, layer.visible ? 1u : 0u);
            appendLE<uint32_t>(record, order);
            appendLE<uint32_t>(record, static_cast<uint32_t>(affected.size()));
            for (const TileCoord& coord : affected)
            {
                appendLE<int32_t>(record, coord.x);
                appendLE<int32_t>(record, coord.z);
            }

            // VK-1648. The name goes HERE -- after `affected`, BEFORE paramByteLength -- and the
            // placement is load-bearing, not stylistic. The reader's unknown-type path steps over a
            // record it cannot evaluate with skip(paramByteLength + crc), so anything that lives
            // after the params is outside that arithmetic: a name there would leave the cursor
            // inside a string and mis-parse every following record of a Degraded sidecar. Read in
            // the common prologue, it costs that path nothing.
            appendLE<uint32_t>(record, static_cast<uint32_t>(name.size()));
            // Byte for byte, with no encoding step of any kind: the format stores a count and
            // those bytes, so whatever UTF-8 the artist typed comes back identical.
            for (const char c : name)
                record.push_back(static_cast<uint8_t>(c));

            appendLE<uint32_t>(record, static_cast<uint32_t>(params.size()));
            record.insert(record.end(), params.begin(), params.end());

            appendLE<uint32_t>(record, resource::crc32(record.data(), record.size()));

            out.insert(out.end(), record.begin(), record.end());
            return true;
        }

        std::vector<uint8_t> readWholeFile(const fs::path& path, bool& outExists)
        {
            std::error_code ec;
            outExists = fs::exists(path, ec) && !ec;
            if (!outExists)
                return {};

            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                outExists = false;
                return {};
            }

            const std::streamoff size = file.tellg();
            if (size <= 0)
                return {};

            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!file.good())
                return {};

            return bytes;
        }

        // Shared by the full read and the header-only peek. Leaves `reader` positioned just past
        // the header so the caller can continue from there.
        TerrainLayerSidecarStatus parseSidecarHeader(const std::vector<uint8_t>& bytes,
                                                     TerrainLayerSidecarMeta& outMeta,
                                                     uint32_t& outLayerCount,
                                                     uint32_t& outBaseCount,
                                                     uint64_t& outLayerTableOffset,
                                                     uint64_t& outBaseIndexOffset,
                                                     uint64_t& outNextLayerId)
        {
            if (bytes.size() < TERRAIN_LAYER_HEADER_SIZE + TERRAIN_LAYER_TRAILER_SIZE)
                return TerrainLayerSidecarStatus::Invalid;

            ByteReader reader(bytes.data(), bytes.size());

            std::array<char, 4> magic{};
            for (char& c : magic)
                c = static_cast<char>(reader.read<uint8_t>());
            if (!reader.good() || magic != TERRAIN_LAYER_MAGIC)
                return TerrainLayerSidecarStatus::Invalid;

            const uint32_t major = reader.read<uint32_t>();
            const uint32_t minor = reader.read<uint32_t>();
            const uint32_t patch = reader.read<uint32_t>();
            if (!reader.good())
                return TerrainLayerSidecarStatus::Invalid;

            // VK-1648. Distinct from Invalid: the magic already matched, so this is a readable file
            // from another build rather than damage. Same recovery, far better diagnostic.
            if (major != TERRAIN_LAYER_VERSION_MAJOR || minor != TERRAIN_LAYER_VERSION_MINOR ||
                patch != TERRAIN_LAYER_VERSION_PATCH)
            {
                return TerrainLayerSidecarStatus::VersionMismatch;
            }

            outMeta.terrainGuid = reader.read<uint64_t>();
            outMeta.generationId = reader.read<uint64_t>();
            outMeta.gridMinX = reader.read<int32_t>();
            outMeta.gridMinZ = reader.read<int32_t>();
            outMeta.gridMaxX = reader.read<int32_t>();
            outMeta.gridMaxZ = reader.read<int32_t>();
            outMeta.resolution = reader.read<uint8_t>();
            outMeta.worldTileSize = reader.read<float>();
            outLayerCount = reader.read<uint32_t>();
            outBaseCount = reader.read<uint32_t>();
            outLayerTableOffset = reader.read<uint64_t>();
            outBaseIndexOffset = reader.read<uint64_t>();
            outNextLayerId = reader.read<uint64_t>(); // VK-1647

            if (!reader.good())
                return TerrainLayerSidecarStatus::Invalid;
            if (outLayerCount > MAX_TERRAIN_EDIT_LAYERS || outBaseCount > MAX_LAYER_AFFECTED_TILES)
                return TerrainLayerSidecarStatus::Invalid;

            const uint64_t trailerOffset = bytes.size() - TERRAIN_LAYER_TRAILER_SIZE;
            if (outLayerTableOffset != TERRAIN_LAYER_HEADER_SIZE ||
                outBaseIndexOffset < outLayerTableOffset || outBaseIndexOffset > trailerOffset)
            {
                return TerrainLayerSidecarStatus::Invalid;
            }

            return TerrainLayerSidecarStatus::Ok;
        }

        // The whole-file checksum and the closing magic. Verified before anything downstream trusts
        // a count or an offset, so a torn write can never drive an allocation.
        bool verifyTrailer(const std::vector<uint8_t>& bytes)
        {
            const size_t trailerOffset = bytes.size() - TERRAIN_LAYER_TRAILER_SIZE;

            ByteReader trailer(bytes.data() + trailerOffset, TERRAIN_LAYER_TRAILER_SIZE);
            const uint32_t storedCrc = trailer.read<uint32_t>();
            const uint64_t commitMagic = trailer.read<uint64_t>();
            if (!trailer.good() || commitMagic != TERRAIN_LAYER_COMMIT_MAGIC)
                return false;

            return resource::crc32(bytes.data(), trailerOffset) == storedCrc;
        }
    }

    fs::path terrainLayerSidecarPath(const fs::path& terrainPath)
    {
        fs::path sidecar = terrainPath;
        sidecar += std::string(TERRAIN_LAYER_EXTENSION);
        return sidecar;
    }

    uint64_t newLayerGenerationId()
    {
        // Same shape as uuid::UUID's generator: thread_local, so a save running on a JobSystem
        // worker needs no lock. Zero is excluded because it is the "no sidecar" value a
        // default-constructed header carries.
        thread_local std::mt19937_64 generator(std::random_device{}());
        thread_local std::uniform_int_distribution<uint64_t> distribution(1, UINT64_MAX);
        return distribution(generator);
    }

    bool writeTerrainLayerSidecar(const fs::path& path, const TerrainLayerSidecarMeta& meta,
                                  const TerrainHeightLayerStore& store)
    {
        const auto& bases = store.allBases();
        const auto& layers = store.layers();

        if (layers.size() > MAX_TERRAIN_EDIT_LAYERS || bases.size() > MAX_LAYER_AFFECTED_TILES)
        {
            vfLogError("TerrainLayerSidecar: {} layer(s) / {} base block(s) exceed the format's "
                       "limits; refusing to write {}",
                       layers.size(), bases.size(), path.string());
            return false;
        }

        std::vector<uint8_t> layerBytes;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            if (encodeLayerRecord(layers[i], static_cast<uint32_t>(i), layerBytes))
                continue;

            // Refuse rather than skip. A dropped layer is invisible on reload — the terrain simply
            // comes back with different ground — whereas a refused save is a message.
            vfLogError("TerrainLayerSidecar: Layer {} has no persistable type; refusing to write "
                       "an incomplete {}", layers[i].id, path.string());
            return false;
        }

        // (z, x) order, so the same store always produces the same bytes and a diff between two
        // saves means the content actually changed.
        std::vector<TileCoord> baseCoords;
        baseCoords.reserve(bases.size());
        for (const auto& entry : bases)
            baseCoords.push_back(entry.first);
        std::sort(baseCoords.begin(), baseCoords.end(),
                  [](const TileCoord& a, const TileCoord& b)
                  { return a.z != b.z ? a.z < b.z : a.x < b.x; });

        const uint64_t layerTableOffset = TERRAIN_LAYER_HEADER_SIZE;
        const uint64_t baseIndexOffset = layerTableOffset + layerBytes.size();
        const uint64_t blocksOffset =
            baseIndexOffset + baseCoords.size() * TERRAIN_LAYER_BASE_INDEX_ENTRY_SIZE;

        std::vector<uint8_t> prefix;
        prefix.reserve(static_cast<size_t>(blocksOffset));

        appendBytes(prefix, TERRAIN_LAYER_MAGIC.data(), TERRAIN_LAYER_MAGIC.size());
        appendLE<uint32_t>(prefix, TERRAIN_LAYER_VERSION_MAJOR);
        appendLE<uint32_t>(prefix, TERRAIN_LAYER_VERSION_MINOR);
        appendLE<uint32_t>(prefix, TERRAIN_LAYER_VERSION_PATCH);
        appendLE<uint64_t>(prefix, meta.terrainGuid);
        appendLE<uint64_t>(prefix, meta.generationId);
        appendLE<int32_t>(prefix, meta.gridMinX);
        appendLE<int32_t>(prefix, meta.gridMinZ);
        appendLE<int32_t>(prefix, meta.gridMaxX);
        appendLE<int32_t>(prefix, meta.gridMaxZ);
        appendLE<uint8_t>(prefix, meta.resolution);
        appendLE<float>(prefix, meta.worldTileSize);
        appendLE<uint32_t>(prefix, static_cast<uint32_t>(layers.size()));
        appendLE<uint32_t>(prefix, static_cast<uint32_t>(baseCoords.size()));
        appendLE<uint64_t>(prefix, layerTableOffset);
        appendLE<uint64_t>(prefix, baseIndexOffset);

        // VK-1647. The id watermark, so a reload cannot re-issue an id that is already live — or
        // one that was live and has since been deleted, which is the harder case: a
        // RoadSplineComponent keeps its splineId in the SCENE and outlives the layer it names.
        //
        // Appended at the END of the header on purpose. TERRAIN_LAYER_GUID_OFFSET and
        // TERRAIN_LAYER_GENERATION_ID_OFFSET are unchanged, so rebindTerrainLayerSidecar keeps
        // patching the right bytes, and layerTableOffset/baseIndexOffset are stored explicitly
        // rather than recovered from the stream position (as VFTR's index offset is), so growing
        // the header shifts nothing a reader has to infer.
        appendLE<uint64_t>(prefix, store.peekNextLayerId());

        if (prefix.size() != TERRAIN_LAYER_HEADER_SIZE)
        {
            vfLogError("TerrainLayerSidecar: Header emitted {} bytes, expected {}",
                       prefix.size(), TERRAIN_LAYER_HEADER_SIZE);
            return false;
        }

        prefix.insert(prefix.end(), layerBytes.begin(), layerBytes.end());

        uint64_t runningOffset = blocksOffset;
        for (const TileCoord& coord : baseCoords)
        {
            const BaseHeightBlock& block = bases.at(coord);
            const uint64_t byteLength = baseBlockByteLength(block.vertexCount);

            // A block whose declared vertex count disagrees with its array cannot be written
            // meaningfully: the reader sizes its allocation from vertexCount.
            if (block.heights.size() * sizeof(float) != byteLength)
            {
                vfLogError("TerrainLayerSidecar: Base block ({}, {}) holds {} height(s) but claims "
                           "vertexCount {}; refusing to write {}",
                           coord.x, coord.z, block.heights.size(), block.vertexCount, path.string());
                return false;
            }

            appendLE<int32_t>(prefix, coord.x);
            appendLE<int32_t>(prefix, coord.z);
            appendLE<uint64_t>(prefix, runningOffset);
            appendLE<uint32_t>(prefix, static_cast<uint32_t>(byteLength));
            appendLE<uint32_t>(prefix, block.vertexCount);
            appendLE<uint32_t>(prefix,
                               resource::crc32(rawBytes(block.heights), static_cast<size_t>(byteLength)));

            runningOffset += byteLength;
        }

        // Byte-granular truncation, the same seam VK-1644 uses: a bool could only ever abort
        // between whole writes, which never reproduces the half-written index entry a real crash
        // leaves behind.
        const auto fault = detail::terrainSaveFault(TerrainSaveStage::SidecarWrite);
        uint64_t budget = fault.abort ? fault.partialBytes
                                      : (std::numeric_limits<uint64_t>::max)();

        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("TerrainLayerSidecar: Cannot create {}", path.string());
                return false;
            }

            uint32_t crcState = resource::crc32Init();

            auto emit = [&](const uint8_t* data, size_t size) -> bool
            {
                crcState = resource::crc32Update(crcState, data, size);
                const size_t allowed = static_cast<size_t>(std::min<uint64_t>(budget, size));
                if (allowed > 0)
                    file.write(reinterpret_cast<const char*>(data),
                               static_cast<std::streamsize>(allowed));
                budget -= allowed;
                return allowed == size;
            };

            bool complete = emit(prefix.data(), prefix.size());
            for (const TileCoord& coord : baseCoords)
            {
                if (!complete)
                    break;
                const BaseHeightBlock& block = bases.at(coord);
                complete = emit(rawBytes(block.heights),
                                static_cast<size_t>(baseBlockByteLength(block.vertexCount)));
            }

            if (complete)
            {
                std::vector<uint8_t> trailer;
                appendLE<uint32_t>(trailer, resource::crc32Finish(crcState));
                appendLE<uint64_t>(trailer, TERRAIN_LAYER_COMMIT_MAGIC);
                complete = emit(trailer.data(), trailer.size());
            }

            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainLayerSidecar: Failed to write {}", path.string());
                return false;
            }

            if (!complete)
                return false; // injected fault: the file is deliberately left torn
        }

        // The bytes have to be durable before the caller renames this temporary over the live
        // sidecar, or a power loss leaves a complete-looking name pointing at a truncated tail.
        return resource::flushFileToDisk(path);
    }

    TerrainLayerSidecarStatus peekTerrainLayerSidecar(const fs::path& path,
                                                     TerrainLayerSidecarMeta& outMeta)
    {
        bool exists = false;
        const std::vector<uint8_t> bytes = readWholeFile(path, exists);
        if (!exists)
            return TerrainLayerSidecarStatus::Absent;

        uint32_t layerCount = 0;
        uint32_t baseCount = 0;
        uint64_t layerTableOffset = 0;
        uint64_t baseIndexOffset = 0;
        uint64_t nextLayerId = 0;
        const auto status = parseSidecarHeader(bytes, outMeta, layerCount, baseCount,
                                               layerTableOffset, baseIndexOffset, nextLayerId);
        if (status != TerrainLayerSidecarStatus::Ok)
            return status;

        return verifyTrailer(bytes) ? TerrainLayerSidecarStatus::Ok
                                    : TerrainLayerSidecarStatus::Invalid;
    }

    TerrainLayerSidecarStatus readTerrainLayerSidecar(const fs::path& path,
                                                     uint64_t expectedGenerationId,
                                                     TerrainLayerSidecarMeta& outMeta,
                                                     TerrainHeightLayerStore& outStore)
    {
        bool exists = false;
        const std::vector<uint8_t> bytes = readWholeFile(path, exists);
        if (!exists)
            return TerrainLayerSidecarStatus::Absent;

        uint32_t layerCount = 0;
        uint32_t baseCount = 0;
        uint64_t layerTableOffset = 0;
        uint64_t baseIndexOffset = 0;
        uint64_t nextLayerId = 0;
        const auto headerStatus = parseSidecarHeader(bytes, outMeta, layerCount, baseCount,
                                                     layerTableOffset, baseIndexOffset, nextLayerId);
        if (headerStatus != TerrainLayerSidecarStatus::Ok)
            return headerStatus;

        if (!verifyTrailer(bytes))
            return TerrainLayerSidecarStatus::Invalid;

        // Checked after the checksum, never before: a torn file's hash field is not evidence of
        // anything, and reporting Stale for it would send the user looking for the wrong problem.
        if (outMeta.generationId != expectedGenerationId)
            return TerrainLayerSidecarStatus::Stale;

        // Decoded into locals first. outStore is only touched once the whole file has proved
        // sound, so a failure halfway through cannot leave a half-populated stack behind — which
        // would still make its tiles covered, because coverage is sticky.
        std::vector<HeightLayerRecord> decodedLayers;
        decodedLayers.reserve(layerCount);

        // VK-1647. Parallel to decodedLayers, holding each record's `order` field so the stack can
        // be rebuilt from it rather than from file sequence. Kept beside the records instead of on
        // them because order is a property of the FILE, not of a live layer — once the stack is
        // built, position is the only thing that means anything.
        std::vector<uint32_t> decodedOrders;
        decodedOrders.reserve(layerCount);

        ByteReader reader(bytes.data() + layerTableOffset,
                          static_cast<size_t>(baseIndexOffset - layerTableOffset));

        bool degraded = false;
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            const uint8_t* recordStart = reader.position();
            const uint32_t recordByteLength = reader.read<uint32_t>();
            if (!reader.good() || recordByteLength <= sizeof(uint32_t) * 2)
                return TerrainLayerSidecarStatus::Invalid;

            const size_t consumedByLength = static_cast<size_t>(recordByteLength);
            const size_t remaining =
                static_cast<size_t>(baseIndexOffset - layerTableOffset) -
                static_cast<size_t>(recordStart - (bytes.data() + layerTableOffset));
            if (consumedByLength > remaining)
                return TerrainLayerSidecarStatus::Invalid;

            const size_t coveredBytes = consumedByLength - sizeof(uint32_t);
            uint32_t storedCrc = 0;
            std::memcpy(&storedCrc, recordStart + coveredBytes, sizeof(uint32_t));
            storedCrc = fromLittleEndian(storedCrc);
            if (resource::crc32(recordStart, coveredBytes) != storedCrc)
                return TerrainLayerSidecarStatus::Invalid;

            HeightLayerRecord record;
            record.id = reader.read<uint64_t>();
            const uint32_t rawType = reader.read<uint32_t>();
            record.visible = reader.read<uint8_t>() != 0;
            // VK-1647: `order` is now AUTHORITATIVE. VK-1646 wrote it and discarded it, reserving
            // it for exactly this. Consuming it means the stack is defined by a field rather than
            // by a file position, so an out-of-sequence stack is both diagnosable in a hex dump and
            // a fact the reader can act on — and reorder needed no format bump to persist.
            const uint32_t order = reader.read<uint32_t>();
            const uint32_t affectedCount = reader.read<uint32_t>();
            if (!reader.good() || affectedCount > MAX_LAYER_AFFECTED_TILES)
                return TerrainLayerSidecarStatus::Invalid;

            for (uint32_t t = 0; t < affectedCount; ++t)
            {
                const int32_t x = reader.read<int32_t>();
                const int32_t z = reader.read<int32_t>();
                record.affected.insert(TileCoord(x, z));
            }

            // VK-1648. Read in the common prologue, so the unknown-type skip below -- which knows
            // only about paramByteLength -- stays correct for a record this build cannot evaluate.
            const uint32_t nameLength = reader.read<uint32_t>();
            if (!reader.good() || nameLength > MAX_LAYER_NAME_LENGTH)
                return TerrainLayerSidecarStatus::Invalid;

            // position() before skip(): skip bounds-checks and only advances on success, so the
            // pointer is provably in range by the time it is dereferenced.
            const uint8_t* nameBytes = reader.position();
            if (!reader.skip(nameLength))
                return TerrainLayerSidecarStatus::Invalid;
            record.name.assign(reinterpret_cast<const char*>(nameBytes), nameLength);

            const uint32_t paramByteLength = reader.read<uint32_t>();
            if (!reader.good())
                return TerrainLayerSidecarStatus::Invalid;

            if (rawType != static_cast<uint32_t>(HeightLayerType::SplineCorridor))
            {
                // A type this build cannot evaluate. The record's length is exactly what lets us
                // step over it — but a stack missing one of its members composes wrong ground, so
                // the whole sidecar is reported degraded rather than silently applied.
                degraded = true;
                if (!reader.skip(paramByteLength + sizeof(uint32_t)))
                    return TerrainLayerSidecarStatus::Invalid;
                continue;
            }

            record.type = HeightLayerType::SplineCorridor;
            record.spline.corridor.corridorWidth = reader.read<float>();
            record.spline.corridor.falloffWidth = reader.read<float>();
            record.spline.corridor.embankmentHeight = reader.read<float>();
            const uint32_t sampleCount = reader.read<uint32_t>();
            if (!reader.good() || sampleCount > MAX_LAYER_SPLINE_SAMPLES)
                return TerrainLayerSidecarStatus::Invalid;

            record.spline.samples.reserve(sampleCount);
            for (uint32_t s = 0; s < sampleCount; ++s)
            {
                const float x = reader.read<float>();
                const float y = reader.read<float>();
                const float z = reader.read<float>();
                record.spline.samples.emplace_back(x, y, z);
            }
            if (!reader.good())
                return TerrainLayerSidecarStatus::Invalid;

            record.eval = makeSplineCorridorEval(record.spline);
            decodedLayers.push_back(std::move(record));
            decodedOrders.push_back(order);

            if (!reader.skip(sizeof(uint32_t))) // the record CRC, already verified above
                return TerrainLayerSidecarStatus::Invalid;
        }

        if (degraded)
            return TerrainLayerSidecarStatus::Degraded;

        // VK-1647. Rebuild composition order from the `order` fields. They must be a permutation of
        // [0, layerCount): a gap or a duplicate would leave the stack order partly undefined, and
        // composition is order-dependent, so "partly undefined" means ground nobody authored. The
        // per-record CRCs have already passed at this point, so a bad set is not disk rot — it is a
        // writer that disagreed with this format, which is exactly what a version exists to catch.
        {
            std::vector<uint32_t> seen = decodedOrders;
            std::sort(seen.begin(), seen.end());
            for (size_t i = 0; i < seen.size(); ++i)
            {
                if (seen[i] != static_cast<uint32_t>(i))
                {
                    vfLogError("TerrainLayerSidecar: Layer order fields are not a permutation of "
                               "[0, {}) in {}", seen.size(), path.string());
                    return TerrainLayerSidecarStatus::Invalid;
                }
            }

            // Permutation-verified, so this is a pure scatter: record with order i lands at index i.
            std::vector<HeightLayerRecord> ordered(decodedLayers.size());
            for (size_t i = 0; i < decodedLayers.size(); ++i)
                ordered[decodedOrders[i]] = std::move(decodedLayers[i]);
            decodedLayers = std::move(ordered);
        }

        // VK-1647. Ids must be unique, checked HERE rather than by letting addLayer refuse one:
        // outStore is only ever touched once the whole file has proved sound (see above), and a
        // loop that bailed halfway would leave a partial stack whose tiles stay covered, because
        // coverage is sticky. The count is already bounded by parseSidecarHeader's
        // MAX_TERRAIN_EDIT_LAYERS check, so uniqueness is the only remaining store precondition.
        {
            std::unordered_set<uint64_t> ids;
            ids.reserve(decodedLayers.size());
            for (const HeightLayerRecord& record : decodedLayers)
            {
                if (!ids.insert(record.id).second)
                {
                    vfLogError("TerrainLayerSidecar: {} carries more than one layer with id {}; "
                               "ids are unique and never reused", path.string(), record.id);
                    return TerrainLayerSidecarStatus::Invalid;
                }
            }
        }

        struct DecodedBase
        {
            TileCoord coord;
            uint32_t vertexCount = 0;
            std::vector<float> heights;
        };

        std::vector<DecodedBase> decodedBases;
        decodedBases.reserve(baseCount);

        const uint64_t trailerOffset = bytes.size() - TERRAIN_LAYER_TRAILER_SIZE;
        ByteReader indexReader(bytes.data() + baseIndexOffset,
                               static_cast<size_t>(trailerOffset - baseIndexOffset));

        for (uint32_t i = 0; i < baseCount; ++i)
        {
            const int32_t x = indexReader.read<int32_t>();
            const int32_t z = indexReader.read<int32_t>();
            const uint64_t blockOffset = indexReader.read<uint64_t>();
            const uint32_t byteLength = indexReader.read<uint32_t>();
            const uint32_t vertexCount = indexReader.read<uint32_t>();
            const uint32_t blockCrc = indexReader.read<uint32_t>();
            if (!indexReader.good())
                return TerrainLayerSidecarStatus::Invalid;

            if (vertexCount == 0 || baseBlockByteLength(vertexCount) != byteLength)
                return TerrainLayerSidecarStatus::Invalid;
            if (blockOffset > trailerOffset || trailerOffset - blockOffset < byteLength)
                return TerrainLayerSidecarStatus::Invalid;

            const uint8_t* blockBytes = bytes.data() + blockOffset;
            if (resource::crc32(blockBytes, byteLength) != blockCrc)
                return TerrainLayerSidecarStatus::Invalid;

            DecodedBase decoded;
            decoded.coord = TileCoord(x, z);
            decoded.vertexCount = vertexCount;
            decoded.heights.resize(static_cast<size_t>(vertexCount) * vertexCount);
            std::memcpy(decoded.heights.data(), blockBytes, byteLength);
            decodedBases.push_back(std::move(decoded));
        }

        // Every precondition addLayer enforces — the cap, id uniqueness, an unlocked store — was
        // established above or holds by construction on a freshly loaded terrain, so this cannot
        // fail. Checked anyway: a silent drop here would be a layer the artist can no longer see
        // but whose tiles stay covered.
        for (HeightLayerRecord& record : decodedLayers)
        {
            const uint64_t id = record.id;
            if (!outStore.addLayer(std::move(record)))
            {
                vfLogError("TerrainLayerSidecar: {} could not be rebuilt — the store refused layer "
                           "{}", path.string(), id);
                return TerrainLayerSidecarStatus::Invalid;
            }
        }
        for (DecodedBase& decoded : decodedBases)
            outStore.adoptBase(decoded.coord, std::move(decoded.heights), decoded.vertexCount);

        // After the records, so addLayer's own watermark raise cannot pull it back down, and it is
        // raise-only anyway. A file written before any id was issued carries 1, the initial value.
        outStore.adoptNextLayerId(nextLayerId);

        return TerrainLayerSidecarStatus::Ok;
    }

    bool rebindTerrainLayerSidecar(const fs::path& path, uint64_t newTerrainGuid,
                                   uint64_t newGenerationId)
    {
        bool exists = false;
        std::vector<uint8_t> bytes = readWholeFile(path, exists);
        if (!exists)
            return false;

        TerrainLayerSidecarMeta meta;
        uint32_t layerCount = 0;
        uint32_t baseCount = 0;
        uint64_t layerTableOffset = 0;
        uint64_t baseIndexOffset = 0;
        uint64_t nextLayerId = 0;
        if (parseSidecarHeader(bytes, meta, layerCount, baseCount, layerTableOffset,
                               baseIndexOffset, nextLayerId) != TerrainLayerSidecarStatus::Ok ||
            !verifyTrailer(bytes))
        {
            vfLogWarning("TerrainLayerSidecar: {} is not a readable sidecar; not rebinding it",
                         path.string());
            return false;
        }

        const uint64_t guidLE = toLittleEndian(newTerrainGuid);
        const uint64_t genLE = toLittleEndian(newGenerationId);
        std::memcpy(bytes.data() + TERRAIN_LAYER_GUID_OFFSET, &guidLE, sizeof(uint64_t));
        std::memcpy(bytes.data() + TERRAIN_LAYER_GENERATION_ID_OFFSET, &genLE, sizeof(uint64_t));

        const size_t trailerOffset = bytes.size() - TERRAIN_LAYER_TRAILER_SIZE;
        const uint32_t crcLE = toLittleEndian(resource::crc32(bytes.data(), trailerOffset));
        std::memcpy(bytes.data() + trailerOffset, &crcLE, sizeof(uint32_t));

        // Written beside the live file and swapped in, not patched in place: the two header fields
        // and the trailing checksum are three separate ranges, and a crash between them would
        // leave a sidecar whose CRC disagrees with its own contents — indistinguishable from
        // corruption.
        fs::path tempPath = path;
        tempPath += ".tmp";
        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("TerrainLayerSidecar: Cannot create {}", tempPath.string());
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainLayerSidecar: Failed to write {}", tempPath.string());
                std::error_code ec;
                fs::remove(tempPath, ec);
                return false;
            }
        }

        return resource::replaceFileAtomically(tempPath, path);
    }
}
