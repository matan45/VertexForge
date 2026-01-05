#pragma once
#include <string>
#include "Types.hpp"
#include "MeshletTypes.hpp"

namespace resource {
	class MeshResource
	{
	private:
		// Safety limits to prevent excessive memory allocation from malformed files
		// 10M vertices = ~320MB (32 bytes/vertex), reasonable for modern GPUs
		static constexpr uint32_t maxVertexCount = 10'000'000;
		// 30M indices = ~120MB (4 bytes/index), allows ~10M triangles per mesh
		static constexpr uint32_t maxIndexCount = 30'000'000;
		// 1M meshlets per submesh maximum
		static constexpr uint32_t maxMeshletCount = 1'000'000;

	public:
		// Load mesh file and return all mesh data (legacy, no meshlet data)
		static MeshesData loadMesh(std::string_view path);

		// Load mesh file with meshlet data (v0.0.4+ format)
		static MeshesDataWithMeshlets loadMeshWithMeshlets(std::string_view path);
	};
}

