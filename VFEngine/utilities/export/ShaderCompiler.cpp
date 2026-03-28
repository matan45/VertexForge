#include "ShaderCompiler.hpp"
#include "print/Log.hpp"
#include "resource/Types.hpp"

#include <fstream>
#include <sstream>

namespace shaderCompiler
{
	shaderc_include_result* ShaderIncluder::GetInclude(const char* requestedSource,
	                                                    shaderc_include_type type,
	                                                    const char* requestingSource,
	                                                    size_t includeDepth)
	{
		std::filesystem::path includePath;
		if (type == shaderc_include_type_relative)
		{
			std::filesystem::path requestingDir = std::filesystem::path(requestingSource).parent_path();
			if (requestingDir.empty())
			{
				requestingDir = basePath;
			}
			else if (!requestingDir.is_absolute())
			{
				requestingDir = basePath / requestingDir;
			}
			includePath = requestingDir / requestedSource;
		}
		else
		{
			includePath = basePath / requestedSource;
		}

		includePath = std::filesystem::weakly_canonical(includePath);

		std::ifstream file(includePath);
		if (!file.is_open())
		{
			auto* result = new shaderc_include_result;
			result->source_name = "";
			result->source_name_length = 0;

			auto* errorMsg = new std::string("Failed to open include file: " + includePath.string());
			result->content = errorMsg->c_str();
			result->content_length = errorMsg->size();
			result->user_data = errorMsg;
			return result;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		auto* result = new shaderc_include_result;

		auto* sourceName = new std::string(includePath.string());
		result->source_name = sourceName->c_str();
		result->source_name_length = sourceName->size();

		auto* content = new std::string(buffer.str());
		result->content = content->c_str();
		result->content_length = content->size();

		auto* userData = new std::pair<std::string*, std::string*>(sourceName, content);
		result->user_data = userData;

		return result;
	}

	void ShaderIncluder::ReleaseInclude(shaderc_include_result* data)
	{
		if (data)
		{
			if (data->user_data)
			{
				if (data->source_name_length > 0)
				{
					auto* userData = static_cast<std::pair<std::string*, std::string*>*>(data->user_data);
					delete userData->first;
					delete userData->second;
					delete userData;
				}
				else
				{
					delete static_cast<std::string*>(data->user_data);
				}
			}
			delete data;
		}
	}

	std::vector<uint32_t> compile(
		std::string_view source,
		vk::ShaderStageFlagBits stage,
		std::string_view name,
		const CompileOptions& options)
	{
		shaderc_shader_kind kind;

		switch (stage)
		{
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
			vfLogError("ShaderCompiler: Unsupported shader stage");
			return {};
		}

		shaderc::Compiler compiler;
		shaderc::CompileOptions compileOptions;
		compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		compileOptions.SetOptimizationLevel(shaderc_optimization_level_performance);

		for (const auto& macro : options.macroNames)
		{
			compileOptions.AddMacroDefinition(macro);
		}
		for (const auto& [macroName, macroValue] : options.macroValues)
		{
			compileOptions.AddMacroDefinition(macroName, macroValue);
		}

		if (!options.includeBasePath.empty())
		{
			compileOptions.SetIncluder(std::make_unique<ShaderIncluder>(options.includeBasePath));
		}

		shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
			source.data(), kind, name.data(), compileOptions);

		if (result.GetCompilationStatus() != shaderc_compilation_status_success)
		{
			vfLogError("ShaderCompiler: Compilation failed for {}: {}", name, result.GetErrorMessage());
			return {};
		}

		return {result.cbegin(), result.cend()};
	}

	std::string computePermutationKey(const CompileOptions& options)
	{
		if (options.macroNames.empty() && options.macroValues.empty())
		{
			return "";
		}

		std::vector<std::string> sorted;
		for (const auto& name : options.macroNames)
		{
			sorted.push_back(name);
		}
		for (const auto& [name, value] : options.macroValues)
		{
			sorted.push_back(name + "=" + value);
		}
		std::sort(sorted.begin(), sorted.end());

		std::string combined;
		for (const auto& s : sorted)
		{
			if (!combined.empty()) combined += ",";
			combined += s;
		}

		size_t hash = std::hash<std::string>{}(combined);

		char buf[17];
		snprintf(buf, sizeof(buf), "%016zx", hash);
		return std::string(buf);
	}

	vk::ShaderStageFlagBits shaderTypeToVulkanStage(uint8_t shaderType)
	{
		using enum vk::ShaderStageFlagBits;
		using enum resource::ShaderType;

		switch (static_cast<resource::ShaderType>(shaderType))
		{
		case VERTEX: return eVertex;
		case FRAGMENT: return eFragment;
		case COMPUTE: return eCompute;
		case GEOMETRY: return eGeometry;
		case TESS_CONTROL: return eTessellationControl;
		case TESS_EVALUATION: return eTessellationEvaluation;
		case MESH: return eMeshEXT;
		case TASK: return eTaskEXT;
		default:
			vfLogError("ShaderCompiler: Unsupported ShaderType");
			return vk::ShaderStageFlagBits();
		}
	}
}
