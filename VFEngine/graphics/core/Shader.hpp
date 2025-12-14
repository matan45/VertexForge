#pragma once

#ifndef VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#include <vulkan/vulkan.hpp>
#include <shaderc/shaderc.hpp>
#include <string>
#include "resource/ResourceManager.hpp"

namespace core {
	class Device;

	class Shader
	{
	private:
		Device& device;
		std::vector<vk::UniqueShaderModule> shaderModules;
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

	public:
		explicit Shader(Device& device);
		~Shader() = default;

		// Load and compile shader from file
		void readShader(std::string_view path);

		// Compile shader from source strings (for generated shaders)
		// The source should contain #type directives like the file format
		bool compileFromSource(std::string_view source, std::string_view shaderName = "generated");

		// Compile vertex and fragment shaders from separate source strings
		bool compileFromSources(std::string_view vertexSource, std::string_view fragmentSource,
		                       std::string_view shaderName = "generated");

		const std::vector<vk::PipelineShaderStageCreateInfo>& getShaderStages() const { return shaderStages; }

		void cleanUp();

	private:
		std::vector<uint32_t> compileShaderToSPIRV(std::string_view source, vk::ShaderStageFlagBits stage, std::string_view shaderName) const;
		void createShaderModule(const std::vector<uint32_t>& code, vk::ShaderStageFlagBits stage);
		vk::ShaderStageFlagBits shaderTypeToVulkanStage(resource::ShaderType shaderType) const;

		// Parse #type directive from source
		struct ShaderSource {
			resource::ShaderType type;
			std::string source;
		};
		std::vector<ShaderSource> parseShaderSource(std::string_view source) const;
	};
}


