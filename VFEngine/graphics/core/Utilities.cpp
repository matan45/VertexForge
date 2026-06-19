#include "Utilities.hpp"
#include "Device.hpp"
#include "VulkanContext.hpp"
#include "print/Log.hpp"
#include <thread>
#include <sstream>
#include <mutex>

namespace core {

	namespace {
		// Stringify the current OS thread for the queue-threading diagnostics below.
		std::string currentThreadIdStr() {
			std::ostringstream oss;
			oss << std::this_thread::get_id();
			return oss.str();
		}
	}


	QueueFamilyIndices Utilities::findQueueFamiliesFromDevice(const vk::PhysicalDevice& device, const vk::SurfaceKHR& surface)
	{
		QueueFamilyIndices indices;
		const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

		int i = 0;
		for (const vk::QueueFamilyProperties& queueFamily : queueFamilies) {

			if (surface && device.getSurfaceSupportKHR(i, surface)) {
				indices.presentFamily = i;
			}

			if ((queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) && (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)) {
				indices.graphicsAndComputeFamily = i;
			}

			// Look for a dedicated transfer queue (transfer-only, no graphics)
			if ((queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
			    !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
			    !indices.transferFamily.has_value()) {
				indices.transferFamily = i;
			}

			i++;
		}

		// Prefer a second queue from the graphics+compute family (supports all pipeline stages
		// including fragment/mesh shader barriers used by async compute dispatches).
		// Only fall back to a dedicated compute-only family if no second queue is available.
		if (indices.graphicsAndComputeFamily.has_value()) {
			uint32_t gfxFamily = indices.graphicsAndComputeFamily.value();
			if (queueFamilies[gfxFamily].queueCount >= 2) {
				indices.asyncComputeFamily = gfxFamily;
				indices.asyncComputeUsesSecondQueue = true;
			}
			else {
				// Fall back to dedicated compute-only family
				for (int j = 0; j < static_cast<int>(queueFamilies.size()); j++) {
					if ((queueFamilies[j].queueFlags & vk::QueueFlagBits::eCompute) &&
					    !(queueFamilies[j].queueFlags & vk::QueueFlagBits::eGraphics)) {
						indices.asyncComputeFamily = j;
						break;
					}
				}
			}
		}

		// If no dedicated transfer queue found, fall back to graphics queue for transfers
		if (!indices.transferFamily.has_value() && indices.graphicsAndComputeFamily.has_value()) {
			indices.transferFamily = indices.graphicsAndComputeFamily;
		}

		return indices;
	}

	core::SwapchainSupportDetails Utilities::querySwapchainSupport(const vk::PhysicalDevice& device, const vk::SurfaceKHR& surface)
	{
		SwapchainSupportDetails details;
		details.capabilities = device.getSurfaceCapabilitiesKHR(surface);
		details.formats = device.getSurfaceFormatsKHR(surface);
		details.presentModes = device.getSurfacePresentModesKHR(surface);
		return details;
	}

	vk::UniqueCommandBuffer Utilities::beginSingleTimeCommands(const vk::Device& device, const vk::CommandPool& commandPool) {
		vk::CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		vk::UniqueCommandBuffer commandBuffer;
		try {
			commandBuffer = std::move(device.allocateCommandBuffersUnique(allocInfo).front());
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to allocate command buffer: {}", err.what());
			throw;
		}

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		commandBuffer->begin(beginInfo);

		return commandBuffer;
	}

	void Utilities::endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer, const vk::Fence& renderFence, std::source_location loc)
	{
		commandBuffer->end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &(*commandBuffer);

		// This overload used to submit without holding Device::graphicsQueueMutex. When called
		// from a job-system worker (e.g. BufferUtilities::copyToBuffer uploading the navmesh
		// debug overlay) concurrently with the render thread's frame submit, the same VkQueue was
		// used from two threads -> validation THREADING ERROR -> DEVICE_LOST. Serialize via the
		// same graphics-queue mutex the render/present paths use. All production callers pass the
		// graphics queue; a null global device (e.g. CPU tests) falls back to an unlocked submit.
		Device* dev = VulkanContext::getDeviceRaw();
		vfLogDebug("[QUEUE-DIAG] endSingleTimeCommands submit on thread {} from {}:{}",
			currentThreadIdStr(), loc.file_name(), loc.line());

		auto doSubmit = [&]() {
			try {
				queue.submit(submitInfo, renderFence);
				queue.waitIdle();
			}
			catch (const vk::SystemError& err) {
				vfLogError("Failed to submit command buffer: {}", err.what());
			}
		};

		if (dev) {
			std::lock_guard<std::mutex> lock(dev->getGraphicsQueueMutex());
			doSubmit();
		} else {
			doSubmit();
		}
	}

	void Utilities::endSingleTimeCommands(const Device& device, const vk::UniqueCommandBuffer& commandBuffer, const vk::Fence& renderFence, std::source_location loc)
	{
		commandBuffer->end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &(*commandBuffer);

		// Guarded path (locks graphicsQueueMutex). Logged at debug level so we can still see
		// which thread/site used the queue when correlating against the unguarded warnings.
		vfLogDebug("[QUEUE-DIAG] guarded endSingleTimeCommands submit on thread {} from {}:{} ({})",
			currentThreadIdStr(), loc.file_name(), loc.line(), loc.function_name());

		try {
			device.submitGraphics(submitInfo, renderFence);
			device.waitGraphicsIdle();
		}
		catch (const vk::SystemError& err) {
			vfLogError("Failed to submit command buffer: {}", err.what());
		}
	}

}
