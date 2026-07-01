#pragma once

#include "BehaviorTreeTypes.hpp"
#include <unordered_map>

namespace behaviortree
{
    class Blackboard
    {
    private:
        std::unordered_map<std::string, BlackboardValue> values;

        // Change-tracking (VK-1456): every mutation bumps a monotonic counter and stamps the key's
        // version, so runtime observers (BlackboardCondition) can re-evaluate only when a watched key
        // actually changed. Deliberately plain (not atomic): the values map is itself unguarded, so
        // atomics here would be false safety. If Blackboard is ever mutex-guarded, the same lock must
        // cover these fields — version and value are one invariant.
        uint64_t globalVersion = 0;
        std::unordered_map<std::string, uint64_t> keyVersions;
    public:
        void set(const std::string& key, const BlackboardValue& value);
        BlackboardValue get(const std::string& key) const;
        bool has(const std::string& key) const;
        void remove(const std::string& key);
        void clear();

        // Monotonic change stamp for a key; 0 if the key is absent. Guaranteed: any change to
        // has(key)/get(key) — via set/remove/clear/initializeFromGraph — moves this to a new value.
        uint64_t getVersion(const std::string& key) const;

        // Initialize from graph's blackboard key definitions
        void initializeFromGraph(const BTGraph& graph);

        // Typed accessors
        float getFloat(const std::string& key, float def = 0.0f) const;
        int32_t getInt(const std::string& key, int32_t def = 0) const;
        bool getBool(const std::string& key, bool def = false) const;
        std::string getString(const std::string& key, const std::string& def = "") const;
        glm::vec3 getVec3(const std::string& key, glm::vec3 def = glm::vec3{0.0f}) const;
        services::EntityHandle getEntity(const std::string& key) const;

        const std::unordered_map<std::string, BlackboardValue>& getAll() const { return values; }
    };
}
