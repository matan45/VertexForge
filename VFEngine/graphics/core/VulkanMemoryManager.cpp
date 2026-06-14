#include "VulkanMemoryManager.hpp"
#include "Device.hpp"
#include "MemoryUtilities.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "memory/GpuMemorySnapshot.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core
{
	// --- VulkanMemoryBlock ---

	VulkanMemoryBlock::VulkanMemoryBlock(const vk::Device& device, uint32_t memoryTypeIndex,
		vk::DeviceSize blockSize, bool hostVisible, bool deviceAddress)
		: device(device)
		, blockSize(blockSize)
		, memoryTypeIndex(memoryTypeIndex)
		, hostVisible(hostVisible)
		, deviceAddress(deviceAddress)
		, allocator(blockSize, "VulkanMemoryBlock")
	{
		// A device-address-capable block must be allocated with the device-address
		// flag so every sub-allocation can hand out a valid buffer device address.
		vk::MemoryAllocateFlagsInfo allocFlags{};
		if (deviceAddress) {
			allocFlags.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
		}

		vk::MemoryAllocateInfo allocInfo{};
		allocInfo.pNext = deviceAddress ? &allocFlags : nullptr;
		allocInfo.allocationSize = blockSize;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		memory = device.allocateMemory(allocInfo);

		if (hostVisible) {
			baseMappedPtr = device.mapMemory(memory, 0, blockSize, {});
		}

		vfLogDebug("VulkanMemoryBlock: Allocated {}MB block (type {}{}{})",
			blockSize / (1024 * 1024), memoryTypeIndex,
			hostVisible ? ", host-visible" : "",
			deviceAddress ? ", device-address" : "");
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
			vfLogWarning("VulkanMemoryManager: {} dedicated allocations still tracked at shutdown (callers handle cleanup)",
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

		// Only oversized requests get a dedicated allocation now. Device-address
		// requests sub-allocate from device-address-capable blocks (the per-buffer
		// device address already accounts for the bind offset, so a non-zero offset
		// is safe).
		auto& config = memory::MemoryPoolConfig::instance();
		vk::DeviceSize threshold = std::min(blockSize / 2, static_cast<vk::DeviceSize>(config.dedicatedThreshold()));
		if (memRequirements.size > threshold) {
			return allocateDedicated(memRequirements.size, memTypeIndex, hostVis, needsDeviceAddress);
		}

		auto& typeData = memoryTypes[memTypeIndex];
		auto& blockList = needsDeviceAddress ? typeData.deviceAddressBlocks : typeData.blocks;
		return allocateFromBlocks(blockList, memRequirements, memTypeIndex, hostVis,
			needsDeviceAddress, blockSize, resourceType);
	}

	VulkanAllocation VulkanMemoryManager::allocateFromBlocks(
		std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks,
		const vk::MemoryRequirements& memRequirements, uint32_t memTypeIndex,
		bool hostVisible, bool deviceAddress, vk::DeviceSize blockSize,
		GpuResourceType resourceType)
	{
		// Try existing blocks; skip any that obviously can't fit (cheap O(1) hint).
		for (auto& block : blocks) {
			if (block->freeBytes() < memRequirements.size) {
				continue;
			}
			auto allocation = block->allocate(memRequirements.size, memRequirements.alignment,
				resourceType, bufferImageGranularity);
			if (allocation.isValid()) {
				return allocation;
			}
		}

		// All existing blocks full (or fragmented) - create a new one.
		auto newBlock = std::make_unique<VulkanMemoryBlock>(device, memTypeIndex, blockSize,
			hostVisible, deviceAddress);
		auto allocation = newBlock->allocate(memRequirements.size, memRequirements.alignment,
			resourceType, bufferImageGranularity);
		blocks.push_back(std::move(newBlock));

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
			auto it = dedicatedAllocations.find(static_cast<VkDeviceMemory>(allocation.memory));
			if (it != dedicatedAllocations.end()) {
				if (it->second.hostVisible && it->second.mappedPtr) {
					device.unmapMemory(it->second.memory);
				}
				device.freeMemory(it->second.memory);
				dedicatedAllocations.erase(it);
				return;
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

		// Reclaim empty blocks from a list, keeping at least one block alive so the
		// common "drop to empty then immediately re-grow" case doesn't thrash.
		auto reclaimList = [](std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks, uint32_t typeIndex) {
			if (blocks.size() <= 1) return;
			auto it = blocks.begin();
			while (it != blocks.end()) {
				if (blocks.size() <= 1) break;
				if ((*it)->getStats().activeAllocationCount == 0) {
					vfLogDebug("VulkanMemoryManager: Reclaiming empty {}MB block (type {})",
						(*it)->getBlockSize() / (1024 * 1024), typeIndex);
					it = blocks.erase(it);
					memory::GpuAllocationStats::blocksReclaimed.fetch_add(1, std::memory_order_relaxed);
				} else {
					++it;
				}
			}
		};

		for (auto& [typeIndex, typeData] : memoryTypes) {
			reclaimList(typeData.blocks, typeIndex);
			reclaimList(typeData.deviceAddressBlocks, typeIndex);
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
			stats.blockCount = static_cast<uint32_t>(typeData.blocks.size() + typeData.deviceAddressBlocks.size());

			auto accumulate = [&stats](const std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks) {
				for (const auto& block : blocks) {
					auto blockStats = block->getStats();
					stats.totalCapacity += blockStats.totalCapacity;
					stats.totalAllocated += blockStats.totalAllocated;
					stats.fragmentationPercent += blockStats.fragmentationPercent;
				}
			};
			accumulate(typeData.blocks);
			accumulate(typeData.deviceAddressBlocks);

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
			auto tally = [&](const std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks) {
				for (const auto& block : blocks) {
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
			};
			tally(typeData.blocks);
			tally(typeData.deviceAddressBlocks);
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
		for (const auto& [mem, ded] : dedicatedAllocations) {
			dedBytes += ded.size;
		}
		memory::GpuAllocationStats::dedicatedAllocationCount.store(
			static_cast<uint32_t>(dedicatedAllocations.size()), std::memory_order_relaxed);
		memory::GpuAllocationStats::dedicatedAllocatedBytes.store(dedBytes, std::memory_order_relaxed);

		// Peak watermarks (monotonic until reset from the UI).
		auto bumpPeak = [](std::atomic<uint64_t>& peak, uint64_t current) {
			uint64_t prev = peak.load(std::memory_order_relaxed);
			while (current > prev && !peak.compare_exchange_weak(prev, current, std::memory_order_relaxed)) {}
		};
		// Per-row peaks mirror the row's Used column (block sub-allocation only);
		// dedicated bytes have their own row, and the grand total is managedPeakBytes.
		bumpPeak(memory::GpuAllocationStats::deviceLocalPeakUsedBytes, dlUsed);
		bumpPeak(memory::GpuAllocationStats::hostVisiblePeakUsedBytes, hvUsed);
		bumpPeak(memory::GpuAllocationStats::managedPeakBytes,
			memory::GpuAllocationStats::managedAllocatedBytes.load(std::memory_order_relaxed));
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
		dedicated.deviceAddress = needsDeviceAddress;

		if (hostVisible && autoMap) {
			dedicated.mappedPtr = device.mapMemory(memory, 0, size, {});
		}

		dedicatedAllocations.emplace(static_cast<VkDeviceMemory>(memory), dedicated);

		VulkanAllocation allocation;
		allocation.memory = memory;
		allocation.offset = 0;
		allocation.size = size;
		allocation.mappedPtr = dedicated.mappedPtr;
		allocation.block = nullptr;
		allocation.isDedicated = true;

		vfLogDebug("VulkanMemoryManager: Dedicated allocation {} bytes ({:.2f}MB) (type {}{}{})",
			size, static_cast<float>(size) / (1024.0f * 1024.0f), memoryTypeIndex,
			hostVisible ? ", host-visible" : "",
			needsDeviceAddress ? ", device-address" : "");

		return allocation;
	}

	void VulkanMemoryManager::refreshDiagnostics() const
	{
		std::lock_guard lock(managerMutex);
		updateGlobalStats();
		queryVramBudget();
		buildSnapshot();
	}

	void VulkanMemoryManager::queryVramBudget() const
	{
		if (!memoryBudgetChecked) {
			memoryBudgetSupported = ownerDevice.isMemoryBudgetSupported();
			memoryBudgetChecked = true;
		}

		// Throttle the driver round-trip: it enumerates heaps and is comparatively
		// expensive. The already-published vram* atomics stay valid between queries.
		if (vramBudgetQueryCounter++ % kVramBudgetQueryInterval != 0)
			return;

		if (memoryBudgetSupported) {
			// Real driver budget/usage (accounts for other processes + driver reserve).
			auto chain = physicalDevice.getMemoryProperties2<
				vk::PhysicalDeviceMemoryProperties2,
				vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();
			const auto& mp = chain.get<vk::PhysicalDeviceMemoryProperties2>().memoryProperties;
			const auto& budgetProps = chain.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

			uint64_t budget = 0, usage = 0;
			for (uint32_t i = 0; i < mp.memoryHeapCount; ++i) {
				if (mp.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
					budget += budgetProps.heapBudget[i];
					usage += budgetProps.heapUsage[i];
				}
			}
			memory::GpuAllocationStats::vramBudgetBytes.store(budget, std::memory_order_relaxed);
			memory::GpuAllocationStats::vramUsageBytes.store(usage, std::memory_order_relaxed);
			return;
		}

		// Fallback: largest device-local heap as the budget, our own tracked bytes
		// (block capacity + dedicated) as the usage.
		auto memProps = physicalDevice.getMemoryProperties();
		uint64_t budget = 0;
		for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
			if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
				budget = std::max<uint64_t>(budget, memProps.memoryHeaps[i].size);
			}
		}
		uint64_t usage = memory::GpuAllocationStats::deviceLocalCapacityBytes.load(std::memory_order_relaxed)
			+ memory::GpuAllocationStats::dedicatedAllocatedBytes.load(std::memory_order_relaxed);
		memory::GpuAllocationStats::vramBudgetBytes.store(budget, std::memory_order_relaxed);
		memory::GpuAllocationStats::vramUsageBytes.store(usage, std::memory_order_relaxed);
	}

	void VulkanMemoryManager::buildSnapshot() const
	{
		memory::GpuMemorySnapshot snap;
		for (const auto& [typeIndex, typeData] : memoryTypes) {
			bool hostVis = isHostVisible(typeIndex);
			auto addBlocks = [&](const std::vector<std::unique_ptr<VulkanMemoryBlock>>& blocks, bool deviceAddr) {
				for (const auto& block : blocks) {
					auto bs = block->getStats();
					memory::GpuBlockView view;
					view.memoryTypeIndex = typeIndex;
					view.hostVisible = hostVis;
					view.deviceAddress = deviceAddr;
					view.capacity = block->getBlockSize();
					view.used = bs.totalAllocated;
					view.fragmentationPercent = bs.fragmentationPercent;
					view.freeSpans = block->getFreeSpans();
					snap.blocks.push_back(std::move(view));
				}
			};
			addBlocks(typeData.blocks, false);
			addBlocks(typeData.deviceAddressBlocks, true);
		}
		for (const auto& [mem, ded] : dedicatedAllocations) {
			snap.dedicated.push_back({ded.size, ded.hostVisible, ded.deviceAddress});
		}
		memory::GpuMemorySnapshot::publish(std::move(snap));
	}
}
