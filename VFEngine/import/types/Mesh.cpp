#include "Mesh.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <bit>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <cfloat>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>

namespace types {
	void Mesh::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
	                        std::string_view location, MeshProgressCallback progressCallback) const
	{
		// Report 0% - starting Assimp load
		if (progressCallback) progressCallback(0.0f);

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(file.path.data(),
			aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
			vfLogError("Failed to load Mesh file: {}", importer.GetErrorString());
			return;
		}

		// Report 20% - Assimp loading complete, starting LOD generation and file write
		if (progressCallback) progressCallback(0.2f);

		saveToFileStreamingWithLOD(location, fileName, scene, progressCallback);

		// Report 100% - complete
		if (progressCallback) progressCallback(1.0f);
	}

	LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh) const
	{
		LODMeshData result;
		result.vertices.reserve(assimpMesh->mNumVertices);

		// Convert vertices
		for (unsigned int v = 0; v < assimpMesh->mNumVertices; ++v) {
			resource::Vertex vertex;
			vertex.position = {
				assimpMesh->mVertices[v].x,
				assimpMesh->mVertices[v].y,
				assimpMesh->mVertices[v].z
			};

			if (assimpMesh->HasNormals()) {
				vertex.normal = {
					assimpMesh->mNormals[v].x,
					assimpMesh->mNormals[v].y,
					assimpMesh->mNormals[v].z
				};
			}
			else {
				vertex.normal = { 0.0f, 0.0f, 0.0f };
			}

			if (assimpMesh->mTextureCoords[0]) {
				vertex.texCoords = {
					assimpMesh->mTextureCoords[0][v].x,
					assimpMesh->mTextureCoords[0][v].y
				};
			}
			else {
				vertex.texCoords = { 0.0f, 0.0f };
			}

			result.vertices.push_back(vertex);
		}

		// Convert indices
		uint32_t totalIndices = 0;
		for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f) {
			totalIndices += assimpMesh->mFaces[f].mNumIndices;
		}
		result.indices.reserve(totalIndices);

		for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f) {
			const aiFace& face = assimpMesh->mFaces[f];
			for (unsigned int k = 0; k < face.mNumIndices; ++k) {
				result.indices.push_back(face.mIndices[k]);
			}
		}

		return result;
	}

	LODMeshData Mesh::simplifyMesh(const LODMeshData& source, float targetRatio) const
	{
		if (source.indices.empty() || source.vertices.empty()) {
			return source;
		}

		// If ratio is 1.0, just return a copy
		if (targetRatio >= 1.0f) {
			return source;
		}

		size_t targetIndexCount = static_cast<size_t>(source.indices.size() * targetRatio);
		// Ensure at least 3 indices (one triangle)
		targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
		// Round to multiple of 3
		targetIndexCount = (targetIndexCount / 3) * 3;

		LODMeshData result;
		result.indices.resize(source.indices.size()); // Allocate max size initially

		// Use meshoptimizer's sloppy simplification for guaranteed reduction
		// This is more aggressive and will reach the target index count
		size_t actualIndexCount = meshopt_simplifySloppy(
			result.indices.data(),
			source.indices.data(),
			source.indices.size(),
			reinterpret_cast<const float*>(source.vertices.data()),
			source.vertices.size(),
			sizeof(resource::Vertex),
			targetIndexCount,
			FLT_MAX,  // No error limit - allow maximum simplification
			nullptr   // No result error output needed
		);

		result.indices.resize(actualIndexCount);

		// If no simplification was possible at all, return original
		if (actualIndexCount == source.indices.size()) {
			vfLogWarning("  Simplification failed for ratio {:.1f}%, keeping original", targetRatio * 100.0f);
			return source;
		}

		// Optimize vertex cache for better GPU performance
		meshopt_optimizeVertexCache(
			result.indices.data(),
			result.indices.data(),
			result.indices.size(),
			source.vertices.size()
		);

		// Create a compact vertex buffer with only referenced vertices
		// First, find all unique vertex indices used
		std::vector<unsigned int> remap(source.vertices.size(), ~0u);
		size_t uniqueVertexCount = 0;

		for (size_t i = 0; i < result.indices.size(); ++i) {
			uint32_t idx = result.indices[i];
			if (remap[idx] == ~0u) {
				remap[idx] = static_cast<unsigned int>(uniqueVertexCount++);
			}
		}

		// Create compacted vertex buffer
		result.vertices.resize(uniqueVertexCount);
		for (size_t i = 0; i < source.vertices.size(); ++i) {
			if (remap[i] != ~0u) {
				result.vertices[remap[i]] = source.vertices[i];
			}
		}

		// Remap indices to use new vertex indices
		for (size_t i = 0; i < result.indices.size(); ++i) {
			result.indices[i] = remap[result.indices[i]];
		}

		return result;
	}

	std::array<LODMeshData, resource::LOD_LEVEL_COUNT> Mesh::generateLODLevels(const LODMeshData& lod0) const
	{
		std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lodLevels;

		// LOD0: Original mesh (100%)
		lodLevels[0] = lod0;

		// Generate LOD1, LOD2, LOD3 by simplifying from LOD0
		for (uint32_t level = 1; level < resource::LOD_LEVEL_COUNT; ++level) {
			lodLevels[level] = simplifyMesh(lod0, lodRatios[level]);

			vfLogInfo("  LOD{}: {} vertices, {} triangles ({}%)",
			          level,
			          lodLevels[level].vertices.size(),
			          lodLevels[level].indices.size() / 3,
			          static_cast<int>(lodRatios[level] * 100));
		}

		return lodLevels;
	}

	void Mesh::writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const
	{
		constexpr size_t verticesPerChunk = chunkSize / sizeof(resource::Vertex);

		// Write vertex count
		resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.vertices.size()));

		// Write vertices in chunks
		for (size_t v = 0; v < lodMesh.vertices.size(); v += verticesPerChunk) {
			size_t chunkEnd = std::min(v + verticesPerChunk, lodMesh.vertices.size());

			for (size_t j = v; j < chunkEnd; ++j) {
				const auto& vertex = lodMesh.vertices[j];
				resource::endian::writeLE<float>(outFile, vertex.position.x);
				resource::endian::writeLE<float>(outFile, vertex.position.y);
				resource::endian::writeLE<float>(outFile, vertex.position.z);
				resource::endian::writeLE<float>(outFile, vertex.normal.x);
				resource::endian::writeLE<float>(outFile, vertex.normal.y);
				resource::endian::writeLE<float>(outFile, vertex.normal.z);
				resource::endian::writeLE<float>(outFile, vertex.texCoords.x);
				resource::endian::writeLE<float>(outFile, vertex.texCoords.y);
			}
		}

		// Write index count
		resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.indices.size()));

		// Write indices in chunks
		constexpr size_t indicesPerChunk = chunkSize / sizeof(uint32_t);
		for (size_t i = 0; i < lodMesh.indices.size(); i += indicesPerChunk) {
			size_t chunkEnd = std::min(i + indicesPerChunk, lodMesh.indices.size());
			std::vector<uint32_t> indexChunk(lodMesh.indices.begin() + i, lodMesh.indices.begin() + chunkEnd);
			resource::endian::writeVectorLE<uint32_t>(outFile, indexChunk);
		}
	}

	void Mesh::saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
	                                      const aiScene* scene, MeshProgressCallback progressCallback) const
	{
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." + FileExtension::mesh);
		std::ofstream outFile(newFileLocation, std::ios::binary);

		if (!outFile) {
			vfLogError("Failed to open file for writing: {}", newFileLocation.string());
			return;
		}

		// Write header with updated version (0.0.3 for LOD support)
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
		resource::endian::writeLE<uint32_t>(outFile, 0); // major
		resource::endian::writeLE<uint32_t>(outFile, 0); // minor
		resource::endian::writeLE<uint32_t>(outFile, 3); // patch - version 0.0.3 for LOD
		resource::endian::writeLE<uint32_t>(outFile, scene->mNumMeshes);

		vfLogInfo("Generating LODs for {} submeshes...", scene->mNumMeshes);

		// Process each mesh
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
			const aiMesh* assimpMesh = scene->mMeshes[i];
			std::string meshName = assimpMesh->mName.C_Str();

			vfLogInfo("Processing submesh '{}' ({} vertices, {} triangles)...",
			          meshName,
			          assimpMesh->mNumVertices,
			          assimpMesh->mNumFaces);

			// Write submesh name
			uint32_t nameLength = static_cast<uint32_t>(meshName.length());
			resource::endian::writeLE<uint32_t>(outFile, nameLength);
			if (nameLength > 0) {
				outFile.write(meshName.data(), nameLength);
			}

			// Write LOD level count
			resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);

			// Convert Assimp mesh to LODMeshData (LOD0)
			LODMeshData lod0 = convertAssimpMesh(assimpMesh);

			// Generate all LOD levels
			auto lodLevels = generateLODLevels(lod0);

			// Write each LOD level
			for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod) {
				writeLODLevel(outFile, lodLevels[lod]);
			}

			// Report progress: 20% + (i+1)/totalMeshes * 80%
			if (progressCallback) {
				float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.8f;
				progressCallback(progress);
			}
		}

		outFile.close();
		vfLogInfo("Mesh with LOD saved to: {}", newFileLocation.string());
	}
}
