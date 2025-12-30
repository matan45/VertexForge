#pragma once

#include <vulkan/vulkan.hpp>

namespace core
{
	class MemoryUtilities
	{
	private:
		MemoryUtilities() = delete;
		~MemoryUtilities() = delete;

	public:
		static uint32_t findMemoryType(const vk::PhysicalDevice& device, uint32_t typeFilter,
			vk::MemoryPropertyFlags properties);
	};
}
