#pragma once

// Gameplay Ability System (VK-816) — hierarchical dotted gameplay tags.
//
// ENGINE-FREE: this header (and everything under gas/core) depends only on the
// C++ standard library and nlohmann/json. No engine, ECS, ImGui or glm types.
// The engine runtime keeps per-entity state and feeds these pure objects in.
//
// Tag match direction (documented contract, mirrors UE GAS):
//   A container "has" a query tag if it owns that exact tag OR owns any
//   *descendant* of it. So a query for "Character" is satisfied by an owned
//   "Character.Stunned"; owning the parent "Character" does NOT satisfy a query
//   for the child "Character.Stunned". Matching respects dotted boundaries, so
//   "Char" never matches "Character".

#include <set>
#include <string>
#include <vector>
#include <algorithm>

namespace gas
{
    // True if owned tag satisfies query: exact match, or owned is a hierarchical
    // descendant of query (owned begins with query + '.'). Boundary-aware.
    inline bool tagMatches(const std::string& owned, const std::string& query)
    {
        if (query.empty())
            return false;
        if (owned == query)
            return true;
        // owned is a descendant of query: "query" is a dotted prefix of "owned".
        if (owned.size() > query.size() &&
            owned.compare(0, query.size(), query) == 0 &&
            owned[query.size()] == '.')
            return true;
        return false;
    }

    // A set of owned dotted tags supporting hierarchical queries.
    class TagContainer
    {
    public:
        TagContainer() = default;
        TagContainer(std::initializer_list<std::string> tags)
        {
            for (const auto& t : tags)
                addTag(t);
        }

        void addTag(const std::string& tag)
        {
            if (!tag.empty())
                m_tags.insert(tag);
        }

        // Removes the EXACT tag only (descendants are untouched).
        void removeTag(const std::string& tag)
        {
            m_tags.erase(tag);
        }

        void clear() { m_tags.clear(); }

        // Exact membership, no hierarchy.
        bool hasTagExact(const std::string& tag) const
        {
            return m_tags.find(tag) != m_tags.end();
        }

        // Hierarchical query (see header contract). Satisfied if any owned tag
        // is `query` itself or a descendant of `query`.
        bool hasTag(const std::string& query) const
        {
            for (const auto& owned : m_tags)
                if (tagMatches(owned, query))
                    return true;
            return false;
        }

        // True only if every query tag is satisfied. Empty list => true (vacuous).
        bool hasAll(const std::vector<std::string>& queries) const
        {
            for (const auto& q : queries)
                if (!hasTag(q))
                    return false;
            return true;
        }

        // True if at least one query tag is satisfied. Empty list => false.
        bool hasAny(const std::vector<std::string>& queries) const
        {
            for (const auto& q : queries)
                if (hasTag(q))
                    return true;
            return false;
        }

        bool empty() const { return m_tags.empty(); }
        std::size_t size() const { return m_tags.size(); }

        const std::set<std::string>& tags() const { return m_tags; }

        std::set<std::string>::const_iterator begin() const { return m_tags.begin(); }
        std::set<std::string>::const_iterator end() const { return m_tags.end(); }

        bool operator==(const TagContainer& o) const { return m_tags == o.m_tags; }
        bool operator!=(const TagContainer& o) const { return !(*this == o); }

    private:
        std::set<std::string> m_tags;
    };

    // A single declared tag in a project tag dictionary, with an optional comment.
    struct TagDefinition
    {
        std::string tag;
        std::string comment;

        bool operator==(const TagDefinition& o) const
        {
            return tag == o.tag && comment == o.comment;
        }
        bool operator!=(const TagDefinition& o) const { return !(*this == o); }
    };

    // Project tag dictionary (the runtime form of a .vfGameplayTags asset). Multiple
    // author-able tables MERGE into one runtime dictionary; merge is a de-duplicating
    // union keyed by tag string (first comment wins for an existing tag).
    class TagTable
    {
    public:
        void declare(const std::string& tag, const std::string& comment = "")
        {
            if (tag.empty())
                return;
            for (auto& def : m_defs)
                if (def.tag == tag)
                {
                    if (def.comment.empty())
                        def.comment = comment;
                    return;
                }
            m_defs.push_back({tag, comment});
        }

        // Exact existence of a declared tag.
        bool contains(const std::string& tag) const
        {
            for (const auto& def : m_defs)
                if (def.tag == tag)
                    return true;
            return false;
        }

        // Immediate children of `prefix` (one dotted level below). With an empty
        // prefix, returns the top-level tags (those with no '.'). Sorted, unique.
        std::vector<std::string> childrenOf(const std::string& prefix) const
        {
            std::set<std::string> out;
            for (const auto& def : m_defs)
            {
                const std::string& t = def.tag;
                if (prefix.empty())
                {
                    if (t.find('.') == std::string::npos)
                        out.insert(t);
                }
                else if (t.size() > prefix.size() &&
                         t.compare(0, prefix.size(), prefix) == 0 &&
                         t[prefix.size()] == '.')
                {
                    // Remainder after "prefix." must contain no further dot to be immediate.
                    const std::string remainder = t.substr(prefix.size() + 1);
                    if (!remainder.empty() && remainder.find('.') == std::string::npos)
                        out.insert(t);
                }
            }
            return {out.begin(), out.end()};
        }

        void merge(const TagTable& other)
        {
            for (const auto& def : other.m_defs)
                declare(def.tag, def.comment);
        }

        const std::vector<TagDefinition>& definitions() const { return m_defs; }
        std::vector<TagDefinition>& definitions() { return m_defs; }

        bool empty() const { return m_defs.empty(); }
        std::size_t size() const { return m_defs.size(); }

        bool operator==(const TagTable& o) const { return m_defs == o.m_defs; }
        bool operator!=(const TagTable& o) const { return !(*this == o); }

    private:
        std::vector<TagDefinition> m_defs;
    };
}
