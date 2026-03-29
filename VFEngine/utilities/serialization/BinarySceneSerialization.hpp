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
		static constexpr uint32_t FORMAT_VERSION = 1;
		static constexpr size_t HEADER_SIZE = 28;
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
		};
		#pragma pack(pop)

		static_assert(sizeof(Header) == HEADER_SIZE, "Header must be exactly 28 bytes");

		// Save scene as binary
		static bool saveBinaryScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename);

		// Load binary scene from pre-read bytes
		static bool loadBinarySceneInto(const std::vector<uint8_t>& data,
		                                 scene::SceneGraphSystem& sceneGraph,
		                                 SceneLoadProgressCallback progressCallback = nullptr);

		// Load binary scene additively from pre-read bytes
		static bool loadBinarySceneAdditive(const std::vector<uint8_t>& data,
		                                     scene::SceneGraphSystem& sceneGraph,
		                                     scene::Entity& containerParent,
		                                     SceneLoadProgressCallback progressCallback = nullptr);

		// Convert JSON .vfscene to binary (used by GameExporter)
		static bool convertJsonToBinary(std::string_view jsonPath, std::string_view binaryPath);

		// Check if data starts with binary magic
		static bool isBinaryScene(const std::vector<uint8_t>& data);

	private:
		static bool validateHeader(const Header& header, size_t dataSize);
		static nlohmann::json decodePayload(const std::vector<uint8_t>& data);
	};
}
