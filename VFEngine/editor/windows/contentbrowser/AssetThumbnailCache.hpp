#pragma once
#include "ContentBrowserTypes.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include <list>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace windows
{
    // Editor-side thumbnail cache for the Content Browser (VK-1379). Turns
    // assets into real GPU thumbnails instead of generic atlas icons. Phase 1
    // covers .vfImage / .vfHdr via the async editor-texture path; unsupported
    // types, pending loads and failures return nullptr so the grid falls back
    // to the atlas icon.
    //
    // Lifecycle notes:
    //  - Entries live in a std::list so each node has a STABLE address that we
    //    hand to the async loader as its void* instanceId. A vector/deque would
    //    relocate nodes and invalidate in-flight load ids.
    //  - GPU descriptors are released with a frame delay (deferredReleases) so
    //    an ImGui draw recorded this frame never samples a freed descriptor.
    //  - update() must be called exactly once per frame, before the grid draws.
    class AssetThumbnailCache
    {
    public:
        AssetThumbnailCache() = default;
        ~AssetThumbnailCache();

        AssetThumbnailCache(const AssetThumbnailCache&) = delete;
        AssetThumbnailCache& operator=(const AssetThumbnailCache&) = delete;

        // Advances the frame counter, polls in-flight loads, promotes queued
        // entries up to the concurrency cap, evicts LRU, and flushes the
        // deferred-release queue. Call once per frame before drawing the grid.
        void update();

        // Ready ImGui descriptor for this asset, or nullptr (caller draws the
        // atlas icon). Kicks off an async load on first request and refreshes
        // when the asset's timestamp changes. Never blocks.
        void* requestThumbnail(const Asset& asset);

        // Phase 1: Texture / HDR only.
        static bool isThumbnailable(AssetType type);

        void invalidate(const std::string& path);
        void invalidateAll();
        void shutdown(); // immediate synchronous release of all handles; idempotent

    private:
        enum class State : uint8_t { Queued, Loading, Ready, Failed };

        // Image  = .vfImage/.vfHdr via the async editor-texture path.
        // Render = mesh/material/material-instance rendered by ThumbnailRenderAdapter.
        enum class Source : uint8_t { Image, Render };

        struct Entry
        {
            std::string path;          // cache key
            int64_t lastModified = 0;  // freshness discriminator
            bool isHDR = false;
            Source source = Source::Image;
            bool materialKind = false; // Render only: true = material/instance, false = mesh
            State state = State::Queued;
            services::EditorTextureHandle handle; // valid only in Ready
            uint64_t lastUsedFrame = 0;
        };

        using EntryList = std::list<Entry>;

        EntryList entries;                                           // front = most-recently-used
        std::unordered_map<std::string, EntryList::iterator> index; // path -> node
        std::vector<EntryList::iterator> pending;                   // FIFO of Queued entries

        struct DeferredRelease { void* handle; uint64_t frameScheduled; Source source; };
        std::vector<DeferredRelease> deferredReleases;

        int inFlight = 0;
        uint64_t frameCounter = 0;
        bool shutDown = false;

        static constexpr int MAX_IN_FLIGHT = 4;
        static constexpr int MAX_PROMOTIONS_PER_FRAME = 2;
        static constexpr size_t MAX_ENTRIES = 256;
        static constexpr uint64_t RELEASE_DELAY_FRAMES = 3;

        void startLoad(EntryList::iterator it);
        void pollLoad(EntryList::iterator it);
        void cancelLoad(EntryList::iterator it);
        void scheduleRelease(services::EditorTextureHandle& handle, Source source);
        void removeEntry(EntryList::iterator it);
        void evictIfNeeded();
        void flushDeferredReleases(bool force);

        void* instanceIdOf(const EntryList::iterator& it) const { return &(*it); }
    };
}
