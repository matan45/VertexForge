#include "VFXBindlessTextures.hpp"

#include "../../gpudriven/scene/BindlessTextureManager.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/DeferredDeletionQueue.hpp"
#include "print/Log.hpp"

#include <exception>
#include <filesystem>
#include <utility>

namespace render::vfx
{
    VFXBindlessTextures::VFXBindlessTextures(core::Device& device, uint32_t maxTextures)
        : device_(device),
          maxTextures_(maxTextures),
          table_(core::DeferredDeletionQueue::FRAMES_BEFORE_DELETE)
    {
    }

    VFXBindlessTextures::~VFXBindlessTextures()
    {
        cleanup();
    }

    void VFXBindlessTextures::init()
    {
        if (initialized_)
        {
            return;
        }

        manager_ = std::make_unique<render::gpudriven::BindlessTextureManager>(device_, maxTextures_);
        manager_->init();

        sampler_ = core::ImageUtilities::createVFXSampler(device_.getLogicalDevice());

        // White 1x1 = slot 0 default; emitters with no texture (or a failed load) sample this.
        createSolidImage({255, 255, 255, 255}, whiteImage_, whiteAlloc_, whiteView_);
        manager_->setDefaultTexture(whiteView_, sampler_);

        // Neutral-normal 1x1 (0.5, 0.5, 1.0) = "no distortion"; a distinct registered slot so
        // distortion emitters with no distortion texture sample it instead of white.
        createSolidImage({128, 128, 255, 255}, neutralImage_, neutralAlloc_, neutralView_);
        neutralIdx_ = manager_->registerTexture("__vfx_neutral_normal__", neutralView_, sampler_);

        initialized_ = true;
    }

    void VFXBindlessTextures::cleanup()
    {
        // No initialized_ guard: init() sets initialized_ only as its final step, so a throw partway
        // through init() (e.g. an image allocation fails) must still free whatever was already created.
        // Every step below is null-guarded / idempotent, so cleanup() is safe on a partially- or
        // never-initialised instance and safe to call twice.

        // Loaded textures: core::Texture destructors free their own GPU resources. The caller
        // (VFXSceneRenderer::cleanUp / cleanupGPUMode) has already idled the device.
        textures_.clear();
        table_.clear(); // drop refcount bookkeeping so a re-init doesn't hand back stale slot indices

        destroySolidImage(whiteImage_, whiteAlloc_, whiteView_);
        destroySolidImage(neutralImage_, neutralAlloc_, neutralView_);

        if (sampler_)
        {
            device_.getLogicalDevice().destroySampler(sampler_);
            sampler_ = nullptr;
        }

        if (manager_)
        {
            manager_->cleanup();
            manager_.reset();
        }

        neutralIdx_ = 0;
        currentFrame_ = 0;
        initialized_ = false;
    }

    void VFXBindlessTextures::setDeletionQueue(core::DeferredDeletionQueue* dq)
    {
        std::lock_guard lock(mutex_);
        deletionQueue_ = dq;
    }

    uint32_t VFXBindlessTextures::acquire(const std::string& path, bool srgb)
    {
        if (path.empty())
        {
            return defaultWhiteIndex();
        }

        std::lock_guard lock(mutex_);

        const std::string key = makeKey(path, srgb);
        const auto r = table_.acquire(key);
        if (!r.needsRegister)
        {
            return r.index; // already resident (or a pending teardown just cancelled)
        }

        if (!std::filesystem::exists(path))
        {
            vfLogWarning("VFX bindless texture not found: {}", path);
            table_.fail(key);
            return defaultWhiteIndex();
        }

        // No deferred-deletion queue wired (should not happen in practice): mirror the legacy
        // synchronous stall the per-pipeline caches used before a load.
        if (!deletionQueue_)
        {
            device_.getLogicalDevice().waitIdle();
        }

        auto tex = std::make_unique<core::Texture>(device_);
        bool loaded = false;
        try
        {
            loaded = tex->loadTextureFromFile(
                path, srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm, false);
        }
        catch (const std::exception& e)
        {
            vfLogError("VFX bindless texture load failed for {}: {}", path, e.what());
            loaded = false;
        }

        if (!loaded)
        {
            table_.fail(key);
            return defaultWhiteIndex();
        }

        const uint32_t idx = manager_->registerTexture(key, tex->getImageView(), tex->getSampler());
        if (idx == core::INVALID_TEXTURE_INDEX)
        {
            vfLogWarning("VFX bindless table full ({} slots); '{}' will use the default texture",
                         maxTextures_, path);
            table_.fail(key);
            return defaultWhiteIndex();
        }

        table_.setIndex(key, idx);
        textures_.emplace(key, std::move(tex));
        return idx;
    }

    void VFXBindlessTextures::release(const std::string& path, bool srgb)
    {
        if (path.empty())
        {
            return;
        }
        std::lock_guard lock(mutex_);
        table_.release(makeKey(path, srgb), currentFrame_);
    }

    void VFXBindlessTextures::tick(uint32_t frameNumber)
    {
        std::lock_guard lock(mutex_);
        currentFrame_ = frameNumber;

        for (const auto& key : table_.collectReady(frameNumber))
        {
            // Re-point the slot to the default + return it to the free-list. Deferred 3 frames, so
            // the emitter that used it stopped being recorded >= MAX_FRAMES_IN_FLIGHT frames ago —
            // no in-flight submission still samples this slot.
            manager_->unregisterTexture(key);

            auto it = textures_.find(key);
            if (it != textures_.end())
            {
                if (deletionQueue_ && it->second)
                {
                    it->second->extractResources(*deletionQueue_);
                }
                textures_.erase(it);
            }
            table_.forget(key);
        }
    }

    vk::DescriptorSetLayout VFXBindlessTextures::getDescriptorSetLayout() const
    {
        return manager_->getDescriptorSetLayout();
    }

    vk::DescriptorSet VFXBindlessTextures::getDescriptorSet() const
    {
        return manager_->getDescriptorSet();
    }

    void VFXBindlessTextures::createSolidImage(const std::array<uint8_t, 4>& rgba,
                                               vk::Image& outImage, core::VulkanAllocation& outAlloc,
                                               vk::ImageView& outView)
    {
        auto vkDevice = device_.getLogicalDevice();
        constexpr uint32_t texSize = 1;

        core::ImageInfoRequest imageInfo(
            vkDevice, device_.getPhysicalDevice(),
            texSize, texSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        core::ImageUtilities::createImage(imageInfo, outImage, outAlloc, device_.getMemoryManager());

        core::ImageViewInfoRequest viewInfo(
            vkDevice, outImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D);
        core::ImageUtilities::createImageView(viewInfo, outView);

        core::ImageUtilities::uploadStagedPixelData(device_, outImage, rgba.data(), rgba.size(), texSize, texSize);
    }

    void VFXBindlessTextures::destroySolidImage(vk::Image& image, core::VulkanAllocation& alloc, vk::ImageView& view)
    {
        auto vkDevice = device_.getLogicalDevice();
        if (view)
        {
            vkDevice.destroyImageView(view);
            view = nullptr;
        }
        if (image)
        {
            vkDevice.destroyImage(image);
            image = nullptr;
        }
        if (alloc)
        {
            device_.getMemoryManager().free(alloc);
            alloc = {};
        }
    }
}
