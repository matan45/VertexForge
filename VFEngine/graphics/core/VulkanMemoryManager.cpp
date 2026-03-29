#include "VulkanMemoryManager.hpp"
#include "Device.hpp"
#include "MemoryUtilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "print/Log.hpp"
#include <algorithm>

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

		vfLogDebug("VulkanMemoryBlock: Allocated {}MB block (type {}{})",
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

	VulkanAllocation VulkanMemoryBlock::allocate(vk::DeviceSize size, vk::DeviceSize alignment,
		GpuResourceType resourceType, vk::DeviceSize bufferImageGranularity)
	{
		// Enforce bufferImageGranularity when block contains mixed resource types
		vk::DeviceSize effectiveAlignment = alignment;
		bool isMixed = (resourceType == GpuResourceType::Buffer && hasImages) ||
			(resourceType == GpuResourceType::Image && hasBuffers);
		if (isMixed && bufferImageGranularity > 1) {
			effectiveAlignment = std::max(alignment, bufferImageGranularity);
		}

		auto handle = allocator.allocate(size, effectiveAlignment);
		if (!handle.isValid()) {
			return {};
		}

		if (resourceType == GpuResourceType::Buffer) hasBuffers = true;
		else hasImages = true;

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
		bufferImageGranularity = physicalDevice.getProperties().limits.bufferImageGranularity;
		vfLogInfo("VulkanMemoryManager: Initialized (bufferImageGranularity={})", bufferImageGranularity);
	}

	VulkanMemoryManager::~VulkanMemoryManager()
	{
		std::lock_guard lock(managerMutex);

		// Don't free dedicated allocations here - callers own their memory lifecycle
		// and will free via destroyBuffer during their own cleanup.
		// Only log any remaining as a diagnostic.
		if (!dedicatedAllocations.empty()) {
			vfLogDebug("VulkanMemoryManager: {} dedicated allocations still tracked at shutdown (callers handle cleanup)",
				dedicatedAllocations.size());
		}
		dedicatedAllocations.clear();

		// Blocks are cleaned up by their destructors via unique_ptr
		memoryTypes.clear();

		vfLogDebug("VulkanMemoryManager: Destroyed");
	}

	VulkanAllocation VulkanMemoryManager::allocate(const vk::MemoryRequirements& memRequirements,
		vk::MemoryPropertyFlags properties,
		bool needsDeviceAddress,
		GpuResourceType resourceType)
	{
		if (memRequirements.size == 0) {
			return {};
		}

		std::lock_guard lock(managerMutex);

		uint32_t memTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
		bool hostVis = isHostVisible(memTypeIndex);
		vk::DeviceSize blockSize = getBlockSizeForType(memTypeIndex);

		// Use dedicated allocation for device-address buffers or oversized requests
		auto& config = memory::MemoryPoolConfig::instance();
		vk::DeviceSize threshold = std::min(blockSize / 2, static_cast<vk::DeviceSize>(config.dedicatedThreshold()));
		if (needsDeviceAddress || memRequirements.size > threshold) {
			return allocateDedicated(memRequirements.size, memTypeIndex, hostVis, needsDeviceAddress);
		}

		// Try existing blocks first
		auto& typeData = memoryTypes[memTypeIndex];
		for (auto& block : typeData.blocks) {
			auto allocation = block->allocate(memRequirements.size, memRequirements.alignment,
				resourceType, bufferImageGranularity);
			if (allocation.isValid()) {
				return allocation;
			}
		}

		// All existing blocks full - create a new one
		auto newBlock = std::make_unique<VulkanMemoryBlock>(device, memTypeIndex, blockSize, hostVis);
		auto allocation = newBlock->allocate(memRequirements.size, memRequirements.alignment,
			resourceType, bufferImageGranularity);
		typeData.blocks.push_back(std::move(newBlock));

		updateGlobalStats();
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

		updateGlobalStats();
	}

	void VulkanMemoryManager::reclaimEmptyBlocks()
	{
		std::lock_guard lock(managerMutex);

		for (auto& [typeIndex, typeData] : memoryTypes) {
			if (typeData.blocks.size() <= 1) continue;

			auto it = typeData.blocks.begin();
			while (it != typeData.blocks.end()) {
				if (typeData.blocks.size() <= 1) break;

				auto blockStats = (*it)->getStats();
				if (blockStats.activeAllocationCount == 0) {
					vfLogDebug("VulkanMemoryManager: Reclaiming empty {}MB block (type {})",
						(*it)->getBlockSize() / (1024 * 1024), typeIndex);
					it = typeData.blocks.erase(it);
					memory::GpuAllocationStats::blocksReclaimed.fetch_add(1, std::memory_order_relaxed);
				} else {
					++it;
				}
			}
		}

		updateGlobalStats();
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

		// Update global atomic stats for the UI (already under lock)
		updateGlobalStats();

		return result;
	}

	void VulkanMemoryManager::updateGlobalStats() const
	{

		uint32_t dlBlocks = 0, hvBlocks = 0;
		uint64_t dlUsed = 0, dlCap = 0, hvUsed = 0, hvCap = 0;
		float dlFrag = 0.0f, hvFrag = 0.0f;
		uint32_t dlFragCount = 0, hvFragCount = 0;

		for (const auto& [typeIndex, typeData] : memoryTypes) {
			bool hostVis = isHostVisible(typeIndex);
			for (const auto& block : typeData.blocks) {
				auto bs = block->getStats();
				if (hostVis) {
					hvBlocks++;
					hvUsed += bs.totalAllocated;
					hvCap += bs.totalCapacity;
					hvFrag += bs.fragmentationPercent;
					hvFragCount++;
				} else {
					dlBlocks++;
					dlUsed += bs.totalAllocated;
					dlCap += bs.totalCapacity;
					dlFrag += bs.fragmentationPercent;
					dlFragCount++;
				}
			}
		}

		memory::GpuAllocationStats::deviceLocalBlockCount.store(dlBlocks, std::memory_order_relaxed);
		memory::GpuAllocationStats::deviceLocalUsedBytes.store(dlUsed, std::memory_order_relaxed);
		memory::GpuAllocationStats::deviceLocalCapacityBytes.store(dlCap, std::memory_order_relaxed);
		memory::GpuAllocationStats::deviceLocalFragPercent.store(
			dlFragCount > 0 ? static_cast<uint32_t>((dlFrag / dlFragCount) * 100) : 0, std::memory_order_relaxed);

		memory::GpuAllocationStats::hostVisibleBlockCount.store(hvBlocks, std::memory_order_relaxed);
		memory::GpuAllocationStats::hostVisibleUsedBytes.store(hvUsed, std::memory_order_relaxed);
		memory::GpuAllocationStats::hostVisibleCapacityBytes.store(hvCap, std::memory_order_relaxed);
		memory::GpuAllocationStats::hostVisibleFragPercent.store(
			hvFragCount > 0 ? static_cast<uint32_t>((hvFrag / hvFragCount) * 100) : 0, std::memory_order_relaxed);

		uint64_t dedBytes = 0;
		for (const auto& ded : dedicatedAllocations) {
			dedBytes += ded.size;
		}
		memory::GpuAllocationStats::dedicatedAllocationCount.store(
			static_cast<uint32_t>(dedicatedAllocations.size()), std::memory_order_relaxed);
		memory::GpuAllocationStats::dedicatedAllocatedBytes.store(dedBytes, std::memory_order_relaxed);
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

		vfLogDebug("VulkanMemoryManager: Dedicated allocation {}MB (type {}{}{})",
			size / (1024 * 1024), memoryTypeIndex,
			hostVisible ? ", host-visible" : "",
			needsDeviceAddress ? ", device-address" : "");

		return allocation;
	}
}
