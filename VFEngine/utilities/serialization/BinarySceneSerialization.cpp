#include "BinarySceneSerialization.hpp"
#include "SceneSerialization.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include "../resource/EndianUtils.hpp"

#include <fstream>
#include <cstring>

namespace serialization
{
	using json = nlohmann::json;

	bool BinarySceneSerialization::isBinaryScene(const std::vector<uint8_t>& data)
	{
		if (data.size() < HEADER_SIZE) return false;
		return std::memcmp(data.data(), MAGIC, 4) == 0;
	}

	bool BinarySceneSerialization::validateHeader(const Header& header, size_t dataSize)
	{
		if (std::memcmp(header.magic, MAGIC, 4) != 0)
		{
			vfLogError("BinaryScene: Invalid magic number");
			return false;
		}

		if (header.version != FORMAT_VERSION)
		{
			vfLogError("BinaryScene: Unsupported version {}, expected {}", header.version, FORMAT_VERSION);
			return false;
		}

		if (header.totalFileSize != dataSize)
		{
			vfLogWarning("BinaryScene: File size mismatch (header={}, actual={})", header.totalFileSize, dataSize);
		}

		if (header.payloadSize == 0 || HEADER_SIZE + header.payloadSize > dataSize)
		{
			vfLogError("BinaryScene: Invalid payload size {}", header.payloadSize);
			return false;
		}

		if (header.payloadSize > MAX_SCENE_SIZE)
		{
			vfLogError("BinaryScene: Payload exceeds maximum size ({} bytes)", header.payloadSize);
			return false;
		}

		return true;
	}

	json BinarySceneSerialization::decodePayload(const std::vector<uint8_t>& data)
	{
		// Public entry point — guard the header read (mirrors isBinaryScene / peekHeader) so a
		// short/truncated buffer can never over-read past the allocation via the memcpy below.
		if (data.size() < HEADER_SIZE) return json{};

		Header header{};
		std::memcpy(&header, data.data(), HEADER_SIZE);

		if (!validateHeader(header, data.size()))
		{
			return json{};
		}

		try
		{
			auto payloadBegin = data.begin() + HEADER_SIZE;
			auto payloadEnd = payloadBegin + header.payloadSize;
			return json::from_msgpack(payloadBegin, payloadEnd);
		}
		catch (const json::parse_error& e)
		{
			vfLogError("BinaryScene: MessagePack decode failed: {}", e.what());
			return json{};
		}
	}

	bool BinarySceneSerialization::peekHeader(const std::vector<uint8_t>& data, Header& outHeader)
	{
		if (data.size() < HEADER_SIZE) return false;
		if (std::memcmp(data.data(), MAGIC, 4) != 0) return false;
		std::memcpy(&outHeader, data.data(), HEADER_SIZE);
		return true;
	}

	bool BinarySceneSerialization::loadBinarySceneInto(const std::vector<uint8_t>& data,
	                                                    scene::SceneGraphSystem& sceneGraph,
	                                                    std::string_view filename,
	                                                    SceneLoadProgressCallback progressCallback)
	{
		json snapshot = decodePayload(data);
		if (snapshot.is_null())
		{
			return false;
		}

		return SceneSerialization::restoreFromSnapshot(snapshot, sceneGraph, filename, progressCallback);
	}

	bool BinarySceneSerialization::loadBinarySceneAdditive(const std::vector<uint8_t>& data,
	                                                       scene::SceneGraphSystem& sceneGraph,
	                                                       scene::Entity& containerParent,
	                                                       std::string_view,
	                                                       SceneLoadProgressCallback progressCallback)
	{
		json snapshot = decodePayload(data);
		if (snapshot.is_null() || !snapshot.contains("root"))
		{
			return false;
		}

		try
		{
			const auto& rootJson = snapshot["root"];

			if (rootJson.contains("children") && rootJson["children"].is_array())
			{
				size_t totalEntities = 0;
				for (const auto& childJson : rootJson["children"])
				{
					totalEntities += SceneSerialization::countEntities(childJson);
				}

				size_t entitiesLoaded = 0;
				DeserializeEntityContext ctx{sceneGraph, false, progressCallback,
				                             entitiesLoaded, totalEntities};

				for (const auto& childJson : rootJson["children"])
				{
					if (!childJson.is_object()) continue;

					std::string childName = childJson.value("name", "Unnamed");
					scene::Entity child(childName);

					if (!child.isValid())
					{
						vfLogError("Failed to create entity '{}' during additive binary scene load", childName);
						continue;
					}

					sceneGraph.addChild(containerParent, child);
					SceneSerialization::deserializeEntity(childJson, child, ctx);
				}
			}

			if (rootJson.contains("components"))
			{
				SceneSerialization::deserializeEntityComponents(rootJson["components"], containerParent);
			}

			return true;
		}
		catch (const std::exception& e)
		{
			vfLogError("BinaryScene: Additive load failed: {}", e.what());
			return false;
		}
	}

	bool BinarySceneSerialization::convertJsonToBinary(std::string_view jsonPath, std::string_view binaryPath,
	                                                   uint64_t sourceHash)
	{
		try
		{
			auto jsonData = resource::readFileBytes(std::string(jsonPath));
			if (jsonData.empty())
			{
				vfLogError("BinaryScene: Failed to read JSON scene: {}", jsonPath);
				return false;
			}

			json sceneJson = json::parse(jsonData.begin(), jsonData.end());

			if (!sceneJson.contains("root"))
			{
				vfLogError("BinaryScene: Invalid JSON scene (no root): {}", jsonPath);
				return false;
			}

			size_t entityCount = SceneSerialization::countEntities(sceneJson["root"]);
			std::vector<uint8_t> msgpack = json::to_msgpack(sceneJson);

			Header header{};
			std::memcpy(header.magic, MAGIC, 4);
			header.version = FORMAT_VERSION;
			header.flags = 0;
			header.entityCount = static_cast<uint32_t>(entityCount);
			header.payloadSize = static_cast<uint32_t>(msgpack.size());
			header.totalFileSize = HEADER_SIZE + msgpack.size();
			header.sourceHash = sourceHash;

			std::ofstream file(std::string(binaryPath), std::ios::binary);
			if (!file.is_open())
			{
				vfLogError("BinaryScene: Failed to create binary file: {}", binaryPath);
				return false;
			}

			file.write(reinterpret_cast<const char*>(&header), HEADER_SIZE);
			file.write(reinterpret_cast<const char*>(msgpack.data()),
			           static_cast<std::streamsize>(msgpack.size()));

			vfLogInfo("BinaryScene: Converted {} -> {} ({} entities, {} bytes)",
			          jsonPath, binaryPath, entityCount, header.totalFileSize);
			return true;
		}
		catch (const std::exception& e)
		{
			vfLogError("BinaryScene: Conversion failed for {}: {}", jsonPath, e.what());
			return false;
		}
	}
}
