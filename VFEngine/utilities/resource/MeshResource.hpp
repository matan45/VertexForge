#pragma once
#include <string>
#include "Types.hpp"

namespace resource {
	class MeshResource
	{
	private:
		inline static const size_t chunkSize = 256 * 1024;  // 256KB chunks (~8K vertices)

		// Safety limits to prevent excessive memory allocation from malformed files
		// 10M vertices = ~320MB (32 bytes/vertex), reasonable for modern GPUs
		static constexpr uint32_t maxVertexCount = 10'000'000;
		// 30M indices = ~120MB (4 bytes/index), allows ~10M triangles per mesh
		static constexpr uint32_t maxIndexCount = 30'000'000;
		// Vertex size: 3 floats pos + 3 floats normal + 2 floats uv = 32 bytes
		static constexpr size_t vertexSize = sizeof(float) * 8;
	public:
		// Legacy: loads entire mesh into CPU memory (for small meshes)
		static MeshesData loadMesh(std::string_view path);

		// Streaming: loads mesh in chunks, calling onChunk for each chunk
		// CPU memory usage limited to chunkSize regardless of mesh size
		static StreamingMeshesData loadMeshStreaming(
			std::string_view path,
			MeshChunkCallback onChunk,
			MeshLoadCompleteCallback onComplete = nullptr
		);

		// Analyze file to get mesh sizes without loading data
		// Use for pre-allocating GPU buffers before streaming
		static StreamingMeshesData analyzeMeshFile(std::string_view path);
	};
}

