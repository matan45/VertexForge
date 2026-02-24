#pragma once

#include <vulkan/vulkan.hpp>
#include <shaderc/shaderc.hpp>
#include <string>
#include <filesystem>
#include "resource/ResourceManager.hpp"

namespace core {
	class Device;

	class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
	public:
		explicit ShaderIncluder(const std::filesystem::path& basePath);

		shaderc_include_result* GetInclude(const char* requestedSource,
		                                   shaderc_include_type type,
		                                   const char* requestingSource,
		                                   size_t includeDepth) override;

		void ReleaseInclude(shaderc_include_result* data) override;

	private:
		std::filesystem::path basePath;
	};

	class Shader
	{
	private:
		Device& device;
		std::vector<vk::UniqueShaderModule> shaderModules;
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		std::string lastCompilationError;
		std::filesystem::path currentShaderBasePath;

	public:
		explicit Shader(Device& device);
		~Shader() = default;

		void readShader(std::string_view path);

		void addMacroDefinition(const std::string& name);

		bool compileFromSource(std::string_view source, std::string_view shaderName = "generated");

		bool compileFromSources(std::string_view vertexSource, std::string_view fragmentSource,
		                       std::string_view shaderName = "generated");

		const std::vector<vk::PipelineShaderStageCreateInfo>& getShaderStages() const { return shaderStages; }

		const std::string& getLastCompilationError() const { return lastCompilationError; }

		void cleanUp();

	private:
		std::vector<std::string> macroDefinitions;
		std::vector<uint32_t> compileShaderToSPIRV(std::string_view source, vk::ShaderStageFlagBits stage, std::string_view shaderName);
		void createShaderModule(const std::vector<uint32_t>& code, vk::ShaderStageFlagBits stage);
		vk::ShaderStageFlagBits shaderTypeToVulkanStage(resource::ShaderType shaderType) const;

		struct ShaderSource {
			resource::ShaderType type;
			std::string source;
		};
		std::vector<ShaderSource> parseShaderSource(std::string_view source) const;
	};
}


