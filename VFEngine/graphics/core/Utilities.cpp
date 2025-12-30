#include "Utilities.hpp"
#include "print/Logger.hpp"

namespace core {

	QueueFamilyIndices Utilities::findQueueFamiliesFromDevice(const vk::PhysicalDevice& device, const vk::SurfaceKHR& surface)
	{
		QueueFamilyIndices indices;
		const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

		if (debug) {
			loggerInfo("Found {} queue families.", queueFamilies.size());
		}

		int i = 0;
		for (const vk::QueueFamilyProperties& queueFamily : queueFamilies) {
			if (debug) {
				loggerInfo("Queue Family {}: Graphics: {}, Compute: {}, Transfer: {}",
					i,
					(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) ? "Yes" : "No",
					(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) ? "Yes" : "No",
					(queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) ? "Yes" : "No"
				);
			}

			if (device.getSurfaceSupportKHR(i, surface)) {
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

		// If no dedicated transfer queue found, fall back to graphics queue for transfers
		if (!indices.transferFamily.has_value() && indices.graphicsAndComputeFamily.has_value()) {
			indices.transferFamily = indices.graphicsAndComputeFamily;
		}

		if (!indices.isComplete()) {
			if (debug) {
				loggerWarning("Could not find complete queue family support.");
			}
		}

		if (debug && indices.hasDedicatedTransferQueue()) {
			loggerInfo("Found dedicated transfer queue family: {}", indices.transferFamily.value());
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
			loggerError("Failed to allocate command buffer: {}", err.what());
			throw;
		}

		vk::CommandBufferBeginInfo beginInfo{};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

		commandBuffer->begin(beginInfo);

		return commandBuffer;
	}

	void Utilities::endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer, const vk::Fence& renderFence)
	{
		commandBuffer->end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &(*commandBuffer);

		try {
			queue.submit(submitInfo, renderFence);
			queue.waitIdle();
		}
		catch (const vk::SystemError& err) {
			loggerError("Failed to submit command buffer: {}", err.what());
		}
	}

}
