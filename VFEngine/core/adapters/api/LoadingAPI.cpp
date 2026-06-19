// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "LoadingAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../../services/events/render/ObjectStreamingEvents.hpp"
#include "../../../services/events/terrain/TerrainEvents.hpp"

#include "resource/ResourceLoadScheduler.hpp"
#include "loading/LoadingProgress.hpp"

#include <atomic>
#include <algorithm>

namespace core::api
{
    namespace
    {
        // Smooth scene-entity load fraction, maintained by subscribing to the
        // scene-load notifications (published from ScenePersistenceService as it
        // deserializes entities). The notifications may fire from a frame-task
        // worker thread, so these are atomic.
        std::atomic<bool> g_sceneLoadActive{false};
        std::atomic<float> g_sceneFraction{1.0f};
        bool g_subscribed = false;

        // High-water marks so the terrain / resource bands can derive a 0-1
        // fraction from a "pending count" that only ever decreases. These are
        // touched only from gatherSignals() (script thread) and reset when idle.
        float g_maxTilePending = 0.0f;
        float g_maxResourceLoads = 0.0f;

        struct LiveSignals
        {
            bool active = false;
            uint32_t pendingTiles = 0;
            uint32_t streamingPending = 0; // gpu queued-for-upload + resource in-flight
            loading::LoadingPhaseInputs inputs;
        };

        // Query a service, tolerating the handler being unbound (e.g. no world /
        // object-streaming service in the current process) by returning a fallback.
        template <typename Q>
        typename Q::ResultType queryOr(const Q& q, typename Q::ResultType fallback)
        {
            try { return events::EventDispatcher::instance().query(q); }
            catch (const std::exception&) { return fallback; }
        }

        LiveSignals gatherSignals()
        {
            LiveSignals s;

            const float sceneFrac = std::clamp(g_sceneFraction.load(), 0.0f, 1.0f);
            const bool sceneActive = g_sceneLoadActive.load();

            // Terrain: pending sector tile actions (world mode). No total is
            // exposed, so track the high-water mark to make a smooth fraction.
            const uint32_t pendingTiles =
                queryOr(events::terrain::GetPendingSectorTileActionCountQuery{}, 0u);
            s.pendingTiles = pendingTiles;

            // GPU object streaming + CPU resource loads -> the "streaming" band.
            const render::gpudriven::ObjectStreamingStats objStats =
                queryOr(events::render::objectstreaming::GetObjectStreamingStatsQuery{},
                        render::gpudriven::ObjectStreamingStats{});
            const uint32_t activeResourceLoads = static_cast<uint32_t>(
                resource::ResourceLoadScheduler::instance().getActiveLoads().size());
            s.streamingPending = objStats.queuedForUpload + activeResourceLoads;

            s.active = sceneActive || pendingTiles > 0 || objStats.queuedForUpload > 0 ||
                       activeResourceLoads > 0;

            // Reset high-water marks between load sessions so a later, smaller
            // load doesn't inherit an inflated denominator.
            if (!s.active)
            {
                g_maxTilePending = 0.0f;
                g_maxResourceLoads = 0.0f;
            }

            // --- terrain band (0-40%) ---
            g_maxTilePending = std::max(g_maxTilePending, static_cast<float>(pendingTiles));
            const float terrainFrac = g_maxTilePending > 0.0f
                ? std::clamp(1.0f - static_cast<float>(pendingTiles) / g_maxTilePending, 0.0f, 1.0f)
                : 1.0f;

            // --- sectors band (40-70%): scene entity deserialization ---
            const float sectorsFrac = sceneActive ? sceneFrac : 1.0f;

            // --- gpu band (70-90%): prefer GPU object streaming ratio, else CPU loads ---
            float gpuFrac = 1.0f;
            if (objStats.totalRegistered > 0)
            {
                gpuFrac = std::clamp(static_cast<float>(objStats.activeOnGPU) /
                                         static_cast<float>(objStats.totalRegistered),
                                     0.0f, 1.0f);
            }
            else
            {
                g_maxResourceLoads =
                    std::max(g_maxResourceLoads, static_cast<float>(activeResourceLoads));
                gpuFrac = g_maxResourceLoads > 0.0f
                    ? std::clamp(1.0f - static_cast<float>(activeResourceLoads) / g_maxResourceLoads,
                                 0.0f, 1.0f)
                    : 1.0f;
            }

            s.inputs.terrain = terrainFrac;
            s.inputs.sectors = sectorsFrac;
            s.inputs.gpu = gpuFrac;
            s.inputs.init = 1.0f; // no dedicated signal yet (physics/navmesh/scripts)
            return s;
        }

        void ensureSubscribed()
        {
            if (g_subscribed) return;
            g_subscribed = true;

            auto& d = events::EventDispatcher::instance();
            d.subscribe<events::scene::SceneLoadingStartedNotification>(
                [](const events::scene::SceneLoadingStartedNotification&)
                {
                    g_sceneLoadActive.store(true);
                    g_sceneFraction.store(0.0f);
                });
            d.subscribe<events::scene::SceneLoadingProgressUpdatedNotification>(
                [](const events::scene::SceneLoadingProgressUpdatedNotification& n)
                {
                    g_sceneFraction.store(std::clamp(n.progress, 0.0f, 1.0f));
                });
            d.subscribe<events::scene::SceneLoadingCompletedNotification>(
                [](const events::scene::SceneLoadingCompletedNotification&)
                {
                    g_sceneFraction.store(1.0f);
                    g_sceneLoadActive.store(false);
                });
        }
    } // namespace

    void LoadingAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        ensureSubscribed();

        // _native_loading_isActive() -> bool
        // True while any tracked subsystem still has loading work in flight.
        interpreter->registerNativeFunction("_native_loading_isActive",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(gatherSignals().active);
            }});

        // _native_loading_getProgress() -> float
        // Weighted 0-1 aggregate across the VK-1268 phase bands (terrain .40 /
        // sectors .30 / gpu .20 / init .10). Returns 1.0 when idle.
        interpreter->registerNativeFunction("_native_loading_getProgress",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                const auto s = gatherSignals();
                const auto p = loading::computeLoadingProgress(s.inputs, s.active);
                return value::Value(p.fraction);
            }});

        // _native_loading_getPhase() -> int (loading::LoadingPhase enum value)
        interpreter->registerNativeFunction("_native_loading_getPhase",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                const auto s = gatherSignals();
                const auto p = loading::computeLoadingProgress(s.inputs, s.active);
                return value::Value(static_cast<int64_t>(static_cast<int>(p.phase)));
            }});

        // _native_loading_getPhaseLabel() -> string (e.g. "Generating terrain...")
        interpreter->registerNativeFunction("_native_loading_getPhaseLabel",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                const auto s = gatherSignals();
                const auto p = loading::computeLoadingProgress(s.inputs, s.active);
                return value::Value(std::string(loading::loadingPhaseLabel(p.phase)));
            }});

        // _native_loading_getSceneProgress() -> float (raw scene-entity fraction 0-1)
        interpreter->registerNativeFunction("_native_loading_getSceneProgress",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(std::clamp(g_sceneFraction.load(), 0.0f, 1.0f));
            }});

        // _native_loading_getPendingTileCount() -> int (terrain tile actions queued)
        interpreter->registerNativeFunction("_native_loading_getPendingTileCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(static_cast<int64_t>(gatherSignals().pendingTiles));
            }});

        // _native_loading_getStreamingPending() -> int (gpu queued + resource in-flight)
        interpreter->registerNativeFunction("_native_loading_getStreamingPending",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(static_cast<int64_t>(gatherSignals().streamingPending));
            }});
    }
}
