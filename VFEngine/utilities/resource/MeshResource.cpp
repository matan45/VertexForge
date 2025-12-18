#include "MeshResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>

namespace resource {

	// Helper function to read a single LOD level from file
	static bool readLODLevel(std::ifstream& inFile, LODLevel& lodLevel, uint32_t meshIdx, uint32_t lodIdx,
	                         uint32_t maxVertexCount, uint32_t maxIndexCount)
	{
		// Read vertex count
		uint32_t vertexCount = endian::readLE<uint32_t>(inFile);

		if (vertexCount > maxVertexCount) {
			vfLogError("Vertex count {} exceeds maximum limit {} in mesh {} LOD {}",
			           vertexCount, maxVertexCount, meshIdx, lodIdx);
			return false;
		}

		// Read vertices
		lodLevel.vertices.resize(vertexCount);
		for (uint32_t v = 0; v < vertexCount; ++v) {
			lodLevel.vertices[v].position.x = endian::readLE<float>(inFile);
			lodLevel.vertices[v].position.y = endian::readLE<float>(inFile);
			lodLevel.vertices[v].position.z = endian::readLE<float>(inFile);
			lodLevel.vertices[v].normal.x = endian::readLE<float>(inFile);
			lodLevel.vertices[v].normal.y = endian::readLE<float>(inFile);
			lodLevel.vertices[v].normal.z = endian::readLE<float>(inFile);
			lodLevel.vertices[v].texCoords.x = endian::readLE<float>(inFile);
			lodLevel.vertices[v].texCoords.y = endian::readLE<float>(inFile);

			if (inFile.fail()) {
				vfLogError("Failed to read vertex {} of mesh {} LOD {}", v, meshIdx, lodIdx);
				return false;
			}
		}

		// Read index count
		uint32_t indexCount = endian::readLE<uint32_t>(inFile);

		if (indexCount > maxIndexCount) {
			vfLogError("Index count {} exceeds maximum limit {} in mesh {} LOD {}",
			           indexCount, maxIndexCount, meshIdx, lodIdx);
			return false;
		}

		// Read indices
		endian::readVectorLE<uint32_t>(inFile, lodLevel.indices, indexCount);

		if (inFile.fail()) {
			vfLogError("Failed to read indices of mesh {} LOD {}", meshIdx, lodIdx);
			return false;
		}

		return true;
	}

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

		result.version.major = majorVersion;
		result.version.minor = minorVersion;
		result.version.patch = patchVersion;

		// Determine format version
		bool isLODFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
		bool isLegacyFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion == 2);

		if (!isLODFormat && !isLegacyFormat) {
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
			if (nameLength > 0 && nameLength < 1024) {
				meshData.name.resize(nameLength);
				inFile.read(meshData.name.data(), nameLength);
			} else if (nameLength == 0) {
				meshData.name = "SubMesh_" + std::to_string(meshIdx);
			}

			if (isLODFormat) {
				// New format: Read LOD level count and all LOD levels
				uint32_t lodLevelCount = endian::readLE<uint32_t>(inFile);

				if (lodLevelCount == 0 || lodLevelCount > 8) {
					vfLogError("Invalid LOD level count {} in mesh {}", lodLevelCount, meshIdx);
					return result;
				}

				meshData.lodLevels.resize(lodLevelCount);

				for (uint32_t lodIdx = 0; lodIdx < lodLevelCount; ++lodIdx) {
					if (!readLODLevel(inFile, meshData.lodLevels[lodIdx], meshIdx, lodIdx,
					                  maxVertexCount, maxIndexCount)) {
						return result;
					}
				}
			} else {
				// Legacy format: Read single vertex/index buffer as LOD0
				meshData.lodLevels.resize(1);
				if (!readLODLevel(inFile, meshData.lodLevels[0], meshIdx, 0,
				                  maxVertexCount, maxIndexCount)) {
					return result;
				}

				// Fill remaining LOD levels with LOD0 data (no simplification for legacy files)
				for (uint32_t lodIdx = 1; lodIdx < LOD_LEVEL_COUNT; ++lodIdx) {
					meshData.lodLevels.push_back(meshData.lodLevels[0]);
				}
			}
		}

		if (isLODFormat) {
			vfLogInfo("Loaded mesh file with {} meshes (LOD format v{}.{}.{}): {}",
			          result.numberOfMeshes, majorVersion, minorVersion, patchVersion, path);
		} else {
			vfLogWarning("Loaded legacy mesh file with {} meshes (v{}.{}.{}). Consider reimporting for LOD support: {}",
			             result.numberOfMeshes, majorVersion, minorVersion, patchVersion, path);
		}

		return result;
	}
}
