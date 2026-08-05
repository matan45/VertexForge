#include "../print/Log.hpp"
#include "GameExporter.hpp"
#include "AssetClosureResolver.hpp"
#include "ExeIconEmbedder.hpp"
#include "PluginExportPlanner.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderPermutationManifest.hpp"
#include "../asset/AssetDatabase.hpp"
#include "../config/Config.hpp"
#include "../serialization/ProjectSerialization.hpp"
#include "../resource/ShaderResource.hpp"
#include "../resource/ShaderBinaryFormat.hpp"
#include "../material/MaterialAsset.hpp"
#include "../material/MaterialInstanceTypes.hpp"
#include "../material/MaterialRuntimeData.hpp"
#include "../material/PipelineWarmupManifest.hpp"
#include "../archive/VFPakWriter.hpp"
#include "../archive/VFPakReader.hpp"
#include "../serialization/BinarySceneSerialization.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <chrono>
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

		// Load previous manifest for incremental export
		fs::path manifestPath = config.outputDirectory / (config.gameName + ".vfmanifest");
		newManifest = ExportManifest{};
		newManifest.gameName = config.gameName;
		newManifest.gameVersion = config.gameVersion;
		newManifest.engineVersion = "VertexForge " + std::to_string(Version::major) + "." +
			std::to_string(Version::minor) + "." + std::to_string(Version::patch);
		newManifest.pluginApiVersion = config.expectedPluginApiVersion;

		if (!config.cleanBuild && fs::exists(manifestPath))
		{
			if (previousManifest.load(manifestPath))
			{
				vfLogInfo("Loaded previous export manifest ({} entries)", previousManifest.getEntries().size());
			}
		}
		else
		{
			previousManifest = ExportManifest{};
		}

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

		// Save export manifest (after copyPlugins — plugin files are manifest
		// entries too, for incremental re-copy)
		{
			auto now = std::chrono::system_clock::now();
			auto time = std::chrono::system_clock::to_time_t(now);
			char timeBuf[32];
			std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time));
			newManifest.exportTimestamp = timeBuf;
			newManifest.save(manifestPath);
			vfLogInfo("Export manifest saved with {} entries", newManifest.getEntries().size());
		}

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

		// Shipped games run only the compiled bytecode (.mt sources are stripped
		// from the pak), so a missing scripts.mtcLib means every script silently
		// no-ops in the exported build — hard error, not a warning.
		fs::path scriptsDir = config.workingDirectory / "scripts";
		if (fs::exists(scriptsDir / "scripts.mtproj"))
		{
			fs::path compiledLib = scriptsDir / "compiled" / "scripts.mtcLib";
			if (!fs::exists(compiledLib))
			{
				result.errorMessage = "Scripts found but not compiled (scripts/compiled/scripts.mtcLib missing). Build scripts before exporting (Scripts > Build Scripts).";
				return false;
			}
		}

		// A navmesh index with no sibling tiles ships a navmesh that can never load
		std::error_code navEc;
		for (auto it = fs::recursive_directory_iterator(config.workingDirectory, navEc);
		     it != fs::recursive_directory_iterator(); it.increment(navEc))
		{
			if (navEc) break;
			if (!it->is_regular_file() || it->path().filename() != "index.vfNavIndex")
				continue;

			bool hasTiles = false;
			std::error_code tileEc;
			for (const auto& sibling : fs::directory_iterator(it->path().parent_path(), tileEc))
			{
				if (sibling.is_regular_file() && sibling.path().extension() == ".vfNavTile")
				{
					hasTiles = true;
					break;
				}
			}

			if (!hasTiles)
			{
				result.warnings.push_back("Navmesh index has no baked tiles: " + it->path().string()
				                          + " — re-save the navmesh before exporting.");
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

		// On clean build, purge cached temp directories
		if (config.cleanBuild)
		{
			fs::remove_all(config.outputDirectory / "_temp_shaders", ec);
			fs::remove_all(config.outputDirectory / "_temp_scenes", ec);
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

		// VK-1624: copy EVERY DLL sitting next to Runtime.exe rather than a hand-maintained list.
		//
		// The list used to name three (OpenAL32, jolt, meshoptimizer) while Runtime.exe statically
		// imports nine more -- Terrain, ECSRegistry, AssetDB, Threading, CpuMemory, Serialization,
		// World, Animation, Audio -- so an exported game died in the Windows loader before main,
		// and OpenAL32 was not even a direct import (it comes in behind Audio.dll). Every engine
		// subsystem promoted to a SharedLib since silently widened that gap, which is exactly the
		// failure mode a hardcoded list produces. The build directory is already the authority on
		// what the runtime needs: each subsystem's premake postbuild puts its DLL here.
		std::error_code dirEc;
		fs::directory_iterator runtimeFiles(runtimeDir, dirEc);
		if (dirEc)
		{
			result.warnings.push_back("Could not read runtime build output at "
									  + runtimeDir.string() + ": " + dirEc.message());
			return true;
		}

		uint32_t copied = 0;
		for (const auto& entry : runtimeFiles)
		{
			if (!entry.is_regular_file())
				continue;

			std::string extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".dll")
				continue;

			const std::string dllName = entry.path().filename().string();

			// shaderc_shared.dll is deliberately excluded: shaders are pre-compiled to SPIR-V at
			// export time, and it is the one DLL the runtime delay-loads rather than importing.
			if (dllName == "shaderc_shared.dll")
				continue;

			std::error_code ec;
			fs::copy_file(entry.path(), config.outputDirectory / dllName,
						  fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				result.warnings.push_back("Failed to copy " + dllName + ": " + ec.message());
				continue;
			}
			++copied;
		}

		if (copied == 0)
		{
			result.warnings.push_back("No runtime DLLs found in " + runtimeDir.string()
									  + " - the exported game will not start");
		}

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
		int skippedCount = 0;
		for (auto it = fs::recursive_directory_iterator(shaderSrc, ec); it != fs::recursive_directory_iterator(); ++it)
		{
			if (!it->is_regular_file() || it->path().extension() != ".glsl")
			{
				continue;
			}

			fs::path relativePath = fs::relative(it->path(), shaderSrc, ec);
			std::string relativeStr = relativePath.generic_string();

			// Check if source changed for incremental skip
			std::string baseArchivePath = "resources/shaders/" + relativePath.generic_string();
			{
				fs::path baseOutPath = shaderDst / relativePath;
				baseOutPath.replace_extension(".vfshader");

				ManifestSource src;
				src.path = relativeStr;
				src.modifiedTime = getFileModifiedTime(it->path());
				src.contentHash = hashFile(it->path());

				if (!previousManifest.hasSourceChanged(baseArchivePath, {src}) && fs::exists(baseOutPath))
				{
					skippedCount++;
					continue;
				}
			}

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

		vfLogInfo("Compiled {} shader variants ({} unchanged, skipped)", compiledCount, skippedCount);

		// Copy IBL resources (pre-baked BRDF LUT)
		fs::path iblSrc = findIBLDirectory();
		if (fs::exists(iblSrc))
		{
			fs::path iblDst = config.outputDirectory / "_temp_shaders" / "resources" / "ibl";
			fs::create_directories(iblDst, ec);
			copyDirectoryRecursive(iblSrc, iblDst, result);
		}

		// VK-1628: engine-shipped default font. Without it, any text whose fontRef is
		// unset or unresolvable renders nothing in the shipped game. Warn rather than
		// fail — an export with no text is still a valid export.
		fs::path fontsSrc = findFontsDirectory();
		if (fs::exists(fontsSrc))
		{
			fs::path fontsDst = config.outputDirectory / "_temp_shaders" / "resources" / "fonts";
			fs::create_directories(fontsDst, ec);
			copyDirectoryRecursive(fontsSrc, fontsDst, result);
		}
		else
		{
			result.warnings.push_back(
				"resources/fonts not found — the default font will be missing from the "
				"exported game and text without an explicit font will not render.");
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
		int skippedMaterialCount = 0;

		// Scan all material assets in the working directory (.vfMat is canonical;
		// .vfMaterial is a legacy spelling kept for existing projects).
		for (auto it = fs::recursive_directory_iterator(config.workingDirectory, ec);
		     it != fs::recursive_directory_iterator(); ++it)
		{
			if (!it->is_regular_file() || !material::isMaterialFile(it->path().string()))
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
				// No cached shader is fine for plain PBR materials (they use the
				// standard pipeline), but a graph-authored material (PBROutput node)
				// without one renders wrong in shipped builds — the runtime has no
				// shaderc to regenerate it.
				if (materialData.graph.findOutputNode() != nullptr)
				{
					result.brokenMaterials.push_back(
						fs::relative(it->path(), config.workingDirectory, ec).generic_string());
				}
				continue;
			}

			material::MaterialRuntimeData runtimeData =
				material::MaterialRuntimeDataBuilder::fromMaterialData(materialData);
			std::string combinedHash = runtimeData.shaderMap.vertexShaderHash + "_" +
				runtimeData.shaderMap.fragmentShaderHash;

			// Skip if already compiled (dedup by hash)
			if (compiledHashes.count(combinedHash))
			{
				continue;
			}
			compiledHashes.insert(combinedHash);

			// Incremental: check if material source changed
			std::string archivePath = runtimeData.shaderMap.compiledShaderPath;
			fs::path outPath = compiledDir / (combinedHash + ".vfshader");
			{
				ManifestSource src;
				src.path = fs::relative(it->path(), config.workingDirectory, ec).generic_string();
				src.modifiedTime = getFileModifiedTime(it->path());
				src.contentHash = hashFile(it->path());

				if (!previousManifest.hasSourceChanged(archivePath, {src}) && fs::exists(outPath))
				{
					skippedMaterialCount++;
					continue;
				}
			}

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

			resource::ShaderBinaryFormat::write(outPath, stages);
			compiledCount++;
		}

		if (compiledCount > 0 || skippedMaterialCount > 0)
		{
			vfLogInfo("Compiled {} material shader variants ({} unchanged, skipped)", compiledCount, skippedMaterialCount);
		}

		if (!result.brokenMaterials.empty())
		{
			std::string materialList;
			for (const auto& path : result.brokenMaterials)
			{
				materialList += "\n  - " + path;
			}

			if (config.failOnEmptyMaterialShaders)
			{
				result.errorMessage = "Materials with a shader graph but no compiled shader (open and re-save them in the Material Editor):" + materialList;
				return false;
			}

			result.warnings.push_back("Materials with a shader graph but no compiled shader (will render with the standard pipeline):" + materialList);
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
			".vfterrain"
		};

		auto shouldCompress = [&](const std::string& ext) -> archive::CompressionType
		{
			std::string lower = ext;
			for (char& c : lower) c = static_cast<char>(std::tolower(c));
			if (noCompressExts.count(lower))
				return archive::CompressionType::None;
			return archive::CompressionType::LZ4;
		};

		// Helper to add a file to the archive and record it in the manifest
		auto addToArchiveWithManifest = [&](const std::string& archivePath,
											const fs::path& filePath,
											archive::CompressionType compression,
											const std::string& sourceType,
											const std::vector<ManifestSource>& sources)
		{
			writer.addFile(archivePath, filePath, compression);

			ManifestEntry manifestEntry;
			manifestEntry.archivePath = archivePath;
			manifestEntry.contentHash = hashFile(filePath);
			std::error_code sizeEc;
			manifestEntry.uncompressedSize = static_cast<uint64_t>(fs::file_size(filePath, sizeEc));
			manifestEntry.sourceType = sourceType;
			manifestEntry.sources = sources;
			newManifest.addEntry(std::move(manifestEntry));
		};

		// Reachability from scenes through the AssetDatabase graph. Invalid
		// closure (empty/stale database) skips classification — everything packs.
		AssetClosure closure = AssetClosureResolver::resolve(config.workingDirectory);
		auto& assetDb = asset::AssetDatabase::instance();
		uint64_t unreferencedBytes = 0;

		// VK-1532 Phase 2: per-scene PSO warm-up manifest. For each scene, record the GUIDs of
		// every material reachable through the AssetDatabase dependency graph so the shipped
		// runtime can warm those pipelines during the loading screen — including materials only
		// referenced by prefabs that scripts spawn later at runtime.
		material::PipelineWarmupManifest psoManifest;
		auto collectSceneMaterialGuids = [&assetDb](const asset::AssetGUID& sceneGuid)
		{
			std::vector<std::string> materialGuids;
			std::unordered_set<asset::AssetGUID, asset::AssetGUID::Hash> visited;
			std::vector<asset::AssetGUID> worklist;
			visited.insert(sceneGuid);
			worklist.push_back(sceneGuid);
			while (!worklist.empty())
			{
				asset::AssetGUID current = worklist.back();
				worklist.pop_back();
				for (const auto& dep : assetDb.getDependencies(current))
				{
					if (!visited.insert(dep).second) continue;
					worklist.push_back(dep);
					if (auto depPath = assetDb.getPath(dep); depPath && material::isMaterialFile(*depPath))
					{
						materialGuids.push_back(dep.toString());
					}
				}
			}
			return materialGuids;
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

			// VK-1646: `.vfterrainlayers` is authoring state — a terrain's authoritative base
			// heights and its reserved layer stack. The runtime only ever consumes the flattened
			// heights already inside the `.vfterrain`, and a fully covered map's sidecar runs to
			// tens of megabytes, so shipping it would be pure weight.
			//
			// The terrain's HAS_EDIT_LAYER_SIDECAR flag stays set in the packed file; the loader
			// treats a missing sidecar as expected in archive mode rather than as damage.
			if (ext == ".vfterrainlayers") continue;

			fs::path relativePath = fs::relative(entry.path(), config.workingDirectory, ec);
			std::string archivePath = "Assets/" + relativePath.generic_string();

			ManifestSource assetSource;
			assetSource.path = relativePath.generic_string();
			assetSource.modifiedTime = getFileModifiedTime(entry.path());
			assetSource.contentHash = hashFile(entry.path());

			// Convert JSON scenes to binary MessagePack for faster loading
			if (ext == ".vfscene")
			{
				// VK-1532 Phase 2: record this scene's material closure into the warm-up manifest.
				if (auto sceneGuid = assetDb.getGUID(entry.path().string()))
				{
					auto materialGuids = collectSceneMaterialGuids(*sceneGuid);
					if (!materialGuids.empty())
					{
						psoManifest.scenes[sceneGuid->toString()] = std::move(materialGuids);
					}
				}

				fs::path tempBinary = config.outputDirectory / "_temp_scenes" / relativePath;

				// Reuse the cached blob only if it is the current format version AND
				// was built from this exact source content (VK-1538). Keying on the
				// blob's own header — not the manifest mtime — invalidates stale
				// blobs cleanly after a FORMAT_VERSION bump, which the mtime/hash
				// source check could not see (it would ship a v1 blob the runtime
				// rejects). Also closes the whole-second mtime granularity hole.
				bool needsConversion = true;
				if (fs::exists(tempBinary))
				{
					std::ifstream cached(tempBinary, std::ios::binary);
					std::vector<uint8_t> headerBytes(serialization::BinarySceneSerialization::HEADER_SIZE);
					serialization::BinarySceneSerialization::Header cachedHeader{};
					if (cached.read(reinterpret_cast<char*>(headerBytes.data()),
					                static_cast<std::streamsize>(headerBytes.size())) &&
					    serialization::BinarySceneSerialization::peekHeader(headerBytes, cachedHeader) &&
					    cachedHeader.version == serialization::BinarySceneSerialization::FORMAT_VERSION &&
					    cachedHeader.sourceHash == assetSource.contentHash)
					{
						needsConversion = false;
					}
				}

				if (needsConversion)
				{
					fs::create_directories(tempBinary.parent_path(), ec);

					if (serialization::BinarySceneSerialization::convertJsonToBinary(
							entry.path().string(), tempBinary.string(), assetSource.contentHash))
					{
						// Conversion succeeded
					}
					else
					{
						// Validate that the JSON scene is at least parseable before packing
						std::ifstream sceneFile(entry.path());
						bool validJson = false;
						if (sceneFile.is_open())
						{
							try
							{
								auto _ = nlohmann::json::parse(sceneFile);
								validJson = true;
							}
							catch (const nlohmann::json::parse_error&) {}
						}

						if (validJson)
						{
							addToArchiveWithManifest(archivePath, entry.path(),
								archive::CompressionType::LZ4, "scene", {assetSource});
							result.warnings.push_back("Failed to convert scene to binary (packed as JSON): " + relativePath.string());
							assetCount++;
							continue;
						}
						else
						{
							result.errorMessage = "Corrupt scene file cannot be exported: " + relativePath.string();
							return false;
						}
					}
				}

				addToArchiveWithManifest(archivePath, tempBinary,
					archive::CompressionType::LZ4, "scene", {assetSource});
				assetCount++;
				continue;
			}

			// Tracked assets no scene reaches are reported, and skipped when
			// stripping is enabled. Untracked files (no database GUID) and
			// always-include matches ship unconditionally.
			if (closure.valid)
			{
				std::string relativeGeneric = relativePath.generic_string();
				if (!AssetClosureResolver::isAlwaysIncluded(relativeGeneric, config.alwaysIncludePatterns))
				{
					auto guid = assetDb.getGUID(entry.path().string());
					if (guid && closure.referencedGuids.count(*guid) == 0)
					{
						std::error_code sizeEc;
						uint64_t sizeBytes = static_cast<uint64_t>(fs::file_size(entry.path(), sizeEc));
						result.unreferencedAssets.push_back({relativeGeneric, sizeBytes});
						unreferencedBytes += sizeBytes;

						if (config.stripUnreferencedAssets) continue;
					}
				}
			}

			addToArchiveWithManifest(archivePath, entry.path(), shouldCompress(ext), "asset", {assetSource});
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

				// Determine source type from path
				std::string sourceType = "engine_shader";
				if (archivePath.find("materials/compiled") != std::string::npos)
					sourceType = "material_shader";
				else if (archivePath.find("ibl") != std::string::npos)
					sourceType = "ibl";
				else if (archivePath.find("resources/fonts") != std::string::npos)
					sourceType = "engine_font";

				ManifestSource src;
				src.path = relativePath.generic_string();
				src.modifiedTime = getFileModifiedTime(it->path());
				src.contentHash = hashFile(it->path());

				addToArchiveWithManifest(archivePath, it->path(),
					archive::CompressionType::LZ4, sourceType, {src});
			}

			// Only clean temp directories on clean builds
			if (config.cleanBuild)
			{
				fs::remove_all(tempShaders, ec);
			}
		}

		// Clean up temp scenes directory only on clean builds
		fs::path tempScenes = config.outputDirectory / "_temp_scenes";
		if (config.cleanBuild && fs::exists(tempScenes))
		{
			fs::remove_all(tempScenes, ec);
		}

		// VK-1532 Phase 2: bake the PSO warm-up manifest into the archive.
		if (!psoManifest.scenes.empty())
		{
			const std::string manifestJson = psoManifest.toJson();
			writer.addMemory(material::kPsoManifestEntry, manifestJson.data(), manifestJson.size(),
			                 archive::CompressionType::LZ4);
			vfLogInfo("Export: PSO warm-up manifest written ({} scenes)", psoManifest.scenes.size());
		}

		if (!writer.finalize())
		{
			result.errorMessage = "Failed to finalize archive";
			return false;
		}

		if (!result.unreferencedAssets.empty())
		{
			char summary[160];
			snprintf(summary, sizeof(summary), "%zu unreferenced asset(s), %.1f MB — %s (see log for the full list)",
					 result.unreferencedAssets.size(),
					 static_cast<double>(unreferencedBytes) / (1024.0 * 1024.0),
					 config.stripUnreferencedAssets ? "stripped from the archive" : "packed anyway");
			result.warnings.push_back(summary);

			for (const auto& unreferenced : result.unreferencedAssets)
			{
				vfLogInfo("Unreferenced asset{}: {} ({} bytes)",
						  config.stripUnreferencedAssets ? " (stripped)" : "",
						  unreferenced.path, unreferenced.sizeBytes);
			}
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

		if (!fs::exists(pluginsDir) || !fs::is_directory(pluginsDir))
		{
			return true;
		}

		// Collect descriptors, then let the planner decide what ships (global
		// enabled flags + export overrides + transitive dependencies + API
		// version validation).
		std::vector<ParsedPluginDescriptor> descriptors;
		std::error_code ec;
		for (const auto& pluginEntry : fs::directory_iterator(pluginsDir, ec))
		{
			if (!pluginEntry.is_directory()) continue;

			std::error_code scanEc;
			for (const auto& file : fs::directory_iterator(pluginEntry.path(), scanEc))
			{
				if (file.is_regular_file() && file.path().extension() == ".vfplugin")
				{
					if (auto descriptor = parsePluginDescriptor(file.path()))
					{
						descriptors.push_back(std::move(*descriptor));
					}
					break;
				}
			}
		}

		auto plan = planPluginExport(descriptors, config.pluginOverrides, config.expectedPluginApiVersion);
		for (const auto& warning : plan.warnings)
		{
			result.warnings.push_back(warning);
		}
		if (!plan.errors.empty())
		{
			result.errorMessage = "Plugin export failed:";
			for (const auto& error : plan.errors)
			{
				result.errorMessage += "\n  - " + error;
			}
			return false;
		}

		// Plugin folders are full development workspaces (source, premake5.lua,
		// PDBs) — only ship what the runtime loads: descriptor, the DLL the
		// descriptor names, and the plugin's assets/resources data. Files are
		// manifest entries ("plugin" source type, verified on disk rather than
		// in the archive), so unchanged plugins skip the copy.
		fs::path pluginsDst = config.outputDirectory / "plugins";

		for (const auto& descriptor : plan.pluginsToShip)
		{
			fs::path pluginSrc = descriptor.descriptorPath.parent_path();
			fs::path dllPath = pluginSrc / descriptor.library;
			if (!fs::exists(dllPath))
			{
				result.errorMessage = "Plugin '" + descriptor.name +
					"': library not found: " + dllPath.string();
				return false;
			}

			fs::path pluginDst = pluginsDst / pluginSrc.filename();
			int copiedCount = 0;
			int skippedCount = 0;

			std::error_code copyEc;
			for (auto it = fs::recursive_directory_iterator(pluginSrc, copyEc);
			     it != fs::recursive_directory_iterator(); it.increment(copyEc))
			{
				if (copyEc) break;
				if (!it->is_regular_file()) continue;

				fs::path relativePath = fs::relative(it->path(), pluginSrc, copyEc);
				if (!shouldShipPluginFile(relativePath, descriptor.library)) continue;

				std::string manifestKey = "plugins/" + pluginSrc.filename().generic_string() +
					"/" + relativePath.generic_string();

				ManifestSource src;
				src.path = manifestKey;
				src.modifiedTime = getFileModifiedTime(it->path());
				src.contentHash = hashFile(it->path());

				fs::path destPath = pluginDst / relativePath;
				if (!previousManifest.hasSourceChanged(manifestKey, {src}) && fs::exists(destPath))
				{
					skippedCount++;
				}
				else
				{
					fs::create_directories(destPath.parent_path(), copyEc);
					fs::copy_file(it->path(), destPath, fs::copy_options::overwrite_existing, copyEc);
					if (copyEc)
					{
						result.warnings.push_back("Warning while copying " + it->path().string() +
							": " + copyEc.message());
						copyEc.clear();
						continue;
					}
					copiedCount++;
				}

				ManifestEntry manifestEntry;
				manifestEntry.archivePath = manifestKey;
				manifestEntry.contentHash = src.contentHash;
				std::error_code sizeEc;
				manifestEntry.uncompressedSize = static_cast<uint64_t>(fs::file_size(it->path(), sizeEc));
				manifestEntry.sourceType = "plugin";
				manifestEntry.sources = {src};
				newManifest.addEntry(std::move(manifestEntry));
			}

			vfLogInfo("Shipped plugin '{}' ({} copied, {} unchanged)", descriptor.name,
					  copiedCount, skippedCount);
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
		projConfig.fontFallbackChain = config.fontFallbackChain;
		projConfig.exeIconPath = config.iconPath;
		if (config.expectedPluginApiVersion != 0)
		{
			projConfig.pluginApiVersion = config.expectedPluginApiVersion;
		}

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

		// Integrity verification using manifest
		if (config.verifyIntegrity)
		{
			fs::path manifestPath = config.outputDirectory / (config.gameName + ".vfmanifest");
			if (fs::exists(manifestPath))
			{
				ExportManifest manifest;
				if (manifest.load(manifestPath))
				{
					int verified = 0;
					for (const auto& entry : manifest.getEntries())
					{
						// Plugin files live loose next to the exe, not in the archive
						if (entry.sourceType == "plugin")
						{
							fs::path pluginFile = config.outputDirectory / entry.archivePath;
							if (!fs::exists(pluginFile))
							{
								result.errorMessage = "Integrity check failed: missing plugin file: " + entry.archivePath;
								return false;
							}
							if (hashFile(pluginFile) != entry.contentHash)
							{
								result.errorMessage = "Integrity check failed: hash mismatch for " + entry.archivePath;
								return false;
							}
							verified++;
							continue;
						}

						if (!reader.contains(entry.archivePath))
						{
							result.errorMessage = "Integrity check failed: missing from archive: " + entry.archivePath;
							return false;
						}

						auto data = reader.readEntry(entry.archivePath);
						uint64_t actualHash = archive::hashBytes(data.data(), data.size());
						if (actualHash != entry.contentHash)
						{
							result.errorMessage = "Integrity check failed: hash mismatch for " + entry.archivePath;
							return false;
						}
						verified++;
					}
					vfLogInfo("Integrity verification passed ({} entries)", verified);
				}
			}
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

	fs::path GameExporter::findFontsDirectory() const
	{
		fs::path cwd = fs::current_path();

		fs::path candidate = cwd / "../../resources/fonts";
		if (fs::exists(candidate))
		{
			return fs::canonical(candidate);
		}

		candidate = cwd / "resources/fonts";
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
