#include "AssetThumbnailCache.hpp"
#include "../material/MaterialGraphEvaluator.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/render/ThumbnailEvents.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "material/MaterialTypes.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include <algorithm>
#include <optional>

namespace windows
{
    namespace
    {
        // Build standard-PBR preview params for a .vfMat / .vfMatInstance, reusing
        // the editor's material-graph evaluator. Returns nullopt on load failure so
        // the caller falls back to the atlas icon. useCustomShader stays off — a
        // 128px thumbnail does not need per-material shader compilation.
        std::optional<services::MaterialPreviewParams> buildMaterialParams(const std::string& path, bool isInstance)
        {
            if (isInstance)
            {
                auto instance = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(path));
                if (!instance)
                {
                    return std::nullopt;
                }
                auto parent = resource::ResourceManager::loadMaterial(instance->parentMaterialRef);
                if (!parent)
                {
                    return std::nullopt;
                }
                auto result = editor::materialeditor::MaterialGraphEvaluator::evaluate(parent->graph);
                services::MaterialPreviewParams params =
                    editor::materialeditor::MaterialGraphEvaluator::toPreviewParams(result, path, parent, false);
                // Apply the instance's fixed PBR scalar overrides (texture overrides
                // are skipped for thumbnails — base textures are representative enough).
                if (instance->albedoOverride) params.albedo = *instance->albedoOverride;
                if (instance->metallicOverride) params.metallic = *instance->metallicOverride;
                if (instance->roughnessOverride) params.roughness = *instance->roughnessOverride;
                if (instance->aoOverride) params.ao = *instance->aoOverride;
                if (instance->emissionOverride) params.emission = *instance->emissionOverride;
                return params;
            }

            auto material = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(path));
            if (!material)
            {
                return std::nullopt;
            }
            auto result = editor::materialeditor::MaterialGraphEvaluator::evaluate(material->graph);
            return editor::materialeditor::MaterialGraphEvaluator::toPreviewParams(result, path, material, false);
        }
    }

    bool AssetThumbnailCache::isThumbnailable(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture:
        case AssetType::HDR:
        case AssetType::Model:
        case AssetType::Material:
        case AssetType::MaterialInstance:
            return true;
        default:
            return false;
        }
    }

    AssetThumbnailCache::~AssetThumbnailCache()
    {
        shutdown();
    }

    void* AssetThumbnailCache::requestThumbnail(const Asset& asset)
    {
        if (shutDown || asset.isDirectory || !isThumbnailable(asset.type))
            return nullptr;

        if (auto idxIt = index.find(asset.path); idxIt != index.end())
        {
            EntryList::iterator it = idxIt->second;

            // Content changed on disk -> drop the stale entry and reload.
            if (it->lastModified != asset.lastModified)
            {
                removeEntry(it);
            }
            else
            {
                it->lastUsedFrame = frameCounter;
                entries.splice(entries.begin(), entries, it); // move-to-front (LRU)
                return it->state == State::Ready ? it->handle.imguiDescriptorSet : nullptr;
            }
        }

        const bool render = (asset.type == AssetType::Model
                             || asset.type == AssetType::Material
                             || asset.type == AssetType::MaterialInstance);

        Entry entry;
        entry.path = asset.path;
        entry.lastModified = asset.lastModified;
        entry.isHDR = (asset.type == AssetType::HDR);
        entry.source = render ? Source::Render : Source::Image;
        entry.materialKind = (asset.type == AssetType::Material || asset.type == AssetType::MaterialInstance);
        entry.state = State::Queued;
        entry.lastUsedFrame = frameCounter;

        entries.push_front(std::move(entry));
        EntryList::iterator it = entries.begin();
        index[it->path] = it;
        pending.push_back(it);
        return nullptr;
    }

    void AssetThumbnailCache::update()
    {
        if (shutDown)
            return;

        ++frameCounter;

        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->state == State::Loading)
                pollLoad(it);
        }

        int promotions = 0;
        while (!pending.empty()
               && inFlight < MAX_IN_FLIGHT
               && promotions < MAX_PROMOTIONS_PER_FRAME)
        {
            EntryList::iterator it = pending.front();
            pending.erase(pending.begin());

            // removeEntry() always pulls the node out of `pending`, so any
            // iterator still here is alive; guard on state defensively.
            if (it->state != State::Queued)
                continue;

            startLoad(it);
            ++promotions;
        }

        evictIfNeeded();
        flushDeferredReleases(false);
    }

    void AssetThumbnailCache::startLoad(EntryList::iterator it)
    {
        if (it->source == Source::Image)
        {
            events::render::LoadEditorTextureAsyncCommand cmd;
            cmd.instanceId = instanceIdOf(it);
            cmd.path = it->path;
            cmd.isHDR = it->isHDR;
            events::EventDispatcher::instance().execute(cmd);
        }
        else
        {
            events::render::LoadRenderThumbnailAsyncCommand cmd;
            cmd.instanceId = instanceIdOf(it);
            if (it->materialKind)
            {
                cmd.kind = events::render::RenderThumbnailKind::Material;
                auto params = buildMaterialParams(it->path, /*isInstance*/ it->path.ends_with(".vfMatInstance"));
                if (!params)
                {
                    it->state = State::Failed; // -> atlas icon fallback
                    return;
                }
                cmd.materialParams = std::move(*params);
            }
            else
            {
                cmd.kind = events::render::RenderThumbnailKind::Mesh;
                cmd.meshPath = it->path;
            }
            events::EventDispatcher::instance().execute(cmd);
        }

        it->state = State::Loading;
        ++inFlight;
    }

    void AssetThumbnailCache::pollLoad(EntryList::iterator it)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        void* id = instanceIdOf(it);

        services::TextureLoadingProgress progress;
        if (it->source == Source::Image)
        {
            events::render::GetTextureLoadingProgressQuery progressQuery;
            progressQuery.instanceId = id;
            progress = dispatcher.query(progressQuery);
        }
        else
        {
            events::render::GetRenderThumbnailProgressQuery progressQuery;
            progressQuery.instanceId = id;
            progress = dispatcher.query(progressQuery);
        }

        if (progress.state == services::LoadingState::Complete)
        {
            // The handle query TAKES the result; query exactly once.
            if (it->source == Source::Image)
            {
                events::render::GetLoadedTextureHandleQuery handleQuery;
                handleQuery.instanceId = id;
                it->handle = dispatcher.query(handleQuery);
            }
            else
            {
                events::render::GetRenderThumbnailHandleQuery handleQuery;
                handleQuery.instanceId = id;
                it->handle = dispatcher.query(handleQuery);
            }
            it->state = it->handle.isValid() ? State::Ready : State::Failed;
            --inFlight;
        }
        else if (progress.state == services::LoadingState::Error
                 || progress.state == services::LoadingState::Cancelled
                 || progress.state == services::LoadingState::Idle)
        {
            // Idle means the async job no longer exists. This happens when the
            // render adapter's frame-budgeted GC reclaims a finished job before
            // this cache observed completion (e.g. the Content Browser was hidden,
            // so update() — and thus pollLoad — stopped running while the adapter
            // kept ticking). Treat it as terminal so the entry leaves Loading and
            // frees its in-flight slot; otherwise the slot leaks permanently and
            // thumbnail loading eventually stalls once all MAX_IN_FLIGHT slots are
            // stuck.
            it->state = State::Failed;
            --inFlight;
        }
        // otherwise still loading; poll again next frame
    }

    void AssetThumbnailCache::cancelLoad(EntryList::iterator it)
    {
        if (it->state != State::Loading)
            return;

        if (it->source == Source::Image)
        {
            events::render::CancelTextureLoadingCommand cmd;
            cmd.instanceId = instanceIdOf(it);
            events::EventDispatcher::instance().execute(cmd);
        }
        else
        {
            events::render::CancelRenderThumbnailCommand cmd;
            cmd.instanceId = instanceIdOf(it);
            events::EventDispatcher::instance().execute(cmd);
        }

        it->state = State::Failed;
        --inFlight;
    }

    void AssetThumbnailCache::scheduleRelease(services::EditorTextureHandle& handle, Source source)
    {
        if (handle.imguiDescriptorSet)
            deferredReleases.push_back({handle.imguiDescriptorSet, frameCounter, source});
        handle = {};
    }

    void AssetThumbnailCache::removeEntry(EntryList::iterator it)
    {
        // Cancel before erasing so a load never completes against a freed node.
        cancelLoad(it);
        scheduleRelease(it->handle, it->source);

        pending.erase(std::remove(pending.begin(), pending.end(), it), pending.end());
        index.erase(it->path);
        entries.erase(it);
    }

    void AssetThumbnailCache::evictIfNeeded()
    {
        while (entries.size() > MAX_ENTRIES)
        {
            EntryList::iterator victim = entries.end();
            for (auto it = entries.end(); it != entries.begin(); )
            {
                --it;
                if (it->lastUsedFrame == frameCounter || it->state == State::Loading)
                    continue;
                victim = it;
                break;
            }
            if (victim == entries.end())
                break; // nothing evictable this pass
            removeEntry(victim);
        }
    }

    void AssetThumbnailCache::flushDeferredReleases(bool force)
    {
        if (deferredReleases.empty())
            return;

        auto& dispatcher = events::EventDispatcher::instance();
        for (auto it = deferredReleases.begin(); it != deferredReleases.end(); )
        {
            if (force || frameCounter - it->frameScheduled >= RELEASE_DELAY_FRAMES)
            {
                if (it->source == Source::Image)
                {
                    events::render::ReleaseEditorTextureCommand cmd;
                    cmd.handle = it->handle;
                    dispatcher.execute(cmd);
                }
                else
                {
                    events::render::ReleaseRenderThumbnailCommand cmd;
                    cmd.handle = it->handle;
                    dispatcher.execute(cmd);
                }
                it = deferredReleases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AssetThumbnailCache::invalidate(const std::string& path)
    {
        if (auto idxIt = index.find(path); idxIt != index.end())
            removeEntry(idxIt->second);
    }

    void AssetThumbnailCache::invalidateAll()
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            cancelLoad(it);
            scheduleRelease(it->handle, it->source);
        }
        entries.clear();
        index.clear();
        pending.clear();
        inFlight = 0;
    }

    void AssetThumbnailCache::shutdown()
    {
        if (shutDown)
            return;
        shutDown = true;

        auto& dispatcher = events::EventDispatcher::instance();
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->state == State::Loading)
            {
                if (it->source == Source::Image)
                {
                    events::render::CancelTextureLoadingCommand cancelCmd;
                    cancelCmd.instanceId = instanceIdOf(it);
                    dispatcher.execute(cancelCmd);
                }
                else
                {
                    events::render::CancelRenderThumbnailCommand cancelCmd;
                    cancelCmd.instanceId = instanceIdOf(it);
                    dispatcher.execute(cancelCmd);
                }
            }
            if (it->handle.imguiDescriptorSet)
            {
                if (it->source == Source::Image)
                {
                    events::render::ReleaseEditorTextureCommand releaseCmd;
                    releaseCmd.handle = it->handle.imguiDescriptorSet;
                    dispatcher.execute(releaseCmd);
                }
                else
                {
                    events::render::ReleaseRenderThumbnailCommand releaseCmd;
                    releaseCmd.handle = it->handle.imguiDescriptorSet;
                    dispatcher.execute(releaseCmd);
                }
            }
        }
        entries.clear();
        index.clear();
        pending.clear();
        inFlight = 0;

        // No more frames will run; release anything still deferred now.
        flushDeferredReleases(true);
    }
}
