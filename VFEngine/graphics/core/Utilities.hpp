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

		// Check if transfer queue is separate from graphics queue
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

	struct ImageInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::PhysicalDevice& physicalDevice;
		uint32_t width;
		uint32_t height;
		uint32_t layers;
		uint32_t mipLevels;
		vk::Format format;
		vk::ImageTiling tiling;
		vk::ImageUsageFlags usage;
		vk::MemoryPropertyFlags properties;
		vk::ImageCreateFlags imageFlags;

		explicit ImageInfoRequest(
			const vk::Device& logicalDevice,
			const vk::PhysicalDevice& physicalDevice,
			uint32_t width = 1,
			uint32_t height = 1,
			uint32_t layers = 1,
			uint32_t mipLevels = 1,
			vk::Format format = vk::Format::eR8G8B8A8Unorm,
			vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
			vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal,
			vk::ImageCreateFlags imageFlags = {}
		)
			: logicalDevice{ logicalDevice },
			physicalDevice{ physicalDevice },
			width{ width },
			height{ height },
			layers{ layers },
			mipLevels{ mipLevels },
			format{ format },
			tiling{ tiling },
			usage{ usage },
			properties{ properties },
			imageFlags{ imageFlags }
		{
		}
	};

	struct ImageViewInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::Image& image;
		vk::Format format;
		vk::ImageAspectFlags aspectFlags;
		vk::ImageViewType imageType;
		uint32_t layerCount;
		uint32_t mipLevels;

		explicit ImageViewInfoRequest(
			const vk::Device& logicalDevice,
			const vk::Image& image,
			vk::Format format = vk::Format::eR8G8B8A8Unorm,
			vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor,
			vk::ImageViewType imageType = vk::ImageViewType::e2D,
			uint32_t layerCount = 1,
			uint32_t mipLevels = 1
		)
			: logicalDevice{ logicalDevice },
			image{ image },
			format{ format },
			aspectFlags{ aspectFlags },
			imageType{ imageType },
			layerCount{ layerCount },
			mipLevels{ mipLevels }
		{
		}
	};

	struct BufferInfoRequest
	{
		const vk::Device& logicalDevice;
		const vk::PhysicalDevice& physicalDevice;
		vk::DeviceSize size;
		vk::BufferUsageFlags usage;
		vk::MemoryPropertyFlags properties;

		explicit BufferInfoRequest(
			const vk::Device& logicalDevice,
			const vk::PhysicalDevice& physicalDevice,
			vk::DeviceSize size = 1024, // Default size of 1024 bytes (1 KB)
			vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlags properties = vk::MemoryPropertyFlagBits::eDeviceLocal
		)
			: logicalDevice{ logicalDevice },
			physicalDevice{ physicalDevice },
			size{ size },
			usage{ usage },
			properties{ properties }
		{
		}
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
		static uint32_t findMemoryType(const vk::PhysicalDevice& device, uint32_t typeFilter,
			vk::MemoryPropertyFlags properties);

		static vk::UniqueCommandBuffer beginSingleTimeCommands(const vk::Device& device,
			const vk::CommandPool& commandPool);
		static void endSingleTimeCommands(const vk::Queue& queue, const vk::UniqueCommandBuffer& commandBuffer,
			const vk::Fence& renderFence = nullptr);

		static void transitionImageLayout(const vk::CommandBuffer& commandBuffer, vk::Image image,
			vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
			vk::ImageAspectFlags aspectMask, uint32_t layer = 1, uint32_t numMips = 1);

		static void createBuffer(const BufferInfoRequest& bufferInfo, vk::Buffer& buffer,
			vk::DeviceMemory& bufferMemory);
		static void createImage(const ImageInfoRequest& imageInfo, vk::Image& image, vk::DeviceMemory& imageMemory);
		static void createImageView(const ImageViewInfoRequest& imageInfoView, vk::ImageView& imageView);

		// Copy data to a buffer at a specific offset using a staging buffer
		// Useful for streaming uploads where chunks are uploaded incrementally
		static void copyToBuffer(
			const vk::Device& device,
			const vk::PhysicalDevice& physicalDevice,
			const vk::Queue& queue,
			const vk::CommandPool& commandPool,
			vk::Buffer dstBuffer,
			const void* srcData,
			vk::DeviceSize size,
			vk::DeviceSize offset = 0
		);
	};

	// Async transfer operation tracking
	struct TransferOperation {
		vk::Fence fence;
		vk::CommandBuffer commandBuffer;
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;
		bool completed = false;
	};

	// TransferManager: Handles async buffer/image transfers using fences
	// Uses dedicated transfer queue if available, falls back to graphics queue
	class TransferManager {
	public:
		TransferManager(const vk::Device& device, const vk::PhysicalDevice& physicalDevice,
		                const vk::Queue& transferQueue, uint32_t transferQueueFamily);
		~TransferManager();

		// Non-copyable
		TransferManager(const TransferManager&) = delete;
		TransferManager& operator=(const TransferManager&) = delete;

		// Submit async buffer copy - returns immediately, call pollTransfers to check completion
		void copyToBufferAsync(vk::Buffer dstBuffer, const void* srcData,
		                       vk::DeviceSize size, vk::DeviceSize offset = 0);

		// Poll and clean up completed transfers
		void pollTransfers();

		// Wait for all pending transfers to complete
		void waitAll();

		// Check if there are pending transfers
		bool hasPendingTransfers() const { return !pendingTransfers.empty(); }

	private:
		const vk::Device& device;
		const vk::PhysicalDevice& physicalDevice;
		const vk::Queue& transferQueue;
		uint32_t transferQueueFamily;

		vk::CommandPool commandPool;
		std::vector<TransferOperation> pendingTransfers;

		void cleanupTransfer(TransferOperation& op);
	};
}
