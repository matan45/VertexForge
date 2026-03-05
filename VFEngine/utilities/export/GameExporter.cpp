#include "../print/Log.hpp"
#include "GameExporter.hpp"
#include "ExeIconEmbedder.hpp"
#include "../serialization/ProjectSerialization.hpp"
#include <fstream>
#include <memory>
#include <cstdlib>

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

		report(0.25f, "Copying shaders...");
		if (!copyShaders(config, result)) return result;

		report(0.40f, "Copying assets...");
		if (!copyAssets(config, result)) return result;

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

		fs::create_directories(config.outputDirectory / "Assets", ec);
		if (ec)
		{
			result.errorMessage = "Failed to create Assets directory: " + ec.message();
			return false;
		}

		fs::create_directories(config.outputDirectory / "resources" / "shaders", ec);
		if (ec)
		{
			result.errorMessage = "Failed to create resources/shaders directory: " + ec.message();
			return false;
		}

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

		{
			char* vulkanSdkBuf = nullptr;
			size_t vulkanSdkLen = 0;
			_dupenv_s(&vulkanSdkBuf, &vulkanSdkLen, "VULKAN_SDK");
			std::unique_ptr<char, decltype(&free)> vulkanSdkGuard(vulkanSdkBuf, &free);

			if (vulkanSdkBuf)
			{
				fs::path shadercDll = fs::path(vulkanSdkBuf) / "Bin" / "shaderc_shared.dll";
				if (fs::exists(shadercDll))
				{
					std::error_code ec;
					fs::copy_file(shadercDll, config.outputDirectory / "shaderc_shared.dll",
								  fs::copy_options::overwrite_existing, ec);
					if (ec)
					{
						result.errorMessage = "Failed to copy shaderc_shared.dll: " + ec.message();
						return false;
					}
				}
				else
				{
					result.errorMessage = "shaderc_shared.dll not found at: " + shadercDll.string();
					return false;
				}
			}
			else
			{
				result.errorMessage = "VULKAN_SDK environment variable not set. Cannot locate shaderc_shared.dll";
				return false;
			}
		}

		return true;
	}

	bool GameExporter::copyShaders(const ExportConfig& config, ExportResult& result)
	{
		fs::path shaderSrc = findShaderDirectory();
		fs::path shaderDst = config.outputDirectory / "resources" / "shaders";

		copyDirectoryRecursive(shaderSrc, shaderDst, result);

		// Copy IBL resources (pre-baked BRDF LUT)
		fs::path iblSrc = findIBLDirectory();
		if (fs::exists(iblSrc))
		{
			fs::path iblDst = config.outputDirectory / "resources" / "ibl";
			std::error_code ec;
			fs::create_directories(iblDst, ec);
			copyDirectoryRecursive(iblSrc, iblDst, result);
		}

		return true;
	}

	bool GameExporter::copyAssets(const ExportConfig& config, ExportResult& result)
	{
		fs::path assetsDst = config.outputDirectory / "Assets";
		copyDirectoryFilteredRecursive(config.workingDirectory, assetsDst, result);
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
		fs::path assetsDir = config.outputDirectory / "Assets";
		fs::path shadersDir = config.outputDirectory / "resources" / "shaders";

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

		if (!fs::exists(assetsDir) || fs::is_empty(assetsDir))
		{
			result.warnings.push_back("Assets directory is empty");
		}

		if (!fs::exists(shadersDir) || fs::is_empty(shadersDir))
		{
			result.errorMessage = "Validation failed: shaders directory is empty";
			return false;
		}

		fs::path scenePath = assetsDir / config.startupScene;
		if (!fs::exists(scenePath))
		{
			result.errorMessage = "Validation failed: startup scene not found in exported assets: " + config.startupScene;
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

	void GameExporter::copyDirectoryFilteredRecursive(const fs::path& src, const fs::path& dst, ExportResult& result) const
	{
		std::error_code ec;
		fs::create_directories(dst, ec);

		for (const auto& entry : fs::recursive_directory_iterator(src, ec))
		{
			fs::path relativePath = fs::relative(entry.path(), src, ec);
			fs::path destPath = dst / relativePath;

			if (entry.is_directory())
			{
				fs::create_directories(destPath, ec);
				continue;
			}

			// Skip .mt source files (keep .mtcLib compiled bytecode)
			auto ext = entry.path().extension().string();
			if (ext == ".mt")
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
