#pragma once
#include <optional>
#include <source_location>

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
		// NOTE (queue threading diagnostics): this overload submits to the passed queue WITHOUT
		// locking Device::graphicsQueueMutex. If called off the render thread (e.g. a job-system
		// worker) while the frame submit runs, two threads use the same VkQueue -> validation
		// THREADING ERROR -> DEVICE_LOST. The defaulted source_location captures the caller site
		// so the log below pinpoints the offending call without touching any call site.
		static void endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr,
			std::source_location loc = std::source_location::current());

		// Thread-safe version that locks the graphics queue mutex
		static void endSingleTimeCommands(const Device& device, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr,
			std::source_location loc = std::source_location::current());
	};
}
