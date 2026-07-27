#include "TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "resource/DefaultFont.hpp"
#include "resource/FontResource.hpp"
#include "resource/PathResolver.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"
// VK-1636. Note the leading "::" on every use below: inside namespace render::text
// an unqualified `text::` binds to this namespace, not the utilities one.
#include "text/FontStyleFace.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <span>
#include <string_view>

namespace render::text
{
    namespace
    {
        [[nodiscard]] bool fallbackPathEqual(std::string_view lhs,
                                             std::string_view rhs) noexcept
        {
            if (lhs.size() != rhs.size()) return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                const char l = lhs[i] == '\\' ? '/' : lhs[i];
                const char r = rhs[i] == '\\' ? '/' : rhs[i];
                if (l != r) return false;
            }
            return true;
        }
    }

    TextFontCache::TextFontCache(core::Device& device)
        : device{device}
    {
    }

    TextFontCache::~TextFontCache() = default;

    void TextFontCache::init()
    {
        createDefaultTexture();
        loadDefaultFont();
        if (fallbackChainCount == 0)
        {
            setFallbackChain(std::span<const std::string>{});
        }
    }

    // VK-1628: the engine-shipped fallback font, registered under a sentinel key so
    // text with no (or an unresolvable) fontRef still renders. Loaded straight off
    // disk via FontResource — deliberately NOT through ResourceManager/AssetRef,
    // which would try to resolve an engine resource against the project's asset
    // database. See resource/DefaultFont.hpp.
    bool TextFontCache::loadEngineFont(const char* enginePath, const char* cacheKey, bool required)
    {
        const std::string path = resource::PathResolver::resolveEnginePath(enginePath);

        // VK-1636: an absent styled face is the normal state, not an error, so check
        // before loading rather than letting FontResource log a failure every time.
        std::error_code ec;
        if (!required && !std::filesystem::exists(path, ec))
        {
            return false;
        }

        auto fontData = std::make_shared<resource::FontData>(resource::FontResource::loadFont(path));
        if (uploadFontAtlas(cacheKey, std::move(fontData)))
        {
            vfLogInfo("Engine font loaded: {} ({})", path, cacheKey);
            return true;
        }

        if (required)
        {
            // Logged once at init, never retried — text without a font stays invisible,
            // which is the pre-VK-1628 behavior rather than a per-frame load storm.
            vfLogError("Failed to load default font from {}. Text with no font assigned "
                       "will not render.", path);
        }
        else
        {
            vfLogWarning("Styled default font {} exists but failed to load; bold/italic "
                         "will stay synthesized.", path);
        }
        return false;
    }

    void TextFontCache::loadDefaultFont()
    {
        loadEngineFont(resource::DEFAULT_FONT_ENGINE_PATH, resource::DEFAULT_FONT_SENTINEL, true);

        // VK-1636: the styled siblings of the shipped font. None of them ships today,
        // so this is three existence checks at init and nothing more. They are loaded
        // here rather than lazily because the sentinel keys have no path for
        // requestFont to resolve, and because init is the only place that may block.
        for (const uint32_t styleBits : {1u, 2u, 3u})
        {
            const char* enginePath = resource::defaultFontEnginePathForStyle(styleBits);
            const char* sentinel = resource::defaultFontSentinelForStyle(styleBits);
            loadEngineFont(enginePath, sentinel, false);
        }
    }

    void TextFontCache::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& [path, cached] : fontCache)
        {
            if (cached.atlasSampler)
                dev.destroySampler(cached.atlasSampler);
            if (cached.atlasImageView)
                dev.destroyImageView(cached.atlasImageView);
            if (cached.atlasImage)
            {
                dev.destroyImage(cached.atlasImage);
                device.getMemoryManager().free(cached.atlasImageAllocation);
                cached.atlasImageAllocation = {};
            }
        }
        fontCache.clear();
        pendingLoads.clear();

        // VK-1636: the memo caches "which sibling exists", which survives a device
        // loss, but its interned strings are handed out as string_views — drop both
        // together so nothing outlives the atlases they described.
        styledFamilies.clear();
        styledPathPool.clear();
        fallbackChain.fill(nullptr);
        fallbackChainCount = 0;
        fallbackPathPool.clear();

        if (defaultSampler)
        {
            dev.destroySampler(defaultSampler);
            defaultSampler = nullptr;
        }
        if (defaultImageView)
        {
            dev.destroyImageView(defaultImageView);
            defaultImageView = nullptr;
        }
        if (defaultImage)
        {
            dev.destroyImage(defaultImage);
            device.getMemoryManager().free(defaultImageAllocation);
            defaultImageAllocation = {};
            defaultImage = nullptr;
        }
    }

    void TextFontCache::createDefaultTexture()
    {
        constexpr uint32_t size = 1;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            size, size, 1, 1,
            vk::Format::eR8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultImage, defaultImageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultImage,
            vk::Format::eR8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultImageView);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        defaultSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Upload 1x1 white pixel
        uint8_t whitePixel = 255;
        vk::DeviceSize imageSize = sizeof(uint8_t);

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        auto cleanupStaging = [&]() {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());
        };

        try
        {
            void* data = stagingAllocation.mappedPtr;
            if (data)
            {
                std::memcpy(data, &whitePixel, imageSize);
            }

            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{size, size, 1};

            cmd->copyBufferToImage(stagingBuffer, defaultImage, vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            core::Utilities::endSingleTimeCommands(device, cmd);

            cleanupStaging();
        }
        catch (...)
        {
            cleanupStaging();
            throw;
        }
    }

    void TextFontCache::requestFont(const std::string& fontPath)
    {
        if (fontCache.contains(fontPath) || pendingLoads.contains(fontPath))
        {
            return;
        }

        // The sentinels are cache keys, not paths. They are populated once by
        // loadDefaultFont(); if that failed there is nothing to retry, and feeding
        // the literal to AssetRef::fromPath below would register it as an asset.
        // VK-1636 added three styled ones — all four must bail out here.
        if (resource::isDefaultFontSentinel(fontPath))
        {
            return;
        }

        PendingLoad pending;
        if (resource::VirtualFileSystem::instance().isArchiveMode())
        {
            // Packaged builds do not populate the editor AssetDatabase. FontResource
            // already reads through VFS, so load archive-relative fallback keys
            // directly rather than manufacturing an invalid AssetRef.
            pending.future = std::async(std::launch::async, [fontPath]
            {
                return std::make_shared<resource::FontData>(
                    resource::FontResource::loadFont(fontPath));
            });
        }
        else
        {
            pending.future =
                resource::ResourceManager::loadFontAsync(asset::AssetRef::fromPath(fontPath));
        }
        pendingLoads.emplace(fontPath, std::move(pending));
    }

    void TextFontCache::processPendingLoads()
    {
        std::vector<std::string> completed;

        for (auto& [path, pending] : pendingLoads)
        {
            if (pending.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                auto fontData = pending.future.get();
                if (fontData)
                {
                    if (uploadFontAtlas(path, fontData))
                    {
                        vfLogInfo("Text font loaded: {}", path);
                    }
                    else
                    {
                        vfLogError("Failed to upload font atlas: {}", path);
                    }
                }
                else
                {
                    vfLogError("Failed to load font: {}", path);
                }
                completed.push_back(path);
            }
        }

        for (const auto& path : completed)
        {
            pendingLoads.erase(path);
        }
    }

    bool TextFontCache::uploadFontAtlas(const std::string& fontPath, std::shared_ptr<resource::FontData> fontData)
    {
        const auto& atlas = fontData->atlas;
        if (atlas.width == 0 || atlas.height == 0 || atlas.pixels.empty())
        {
            return false;
        }

        const size_t bytesPerPixel = resource::fontAtlasBytesPerPixel(atlas.format);
        if (bytesPerPixel == 0)
        {
            return false;
        }

        const size_t width = static_cast<size_t>(atlas.width);
        const size_t height = static_cast<size_t>(atlas.height);
        if (width > std::numeric_limits<size_t>::max() / height)
        {
            return false;
        }
        const size_t pixelCount = width * height;
        if (pixelCount > std::numeric_limits<size_t>::max() / bytesPerPixel ||
            atlas.pixels.size() != pixelCount * bytesPerPixel)
        {
            return false;
        }

        CachedFont cached;
        cached.fontData = fontData;
        if (atlas.format == resource::FontAtlasFormat::RGBA_32)
        {
            cached.glyphMode = 1;
        }
        else if (atlas.format == resource::FontAtlasFormat::MTSDF_RGBA_32)
        {
            cached.glyphMode = 2;
        }

        // Only glyphMode 2 reads this. Both .vfFont validators pin it to [1, 16] for
        // MTSDF; every other format leaves the field at its zero default.
        cached.pxRange = fontData->sdfParams.pxRange;

        // Color and MTSDF atlases are both four-channel linear data. In
        // particular, MTSDF must never be uploaded as sRGB: median-RGB distance
        // reconstruction operates on the authored normalized channel values.
        const bool isFourChannel = bytesPerPixel == 4;
        vk::Format atlasFormat = isFourChannel
            ? vk::Format::eR8G8B8A8Unorm
            : vk::Format::eR8Unorm;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            atlas.width, atlas.height, 1, 1,
            atlasFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, cached.atlasImage, cached.atlasImageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            cached.atlasImage,
            atlasFormat,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, cached.atlasImageView);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        cached.atlasSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Upload atlas pixels
        vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(atlas.pixels.size());

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        auto cleanupStaging = [&]() {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());
        };

        try
        {
            void* data = stagingAllocation.mappedPtr;
            if (data)
            {
                std::memcpy(data, atlas.pixels.data(), imageSize);
            }

            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

            core::ImageUtilities::transitionImageLayout(cmd.get(), cached.atlasImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{atlas.width, atlas.height, 1};

            cmd->copyBufferToImage(stagingBuffer, cached.atlasImage, vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(cmd.get(), cached.atlasImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            core::Utilities::endSingleTimeCommands(device, cmd);

            cleanupStaging();
        }
        catch (...)
        {
            cleanupStaging();

            auto& dev = device.getLogicalDevice();
            if (cached.atlasSampler) dev.destroySampler(cached.atlasSampler);
            if (cached.atlasImageView) dev.destroyImageView(cached.atlasImageView);
            if (cached.atlasImage)
            {
                dev.destroyImage(cached.atlasImage);
                device.getMemoryManager().free(cached.atlasImageAllocation);
                cached.atlasImageAllocation = {};
            }
            return false;
        }

        fontCache.emplace(fontPath, std::move(cached));
        return true;
    }

    bool TextFontCache::isFontReady(const std::string& fontPath) const
    {
        return fontCache.contains(fontPath);
    }

    const CachedFont* TextFontCache::getFont(const std::string& fontPath) const
    {
        auto it = fontCache.find(fontPath);
        if (it != fontCache.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    // VK-1628: the single place that decides font substitution. A font that is still
    // loading, failed to load, or failed to upload resolves to the default instead of
    // dropping the text.
    //
    // Callers MUST group instances and key descriptor sets by the returned key, never
    // by the raw path — otherwise a descriptor built while the real font was still
    // loading gets cached against the default atlas and never heals. getFont() stays
    // an exact lookup for that reason: no caller should get a default font back under
    // a key that is not the sentinel.
    const std::string& TextFontCache::resolveFontKey(const std::string& fontPath) const
    {
        static const std::string sentinel{resource::DEFAULT_FONT_SENTINEL};
        if (!fontPath.empty() && fontCache.contains(fontPath))
        {
            return fontPath;
        }
        return sentinel;
    }

    void TextFontCache::setFallbackChain(std::span<const std::string> fontPaths)
    {
        fallbackChain.fill(nullptr);
        fallbackChainCount = 0;
        fallbackPathPool.clear();

        auto trimAndNormalize = [](std::string_view value)
        {
            constexpr std::string_view whitespace{" \t\n\r\f\v"};
            const size_t first = value.find_first_not_of(whitespace);
            if (first == std::string_view::npos) return std::string{};
            const size_t last = value.find_last_not_of(whitespace);
            std::string result{value.substr(first, last - first + 1)};
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        };

        // Validate before normalization/capping: a stale first entry must not crowd
        // out a later valid authored fallback. This is not a frame path, so owning
        // temporary strings here is preferable to weakening the policy helper.
        std::vector<std::string> validPaths;
        validPaths.reserve(fontPaths.size());
        for (const std::string& authored : fontPaths)
        {
            std::string normalized = trimAndNormalize(authored);
            if (normalized.empty()) continue;
            if (resource::isDefaultFontSentinel(normalized))
            {
                continue; // normalizeFallbackChain appends it exactly once
            }
            if (!resource::VirtualFileSystem::instance().exists(normalized))
            {
                vfLogWarning("Ignoring missing font fallback: {}", normalized);
                continue;
            }
            validPaths.push_back(std::move(normalized));
        }

        const ::text::NormalizedFallbackChain normalized =
            ::text::normalizeFallbackChain(validPaths);
        for (uint8_t i = 0; i < normalized.count; ++i)
        {
            auto [it, inserted] =
                fallbackPathPool.emplace(normalized.paths[i]);
            if (!inserted) continue;

            fallbackChain[fallbackChainCount++] = &*it;
            if (!resource::isDefaultFontSentinel(*it))
            {
                requestFont(*it);
            }
        }
    }

    TextFontCache::FallbackFaces TextFontCache::resolveFallbackFaces(
        const std::string& primaryKey) const noexcept
    {
        FallbackFaces result;
        for (uint8_t i = 0; i < fallbackChainCount; ++i)
        {
            const std::string* key = fallbackChain[i];
            if (!key || fallbackPathEqual(*key, primaryKey)) continue;

            const auto it = fontCache.find(*key);
            if (it == fontCache.end() || !it->second.fontData) continue;

            result.keys[result.count] = key;
            result.faces[result.count] = it->second.fontData.get();
            ++result.count;
        }
        return result;
    }

    // VK-1636 ------------------------------------------------------------------

    void TextFontCache::probeStyledSlot(const std::string& basePath, uint32_t styleBits,
                                        StyledSlot& slot)
    {
        slot.candidates.clear();
        slot.probed = true;
        slot.lastProbe = std::chrono::steady_clock::now();

        const bool isDefaultFamily = basePath.empty() || basePath == resource::DEFAULT_FONT_SENTINEL;

        for (const uint32_t candidateBits : ::text::styleDowngradeOrder(styleBits))
        {
            if (isDefaultFamily)
            {
                // The engine font has no path, so its "siblings" are the styled
                // sentinels loadDefaultFont() already tried. Presence in the cache IS
                // existence; nothing can appear later, so this probe is final.
                const std::string sentinel{resource::defaultFontSentinelForStyle(candidateBits)};
                if (fontCache.contains(sentinel))
                {
                    const std::string* interned = &*styledPathPool.insert(sentinel).first;
                    slot.candidates.push_back({interned, candidateBits});
                }
                continue;
            }

            for (const std::string& candidate : ::text::styledPathCandidates(basePath, candidateBits))
            {
                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec)) continue;

                const std::string* interned = &*styledPathPool.insert(candidate).first;
                slot.candidates.push_back({interned, candidateBits});
                // One file per style rung is enough; the rest of the spellings for
                // this rung would be the same face under a different name.
                break;
            }
        }
    }

    TextFontCache::StyledFontResolution TextFontCache::resolveStyledFont(const std::string& basePath,
                                                                        uint32_t styleBits)
    {
        StyledFontResolution resolution;
        resolution.synthesizedBits = styleBits & ::text::STYLE_MASK;

        // The unstyled case must cost exactly what it cost before this ticket: no
        // probe, no memo lookup, no allocation.
        if (resolution.synthesizedBits == 0)
        {
            resolution.key = &resolveFontKey(basePath);
            return resolution;
        }

        const bool isDefaultFamily = basePath.empty() || basePath == resource::DEFAULT_FONT_SENTINEL;
        const std::string& familyKey =
            isDefaultFamily ? resolveFontKey(basePath) : basePath;

        StyledFamily& family = styledFamilies[familyKey];
        StyledSlot& slot = family.slots[resolution.synthesizedBits];

        // Re-probe until the exact face turns up. That covers both "the family has no
        // styled faces" and "we settled for Bold while BoldItalic was still missing",
        // so importing the missing face with the editor open takes effect on its own.
        // Once candidates[0] covers the whole request there is nothing better to find.
        const bool satisfied =
            !slot.candidates.empty() && slot.candidates.front().satisfiedBits == resolution.synthesizedBits;
        if (!slot.probed ||
            (!satisfied && !isDefaultFamily &&
             std::chrono::steady_clock::now() - slot.lastProbe >= STYLE_REPROBE_INTERVAL))
        {
            probeStyledSlot(basePath, resolution.synthesizedBits, slot);

            // Only the best candidate is worth an atlas. A lower rung is still used
            // below if some other label already made it resident, but paying VRAM for
            // a Bold we are about to replace with a BoldItalic is waste.
            if (!slot.candidates.empty() && !isDefaultFamily)
            {
                requestFont(*slot.candidates.front().path);
            }
        }

        // Residency is re-evaluated every frame: the atlas may still be uploading.
        // Stack storage, not a vector — this runs per styled label per frame and a
        // heap allocation here would be a real regression. styleDowngradeOrder yields
        // at most three rungs and probeStyledSlot keeps one file per rung, so three
        // entries is the hard ceiling.
        std::array<::text::StyleFaceProbe, 3> probes{};
        size_t probeCount = 0;
        for (const StyledCandidate& candidate : slot.candidates)
        {
            if (probeCount >= probes.size()) break;
            probes[probeCount++] = {candidate.satisfiedBits, true,
                                    fontCache.contains(*candidate.path)};
        }

        const ::text::StyleChoice choice = ::text::chooseStyleFace(
            resolution.synthesizedBits, std::span{probes.data(), probeCount});

        if (choice.index < 0)
        {
            // No real face, or the one we want has not finished loading. Fall back to
            // the base face and keep synthesizing the FULL request so the glyphs do
            // not pop from Regular to Bold when the upload lands.
            resolution.key = &resolveFontKey(basePath);
            return resolution;
        }

        // Safe to hand out: chooseStyleFace only picks a resident candidate, which is
        // what resolveFontKey's "never key a descriptor by a non-resident path" rule
        // demands. Interned, so the pointer outlives the candidate vector.
        resolution.key = slot.candidates[static_cast<size_t>(choice.index)].path;
        resolution.synthesizedBits = choice.synthesizedBits;
        return resolution;
    }
}
