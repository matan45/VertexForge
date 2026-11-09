#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mcp::undo
{
    // VK-1651: undoing an entity delete (or redoing a create) re-instantiates the
    // subtree from JSON, and the engine mints fresh entity ids for it. MCP undo
    // commands therefore store the ids they saw when they were recorded and resolve
    // them through this map at execute()/undo() time.
    //
    // Entries are kept final: a chain A -> B -> C is stored as A -> C and B -> C, so
    // resolve() is a single lookup. Cleared on SceneClearedNotification together with
    // the undo history (registerUndoTools owns that subscription).
    class EntityIdRemap
    {
    public:
        static EntityIdRemap& instance();

        // `oldId` (or whatever it currently resolves to) now lives on as `newId`.
        void record(uint32_t oldId, uint32_t newId);

        // The live id for `id`; `id` itself when it was never remapped.
        uint32_t resolve(uint32_t id) const;

        void clear();

        std::map<uint32_t, uint32_t> snapshot() const;

        // Entries of `after` that are new or point elsewhere than in `before`, as (from, to).
        static std::vector<std::pair<uint32_t, uint32_t>> changes(const std::map<uint32_t, uint32_t>& before,
                                                                  const std::map<uint32_t, uint32_t>& after);

    private:
        mutable std::mutex mutex;
        std::unordered_map<uint32_t, uint32_t> entries;
    };
}
