#pragma once

#include "SerializationExport.hpp"
#include <string_view>
#include <vector>
#include <cstdint>
#include "SceneSerialization.hpp"

namespace scene { class SceneGraphSystem; }

namespace serialization
{
	class VF_SERIALIZATION_API BinarySceneSerialization
	{
	public:
		static constexpr char MAGIC[4] = {'V', 'F', 'B', 'S'};
		// v2 (VK-1538): added Header::sourceHash for content-hash invalidation.
		// A blob is a derived cache of a JSON scene, always regenerable, so there
		// is no supported-version window — an older blob is rejected and rebuilt.
		static constexpr uint32_t FORMAT_VERSION = 2;
		static constexpr size_t HEADER_SIZE = 36;
		static constexpr size_t MAX_SCENE_SIZE = 512 * 1024 * 1024;

		#pragma pack(push, 1)
		struct Header
		{
			char magic[4];
			uint32_t version;
			uint32_t flags;
			uint32_t entityCount;
			uint64_t totalFileSize;
			uint32_t payloadSize;
			uint64_t sourceHash;  // FNV-1a of the source JSON; 0 when unknown
		};
		#pragma pack(pop)

		static_assert(sizeof(Header) == HEADER_SIZE, "Header must be exactly 36 bytes");

		// Load binary scene from pre-read bytes
		static bool loadBinarySceneInto(const std::vector<uint8_t>& data,
		                                 scene::SceneGraphSystem& sceneGraph,
		                                 std::string_view filename = {},
		                                 SceneLoadProgressCallback progressCallback = nullptr);

		// Load binary scene additively from pre-read bytes
		static bool loadBinarySceneAdditive(const std::vector<uint8_t>& data,
		                                     scene::SceneGraphSystem& sceneGraph,
		                                     scene::Entity& containerParent,
		                                     std::string_view filename = {},
		                                     SceneLoadProgressCallback progressCallback = nullptr);

		// Convert JSON .vfscene to binary (used by GameExporter). sourceHash is the
		// content hash of the source JSON, stored in the header for invalidation.
		static bool convertJsonToBinary(std::string_view jsonPath, std::string_view binaryPath,
		                                uint64_t sourceHash);

		// Check if data starts with binary magic
		static bool isBinaryScene(const std::vector<uint8_t>& data);

		// Copy the header out of pre-read bytes if the magic matches (no version
		// check — the caller decides what versions/hashes it accepts). Used by the
		// exporter to decide whether a cached blob can be reused.
		static bool peekHeader(const std::vector<uint8_t>& data, Header& outHeader);

		// Decode the MessagePack payload into a scene snapshot (json{} on failure).
		// Public so the incremental loader can decode a blob then reuse the shared
		// DFS-stack path (VK-1268).
		static nlohmann::json decodePayload(const std::vector<uint8_t>& data);

	private:
		static bool validateHeader(const Header& header, size_t dataSize);
	};
}
