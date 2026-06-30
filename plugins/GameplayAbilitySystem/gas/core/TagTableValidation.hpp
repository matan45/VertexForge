#pragma once

// Gameplay Ability System (VK-816) — tag-table validation + rename helpers.
// ENGINE-FREE (std only). Pure functions powering the Tag Table editor's
// validation + hierarchical rename, and unit-testable without an engine.

#include "Tags.hpp"

#include <set>
#include <string>
#include <vector>

namespace gas
{
    // Tags that appear more than once in the table (sorted, unique). Empty when
    // the table is well-formed.
    inline std::vector<std::string> findDuplicateTags(const TagTable& table)
    {
        std::set<std::string> seen;
        std::set<std::string> dups;
        for (const auto& def : table.definitions())
            if (!seen.insert(def.tag).second)
                dups.insert(def.tag);
        return {dups.begin(), dups.end()};
    }

    // Rename a tag and CASCADE to its hierarchical descendants: renaming "A" to
    // "X" turns "A", "A.B" and "A.B.C" into "X", "X.B", "X.B.C". Exact-or-
    // descendant match on dotted boundaries. Returns the number of definitions
    // changed. Pure (no engine, no I/O).
    inline int renameTagCascade(TagTable& table, const std::string& from, const std::string& to)
    {
        if (from.empty() || to.empty() || from == to)
            return 0;
        int changed = 0;
        for (auto& def : table.definitions())
        {
            if (def.tag == from)
            {
                def.tag = to;
                ++changed;
            }
            else if (def.tag.size() > from.size() &&
                     def.tag.compare(0, from.size(), from) == 0 &&
                     def.tag[from.size()] == '.')
            {
                def.tag = to + def.tag.substr(from.size());
                ++changed;
            }
        }
        return changed;
    }
}
