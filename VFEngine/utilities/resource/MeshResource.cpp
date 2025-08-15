#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"

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

		// Read the header file type
		uint8_t headerFileType;
		inFile.read(std::bit_cast<char*>(&headerFileType), sizeof(headerFileType));
		meshesData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version
		// Read the version information
		uint32_t majorVersion, minorVersion, patchVersion;
		inFile.read(std::bit_cast<char*>(&majorVersion), sizeof(majorVersion));
		inFile.read(std::bit_cast<char*>(&minorVersion), sizeof(minorVersion));
		inFile.read(std::bit_cast<char*>(&patchVersion), sizeof(patchVersion));

		// Validate version compatibility
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read the number of meshes
		uint32_t numberOfMeshes = 0;
		inFile.read(std::bit_cast<char*>(&numberOfMeshes), sizeof(numberOfMeshes));
		meshesData.numberOfMeshes = numberOfMeshes;
		meshesData.meshes.resize(numberOfMeshes);

		for (uint32_t i = 0; i < numberOfMeshes; i++)
		{
			MeshData meshData;
			uint32_t vertexCount = 0;
			inFile.read(std::bit_cast<char*>(&vertexCount), sizeof(uint32_t));
			
			// Validate vertex count to prevent excessive memory allocation
			if (vertexCount > 10000000) { // 10M vertices seems reasonable limit
				vfLogError("Vertex count {} exceeds maximum limit", vertexCount);
				return {};
			}
			
			meshData.vertices.resize(vertexCount); // ✅ FIXED: Use resize() not reserve()
			
			size_t verticesProcessed = 0;
			while (verticesProcessed < vertexCount) {
				size_t chunkToRead = std::min(chunkSize / sizeof(resource::Vertex), vertexCount - verticesProcessed);
				inFile.read(std::bit_cast<char*>(meshData.vertices.data() + verticesProcessed),
					chunkToRead * sizeof(resource::Vertex));
				verticesProcessed += chunkToRead;

				// Check for stream failure
				if (inFile.fail()) {
					vfLogError("Failed to read vertex data from file.");
					return {};
				}
			}
			// Read indices
			uint32_t indexCount = 0;
			inFile.read(std::bit_cast<char*>(&indexCount), sizeof(uint32_t));
			
			// Validate index count to prevent excessive memory allocation
			if (indexCount > 30000000) { // 30M indices seems reasonable limit
				vfLogError("Index count {} exceeds maximum limit", indexCount);
				return {};
			}
			
			meshData.indices.resize(indexCount); // ✅ FIXED: Use resize() not reserve()
			// Read the index data in chunks
			size_t indicesProcessed = 0;
			while (indicesProcessed < indexCount) {
				size_t chunkToRead = std::min(chunkSize / sizeof(uint32_t), static_cast<size_t>(indexCount) - indicesProcessed);
				inFile.read(std::bit_cast<char*>(meshData.indices.data() + indicesProcessed),
					chunkToRead * sizeof(uint32_t));
				indicesProcessed += chunkToRead;

				if (inFile.fail()) {
					vfLogError("Failed to read index data from file.");
					return {};
				}
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

