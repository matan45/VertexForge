#pragma once
#include <string>
#include "Types.hpp"

namespace resource {
	class MeshResource
	{
	private:
		// Safety limits to prevent excessive memory allocation from malformed files
		// 10M vertices = ~320MB (32 bytes/vertex), reasonable for modern GPUs
		static constexpr uint32_t maxVertexCount = 10'000'000;
		// 30M indices = ~120MB (4 bytes/index), allows ~10M triangles per mesh
		static constexpr uint32_t maxIndexCount = 30'000'000;

	public:
		// Load mesh file and return all mesh data
		static MeshesData loadMesh(std::string_view path);
	};
}

