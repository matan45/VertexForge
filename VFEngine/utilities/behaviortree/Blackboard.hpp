#pragma once

#include "BehaviorTreeTypes.hpp"
#include <unordered_map>

namespace behaviortree
{
    class Blackboard
    {
    public:
        void set(const std::string& key, const BlackboardValue& value);
        BlackboardValue get(const std::string& key) const;
        bool has(const std::string& key) const;
        void remove(const std::string& key);
        void clear();

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

    private:
        std::unordered_map<std::string, BlackboardValue> values;
    };
}
