#pragma once

#include <vulkan/vulkan.hpp>
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
        vk::DeviceMemory atlasImageMemory;
        vk::ImageView atlasImageView;
        vk::Sampler atlasSampler;

        bool isColorFont = false;
    };

    class TextFontCache
    {
    private:
        core::Device& device;

        std::unordered_map<std::string, CachedFont> fontCache;

        // Default placeholder (1x1 white pixel)
        vk::Image defaultImage;
        vk::DeviceMemory defaultImageMemory;
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
        const CachedFont* getFont(const std::string& fontPath) const;

        vk::ImageView getDefaultImageView() const { return defaultImageView; }
        vk::Sampler getDefaultSampler() const { return defaultSampler; }

    private:
        void createDefaultTexture();
        bool uploadFontAtlas(const std::string& fontPath, std::shared_ptr<resource::FontData> fontData);
    };
}
