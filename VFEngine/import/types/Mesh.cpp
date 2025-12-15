#include "Mesh.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <bit>  // For std::bit_cast
#include <filesystem>
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

		// Report 30% - Assimp loading complete, starting file write
		if (progressCallback) progressCallback(0.3f);

		//TODO also extract the animation each animation to single file
		// and extract texture that embedded
		saveToFileStreaming(location, fileName, scene, progressCallback);

		// Report 100% - complete
		if (progressCallback) progressCallback(1.0f);
	}

	void Mesh::saveToFileStreaming(std::string_view location, std::string_view fileName,
	                               const aiScene* scene, MeshProgressCallback progressCallback) const
	{
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." + FileExtension::mesh);
		std::ofstream outFile(newFileLocation, std::ios::binary);

		if (!outFile) {
			vfLogError("Failed to open file for writing: {}", newFileLocation.string());
			return;
		}

		// Write header
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
		resource::endian::writeLE<uint32_t>(outFile, Version::major);
		resource::endian::writeLE<uint32_t>(outFile, Version::minor);
		resource::endian::writeLE<uint32_t>(outFile, Version::patch);
		resource::endian::writeLE<uint32_t>(outFile, scene->mNumMeshes);

		// Process each mesh one at a time (not all at once)
		for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
			writeMeshChunked(outFile, scene->mMeshes[i]);

			// Report progress: 30% + (i+1)/totalMeshes * 70%
			if (progressCallback) {
				float progress = 0.3f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.7f;
				progressCallback(progress);
			}
		}

		outFile.close();
	}

	void Mesh::writeMeshChunked(std::ofstream& outFile, const aiMesh* assimpMesh) const
	{
		constexpr size_t verticesPerChunk = chunkSize / sizeof(resource::Vertex);  // ~8K vertices per chunk

		// Write submesh name (from aiMesh->mName)
		std::string meshName = assimpMesh->mName.C_Str();
		uint32_t nameLength = static_cast<uint32_t>(meshName.length());
		resource::endian::writeLE<uint32_t>(outFile, nameLength);
		if (nameLength > 0) {
			outFile.write(meshName.data(), nameLength);
		}

		// Write vertex count
		resource::endian::writeLE<uint32_t>(outFile, assimpMesh->mNumVertices);

		// Process vertices in chunks to limit memory usage
		std::vector<resource::Vertex> chunkBuffer;
		chunkBuffer.reserve(verticesPerChunk);

		for (unsigned int v = 0; v < assimpMesh->mNumVertices; ) {
			unsigned int chunkEnd = static_cast<unsigned int>(
				std::min(static_cast<size_t>(v + verticesPerChunk),
				         static_cast<size_t>(assimpMesh->mNumVertices))
			);
			
			for (unsigned int j = v; j < chunkEnd; ++j) {
				resource::Vertex vertex;
				vertex.position = {
					assimpMesh->mVertices[j].x,
					assimpMesh->mVertices[j].y,
					assimpMesh->mVertices[j].z
				};

				if (assimpMesh->HasNormals()) {
					vertex.normal = {
						assimpMesh->mNormals[j].x,
						assimpMesh->mNormals[j].y,
						assimpMesh->mNormals[j].z
					};
				}
				else {
					vertex.normal = { 0.0f, 0.0f, 0.0f };
				}

				if (assimpMesh->mTextureCoords[0]) {
					vertex.texCoords = {
						assimpMesh->mTextureCoords[0][j].x,
						assimpMesh->mTextureCoords[0][j].y
					};
				}
				else {
					vertex.texCoords = { 0.0f, 0.0f };
				}

				chunkBuffer.push_back(vertex);
			}
			
			for (const auto& vertex : chunkBuffer) {
				resource::endian::writeLE<float>(outFile, vertex.position.x);
				resource::endian::writeLE<float>(outFile, vertex.position.y);
				resource::endian::writeLE<float>(outFile, vertex.position.z);
				resource::endian::writeLE<float>(outFile, vertex.normal.x);
				resource::endian::writeLE<float>(outFile, vertex.normal.y);
				resource::endian::writeLE<float>(outFile, vertex.normal.z);
				resource::endian::writeLE<float>(outFile, vertex.texCoords.x);
				resource::endian::writeLE<float>(outFile, vertex.texCoords.y);
			}

			v = chunkEnd;
			chunkBuffer.clear();
		}
		
		uint32_t totalIndices = 0;
		for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f) {
			totalIndices += assimpMesh->mFaces[f].mNumIndices;
		}
		resource::endian::writeLE<uint32_t>(outFile, totalIndices);
		
		constexpr size_t indicesPerChunk = chunkSize / sizeof(uint32_t);  // ~64K indices per chunk
		std::vector<uint32_t> indexBuffer;
		indexBuffer.reserve(indicesPerChunk);

		for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f) {
			const aiFace& face = assimpMesh->mFaces[f];
			for (unsigned int k = 0; k < face.mNumIndices; ++k) {
				indexBuffer.push_back(face.mIndices[k]);

				// Flush when chunk is full
				if (indexBuffer.size() >= indicesPerChunk) {
					resource::endian::writeVectorLE<uint32_t>(outFile, indexBuffer);
					indexBuffer.clear();
				}
			}
		}

		// Write remaining indices
		if (!indexBuffer.empty()) {
			resource::endian::writeVectorLE<uint32_t>(outFile, indexBuffer);
		}
	}
}
