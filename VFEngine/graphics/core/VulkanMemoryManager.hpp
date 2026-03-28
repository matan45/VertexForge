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
			vk::DeviceSize blockSize, bool hostVisible);
		~VulkanMemoryBlock();

		VulkanMemoryBlock(const VulkanMemoryBlock&) = delete;
		VulkanMemoryBlock& operator=(const VulkanMemoryBlock&) = delete;

		VulkanAllocation allocate(vk::DeviceSize size, vk::DeviceSize alignment);
		void free(vk::DeviceSize offset, vk::DeviceSize size);

		vk::DeviceMemory getMemory() const { return memory; }
		vk::DeviceSize getBlockSize() const { return blockSize; }
		uint32_t getMemoryTypeIndex() const { return memoryTypeIndex; }
		bool isHostVisible() const { return hostVisible; }
		void* getBaseMappedPtr() const { return baseMappedPtr; }

		memory::AllocatorStats getStats() const { return allocator.getStats(); }

	private:
		const vk::Device& device;
		vk::DeviceMemory memory;
		vk::DeviceSize blockSize;
		uint32_t memoryTypeIndex;
		bool hostVisible;
		void* baseMappedPtr = nullptr;
		memory::FreeListHeapAllocator allocator;
	};

	class VulkanMemoryManager
	{
	public:
		explicit VulkanMemoryManager(Device& device);
		~VulkanMemoryManager();

		VulkanMemoryManager(const VulkanMemoryManager&) = delete;
		VulkanMemoryManager& operator=(const VulkanMemoryManager&) = delete;

		VulkanAllocation allocate(const vk::MemoryRequirements& memRequirements,
			vk::MemoryPropertyFlags properties,
			bool needsDeviceAddress = false);

		void free(const VulkanAllocation& allocation);

		// Legacy API: allocate/free dedicated allocations by DeviceMemory handle
		// Used by old createBuffer/createImage callers that store vk::DeviceMemory
		VulkanAllocation allocateLegacy(const vk::MemoryRequirements& memRequirements,
			vk::MemoryPropertyFlags properties, bool needsDeviceAddress = false);
		void freeLegacy(vk::DeviceMemory memory);

		struct MemoryTypeStats
		{
			uint32_t memoryTypeIndex = 0;
			uint32_t blockCount = 0;
			vk::DeviceSize totalCapacity = 0;
			vk::DeviceSize totalAllocated = 0;
			float fragmentationPercent = 0.0f;
		};

		std::vector<MemoryTypeStats> getStats() const;

	private:
		Device& ownerDevice;
		const vk::Device& device;
		const vk::PhysicalDevice& physicalDevice;

		struct MemoryTypeData
		{
			std::vector<std::unique_ptr<VulkanMemoryBlock>> blocks;
		};

		std::unordered_map<uint32_t, MemoryTypeData> memoryTypes;
		mutable std::mutex managerMutex;

		// Dedicated allocations (oversized, device-address)
		struct DedicatedAllocation
		{
			vk::DeviceMemory memory;
			vk::DeviceSize size;
			void* mappedPtr = nullptr;
			bool hostVisible = false;
		};
		std::vector<DedicatedAllocation> dedicatedAllocations;

		uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
		bool isHostVisible(uint32_t memoryTypeIndex) const;
		vk::DeviceSize getBlockSizeForType(uint32_t memoryTypeIndex) const;

		VulkanAllocation allocateDedicated(vk::DeviceSize size, uint32_t memoryTypeIndex,
			bool hostVisible, bool needsDeviceAddress, bool autoMap = true);
	};
}
