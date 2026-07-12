#include "ToonProfileGpuTable.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "material/ToonProfileManager.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::gpudriven
{
    ToonProfileGpuTable* ToonProfileGpuTable::s_active = nullptr;

    ToonProfileGpuTable::ToonProfileGpuTable(core::Device& device) : device_(device) {}

    ToonProfileGpuTable::~ToonProfileGpuTable() { cleanup(); }

    void ToonProfileGpuTable::fillRow(ToonProfileGPU& row, const material::ToonProfile& p)
    {
        row.shadeColor      = glm::vec4(p.shadeColor, 0.0f);
        row.midColor        = glm::vec4(p.midColor, 0.0f);
        row.diffParams      = glm::vec4(p.shadowThreshold, p.midThreshold, p.bandSmoothness, p.giScale);
        row.specParams      = glm::vec4(p.specThreshold, p.specSmoothness, p.specIntensity, p.specShininess);
        row.specColorRimPow = glm::vec4(p.specColor, p.rimPower);
        row.rimParams       = glm::vec4(p.rimColor, p.rimIntensity);
    }

    void ToonProfileGpuTable::init()
    {
        if (initialized_)
            return;

        const vk::DeviceSize size = sizeof(ToonProfileGPU) * TOON_PROFILE_TABLE_CAPACITY;

        core::BufferInfoRequest devReq(device_.getLogicalDevice(), device_.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::BufferUtilities::createBuffer(devReq, deviceBuffer_, deviceAlloc_, device_.getMemoryManager());

        core::BufferInfoRequest stgReq(device_.getLogicalDevice(), device_.getPhysicalDevice(), size,
                                       vk::BufferUsageFlagBits::eTransferSrc,
                                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        core::BufferUtilities::createBuffer(stgReq, stagingBuffer_, stagingAlloc_, device_.getMemoryManager());

        // Slot 0 = built-in default (also the unknown-ref fallback). Every other slot
        // starts as a copy of the default so a stale index can never read garbage.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ToonProfileGPU defaultRow{};
            fillRow(defaultRow, material::ToonProfileManager::defaultProfile());
            cpuMirror_.fill(defaultRow);
            dirty_ = true;
            initialized_ = true;
        }

        // Refresh the matching row in place whenever a referenced profile is edited/saved.
        changeCallbackId_ = material::ToonProfileManager::instance().registerChangeCallback(
            [this](const std::string& path) { onProfileChanged(path); });
    }

    void ToonProfileGpuTable::cleanup()
    {
        if (!initialized_)
            return;

        if (changeCallbackId_ != 0)
        {
            material::ToonProfileManager::instance().unregisterChangeCallback(changeCallbackId_);
            changeCallbackId_ = 0;
        }

        core::BufferUtilities::destroyBuffer(device_.getLogicalDevice(), deviceBuffer_, deviceAlloc_, device_.getMemoryManager());
        core::BufferUtilities::destroyBuffer(device_.getLogicalDevice(), stagingBuffer_, stagingAlloc_, device_.getMemoryManager());

        std::lock_guard<std::mutex> lock(mutex_);
        pathToIndex_.clear();
        nextIndex_ = 1;
        warnedOverflow_ = false;
        initialized_ = false;
    }

    void ToonProfileGpuTable::loadInto(uint8_t slot, const std::string& path)
    {
        // caller holds mutex_
        auto profile = material::ToonProfileManager::instance().getOrLoad(path);
        if (profile)
            fillRow(cpuMirror_[slot], *profile);
        else
            fillRow(cpuMirror_[slot], material::ToonProfileManager::defaultProfile());
        dirty_ = true;
    }

    uint8_t ToonProfileGpuTable::resolveIndex(const std::string& profilePath)
    {
        if (profilePath.empty())
            return TOON_PROFILE_DEFAULT_INDEX;

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = pathToIndex_.find(profilePath);
        if (it != pathToIndex_.end())
            return it->second;

        if (nextIndex_ >= TOON_PROFILE_TABLE_CAPACITY)
        {
            if (!warnedOverflow_)
            {
                vfLogWarning("ToonProfileGpuTable: exceeded {} profiles; extra profiles use the default (0)",
                             TOON_PROFILE_TABLE_CAPACITY);
                warnedOverflow_ = true;
            }
            return TOON_PROFILE_DEFAULT_INDEX;
        }

        const uint8_t slot = nextIndex_++;
        pathToIndex_[profilePath] = slot;
        loadInto(slot, profilePath);
        return slot;
    }

    void ToonProfileGpuTable::onProfileChanged(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pathToIndex_.find(path);
        if (it == pathToIndex_.end())
            return;                 // not referenced by anything on the GPU yet
        loadInto(it->second, path); // same slot → every referencing object updates live
    }

    void ToonProfileGpuTable::uploadIfDirty(vk::CommandBuffer cmd)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_ || !initialized_)
            return;

        const vk::DeviceSize bytes = sizeof(ToonProfileGPU) * TOON_PROFILE_TABLE_CAPACITY;
        std::memcpy(stagingAlloc_.mappedPtr, cpuMirror_.data(), bytes);

        vk::BufferCopy copy{};
        copy.size = bytes;
        cmd.copyBuffer(stagingBuffer_, deviceBuffer_, 1, &copy);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = deviceBuffer_;
        barrier.offset = 0;
        barrier.size = bytes;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        dirty_ = false;
    }
}
