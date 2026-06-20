#pragma once
#include <optional>

#include <vulkan/vulkan.hpp>
// Vulkan validation layers + debug messenger. Decoupled from the build config:
// VF_ENABLE_VALIDATION is defined only in the Graphics project's Debug filter
// (premake5.lua), so Debug = validation ON, Development/Release = OFF.
#ifdef VF_ENABLE_VALIDATION
constexpr bool debug = true;
#else
constexpr bool debug = false;
#endif

namespace core
{
	class Device;  // Forward declaration for thread-safe endSingleTimeCommands

	struct QueueFamilyIndices
	{
		std::optional<uint32_t> presentFamily;
		std::optional<uint32_t> graphicsAndComputeFamily;
		std::optional<uint32_t> transferFamily;
		std::optional<uint32_t> asyncComputeFamily;

		// When asyncComputeFamily == graphicsAndComputeFamily, we use a second queue from the same family
		bool asyncComputeUsesSecondQueue = false;

		bool isComplete() const
		{
			return presentFamily.has_value() && graphicsAndComputeFamily.has_value();
		}

		// Headless mode: only requires compute, no present queue needed
		bool isCompleteHeadless() const
		{
			return graphicsAndComputeFamily.has_value();
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

		bool hasAsyncComputeQueue() const
		{
			return asyncComputeFamily.has_value();
		}

		bool hasDedicatedComputeFamily() const
		{
			return asyncComputeFamily.has_value() &&
			       graphicsAndComputeFamily.has_value() &&
			       asyncComputeFamily.value() != graphicsAndComputeFamily.value();
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
		// Submits a one-time command buffer on the given queue. Serializes internally on the
		// Device's graphics-queue mutex (see .cpp), so it is safe to call from job-system workers
		// without racing the render thread's frame submit (VkQueue must not be used concurrently).
		static void endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr);

		// Thread-safe version that locks the graphics queue mutex and submits to the graphics queue.
		static void endSingleTimeCommands(const Device& device, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr);
	};
}
