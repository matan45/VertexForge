#pragma once
#include <string>
#include <fstream>
#include <functional>
#include <array>
#include "config/Config.hpp"
#include "resource/Types.hpp"
struct aiScene;
struct aiMesh;

namespace types {

	using MeshProgressCallback = std::function<void(float progress)>;

	// Internal structure for LOD mesh data during import
	struct LODMeshData {
		std::vector<resource::Vertex> vertices;
		std::vector<uint32_t> indices;
	};

	class Mesh
	{
	public:
		void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
		                  std::string_view location, MeshProgressCallback progressCallback = nullptr) const;

	private:
		// Save mesh with LOD levels
		void saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
		                                const aiScene* scene, MeshProgressCallback progressCallback) const;

		// Convert aiMesh to LODMeshData
		LODMeshData convertAssimpMesh(const aiMesh* assimpMesh) const;

		// Generate all 4 LOD levels from the original mesh
		std::array<LODMeshData, resource::LOD_LEVEL_COUNT> generateLODLevels(const LODMeshData& lod0) const;

		// Simplify mesh to target ratio using meshoptimizer
		LODMeshData simplifyMesh(const LODMeshData& source, float targetRatio) const;

		// Write a single LOD level to file
		void writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const;

		// Chunk size for streaming (256KB)
		static constexpr size_t chunkSize = 256 * 1024;

		// LOD target ratios (percentage of original triangles)
		static constexpr std::array<float, resource::LOD_LEVEL_COUNT> lodRatios = { 1.0f, 0.5f, 0.25f, 0.125f };
	};

}


