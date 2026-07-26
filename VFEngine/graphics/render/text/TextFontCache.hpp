#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <future>

namespace core
{
    class Device;
}

namespace resource
{
    struct FontData;
}

namespace render::text
{
    struct CachedFont
    {
        std::shared_ptr<resource::FontData> fontData;

        vk::Image atlasImage;
        core::VulkanAllocation atlasImageAllocation;
        vk::ImageView atlasImageView;
        vk::Sampler atlasSampler;

        // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF.
        // Kept as the shader-facing integer so command recording does not need a
        // second atlas-format switch and the push-constant ABI stays unchanged.
        uint32_t glyphMode = 0;
    };

    class TextFontCache
    {
    private:
        core::Device& device;

        std::unordered_map<std::string, CachedFont> fontCache;

        // Default placeholder (1x1 white pixel)
        vk::Image defaultImage;
        core::VulkanAllocation defaultImageAllocation;
        vk::ImageView defaultImageView;
        vk::Sampler defaultSampler;

        // Pending async loads
        struct PendingLoad
        {
            std::future<std::shared_ptr<resource::FontData>> future;
        };
        std::unordered_map<std::string, PendingLoad> pendingLoads;

    public:
        explicit TextFontCache(core::Device& device);
        ~TextFontCache();

        void init();
        void cleanUp();

        void requestFont(const std::string& fontPath);
        void processPendingLoads();

        bool isFontReady(const std::string& fontPath) const;

        // Exact lookup — returns null when the path is not resident. Pass a key from
        // resolveFontKey(), not a raw component path.
        const CachedFont* getFont(const std::string& fontPath) const;

        // VK-1628: maps a font path to the cache key that actually backs it — the path
        // itself when resident, otherwise the default-font sentinel. Group instances
        // and key descriptor sets by this.
        const std::string& resolveFontKey(const std::string& fontPath) const;

        vk::ImageView getDefaultImageView() const { return defaultImageView; }
        vk::Sampler getDefaultSampler() const { return defaultSampler; }

    private:
        void createDefaultTexture();
        void loadDefaultFont();
        bool uploadFontAtlas(const std::string& fontPath, std::shared_ptr<resource::FontData> fontData);
    };
}
