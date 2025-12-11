#pragma once
#include <string>
#include <fstream>
#include <functional>
#include "config/Config.hpp"
#include "resource/Types.hpp"
struct aiScene;
struct aiMesh;

namespace types {
	
	using MeshProgressCallback = std::function<void(float progress)>;

	class Mesh
	{
	public:
		void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
		                  std::string_view location, MeshProgressCallback progressCallback = nullptr) const;

	private:
		void saveToFileStreaming(std::string_view location, std::string_view fileName,
		                         const aiScene* scene, MeshProgressCallback progressCallback) const;

		// Write a single mesh's data in chunks to limit memory usage
		void writeMeshChunked(std::ofstream& outFile, const aiMesh* mesh) const;

		// Chunk size for streaming (256KB)
		static constexpr size_t chunkSize = 256 * 1024;
	};

}


