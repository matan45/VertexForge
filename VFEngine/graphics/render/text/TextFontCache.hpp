#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"
#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <future>
#include "text/FontFallback.hpp"

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

        // VK-1634: SDFParameters::pxRange, mirrored here for the same reason as
        // glyphMode - the record loops push it per batch without dereferencing
        // fontData. Meaningful only for glyphMode 2; the importer leaves it zero for
        // every other atlas format.
        float pxRange = 0.0f;
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

        // Paths whose last load attempt failed, and when. processPendingLoads drops a
        // pending entry whether it succeeded or not, so without this a caller that
        // re-requests every frame (see resolveStyledFont's residency retry) would spawn
        // a fresh std::async for a broken file on every single frame.
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> failedLoads;

        // Paths already warned about. VK-1628 substitutes the built-in default font for a
        // font that will not load, so the text still renders - in the wrong typeface and
        // at the wrong metrics - and the raw load error reads as harmless. The
        // consequence is worth saying, but exactly once per path per session.
        std::unordered_set<std::string> loggedSubstitutions;

        // VK-1638: reimport invalidation. requestInvalidate() may be called from the
        // importer's detached worker thread, so it only parks the path here; every
        // Vulkan touch happens on the render thread when processPendingLoads() drains it.
        std::mutex invalidateMutex;
        std::vector<std::string> pendingInvalidations;

        // Paths dropped by an invalidation and not yet re-uploaded. Only these can have
        // a stale descriptor set pointing at them - a first-time load has no set yet -
        // so only these bump the generation when their new atlas lands.
        std::unordered_set<std::string> invalidatedPaths;

        // Bumped whenever a resident atlas is dropped or replaced. Pipelines key their
        // descriptor sets by font PATH, and a reimport keeps the path while replacing
        // the image view behind it, so they re-point their sets when this changes.
        uint64_t atlasGen = 0;

        // ----------------------------------------------------------------
        // VK-1636: sibling style faces.
        // ----------------------------------------------------------------

        // A styled sibling that was found on disk. `path` points into styledPathPool
        // rather than owning the string: the candidate list is rebuilt on every
        // re-probe, and callers hold the returned key as a std::string_view for the
        // rest of the frame, so the storage has to outlive the vector.
        struct StyledCandidate
        {
            const std::string* path = nullptr;
            uint32_t satisfiedBits = 0;
        };

        // One requested style (bold / italic / both) of one base font.
        struct StyledSlot
        {
            bool probed = false;
            std::chrono::steady_clock::time_point lastProbe{};
            // Existing files only, in descending preference. Usually empty.
            std::vector<StyledCandidate> candidates;
        };

        struct StyledFamily
        {
            // Indexed by style bits; [0] is unused (an unstyled request never probes).
            StyledSlot slots[4];
        };

        std::unordered_map<std::string, StyledFamily> styledFamilies;

        // Interned styled paths. std::unordered_set nodes never move, so a
        // const std::string* into it stays valid until cleanUp().
        std::unordered_set<std::string> styledPathPool;

        // VK-1638: project-wide regular-face fallbacks. The configured keys point
        // into fallbackPathPool; unordered_set nodes are stable across rehash, so
        // render code can retain a key pointer for the rest of the frame.
        std::unordered_set<std::string> fallbackPathPool;
        std::array<const std::string*, ::text::MAX_FALLBACK_FACES> fallbackChain{};
        uint8_t fallbackChainCount = 0;

        // How long a slot that has not yet found its exact face waits before looking
        // again. Importing the Bold face with the editor open should take effect
        // without a restart; re-probing forever would put a handful of
        // std::filesystem::exists calls on the frame path, so cap the rate.
        static constexpr std::chrono::milliseconds STYLE_REPROBE_INTERVAL{2000};

        // How long a path that failed to load or upload is left alone before another
        // attempt. Long enough that a permanently broken font costs nothing per frame,
        // short enough that fixing and reimporting it heals without a restart.
        static constexpr std::chrono::milliseconds FONT_RETRY_INTERVAL{5000};

    public:
        explicit TextFontCache(core::Device& device);
        ~TextFontCache();

        void init();
        void cleanUp();

        void requestFont(const std::string& fontPath);
        void processPendingLoads();

        // VK-1638: drop the cached atlas for `fontPath` so the next frame reloads it.
        // Call after a reimport, or the editor keeps drawing the pre-reimport glyphs
        // until it is restarted (requestFont early-returns for any resident key).
        //
        // THREAD-SAFE, and deliberately does no work of its own: the import that
        // triggers it finishes on a detached worker thread. The teardown happens in
        // processPendingLoads() on the render thread.
        void requestInvalidate(std::string fontPath);

        // Changes whenever a resident atlas is dropped or replaced. Pipelines compare
        // it against the value they last refreshed their font descriptor sets at.
        [[nodiscard]] uint64_t atlasGeneration() const noexcept { return atlasGen; }

        bool isFontReady(const std::string& fontPath) const;

        // Exact lookup — returns null when the path is not resident. Pass a key from
        // resolveFontKey(), not a raw component path.
        const CachedFont* getFont(const std::string& fontPath) const;

        // VK-1628: maps a font path to the cache key that actually backs it — the path
        // itself when resident, otherwise the default-font sentinel. Group instances
        // and key descriptor sets by this.
        const std::string& resolveFontKey(const std::string& fontPath) const;

        // VK-1636: the cache key for `basePath` rendered in `styleBits`
        // (text::STYLE_BOLD / STYLE_ITALIC), preferring a real sibling face over the
        // shader's synthesis.
        struct StyledFontResolution
        {
            // Never null, and always a key whose atlas is resident (or the default
            // sentinel) — the same guarantee resolveFontKey gives, because the
            // per-glyph descriptor sets are keyed by it. Stable for the process
            // lifetime, so it is safe to hold as a std::string_view across the frame.
            const std::string* key = nullptr;
            // What the chosen face does NOT provide, i.e. the styleFlags the shader
            // must still fake. Equal to styleBits when no real face was used.
            uint32_t synthesizedBits = 0;
        };

        // styleBits == 0 is exactly resolveFontKey(basePath) with no synthesis, and
        // costs nothing extra — the overwhelmingly common unstyled case never probes.
        //
        // Not const: the first call for a (basePath, styleBits) pair probes the disk,
        // memoises the answer and may kick off an async load of the styled atlas.
        StyledFontResolution resolveStyledFont(const std::string& basePath, uint32_t styleBits);

        struct FallbackFaces
        {
            std::array<const std::string*, ::text::MAX_FALLBACK_FACES> keys{};
            std::array<const resource::FontData*, ::text::MAX_FALLBACK_FACES> faces{};
            uint8_t count = 0;
        };

        // Installs up to three authored regular faces plus the mandatory built-in
        // default tail. Invalid VFS paths are ignored before requestFont() can
        // create metadata for them.
        void setFallbackChain(std::span<const std::string> fontPaths);

        // Allocation-free, by-value snapshot of the currently resident chain.
        // Residency is checked on every call so async uploads heal automatically.
        [[nodiscard]] FallbackFaces resolveFallbackFaces(const std::string& primaryKey) const noexcept;

        vk::ImageView getDefaultImageView() const { return defaultImageView; }
        vk::Sampler getDefaultSampler() const { return defaultSampler; }

    private:
        void createDefaultTexture();
        void loadDefaultFont();
        // VK-1636: loads one engine-relative font straight off disk under `cacheKey`,
        // bypassing ResourceManager/AssetRef. Returns false (quietly) when the file is
        // simply not there, which is the normal state for the styled default faces.
        bool loadEngineFont(const char* enginePath, const char* cacheKey, bool required);
        bool uploadFontAtlas(const std::string& fontPath, std::shared_ptr<resource::FontData> fontData);

        // VK-1636. Fills `slot` with the styled siblings of `basePath` that exist.
        void probeStyledSlot(const std::string& basePath, uint32_t styleBits, StyledSlot& slot);

        // Render thread only — destroys Vulkan objects. Drained from processPendingLoads.
        void drainPendingInvalidations();

        // True when `fontPath` failed recently enough that another attempt would just
        // burn a thread and fail again.
        [[nodiscard]] bool isInFailureBackoff(const std::string& fontPath) const noexcept;
    };
}
