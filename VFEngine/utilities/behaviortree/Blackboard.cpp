#include "Blackboard.hpp"

namespace behaviortree
{
    void Blackboard::set(const std::string& key, const BlackboardValue& value)
    {
        values[key] = value;
        keyVersions[key] = ++globalVersion;
    }

    BlackboardValue Blackboard::get(const std::string& key) const
    {
        auto it = values.find(key);
        if (it != values.end())
        {
            return it->second;
        }
        return 0.0f; // default float
    }

    bool Blackboard::has(const std::string& key) const
    {
        return values.find(key) != values.end();
    }

    void Blackboard::remove(const std::string& key)
    {
        if (values.erase(key) > 0)
        {
            keyVersions.erase(key);
            ++globalVersion;
        }
    }

    void Blackboard::clear()
    {
        values.clear();
        keyVersions.clear();
        ++globalVersion;
    }

    uint64_t Blackboard::getVersion(const std::string& key) const
    {
        auto it = keyVersions.find(key);
        return it != keyVersions.end() ? it->second : 0;
    }

    void Blackboard::initializeFromGraph(const BTGraph& graph)
    {
        clear();
        for (const auto& keyDef : graph.blackboardKeys)
        {
            // Route through set() so each default key is stamped with a non-zero version. A key left
            // present-with-version-0 would let a later remove()/clear() flip has() true->false without
            // a version change, leaving a stale cached observer result. (VK-1456 correctness invariant.)
            set(keyDef.name, keyDef.defaultValue);
        }
    }

    float Blackboard::getFloat(const std::string& key, float def) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<float>(it->second))
        {
            return std::get<float>(it->second);
        }
        return def;
    }

    int32_t Blackboard::getInt(const std::string& key, int32_t def) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<int32_t>(it->second))
        {
            return std::get<int32_t>(it->second);
        }
        return def;
    }

    bool Blackboard::getBool(const std::string& key, bool def) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<bool>(it->second))
        {
            return std::get<bool>(it->second);
        }
        return def;
    }

    std::string Blackboard::getString(const std::string& key, const std::string& def) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<std::string>(it->second))
        {
            return std::get<std::string>(it->second);
        }
        return def;
    }

    glm::vec3 Blackboard::getVec3(const std::string& key, glm::vec3 def) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<glm::vec3>(it->second))
        {
            return std::get<glm::vec3>(it->second);
        }
        return def;
    }

    services::EntityHandle Blackboard::getEntity(const std::string& key) const
    {
        auto it = values.find(key);
        if (it != values.end() && std::holds_alternative<services::EntityHandle>(it->second))
        {
            return std::get<services::EntityHandle>(it->second);
        }
        return services::EntityHandle::invalid();
    }
}
