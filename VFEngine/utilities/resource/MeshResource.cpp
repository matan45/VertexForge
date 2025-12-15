#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>

namespace resource {

	MeshesData MeshResource::loadMesh(std::string_view path)
	{
		MeshesData result;

		std::string filePath(path);
		std::ifstream inFile(filePath, std::ios::binary);
		if (!inFile) {
			vfLogError("Failed to open mesh file: {}", path);
			return result;
		}

		// Read header
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		result.headerFileType = static_cast<FileType>(headerFileType);

		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible mesh file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return result;
		}

		result.numberOfMeshes = endian::readLE<uint32_t>(inFile);
		result.meshes.resize(result.numberOfMeshes);

		if (inFile.fail()) {
			vfLogError("Failed to read mesh header: {}", path);
			return result;
		}

		// Read each mesh
		for (uint32_t meshIdx = 0; meshIdx < result.numberOfMeshes; ++meshIdx) {
			auto& meshData = result.meshes[meshIdx];

			// Read submesh name
			uint32_t nameLength = endian::readLE<uint32_t>(inFile);
			if (nameLength > 0 && nameLength < 1024) {  // Sanity check
				meshData.name.resize(nameLength);
				inFile.read(meshData.name.data(), nameLength);
			} else if (nameLength == 0) {
				// Generate default name if empty
				meshData.name = "SubMesh_" + std::to_string(meshIdx);
			}

			// Read vertex count
			uint32_t vertexCount = endian::readLE<uint32_t>(inFile);

			if (vertexCount > maxVertexCount) {
				vfLogError("Vertex count {} exceeds maximum limit {} in mesh {}", vertexCount, maxVertexCount, meshIdx);
				return result;
			}

			// Read vertices
			meshData.vertices.resize(vertexCount);
			for (uint32_t v = 0; v < vertexCount; ++v) {
				meshData.vertices[v].position.x = endian::readLE<float>(inFile);
				meshData.vertices[v].position.y = endian::readLE<float>(inFile);
				meshData.vertices[v].position.z = endian::readLE<float>(inFile);
				meshData.vertices[v].normal.x = endian::readLE<float>(inFile);
				meshData.vertices[v].normal.y = endian::readLE<float>(inFile);
				meshData.vertices[v].normal.z = endian::readLE<float>(inFile);
				meshData.vertices[v].texCoords.x = endian::readLE<float>(inFile);
				meshData.vertices[v].texCoords.y = endian::readLE<float>(inFile);

				if (inFile.fail()) {
					vfLogError("Failed to read vertex {} of mesh {}", v, meshIdx);
					return result;
				}
			}

			// Read index count
			uint32_t indexCount = endian::readLE<uint32_t>(inFile);

			if (indexCount > maxIndexCount) {
				vfLogError("Index count {} exceeds maximum limit {} in mesh {}", indexCount, maxIndexCount, meshIdx);
				return result;
			}

			// Read indices
			endian::readVectorLE<uint32_t>(inFile, meshData.indices, indexCount);

			if (inFile.fail()) {
				vfLogError("Failed to read indices of mesh {}", meshIdx);
				return result;
			}
		}

		vfLogInfo("Loaded mesh file with {} meshes: {}", result.numberOfMeshes, path);
		return result;
	}
}
