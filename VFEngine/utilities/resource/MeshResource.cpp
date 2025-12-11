#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast
#include <algorithm>
#include <limits>

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

	StreamingMeshesData MeshResource::analyzeMeshFile(std::string_view path)
	{
		StreamingMeshesData result;

		std::string filePath(path);
		std::ifstream inFile(filePath, std::ios::binary);
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

			// Validate vertex count and check for overflow before seeking
			if (vertexCount > maxVertexCount) {
				vfLogError("Vertex count {} exceeds maximum limit {} in mesh {}", vertexCount, maxVertexCount, meshIdx);
				meshData.state = MeshLoadState::Error;
				return result;
			}

			// Calculate seek offset safely (vertexCount <= maxVertexCount ensures no overflow)
			// Skip vertex data (32 bytes per vertex: 3 floats pos + 3 floats normal + 2 floats uv)
			size_t vertexDataSize = static_cast<size_t>(vertexCount) * vertexSize;
			if (vertexDataSize > static_cast<size_t>(std::numeric_limits<std::streamoff>::max())) {
				vfLogError("Vertex data size too large for file seek in mesh {}", meshIdx);
				meshData.state = MeshLoadState::Error;
				return result;
			}
			inFile.seekg(static_cast<std::streamoff>(vertexDataSize), std::ios::cur);

			// Read index count
			uint32_t indexCount = endian::readLE<uint32_t>(inFile);
			meshData.totalIndices = indexCount;

			// Validate index count and check for overflow before seeking
			if (indexCount > maxIndexCount) {
				vfLogError("Index count {} exceeds maximum limit {} in mesh {}", indexCount, maxIndexCount, meshIdx);
				meshData.state = MeshLoadState::Error;
				return result;
			}

			// Calculate seek offset safely
			size_t indexDataSize = static_cast<size_t>(indexCount) * sizeof(uint32_t);
			if (indexDataSize > static_cast<size_t>(std::numeric_limits<std::streamoff>::max())) {
				vfLogError("Index data size too large for file seek in mesh {}", meshIdx);
				meshData.state = MeshLoadState::Error;
				return result;
			}
			inFile.seekg(static_cast<std::streamoff>(indexDataSize), std::ios::cur);

			if (inFile.fail()) {
				vfLogError("Failed to analyze mesh {} in file: {}", meshIdx, path);
				meshData.state = MeshLoadState::Error;
				return result;
			}

			meshData.state = MeshLoadState::NotStarted;
		}

		return result;
	}

	MeshesData MeshResource::loadMeshStreaming(std::string_view path)
	{
		MeshesData result;

		// First, analyze the file to get mesh sizes for pre-allocation
		auto analysis = analyzeMeshFile(path);
		if (analysis.numberOfMeshes == 0) {
			vfLogError("Failed to analyze mesh file or file is empty: {}", path);
			return result;
		}

		// Pre-allocate result structure based on analysis
		result.headerFileType = analysis.headerFileType;
		result.numberOfMeshes = analysis.numberOfMeshes;
		result.meshes.resize(analysis.numberOfMeshes);

		// Pre-allocate vertex and index buffers for each mesh
		for (uint32_t i = 0; i < analysis.numberOfMeshes; ++i) {
			result.meshes[i].vertices.reserve(analysis.meshes[i].totalVertices);
			result.meshes[i].indices.reserve(analysis.meshes[i].totalIndices);
		}

		// Use streaming loader with callback to fill pre-allocated buffers
		auto onChunk = [&result](uint32_t meshIdx,
		                         const std::vector<Vertex>& vertices,
		                         const std::vector<uint32_t>& indices,
		                         uint32_t /*vertexOffset*/,
		                         uint32_t /*indexOffset*/) {
			if (meshIdx < result.meshes.size()) {
				auto& meshData = result.meshes[meshIdx];

				// Append vertices
				if (!vertices.empty()) {
					meshData.vertices.insert(meshData.vertices.end(),
					                         vertices.begin(), vertices.end());
				}

				// Append indices
				if (!indices.empty()) {
					meshData.indices.insert(meshData.indices.end(),
					                        indices.begin(), indices.end());
				}
			}
		};

		// Load using streaming with our accumulation callback
		loadMeshStreaming(path, onChunk);

		return result;
	}

	StreamingMeshesData MeshResource::loadMeshStreaming(
		std::string_view path,
		MeshChunkCallback onChunk)
	{
		StreamingMeshesData result;

		std::string filePath(path);
		std::ifstream inFile(filePath, std::ios::binary);
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
		}

		return result;
	}
}

