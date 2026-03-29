#pragma once

#include "GameExportExport.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <functional>

#include <vulkan/vulkan.hpp>
#include <shaderc/shaderc.hpp>

namespace shaderCompiler
{
	struct CompileOptions
	{
		std::vector<std::string> macroNames;
		std::vector<std::pair<std::string, std::string>> macroValues;
		std::filesystem::path includeBasePath;
	};

	class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface
	{
	public:
		explicit ShaderIncluder(const std::filesystem::path& basePath)
			: basePath(basePath) {}

		shaderc_include_result* GetInclude(const char* requestedSource,
		                                    shaderc_include_type type,
		                                    const char* requestingSource,
		                                    size_t includeDepth) override;

		void ReleaseInclude(shaderc_include_result* data) override;

	private:
		std::filesystem::path basePath;
	};

	VF_GAMEEXPORT_API std::vector<uint32_t> compile(
		std::string_view source,
		vk::ShaderStageFlagBits stage,
		std::string_view name,
		const CompileOptions& options);

	VF_GAMEEXPORT_API std::string computePermutationKey(const CompileOptions& options);

	VF_GAMEEXPORT_API vk::ShaderStageFlagBits shaderTypeToVulkanStage(uint8_t shaderType);
}
