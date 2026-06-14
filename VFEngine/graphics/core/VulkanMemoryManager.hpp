#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include "memory/FreeListHeapAllocator.hpp"

namespace core
{
	class Device;
	class VulkanMemoryBlock;

	enum class GpuResourceType : uint8_t { Buffer, Image };

	struct VulkanAllocation
	{
		vk::DeviceMemory memory;
		vk::DeviceSize offset = 0;
		vk::DeviceSize size = 0;
		void* mappedPtr = nullptr;
		VulkanMemoryBlock* block = nullptr;
		bool isDedicated = false;

		bool isValid() const { return memory && size > 0; }
		explicit operator bool() const { return isValid(); }
	};

	class VulkanMemoryBlock
	{
	public:
		VulkanMemoryBlock(const vk::Device& device, uint32_t memoryTypeIndex,
			vk::DeviceSize blockSize, bool hostVisible, bool deviceAddress = false);
		~VulkanMemoryBlock();

		VulkanMemoryBlock(const VulkanMemoryBlock&) = delete;
		VulkanMemoryBlock& operator=(const VulkanMemoryBlock&) = delete;

		[[nodiscard]] VulkanAllocation allocate(vk::DeviceSize size, vk::DeviceSize alignment,
			GpuResourceType resourceType, vk::DeviceSize bufferImageGranularity);
		void free(vk::DeviceSize offset, vk::DeviceSize size);

		vk::DeviceMemory getMemory() const { return memory; }
		vk::DeviceSize getBlockSize() const { return blockSize; }
		uint32_t getMemoryTypeIndex() const { return memoryTypeIndex; }
		bool isHostVisible() const { return hostVisible; }
		bool isDeviceAddress() const { return deviceAddress; }
		void* getBaseMappedPtr() const { return baseMappedPtr; }

		memory::AllocatorStats getStats() const { return allocator.getStats(); }
		// O(1) hint: skip this block in the search loop if it can't possibly fit.
		vk::DeviceSize freeBytes() const { return allocator.getFreeBytes(); }
		std::vector<memory::FreeSpan> getFreeSpans() const { return allocator.getFreeSpans(); }

	private:
		const vk::Device& device;
		vk::DeviceMemory memory;
		vk::DeviceSize blockSize;
		uint32_t memoryTypeIndex;
		bool hostVisible;
		bool deviceAddress;
		void* baseMappedPtr = nullptr;
		bool hasBuffers = false;
		bool hasImages = false;
		memory::FreeListHeapAllocator allocator;
	};

	class VulkanMemoryManager
	{
	public:
		explicit VulkanMemoryManager(Device& device);
		~VulkanMemoryManager();

		VulkanMemoryManager(const VulkanMemoryManager&) = delete;
		VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

		[[nodiscard]] VulkanAllocation allocate(const vk::MemoryRequirements& memRequirements,
			vk::MemoryPropertyFlags properties,
			bool needsDeviceAddress = false,
			GpuResourceType resourceType = GpuResourceType::Buffer);

		void free(const VulkanAllocation& allocation);

		// Reclaim empty blocks to return memory to the OS.
		// Call at a safe point when no GPU work is in flight (e.g., after device idle).
		void reclaimEmptyBlocks();

		// Refresh the heavier diagnostics consumed by the editor memory window:
		// queries the real VRAM budget (throttled) and assembles the per-block
		// occupancy snapshot. Call periodically (a few times per second), not in
		// the allocation hot path.
		void refreshDiagnostics() const;

		struct MemoryTypeStats
		{
			uint32_t memoryTypeIndex = 0;
			uint32_t blockCount = 0;
			vk::DeviceSize totalCapacity = 0;
			vk::DeviceSize totalAllocated = 0;
			float fragmentationPercent = 0.0f;
		};

		std::vector<MemoryTypeStats> getStats() const;
		void updateGlobalStats() const;

	private:
		Device& ownerDevice;
		const vk::Device& device;
		const vk::PhysicalDevice& physicalDevice;

		struct MemoryTypeData
		{
			// Plain sub-allocation blocks and device-address-capable blocks are kept
			// separate so a device-address request only draws from memory that was
			// allocated with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT.
			std::vector<std::unique_ptr<VulkanMemoryBlock>> blocks;
			std::vector<std::unique_ptr<VulkanMemoryBlock>> deviceAddressBlocks;
		};

		std::unordered_map<uint32_t, MemoryTypeData> memoryTypes;
		mutable std::mutex managerMutex;
		vk::DeviceSize bufferImageGranularity = 1;

		// Dedicated allocations (oversized) — keyed by their VkDeviceMemory handle
		// for O(1) free.
		struct DedicatedAllocation
		{
			vk::DeviceMemory memory;
			vk::DeviceSize size;
			void* mappedPtr = nullptr;
			bool hostVisible = false;
			bool deviceAddress = false;
		};
		std::unordered_map<VkDeviceMemory, DedicatedAllocation> dedicatedAllocations;

		// VK_EXT_memory_budget query throttle (refreshDiagnostics runs often; the
		// syscall need not). The driver query runs once every kVramBudgetQueryInterval
		// calls; the published vram* atomics are reused in between.
		mutable bool memoryBudgetSupported = false;
		mutable bool memoryBudgetChecked = false;
		mutable uint32_t vramBudgetQueryCounter = 0;
		static constexpr uint32_t kVramBudgetQueryInterval = 4;

		uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
		bool isHostVisible(uint32_t memoryTypeIndex) const;
		vk::DeviceSize getBlockSizeForType(uint32_t memoryTypeIndex) const;

		VulkanAllocation allocateDedicated(vk::DeviceSize size, uint32_t memoryTypeIndex,
			bool hostVisible, bool needsDeviceAddress, bool autoMap = true);

		// Try to sub-allocate from an existing block list, appending a new block if
		// none fit. Caller holds managerMutex.
		VulkanAllocation allocateFromBlocks(std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks,
			const vk::MemoryRequirements& memRequirements, uint32_t memTypeIndex,
			bool hostVisible, bool deviceAddress, vk::DeviceSize blockSize,
			GpuResourceType resourceType);

		void queryVramBudget() const;       // fills GpuAllocationStats vram* atomics
		void buildSnapshot() const;         // publishes GpuMemorySnapshot
	};
}
