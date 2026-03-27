#include "../print/Log.hpp"
#include "GameExporter.hpp"
#include "ExeIconEmbedder.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderPermutationManifest.hpp"
#include "../serialization/ProjectSerialization.hpp"
#include "../resource/ShaderResource.hpp"
#include "../resource/ShaderBinaryFormat.hpp"
#include "../material/MaterialAsset.hpp"
#include "../archive/VFPakWriter.hpp"
#include "../archive/VFPakReader.hpp"
#include <fstream>
#include <memory>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace gameExport
{
	namespace fs = std::filesystem;

	ExportResult GameExporter::exportGame(const ExportConfig& config, ExportProgressCallback progressCallback)
	{
		ExportResult result;
		result.outputPath = config.outputDirectory;

		auto report = [&](float progress, const std::string& status)
		{
			vfLogInfo("Export [{}%]: {}", static_cast<int>(progress * 100), status);
			if (progressCallback) progressCallback(progress, status);
		};

		report(0.0f, "Validating prerequisites...");
		if (!validatePrerequisites(config, result)) return result;

		report(0.05f, "Creating output structure...");
		if (!createOutputStructure(config, result)) return result;

		report(0.10f, "Copying runtime executable...");
		if (!copyRuntimeExecutable(config, result)) return result;

		report(0.15f, "Copying runtime dependencies...");
		if (!copyRuntimeDependencies(config, result)) return result;

		report(0.25f, "Compiling shaders...");
		if (!compileShaders(config, result)) return result;

		report(0.35f, "Compiling material shaders...");
		if (!compileMaterialShaders(config, result)) return result;

		report(0.40f, "Packing assets into archive...");
		if (!packAssets(config, result)) return result;

		report(0.80f, "Copying plugins...");
		if (!copyPlugins(config, result)) return result;

		report(0.85f, "Generating project config...");
		if (!generateProjectConfig(config, result)) return result;

		report(0.90f, "Embedding executable icon...");
		if (!embedExeIcon(config, result)) return result;

		report(0.95f, "Validating exported build...");
		if (!validateExportedBuild(config, result)) return result;

		report(1.0f, "Export complete!");
		result.success = true;
		return result;
	}

	bool GameExporter::validatePrerequisites(const ExportConfig& config, ExportResult& result)
	{
		if (!config.isValid())
		{
			result.errorMessage = "Invalid export configuration";
			return false;
		}

		if (!fs::exists(config.workingDirectory))
		{
			result.errorMessage = "Project working directory not found: " + config.workingDirectory.string();
			return false;
		}

		fs::path scenePath = config.workingDirectory / config.startupScene;
		if (!fs::exists(scenePath))
		{
			result.errorMessage = "Startup scene not found: " + scenePath.string();
			return false;
		}

		fs::path runtimeExe = findRuntimeExe();
		if (runtimeExe.empty() || !fs::exists(runtimeExe))
		{
			result.errorMessage = "Runtime executable not found. Build the Runtime project in Release mode first.";
			return false;
		}

		fs::path shaderDir = findShaderDirectory();
		if (shaderDir.empty() || !fs::exists(shaderDir))
		{
			result.errorMessage = "Engine shaders directory not found at: " + shaderDir.string();
			return false;
		}

		fs::path scriptsDir = config.workingDirectory / "scripts";
		if (fs::exists(scriptsDir))
		{
			fs::path compiledLib = scriptsDir / "compiled" / "scripts.mtcLib";
			if (!fs::exists(compiledLib))
			{
				result.warnings.push_back("Scripts found but not compiled. Build scripts before exporting (Scripts > Build Scripts).");
			}
		}

		return true;
	}

	bool GameExporter::createOutputStructure(const ExportConfig& config, ExportResult& result)
	{
		std::error_code ec;

		fs::create_directories(config.outputDirectory, ec);
		if (ec)
		{
			result.errorMessage = "Failed to create output directory: " + ec.message();
			return false;
		}

		// Temp directory for intermediate shader compilation before packing into archive
		fs::create_directories(config.outputDirectory / "_temp_shaders", ec);

		return true;
	}

	bool GameExporter::copyRuntimeExecutable(const ExportConfig& config, ExportResult& result)
	{
		fs::path runtimeExe = findRuntimeExe();
		fs::path destExe = config.outputDirectory / (config.gameName + ".exe");

		std::error_code ec;
		fs::copy_file(runtimeExe, destExe, fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			result.errorMessage = "Failed to copy runtime executable: " + ec.message();
			return false;
		}

		return true;
	}

	bool GameExporter::copyRuntimeDependencies(const ExportConfig& config, ExportResult& result)
	{
		fs::path runtimeDir = findRuntimeExe().parent_path();

		// Copy DLLs from the runtime build directory
		const std::vector<std::string> runtimeDlls = {
			"OpenAL32.dll",
			"jolt.dll",
			"meshoptimizer.dll"
		};

		for (const auto& dllName : runtimeDlls)
		{
			fs::path dllPath = runtimeDir / dllName;
			if (fs::exists(dllPath))
			{
				std::error_code ec;
				fs::copy_file(dllPath, config.outputDirectory / dllName,
							  fs::copy_options::overwrite_existing, ec);
				if (ec)
				{
					result.warnings.push_back("Failed to copy " + dllName + ": " + ec.message());
				}
			}
			else
			{
				result.warnings.push_back(dllName + " not found in runtime build output");
			}
		}

		// shaderc_shared.dll is no longer needed in exported builds:
		// shaders are pre-compiled to SPIR-V at export time

		return true;
	}

	bool GameExporter::compileShaders(const ExportConfig& config, ExportResult& result)
	{
		fs::path shaderSrc = findShaderDirectory();
		fs::path shaderDst = config.outputDirectory / "_temp_shaders" / "resources" / "shaders";

		// Build permutation lookup: glslPath -> list of permutations
		auto permutations = shaderCompiler::getShaderPermutations();
		std::unordered_map<std::string, std::vector<const shaderCompiler::ShaderPermutation*>> permutationMap;
		for (const auto& perm : permutations)
		{
			permutationMap[perm.glslPath].push_back(&perm);
		}

		// Walk all .glsl files and compile to .vfshader
		std::error_code ec;
		int compiledCount = 0;
		for (auto it = fs::recursive_directory_iterator(shaderSrc, ec); it != fs::recursive_directory_iterator(); ++it)
		{
			if (!it->is_regular_file() || it->path().extension() != ".glsl")
			{
				continue;
			}

			fs::path relativePath = fs::relative(it->path(), shaderSrc, ec);
			std::string relativeStr = relativePath.generic_string();

			// Parse the GLSL file to get shader stages
			auto shaderModels = resource::ShaderResource::readShaderFile(it->path().string());
			if (shaderModels.empty())
			{
				result.warnings.push_back("Failed to parse shader: " + relativeStr);
				continue;
			}

			// Compile base variant (no macros)
			{
				shaderCompiler::CompileOptions opts;
				opts.includeBasePath = it->path().parent_path();

				std::vector<resource::StageSPIRV> stages;
				bool success = true;
				for (const auto& model : shaderModels)
				{
					auto stage = shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(model.type));
					auto spirv = shaderCompiler::compile(model.source, stage, relativeStr, opts);
					if (spirv.empty())
					{
						result.warnings.push_back("Failed to compile base variant: " + relativeStr);
						success = false;
						break;
					}
					stages.push_back({static_cast<uint32_t>(stage), std::move(spirv)});
				}

				if (success)
				{
					fs::path outPath = shaderDst / relativePath;
					outPath.replace_extension(".vfshader");
					fs::create_directories(outPath.parent_path(), ec);
					resource::ShaderBinaryFormat::write(outPath, stages);
					compiledCount++;
				}
			}

			// Compile permutation variants
			auto permIt = permutationMap.find(relativeStr);
			if (permIt != permutationMap.end())
			{
				for (const auto* perm : permIt->second)
				{
					shaderCompiler::CompileOptions opts;
					opts.macroNames = perm->macroNames;
					opts.macroValues = perm->macroValues;
					opts.includeBasePath = it->path().parent_path();

					std::string permKey = shaderCompiler::computePermutationKey(opts);

					std::vector<resource::StageSPIRV> stages;
					bool success = true;
					for (const auto& model : shaderModels)
					{
						auto stage = shaderCompiler::shaderTypeToVulkanStage(static_cast<uint8_t>(model.type));
						auto spirv = shaderCompiler::compile(model.source, stage, relativeStr, opts);
						if (spirv.empty())
						{
							result.warnings.push_back("Failed to compile permutation " + permKey + " of " + relativeStr);
							success = false;
							break;
						}
						stages.push_back({static_cast<uint32_t>(stage), std::move(spirv)});
					}

					if (success)
					{
						fs::path outPath = shaderDst / relativePath;
						std::string stem = outPath.stem().string();
						outPath.replace_filename(stem + "_" + permKey + ".vfshader");
						resource::ShaderBinaryFormat::write(outPath, stages);
						compiledCount++;
					}
				}
			}
		}

		vfLogInfo("Compiled {} shader variants", compiledCount);

		// Copy IBL resources (pre-baked BRDF LUT)
		fs::path iblSrc = findIBLDirectory();
		if (fs::exists(iblSrc))
		{
			fs::path iblDst = config.outputDirectory / "_temp_shaders" / "resources" / "ibl";
			fs::create_directories(iblDst, ec);
			copyDirectoryRecursive(iblSrc, iblDst, result);
		}

		return true;
	}

	bool GameExporter::compileMaterialShaders(const ExportConfig& config, ExportResult& result)
	{
		fs::path compiledDir = config.outputDirectory / "_temp_shaders" / "Assets" / "materials" / "compiled";
		std::error_code ec;
		fs::create_directories(compiledDir, ec);

		std::unordered_set<std::string> compiledHashes;
		int compiledCount = 0;

		// Scan all .vfmaterial files in the working directory
		for (auto it = fs::recursive_directory_iterator(config.workingDirectory, ec);
		     it != fs::recursive_directory_iterator(); ++it)
		{
			if (!it->is_regular_file() || it->path().extension() != ".vfmaterial")
			{
				continue;
			}

			// Load the material to get cached shader source
			auto materialOpt = material::MaterialAsset::load(it->path().string());
			if (!materialOpt)
			{
				continue;
			}
			const auto& materialData = *materialOpt;

			if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty())
			{
				continue;
			}

			// Compute hash key matching MaterialShaderCache::hashShaderSource
			std::hash<std::string> hasher;
			std::string vsHash = std::to_string(hasher(materialData.cachedVertexShader));
			std::string fsHash = std::to_string(hasher(materialData.cachedFragmentShader));
			std::string combinedHash = vsHash + "_" + fsHash;

			// Skip if already compiled (dedup by hash)
			if (compiledHashes.count(combinedHash))
			{
				continue;
			}
			compiledHashes.insert(combinedHash);

			// Compile vertex and fragment shaders
			shaderCompiler::CompileOptions opts;
			std::vector<resource::StageSPIRV> stages;

			// Strip #type directive if present
			std::string vsSource = materialData.cachedVertexShader;
			if (auto pos = vsSource.find("#type"); pos != std::string::npos)
			{
				auto lineEnd = vsSource.find('\n', pos);
				if (lineEnd != std::string::npos)
					vsSource = vsSource.substr(lineEnd + 1);
			}

			std::string fsSource = materialData.cachedFragmentShader;
			if (auto pos = fsSource.find("#type"); pos != std::string::npos)
			{
				auto lineEnd = fsSource.find('\n', pos);
				if (lineEnd != std::string::npos)
					fsSource = fsSource.substr(lineEnd + 1);
			}

			auto vsSPIRV = shaderCompiler::compile(vsSource,
				vk::ShaderStageFlagBits::eVertex, materialData.name, opts);
			if (vsSPIRV.empty())
			{
				result.warnings.push_back("Failed to compile vertex shader for material: " +
					it->path().filename().string());
				continue;
			}
			stages.push_back({static_cast<uint32_t>(vk::ShaderStageFlagBits::eVertex), std::move(vsSPIRV)});

			auto fsSPIRV = shaderCompiler::compile(fsSource,
				vk::ShaderStageFlagBits::eFragment, materialData.name, opts);
			if (fsSPIRV.empty())
			{
				result.warnings.push_back("Failed to compile fragment shader for material: " +
					it->path().filename().string());
				continue;
			}
			stages.push_back({static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment), std::move(fsSPIRV)});

			fs::path outPath = compiledDir / (combinedHash + ".vfshader");
			resource::ShaderBinaryFormat::write(outPath, stages);
			compiledCount++;
		}

		if (compiledCount > 0)
		{
			vfLogInfo("Compiled {} material shader variants", compiledCount);
		}

		return true;
	}

	bool GameExporter::packAssets(const ExportConfig& config, ExportResult& result)
	{
		fs::path pakPath = config.outputDirectory / (config.gameName + ".vfpak");
		archive::VFPakWriter writer;

		if (!writer.create(pakPath))
		{
			result.errorMessage = "Failed to create archive file: " + pakPath.string();
			return false;
		}

		// Extensions that should NOT be LZ4-compressed (already compressed or need streaming)
		static const std::unordered_set<std::string> noCompressExts = {
			".vfimage", ".vfhdr", ".vfmesh", ".vfaudio",
			".vfterrain", ".vfsvt"
		};

		auto shouldCompress = [&](const std::string& ext) -> archive::CompressionType
		{
			std::string lower = ext;
			for (char& c : lower) c = static_cast<char>(std::tolower(c));
			if (noCompressExts.count(lower))
				return archive::CompressionType::None;
			return archive::CompressionType::LZ4;
		};

		// 1. Pack game assets from working directory
		std::error_code ec;
		fs::path excludeAbsolute;
		if (!config.outputDirectory.empty())
		{
			excludeAbsolute = fs::weakly_canonical(config.outputDirectory, ec);
		}

		uint32_t assetCount = 0;
		for (auto it = fs::recursive_directory_iterator(config.workingDirectory, ec);
		     it != fs::recursive_directory_iterator(); ++it)
		{
			const auto& entry = *it;

			// Skip export output directory
			if (entry.is_directory() && !excludeAbsolute.empty())
			{
				std::error_code cmpEc;
				fs::path entryAbs = fs::weakly_canonical(entry.path(), cmpEc);
				if (!cmpEc && entryAbs == excludeAbsolute)
				{
					it.disable_recursion_pending();
					continue;
				}
			}

			if (!entry.is_regular_file()) continue;

			auto ext = entry.path().extension().string();

			// Skip source scripts and project files
			if (ext == ".mt" || ext == ".vfproj") continue;

			fs::path relativePath = fs::relative(entry.path(), config.workingDirectory, ec);
			std::string archivePath = "Assets/" + relativePath.generic_string();

			writer.addFile(archivePath, entry.path(), shouldCompress(ext));
			assetCount++;
		}

		// 2. Pack compiled shaders from temp directory
		fs::path tempShaders = config.outputDirectory / "_temp_shaders";
		if (fs::exists(tempShaders))
		{
			for (auto it = fs::recursive_directory_iterator(tempShaders, ec);
			     it != fs::recursive_directory_iterator(); ++it)
			{
				if (!it->is_regular_file()) continue;

				fs::path relativePath = fs::relative(it->path(), tempShaders, ec);
				std::string archivePath = relativePath.generic_string();

				// .vfshader files benefit from LZ4 compression (SPIR-V is not pre-compressed)
				writer.addFile(archivePath, it->path(), archive::CompressionType::LZ4);
			}

			// Clean up temp directory
			fs::remove_all(tempShaders, ec);
		}

		if (!writer.finalize())
		{
			result.errorMessage = "Failed to finalize archive";
			return false;
		}

		vfLogInfo("Packed {} game assets into archive", assetCount);
		return true;
	}

	bool GameExporter::copyPlugins(const ExportConfig& config, ExportResult& result)
	{
		// Locate plugins using the same heuristic as Editor/Runtime plugin loading.
		// This runs inside the Editor process, so current_path() matches where plugins were loaded from.
		fs::path cwd = fs::current_path();
		fs::path pluginsDir = cwd / "plugins";
		if (!fs::exists(pluginsDir))
		{
			pluginsDir = cwd / "../../plugins";
		}

		if (fs::exists(pluginsDir) && fs::is_directory(pluginsDir))
		{
			fs::path pluginsDst = config.outputDirectory / "plugins";
			std::error_code ec;
			fs::create_directories(pluginsDst, ec);
			copyDirectoryRecursive(pluginsDir, pluginsDst, result);
		}

		return true;
	}

	bool GameExporter::generateProjectConfig(const ExportConfig& config, ExportResult& result)
	{
		config::ProjectConfig projConfig;
		projConfig.projectName = config.gameName;
		projConfig.version = config.gameVersion;
		projConfig.workingDirectory = "Assets";
		projConfig.startupScene = config.startupScene;
		projConfig.exeIconPath = config.iconPath;

		fs::path projPath = config.outputDirectory / (config.gameName + ".vfproj");

		if (!serialization::ProjectSerialization::saveProject(projConfig, projPath.string()))
		{
			result.errorMessage = "Failed to write project configuration file";
			return false;
		}

		return true;
	}

	bool GameExporter::embedExeIcon(const ExportConfig& config, ExportResult& result)
	{
		if (config.iconPath.empty())
		{
			return true; // No icon to embed, not an error
		}

		fs::path icoPath = config.iconPath;

		if (icoPath.is_relative())
		{
			icoPath = config.workingDirectory / icoPath;
		}

		if (!fs::exists(icoPath))
		{
			result.warnings.push_back("Icon file not found for embedding: " + icoPath.string());
			return true; // Non-fatal
		}

		fs::path destExe = config.outputDirectory / (config.gameName + ".exe");
		std::string error;
		if (!ExeIconEmbedder::embedIcon(destExe, icoPath, error))
		{
			result.warnings.push_back("Failed to embed icon: " + error);
		}

		return true;
	}

	bool GameExporter::validateExportedBuild(const ExportConfig& config, ExportResult& result)
	{
		fs::path exePath = config.outputDirectory / (config.gameName + ".exe");
		fs::path projPath = config.outputDirectory / (config.gameName + ".vfproj");
		fs::path pakPath = config.outputDirectory / (config.gameName + ".vfpak");

		if (!fs::exists(exePath))
		{
			result.errorMessage = "Validation failed: executable not found in output";
			return false;
		}

		if (!fs::exists(projPath))
		{
			result.errorMessage = "Validation failed: project file not found in output";
			return false;
		}

		if (!fs::exists(pakPath))
		{
			result.errorMessage = "Validation failed: archive (.vfpak) not found in output";
			return false;
		}

		// Verify archive is not empty
		auto pakSize = fs::file_size(pakPath);
		if (pakSize <= archive::VFPAK_HEADER_SIZE)
		{
			result.errorMessage = "Validation failed: archive is empty or corrupt";
			return false;
		}

		// Verify archive can be opened and contains the startup scene
		archive::VFPakReader reader;
		if (!reader.open(pakPath))
		{
			result.errorMessage = "Validation failed: failed to read archive";
			return false;
		}

		std::string scenePath = "Assets/" + config.startupScene;
		if (!reader.contains(scenePath))
		{
			result.errorMessage = "Validation failed: startup scene not found in archive: " + config.startupScene;
			return false;
		}

		return true;
	}

	fs::path GameExporter::findRuntimeExe() const
	{
		// Look relative to the current working directory (which is bin/Editor/<Config>/x64/ in dev)
		// The runtime exe is at bin/Runtime/Release/x64/Runtime.exe

		fs::path cwd = fs::current_path();

		// Try relative to editor exe location: ../../.. takes us to bin/, then Runtime/Release/x64/
		fs::path candidate = cwd / "../../../Runtime/Release/x64/Runtime.exe";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		// Try from repo root pattern
		candidate = cwd / "../../../../bin/Runtime/Release/x64/Runtime.exe";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		// Try direct relative
		candidate = cwd / "../../bin/Runtime/Release/x64/Runtime.exe";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		return {};
	}

	fs::path GameExporter::findShaderDirectory() const
	{
		fs::path cwd = fs::current_path();

		// From bin/Editor/<Config>/x64/ -> ../../resources/shaders
		fs::path candidate = cwd / "../../resources/shaders";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		// From repo root
		candidate = cwd / "resources/shaders";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		return {};
	}

	fs::path GameExporter::findIBLDirectory() const
	{
		fs::path cwd = fs::current_path();

		fs::path candidate = cwd / "../../resources/ibl";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		candidate = cwd / "resources/ibl";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		return {};
	}


	void GameExporter::copyDirectoryRecursive(const fs::path& src, const fs::path& dst, ExportResult& result) const
	{
		std::error_code ec;
		fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			result.warnings.push_back("Warning while copying " + src.string() + ": " + ec.message());
		}
	}

	void GameExporter::copyDirectoryFilteredRecursive(const fs::path& src, const fs::path& dst,
													ExportResult& result, const fs::path& excludeDir) const
	{
		std::error_code ec;
		fs::create_directories(dst, ec);

		// Resolve the exclude directory (the export output root) so we can skip it
		// when the user exports into a subfolder of the working directory.
		fs::path excludeAbsolute;
		if (!excludeDir.empty())
		{
			excludeAbsolute = fs::weakly_canonical(excludeDir, ec);
		}

		for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); ++it)
		{
			const auto& entry = *it;

			// Skip the export output directory to prevent infinite recursion
			if (entry.is_directory() && !excludeAbsolute.empty())
			{
				std::error_code cmpEc;
				fs::path entryAbs = fs::weakly_canonical(entry.path(), cmpEc);
				if (!cmpEc && entryAbs == excludeAbsolute)
				{
					it.disable_recursion_pending();
					continue;
				}
			}

			fs::path relativePath = fs::relative(entry.path(), src, ec);
			fs::path destPath = dst / relativePath;

			if (entry.is_directory())
			{
				fs::create_directories(destPath, ec);
				continue;
			}

			// Skip .mt source files (keep .mtcLib compiled bytecode)
			// Skip .vfproj files (a new one is generated in the export root)
			auto ext = entry.path().extension().string();
			if (ext == ".mt" || ext == ".vfproj")
			{
				continue;
			}

			fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				result.warnings.push_back("Warning while copying " + entry.path().string() + ": " + ec.message());
				ec.clear();
			}
		}
	}
}
