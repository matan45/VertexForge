#include "TerrainSaveFaultInjection.hpp"

#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace terrain
{
    namespace
    {
        std::shared_mutex injectorMutex;
        TerrainSaveFaultInjector injector;
        std::atomic<bool> injectorArmed{false};
    }

    void setTerrainSaveFaultInjector(TerrainSaveFaultInjector newInjector)
    {
        std::unique_lock lock(injectorMutex);
        injector = std::move(newInjector);
        injectorArmed.store(static_cast<bool>(injector), std::memory_order_release);
    }

    void resetTerrainSaveFaultInjector()
    {
        std::unique_lock lock(injectorMutex);
        injector = nullptr;
        injectorArmed.store(false, std::memory_order_release);
    }

    namespace detail
    {
        TerrainSaveFault terrainSaveFault(TerrainSaveStage stage)
        {
            if (!injectorArmed.load(std::memory_order_acquire))
                return {};

            TerrainSaveFaultInjector callback;
            {
                std::shared_lock lock(injectorMutex);
                callback = injector;
            }
            if (!callback)
                return {};

            try { return callback(stage); }
            catch (...) { return {}; }
        }
    }
}
