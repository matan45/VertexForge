#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast
#include <algorithm>

namespace resource {

	// Helper to read and validate header, returns true on success
	static bool readMeshHeader(std::ifstream& inFile, StreamingMeshesData& result) {
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		result.headerFileType = static_cast<FileType>(headerFileType);

		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return false;
		}

		result.numberOfMeshes = endian::readLE<uint32_t>(inFile);
		result.meshes.resize(result.numberOfMeshes);

		return !inFile.fail();
	}

	// Helper to read a single vertex
	static Vertex readVertex(std::ifstream& inFile) {
		Vertex v;
		v.position.x = endian::readLE<float>(inFile);
		v.position.y = endian::readLE<float>(inFile);
		v.position.z = endian::readLE<float>(inFile);
		v.normal.x = endian::readLE<float>(inFile);
		v.normal.y = endian::readLE<float>(inFile);
		v.normal.z = endian::readLE<float>(inFile);
		v.texCoords.x = endian::readLE<float>(inFile);
		v.texCoords.y = endian::readLE<float>(inFile);
		return v;
	}

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

	StreamingMeshesData MeshResource::analyzeMeshFile(std::string_view path)
	{
		StreamingMeshesData result;

		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile) {
			vfLogError("Failed to open file for analysis: {}", path);
			return result;
		}

		if (!readMeshHeader(inFile, result)) {
			vfLogError("Failed to read mesh header: {}", path);
			return result;
		}

		// Read vertex/index counts for each mesh without loading data
		for (uint32_t meshIdx = 0; meshIdx < result.numberOfMeshes; ++meshIdx) {
			auto& meshData = result.meshes[meshIdx];

			// Read vertex count
			uint32_t vertexCount = endian::readLE<uint32_t>(inFile);
			meshData.totalVertices = vertexCount;

			// Skip vertex data (32 bytes per vertex: 3 floats pos + 3 floats normal + 2 floats uv)
			constexpr size_t vertexSize = sizeof(float) * 8;
			inFile.seekg(static_cast<std::streamoff>(vertexCount * vertexSize), std::ios::cur);

			// Read index count
			uint32_t indexCount = endian::readLE<uint32_t>(inFile);
			meshData.totalIndices = indexCount;

			// Skip index data
			inFile.seekg(static_cast<std::streamoff>(indexCount * sizeof(uint32_t)), std::ios::cur);

			if (inFile.fail()) {
				vfLogError("Failed to analyze mesh {} in file: {}", meshIdx, path);
				meshData.state = MeshLoadState::Error;
				return result;
			}

			meshData.state = MeshLoadState::NotStarted;
		}

		return result;
	}

	StreamingMeshesData MeshResource::loadMeshStreaming(
		std::string_view path,
		MeshChunkCallback onChunk,
		MeshLoadCompleteCallback onComplete)
	{
		StreamingMeshesData result;

		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile) {
			vfLogError("Failed to open file for streaming: {}", path);
			return result;
		}

		if (!readMeshHeader(inFile, result)) {
			vfLogError("Failed to read mesh header: {}", path);
			return result;
		}

		// Calculate vertices per chunk based on chunkSize
		const size_t verticesPerChunk = chunkSize / sizeof(Vertex);  // ~8192 vertices per 256KB
		const size_t indicesPerChunk = chunkSize / sizeof(uint32_t);  // ~65536 indices per 256KB

		for (uint32_t meshIdx = 0; meshIdx < result.numberOfMeshes; ++meshIdx) {
			auto& meshData = result.meshes[meshIdx];
			meshData.state = MeshLoadState::Loading;
			bool meshFailed = false;

			// Read vertex count
			uint32_t vertexCount = endian::readLE<uint32_t>(inFile);
			meshData.totalVertices = vertexCount;

			if (inFile.fail()) {
				vfLogError("Failed to read vertex count for mesh {}", meshIdx);
				meshData.state = MeshLoadState::Error;
				if (onComplete) onComplete(meshIdx, false);
				continue;
			}

			// Stream vertices in chunks
			std::vector<Vertex> chunkBuffer;
			chunkBuffer.reserve(verticesPerChunk);

			uint32_t verticesLoaded = 0;
			while (verticesLoaded < vertexCount && !meshFailed) {
				// Calculate how many vertices in this chunk
				uint32_t chunkVertexCount = static_cast<uint32_t>(
					std::min(verticesPerChunk, static_cast<size_t>(vertexCount - verticesLoaded))
				);

				chunkBuffer.resize(chunkVertexCount);

				// Read vertices for this chunk
				for (uint32_t i = 0; i < chunkVertexCount; ++i) {
					chunkBuffer[i] = readVertex(inFile);

					if (inFile.fail()) {
						vfLogError("Failed to read vertex {} of mesh {}", verticesLoaded + i, meshIdx);
						meshData.state = MeshLoadState::Error;
						if (onComplete) onComplete(meshIdx, false);
						meshFailed = true;
						break;
					}
				}

				if (meshFailed) break;

				// Invoke callback with vertex chunk
				if (onChunk) {
					onChunk(meshIdx, chunkBuffer, {}, verticesLoaded, 0);
				}

				verticesLoaded += chunkVertexCount;

				// Clear chunk buffer to release memory (but keep capacity)
				chunkBuffer.clear();
			}

			if (meshFailed) continue;

			// Read index count
			uint32_t indexCount = endian::readLE<uint32_t>(inFile);
			meshData.totalIndices = indexCount;

			if (inFile.fail()) {
				vfLogError("Failed to read index count for mesh {}", meshIdx);
				meshData.state = MeshLoadState::Error;
				if (onComplete) onComplete(meshIdx, false);
				continue;
			}

			// Stream indices in chunks
			std::vector<uint32_t> indexBuffer;
			indexBuffer.reserve(indicesPerChunk);

			uint32_t indicesLoaded = 0;
			while (indicesLoaded < indexCount && !meshFailed) {
				uint32_t chunkIndexCount = static_cast<uint32_t>(
					std::min(indicesPerChunk, static_cast<size_t>(indexCount - indicesLoaded))
				);

				indexBuffer.resize(chunkIndexCount);
				endian::readVectorLE<uint32_t>(inFile, indexBuffer, chunkIndexCount);

				if (inFile.fail()) {
					vfLogError("Failed to read indices for mesh {}", meshIdx);
					meshData.state = MeshLoadState::Error;
					if (onComplete) onComplete(meshIdx, false);
					meshFailed = true;
					break;
				}

				// Invoke callback with index chunk
				if (onChunk) {
					onChunk(meshIdx, {}, indexBuffer, 0, indicesLoaded);
				}

				indicesLoaded += chunkIndexCount;
				indexBuffer.clear();
			}

			if (meshFailed) continue;

			meshData.state = MeshLoadState::Complete;
			if (onComplete) onComplete(meshIdx, true);
		}

		return result;
	}
}

