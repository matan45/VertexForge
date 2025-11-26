#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
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

		void readShader(std::string_view path);
		const std::vector<vk::PipelineShaderStageCreateInfo>& getShaderStages() const { return shaderStages; }

		void cleanUp();
	private:
		std::vector<uint32_t> compileShaderToSPIRV(std::string_view path, vk::ShaderStageFlagBits stage, std::string_view shaderName) const;
		void createShaderModule(const std::vector<uint32_t>& code, vk::ShaderStageFlagBits stage);
		vk::ShaderStageFlagBits shaderTypeToVulkanStage(resource::ShaderType shaderType) const;
	};
}


