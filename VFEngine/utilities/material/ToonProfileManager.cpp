#include "ToonProfileManager.hpp"
#include "ToonProfileAsset.hpp"
#include "../print/Log.hpp"

namespace material
{
    ToonProfileManager& ToonProfileManager::instance()
    {
        static ToonProfileManager inst;
        return inst;
    }

    ToonProfile ToonProfileManager::defaultProfile()
    {
        // The struct's in-class initializers already describe a sane, visible
        // banded look (specular + rim off). Keep this as the single source of the
        // built-in default so the GPU table slot 0 and the manager agree.
        return ToonProfile{};
    }

    std::shared_ptr<ToonProfile> ToonProfileManager::getOrLoad(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(path);
            if (it != cache.end())
                return it->second;
        }

        // Load outside the lock (avoids holding it across disk I/O).
        auto loaded = ToonProfileAsset::load(path);
        if (!loaded)
            return nullptr;

        std::lock_guard<std::mutex> lock(mutex);
        // Another thread may have populated it meanwhile; keep the first winner.
        auto it = cache.find(path);
        if (it != cache.end())
            return it->second;
        auto ptr = std::make_shared<ToonProfile>(*loaded);
        cache[path] = ptr;
        return ptr;
    }

    bool ToonProfileManager::reload(const std::string& path)
    {
        if (path.empty())
            return false;

        auto loaded = ToonProfileAsset::load(path);
        if (!loaded)
        {
            vfLogError("Failed to reload toon profile: {}", path);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(path);
            if (it != cache.end())
                *it->second = *loaded;               // update in place so holders see it
            else
                cache[path] = std::make_shared<ToonProfile>(*loaded);
        }

        notifyProfileChanged(path);
        return true;
    }

    bool ToonProfileManager::save(const std::string& path, const ToonProfile& profile)
    {
        if (!ToonProfileAsset::save(path, profile))
        {
            vfLogError("Failed to save toon profile: {}", path);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = cache.find(path);
            if (it != cache.end())
                *it->second = profile;               // update in place
            else
                cache[path] = std::make_shared<ToonProfile>(profile);
        }

        notifyProfileChanged(path);
        return true;
    }

    uint64_t ToonProfileManager::registerChangeCallback(ToonProfileChangedCallback cb)
    {
        std::lock_guard<std::mutex> lock(mutex);
        uint64_t id = nextCallbackId++;
        callbacks[id] = std::move(cb);
        return id;
    }

    void ToonProfileManager::unregisterChangeCallback(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        callbacks.erase(id);
    }

    void ToonProfileManager::notifyProfileChanged(const std::string& path)
    {
        std::unordered_map<uint64_t, ToonProfileChangedCallback> copy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            copy = callbacks;
        }
        for (const auto& [id, cb] : copy)
            cb(path);
    }
}
