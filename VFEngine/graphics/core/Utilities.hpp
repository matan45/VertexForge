#pragma once
#include <optional>

#include <vulkan/vulkan.hpp>
#ifdef NDEBUG
constexpr bool debug = false;
#else
constexpr bool debug = true;
#endif

namespace core
{
	struct QueueFamilyIndices
	{
		std::optional<uint32_t> presentFamily;
		std::optional<uint32_t> graphicsAndComputeFamily;
		std::optional<uint32_t> transferFamily;

		bool isComplete() const
		{
			return presentFamily.has_value() && graphicsAndComputeFamily.has_value();
		}

		bool hasTransferQueue() const
		{
			return transferFamily.has_value();
		}

		bool hasDedicatedTransferQueue() const
		{
			return transferFamily.has_value() &&
			       graphicsAndComputeFamily.has_value() &&
			       transferFamily.value() != graphicsAndComputeFamily.value();
		}
	};

	struct SwapchainSupportDetails
	{
		vk::SurfaceCapabilitiesKHR capabilities;
		std::vector<vk::SurfaceFormatKHR> formats;
		std::vector<vk::PresentModeKHR> presentModes;
	};

	class Utilities
	{
	private:
		Utilities() = delete;
		~Utilities() = delete;

	public:
		static QueueFamilyIndices findQueueFamiliesFromDevice(const vk::PhysicalDevice& device,
			const vk::SurfaceKHR& surface);
		static SwapchainSupportDetails querySwapchainSupport(const vk::PhysicalDevice& device,
			const vk::SurfaceKHR& surface);

		static vk::UniqueCommandBuffer beginSingleTimeCommands(const vk::Device& device,
			const vk::CommandPool& commandPool);
		static void endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr);
	};
}
