#include "VulkanMemoryManager.hpp"
#include "Device.hpp"
#include "MemoryUtilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "print/Log.hpp"

namespace core
{
	// --- VulkanMemoryBlock ---

	VulkanMemoryBlock::VulkanMemoryBlock(const vk::Device& device, uint32_t memoryTypeIndex,
		vk::DeviceSize blockSize, bool hostVisible)
		: device(device)
		, blockSize(blockSize)
		, memoryTypeIndex(memoryTypeIndex)
		, hostVisible(hostVisible)
		, allocator(blockSize, "VulkanMemoryBlock")
	{
		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.allocationSize = blockSize;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		memory = device.allocateMemory(allocInfo);

		if (hostVisible) {
			baseMappedPtr = device.mapMemory(memory, 0, blockSize, {});
		}

		vfLogInfo("VulkanMemoryBlock: Allocated {}MB block (type {}{})",
			blockSize / (1024 * 1024), memoryTypeIndex,
			hostVisible ? ", host-visible" : "");
	}

	VulkanMemoryBlock::~VulkanMemoryBlock()
	{
		if (hostVisible && baseMappedPtr) {
			device.unmapMemory(memory);
		}
		if (memory) {
			device.freeMemory(memory);
		}
	}

	VulkanAllocation VulkanMemoryBlock::allocate(vk::DeviceSize size, vk::DeviceSize alignment)
	{
		auto handle = allocator.allocate(size, alignment);
		if (!handle.isValid()) {
			return {};
		}

		VulkanAllocation allocation;
		allocation.memory = memory;
		allocation.offset = handle.offset;
		allocation.size = size;
		allocation.block = this;
		allocation.isDedicated = false;

		if (hostVisible && baseMappedPtr) {
			allocation.mappedPtr = static_cast<uint8_t*>(baseMappedPtr) + handle.offset;
		}

		return allocation;
	}

	void VulkanMemoryBlock::free(vk::DeviceSize offset, vk::DeviceSize size)
	{
		memory::AllocationHandle handle;
		handle.offset = offset;
		handle.size = size;
		handle.allocatorId = 0; // Not validated in free path
		allocator.free(handle);
	}

	// --- VulkanMemoryManager ---

	VulkanMemoryManager::VulkanMemoryManager(Device& device)
		: ownerDevice(device)
		, device(device.getLogicalDevice())
		, physicalDevice(device.getPhysicalDevice())
	{
		vfLogInfo("VulkanMemoryManager: Initialized");
	}

	VulkanMemoryManager::~VulkanMemoryManager()
	{
		std::lock_guard lock(managerMutex);

		// Don't free dedicated allocations here - callers own their memory lifecycle
		// and will free via freeLegacy/destroyBuffer during their own cleanup.
		// Only log any remaining as a diagnostic.
		if (!dedicatedAllocations.empty()) {
			vfLogInfo("VulkanMemoryManager: {} dedicated allocations still tracked at shutdown (callers handle cleanup)",
				dedicatedAllocations.size());
		}
		dedicatedAllocations.clear();

		// Blocks are cleaned up by their destructors via unique_ptr
		memoryTypes.clear();

		vfLogInfo("VulkanMemoryManager: Destroyed");
	}

	VulkanAllocation VulkanMemoryManager::allocate(const vk::MemoryRequirements& memRequirements,
		vk::MemoryPropertyFlags properties,
		bool needsDeviceAddress)
	{
		if (memRequirements.size == 0) {
			return {};
		}

		std::lock_guard lock(managerMutex);

		uint32_t memTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
		bool hostVis = isHostVisible(memTypeIndex);
		vk::DeviceSize blockSize = getBlockSizeForType(memTypeIndex);

		// Use dedicated allocation for oversized requests or device-address buffers
		if (needsDeviceAddress || memRequirements.size > blockSize / 2) {
			return allocateDedicated(memRequirements.size, memTypeIndex, hostVis, needsDeviceAddress);
		}

		// Try existing blocks first
		auto& typeData = memoryTypes[memTypeIndex];
		for (auto& block : typeData.blocks) {
			auto allocation = block->allocate(memRequirements.size, memRequirements.alignment);
			if (allocation.isValid()) {
				return allocation;
			}
		}

		// All existing blocks full - create a new one
		auto newBlock = std::make_unique<VulkanMemoryBlock>(device, memTypeIndex, blockSize, hostVis);
		auto allocation = newBlock->allocate(memRequirements.size, memRequirements.alignment);
		typeData.blocks.push_back(std::move(newBlock));

		return allocation;
	}

	void VulkanMemoryManager::free(const VulkanAllocation& allocation)
	{
		if (!allocation.isValid()) {
			return;
		}

		std::lock_guard lock(managerMutex);

		if (allocation.isDedicated) {
			// Find and remove the dedicated allocation
			for (auto it = dedicatedAllocations.begin(); it != dedicatedAllocations.end(); ++it) {
				if (it->memory == allocation.memory) {
					if (it->hostVisible && it->mappedPtr) {
						device.unmapMemory(it->memory);
					}
					device.freeMemory(it->memory);
					dedicatedAllocations.erase(it);
					return;
				}
			}
			vfLogWarning("VulkanMemoryManager: Attempted to free unknown dedicated allocation");
			return;
		}

		if (allocation.block) {
			allocation.block->free(allocation.offset, allocation.size);
		}
	}

	VulkanAllocation VulkanMemoryManager::allocateLegacy(const vk::MemoryRequirements& memRequirements,
		vk::MemoryPropertyFlags properties, bool needsDeviceAddress)
	{
		if (memRequirements.size == 0) {
			return {};
		}

		std::lock_guard lock(managerMutex);

		uint32_t memTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
		bool hostVis = isHostVisible(memTypeIndex);

		// Legacy callers handle map/unmap themselves - don't auto-map
		return allocateDedicated(memRequirements.size, memTypeIndex, hostVis, needsDeviceAddress, false);
	}

	void VulkanMemoryManager::freeLegacy(vk::DeviceMemory memory)
	{
		if (!memory) {
			return;
		}

		std::lock_guard lock(managerMutex);

		for (auto it = dedicatedAllocations.begin(); it != dedicatedAllocations.end(); ++it) {
			if (it->memory == memory) {
				// Legacy callers handle unmap themselves - just free and remove from tracking
				dedicatedAllocations.erase(it);
				device.freeMemory(memory);
				return;
			}
		}

		// Not tracked - either a pre-init allocation or already removed during shutdown
		// Try to free directly, ignore if already freed
		try {
			device.freeMemory(memory);
		} catch (...) {
			// Memory was already freed (e.g., during shutdown cleanup)
		}
	}

	std::vector<VulkanMemoryManager::MemoryTypeStats> VulkanMemoryManager::getStats() const
	{
		std::lock_guard lock(managerMutex);

		std::vector<MemoryTypeStats> result;
		for (const auto& [typeIndex, typeData] : memoryTypes) {
			MemoryTypeStats stats;
			stats.memoryTypeIndex = typeIndex;
			stats.blockCount = static_cast<uint32_t>(typeData.blocks.size());

			for (const auto& block : typeData.blocks) {
				auto blockStats = block->getStats();
				stats.totalCapacity += blockStats.totalCapacity;
				stats.totalAllocated += blockStats.totalAllocated;
				stats.fragmentationPercent += blockStats.fragmentationPercent;
			}

			if (stats.blockCount > 0) {
				stats.fragmentationPercent /= static_cast<float>(stats.blockCount);
			}

			result.push_back(stats);
		}

		return result;
	}

	uint32_t VulkanMemoryManager::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
	{
		return MemoryUtilities::findMemoryType(physicalDevice, typeFilter, properties);
	}

	bool VulkanMemoryManager::isHostVisible(uint32_t memoryTypeIndex) const
	{
		auto memProps = physicalDevice.getMemoryProperties();
		if (memoryTypeIndex < memProps.memoryTypeCount) {
			return (memProps.memoryTypes[memoryTypeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
				!= vk::MemoryPropertyFlags{};
		}
		return false;
	}

	vk::DeviceSize VulkanMemoryManager::getBlockSizeForType(uint32_t memoryTypeIndex) const
	{
		static constexpr vk::DeviceSize DEFAULT_DEVICE_LOCAL = 256ull * 1024 * 1024;
		static constexpr vk::DeviceSize DEFAULT_HOST_VISIBLE = 64ull * 1024 * 1024;

		auto& config = memory::MemoryPoolConfig::instance();
		if (isHostVisible(memoryTypeIndex)) {
			vk::DeviceSize size = config.hostVisibleBlockSize();
			return size > 0 ? size : DEFAULT_HOST_VISIBLE;
		}
		vk::DeviceSize size = config.deviceLocalBlockSize();
		return size > 0 ? size : DEFAULT_DEVICE_LOCAL;
	}

	VulkanAllocation VulkanMemoryManager::allocateDedicated(vk::DeviceSize size, uint32_t memoryTypeIndex,
		bool hostVisible, bool needsDeviceAddress, bool autoMap)
	{
		vk::MemoryAllocateFlagsInfo allocFlags{};
		if (needsDeviceAddress) {
			allocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
		}

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.pNext = needsDeviceAddress ? &allocFlags : nullptr;
		allocInfo.allocationSize = size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		vk::DeviceMemory memory = device.allocateMemory(allocInfo);

		DedicatedAllocation dedicated;
		dedicated.memory = memory;
		dedicated.size = size;
		dedicated.hostVisible = hostVisible;

		if (hostVisible && autoMap) {
			dedicated.mappedPtr = device.mapMemory(memory, 0, size, {});
		}

		dedicatedAllocations.push_back(dedicated);

		VulkanAllocation allocation;
		allocation.memory = memory;
		allocation.offset = 0;
		allocation.size = size;
		allocation.mappedPtr = dedicated.mappedPtr;
		allocation.block = nullptr;
		allocation.isDedicated = true;

		vfLogInfo("VulkanMemoryManager: Dedicated allocation {}MB (type {}{}{})",
			size / (1024 * 1024), memoryTypeIndex,
			hostVisible ? ", host-visible" : "",
			needsDeviceAddress ? ", device-address" : "");

		return allocation;
	}
}
