#pragma once
#include <string>
#include "Types.hpp"

namespace resource {
	class MeshResource
	{
	private:
		inline static const size_t chunkSize = 256 * 1024;  // 256KB chunks (~8K vertices)
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

