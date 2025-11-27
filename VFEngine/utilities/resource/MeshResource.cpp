#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast

namespace resource {

	MeshesData MeshResource::loadMesh(std::string_view path)
	{
		resource::MeshesData meshesData;
		// Open the file in binary mode
		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile) {
			vfLogError("Failed to open file for reading: ", path);
			return {}; // Return an empty audioData on failure
		}

		// Read the header file type (endian-safe)
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		meshesData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version information (endian-safe)
		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		// Validate version compatibility
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read the number of meshes (endian-safe)
		uint32_t numberOfMeshes = endian::readLE<uint32_t>(inFile);
		meshesData.numberOfMeshes = numberOfMeshes;
		meshesData.meshes.resize(numberOfMeshes);

		for (uint32_t i = 0; i < numberOfMeshes; i++)
		{
			MeshData meshData;
			uint32_t vertexCount = endian::readLE<uint32_t>(inFile);
			
			// Validate vertex count to prevent excessive memory allocation
			if (vertexCount > 10000000) { // 10M vertices seems reasonable limit
				vfLogError("Vertex count {} exceeds maximum limit", vertexCount);
				return {};
			}
			
			// Read vertex data (endian-safe)
			meshData.vertices.resize(vertexCount);
			
			// For vertex data, we need to handle each component (glm::vec3, glm::vec2 contain floats)
			for (uint32_t v = 0; v < vertexCount; ++v) {
				// Read position (3 floats)
				meshData.vertices[v].position.x = endian::readLE<float>(inFile);
				meshData.vertices[v].position.y = endian::readLE<float>(inFile);
				meshData.vertices[v].position.z = endian::readLE<float>(inFile);
				
				// Read normal (3 floats)
				meshData.vertices[v].normal.x = endian::readLE<float>(inFile);
				meshData.vertices[v].normal.y = endian::readLE<float>(inFile);
				meshData.vertices[v].normal.z = endian::readLE<float>(inFile);
				
				// Read texture coordinates (2 floats)
				meshData.vertices[v].texCoords.x = endian::readLE<float>(inFile);
				meshData.vertices[v].texCoords.y = endian::readLE<float>(inFile);
				
				if (inFile.fail()) {
					vfLogError("Failed to read vertex {} from file", v);
					return {};
				}
			}
			// Read indices (endian-safe)
			uint32_t indexCount = endian::readLE<uint32_t>(inFile);
			
			// Validate index count to prevent excessive memory allocation
			if (indexCount > 30000000) { // 30M indices seems reasonable limit
				vfLogError("Index count {} exceeds maximum limit", indexCount);
				return {};
			}
			
			// Read index data using endian-safe vector read
			endian::readVectorLE<uint32_t>(inFile, meshData.indices, indexCount);
			
			if (inFile.fail()) {
				vfLogError("Failed to read index data from file");
				return {};
			}
			
			// Validate that we read the expected amount of data
			if (meshData.vertices.size() != vertexCount || meshData.indices.size() != indexCount) {
				vfLogError("Mesh data size mismatch: expected {} vertices and {} indices, got {} and {}",
					vertexCount, indexCount, meshData.vertices.size(), meshData.indices.size());
				return {};
			}
			
			meshesData.meshes.emplace_back(std::move(meshData));
		}

		return meshesData;
	}
}

