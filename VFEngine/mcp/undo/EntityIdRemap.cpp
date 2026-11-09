#include "EntityIdRemap.hpp"

namespace mcp::undo
{
    EntityIdRemap& EntityIdRemap::instance()
    {
        static EntityIdRemap remap;
        return remap;
    }

    void EntityIdRemap::record(uint32_t oldId, uint32_t newId)
    {
        std::lock_guard lock(mutex);

        auto found = entries.find(oldId);
        const uint32_t current = found != entries.end() ? found->second : oldId;
        if (current == newId)
        {
            return;
        }

        for (auto& [original, target] : entries)
        {
            if (target == current)
            {
                target = newId;
            }
        }
        entries[oldId] = newId;
        if (current != oldId)
        {
            entries[current] = newId;
        }
        // newId is a live identity now; a stale entry keyed by it would redirect it.
        entries.erase(newId);
    }

    uint32_t EntityIdRemap::resolve(uint32_t id) const
    {
        std::lock_guard lock(mutex);
        auto found = entries.find(id);
        return found != entries.end() ? found->second : id;
    }

    void EntityIdRemap::clear()
    {
        std::lock_guard lock(mutex);
        entries.clear();
    }

    std::map<uint32_t, uint32_t> EntityIdRemap::snapshot() const
    {
        std::lock_guard lock(mutex);
        return {entries.begin(), entries.end()};
    }

    std::vector<std::pair<uint32_t, uint32_t>> EntityIdRemap::changes(const std::map<uint32_t, uint32_t>& before,
                                                                     const std::map<uint32_t, uint32_t>& after)
    {
        std::vector<std::pair<uint32_t, uint32_t>> changed;
        for (const auto& [from, to] : after)
        {
            auto previous = before.find(from);
            if (previous == before.end() || previous->second != to)
            {
                changed.emplace_back(from, to);
            }
        }
        return changed;
    }
}
