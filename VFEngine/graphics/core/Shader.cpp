#include "Shader.hpp"
#include "Device.hpp"
#include "print/Logger.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace core {

	ShaderIncluder::ShaderIncluder(const std::filesystem::path& basePath)
		: basePath(basePath)
	{
	}

	shaderc_include_result* ShaderIncluder::GetInclude(const char* requestedSource,
	                                                    shaderc_include_type type,
	                                                    const char* requestingSource,
	                                                    size_t includeDepth)
	{
		std::filesystem::path includePath;
		if (type == shaderc_include_type_relative) {
			std::filesystem::path requestingDir = std::filesystem::path(requestingSource).parent_path();
			if (requestingDir.empty()) {
				requestingDir = basePath;
			} else if (!requestingDir.is_absolute()) {
				requestingDir = basePath / requestingDir;
			}
			includePath = requestingDir / requestedSource;
		} else {
			includePath = basePath / requestedSource;
		}

		includePath = std::filesystem::weakly_canonical(includePath);

		std::ifstream file(includePath);
		if (!file.is_open()) {
			auto* result = new shaderc_include_result;
			result->source_name = "";
			result->source_name_length = 0;

			std::string* errorMsg = new std::string("Failed to open include file: " + includePath.string());
			result->content = errorMsg->c_str();
			result->content_length = errorMsg->size();
			result->user_data = errorMsg;
			return result;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		auto* result = new shaderc_include_result;

		std::string* sourceName = new std::string(includePath.string());
		result->source_name = sourceName->c_str();
		result->source_name_length = sourceName->size();

		std::string* content = new std::string(buffer.str());
		result->content = content->c_str();
		result->content_length = content->size();

		auto* userData = new std::pair<std::string*, std::string*>(sourceName, content);
		result->user_data = userData;

		return result;
	}

	void ShaderIncluder::ReleaseInclude(shaderc_include_result* data)
	{
		if (data) {
			if (data->user_data) {
				if (data->source_name_length > 0) {
					auto* userData = static_cast<std::pair<std::string*, std::string*>*>(data->user_data);
					delete userData->first;
					delete userData->second;
					delete userData;
				} else {
					delete static_cast<std::string*>(data->user_data);
				}
			}
			delete data;
		}
	}

	Shader::Shader(Device& device) :device{ device }
	{
	}

	void Shader::readShader(std::string_view path)
	{
		auto futureShaders = resource::ResourceManager::loadShaderAsync(path);
		std::string shaderName = std::filesystem::path(path).stem().string();

		currentShaderBasePath = std::filesystem::path(path).parent_path();
		if (currentShaderBasePath.empty()) {
			currentShaderBasePath = ".";
		}

		auto shaders = futureShaders.get();
		if (!shaders) {
			loggerError("Failed to load shaders from file: {}", path);
			return;
		}

		for (const auto& shader : *shaders) {
			vk::ShaderStageFlagBits stage = shaderTypeToVulkanStage(shader.type);
			std::vector<uint32_t> spirvCode = compileShaderToSPIRV(shader.source, stage, shaderName);
			createShaderModule(spirvCode, stage);
		}
	}

	void Shader::addMacroDefinition(const std::string& name)
	{
		macroDefinitions.push_back(name);
	}

	void Shader::cleanUp()
	{
		shaderModules.clear();
	}

	std::vector<uint32_t> Shader::compileShaderToSPIRV(std::string_view source, vk::ShaderStageFlagBits stage, std::string_view shaderName)
	{
		shaderc_shader_kind kind;

		switch (stage) {
			using enum vk::ShaderStageFlagBits;
		case eVertex: kind = shaderc_vertex_shader; break;
		case eFragment: kind = shaderc_fragment_shader; break;
		case eGeometry: kind = shaderc_geometry_shader; break;
		case eCompute: kind = shaderc_compute_shader; break;
		case eTessellationControl: kind = shaderc_tess_control_shader; break;
		case eTessellationEvaluation: kind = shaderc_tess_evaluation_shader; break;
		case eMeshEXT: kind = shaderc_mesh_shader; break;
		case eTaskEXT: kind = shaderc_task_shader; break;
		default:
			loggerError("Unsupported shader stage");
			lastCompilationError = "Unsupported shader stage";
			return {};
		}

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		options.SetOptimizationLevel(shaderc_optimization_level_performance);

		for (const auto& name : macroDefinitions) {
			options.AddMacroDefinition(name);
		}

		if (!currentShaderBasePath.empty()) {
			options.SetIncluder(std::make_unique<ShaderIncluder>(currentShaderBasePath));
		}

		shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(source.data(), kind, shaderName.data(), options);

		if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
			lastCompilationError = result.GetErrorMessage();
			loggerError("Shader compilation failed for {}: {}", shaderName, lastCompilationError);
			vfLogError("Shader compilation failed for {}: {}", shaderName, lastCompilationError);
			return {};
		}

		return { result.cbegin(), result.cend() };
	}

	void Shader::createShaderModule(const std::vector<uint32_t>& code, vk::ShaderStageFlagBits stage)
	{
		vk::ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = code.size() * sizeof(uint32_t);
		createInfo.pCode = code.data();

		vk::UniqueShaderModule shaderModule = device.getLogicalDevice().createShaderModuleUnique(createInfo);

		vk::PipelineShaderStageCreateInfo shaderStageInfo{};
		shaderStageInfo.stage = stage;
		shaderStageInfo.module = shaderModule.get();
		shaderStageInfo.pName = "main";

		shaderModules.push_back(std::move(shaderModule));
		shaderStages.push_back(shaderStageInfo);
	}

	vk::ShaderStageFlagBits Shader::shaderTypeToVulkanStage(resource::ShaderType shaderType) const
	{
		switch (shaderType) {
		using enum vk::ShaderStageFlagBits;
		using enum resource::ShaderType;
		case VERTEX:
			return eVertex;
		case FRAGMENT:
			return eFragment;
		case COMPUTE:
			return eCompute;
		case GEOMETRY:
			return eGeometry;
		case TESS_CONTROL:
			return eTessellationControl;
		case TESS_EVALUATION:
			return eTessellationEvaluation;
		case MESH:
			return eMeshEXT;
		case TASK:
			return eTaskEXT;
		default:
			loggerError("Unsupported ShaderType.");
			return vk::ShaderStageFlagBits();
		}
	}

	bool Shader::compileFromSource(std::string_view source, std::string_view shaderName)
	{
		if (currentShaderBasePath.empty()) {
			currentShaderBasePath = ".";
		}

		auto shaders = parseShaderSource(source);
		if (shaders.empty()) {
			loggerError("No shader types found in source: {}", shaderName);
			return false;
		}

		bool success = true;
		for (const auto& shader : shaders) {
			vk::ShaderStageFlagBits stage = shaderTypeToVulkanStage(shader.type);
			std::vector<uint32_t> spirvCode = compileShaderToSPIRV(shader.source, stage, shaderName);
			if (spirvCode.empty()) {
				success = false;
				continue;
			}
			createShaderModule(spirvCode, stage);
		}

		return success;
	}

	bool Shader::compileFromSources(std::string_view vertexSource, std::string_view fragmentSource,
	                               std::string_view shaderName)
	{
		lastCompilationError.clear();
		bool success = true;

		if (!vertexSource.empty()) {
			std::string vsSource(vertexSource);
			size_t typePos = vsSource.find("#type");
			if (typePos != std::string::npos) {
				size_t lineEnd = vsSource.find('\n', typePos);
				if (lineEnd != std::string::npos) {
					vsSource = vsSource.substr(lineEnd + 1);
				}
			}

			std::vector<uint32_t> spirvCode = compileShaderToSPIRV(vsSource, vk::ShaderStageFlagBits::eVertex, shaderName);
			if (spirvCode.empty()) {
				success = false;
			} else {
				createShaderModule(spirvCode, vk::ShaderStageFlagBits::eVertex);
			}
		}

		if (!fragmentSource.empty()) {
			std::string fsSource(fragmentSource);
			size_t typePos = fsSource.find("#type");
			if (typePos != std::string::npos) {
				size_t lineEnd = fsSource.find('\n', typePos);
				if (lineEnd != std::string::npos) {
					fsSource = fsSource.substr(lineEnd + 1);
				}
			}

			std::vector<uint32_t> spirvCode = compileShaderToSPIRV(fsSource, vk::ShaderStageFlagBits::eFragment, shaderName);
			if (spirvCode.empty()) {
				success = false;
			} else {
				createShaderModule(spirvCode, vk::ShaderStageFlagBits::eFragment);
			}
		}

		return success;
	}

	std::vector<Shader::ShaderSource> Shader::parseShaderSource(std::string_view source) const
	{
		std::vector<ShaderSource> result;

		std::string sourceStr(source);
		size_t pos = 0;

		while (pos < sourceStr.size()) {
			size_t typeStart = sourceStr.find("#type", pos);
			if (typeStart == std::string::npos) break;

			size_t lineEnd = sourceStr.find('\n', typeStart);
			if (lineEnd == std::string::npos) lineEnd = sourceStr.size();

			std::string typeLine = sourceStr.substr(typeStart + 5, lineEnd - typeStart - 5);
			size_t start = typeLine.find_first_not_of(" \t\r");
			size_t end = typeLine.find_last_not_of(" \t\r");
			if (start != std::string::npos && end != std::string::npos) {
				typeLine = typeLine.substr(start, end - start + 1);
			}

			resource::ShaderType shaderType = resource::ShaderType::UNKNOWN;
			if (typeLine == "VERTEX" || typeLine == "vertex") {
				shaderType = resource::ShaderType::VERTEX;
			} else if (typeLine == "FRAGMENT" || typeLine == "fragment") {
				shaderType = resource::ShaderType::FRAGMENT;
			} else if (typeLine == "COMPUTE" || typeLine == "compute") {
				shaderType = resource::ShaderType::COMPUTE;
			} else if (typeLine == "GEOMETRY" || typeLine == "geometry") {
				shaderType = resource::ShaderType::GEOMETRY;
			} else if (typeLine == "TESS_CONTROL" || typeLine == "tess_control") {
				shaderType = resource::ShaderType::TESS_CONTROL;
			} else if (typeLine == "TESS_EVALUATION" || typeLine == "tess_evaluation") {
				shaderType = resource::ShaderType::TESS_EVALUATION;
			} else if (typeLine == "MESH" || typeLine == "mesh") {
				shaderType = resource::ShaderType::MESH;
			} else if (typeLine == "TASK" || typeLine == "task") {
				shaderType = resource::ShaderType::TASK;
			}

			if (shaderType != resource::ShaderType::UNKNOWN) {
				size_t sourceStart = lineEnd + 1;
				size_t nextType = sourceStr.find("#type", sourceStart);
				size_t sourceEnd = (nextType != std::string::npos) ? nextType : sourceStr.size();

				ShaderSource ss;
				ss.type = shaderType;
				ss.source = sourceStr.substr(sourceStart, sourceEnd - sourceStart);
				result.push_back(ss);

				pos = sourceEnd;
			} else {
				pos = lineEnd + 1;
			}
		}

		return result;
	}

}