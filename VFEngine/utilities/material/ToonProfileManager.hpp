#pragma once
#include "ToonProfile.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace material
{
    // CPU asset store + change-notification hub for `.vfToonProfile` assets.
    // Mirrors MaterialManager (singleton + change-callback registry). Owns the
    // authoritative cached CPU profiles; the GPU table (ToonProfileGpuTable) and
    // editor talk to this. It does NOT own GPU slots.
    using ToonProfileChangedCallback = std::function<void(const std::string& profilePath)>;

    class ToonProfileManager
    {
    public:
        static ToonProfileManager& instance();

        // Cached load by path. Returns nullptr on empty path / load failure.
        std::shared_ptr<ToonProfile> getOrLoad(const std::string& path);

        // Re-read from disk, update the cached object in place, notify subscribers.
        bool reload(const std::string& path);

        // Write to disk, update the cached object in place, notify subscribers.
        bool save(const std::string& path, const ToonProfile& profile);

        // Built-in default (also used for GPU table slot 0 / unknown-ref fallback).
        static ToonProfile defaultProfile();

        uint64_t registerChangeCallback(ToonProfileChangedCallback cb);
        void unregisterChangeCallback(uint64_t id);

    private:
        ToonProfileManager() = default;
        ~ToonProfileManager() = default;
        ToonProfileManager(const ToonProfileManager&) = delete;
        ToonProfileManager& operator=(const ToonProfileManager&) = delete;

        void notifyProfileChanged(const std::string& path);

        mutable std::mutex mutex;
        std::unordered_map<std::string, std::shared_ptr<ToonProfile>> cache;
        std::unordered_map<uint64_t, ToonProfileChangedCallback> callbacks;
        uint64_t nextCallbackId = 1;
    };
}
