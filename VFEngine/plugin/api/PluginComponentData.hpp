#pragma once
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <string>

namespace plugin
{
    class PluginComponentData
    {
    public:
        explicit PluginComponentData(nlohmann::json* data)
            : data(data)
        {
        }

        // Getters
        int getInt(const std::string& name) const
        {
            return data->value(name, 0);
        }

        float getFloat(const std::string& name) const
        {
            return data->value(name, 0.0f);
        }

        bool getBool(const std::string& name) const
        {
            return data->value(name, false);
        }

        std::string getString(const std::string& name) const
        {
            return data->value(name, std::string{});
        }

        glm::vec2 getVec2(const std::string& name) const
        {
            if (auto it = data->find(name); it != data->end() && it->is_array() && it->size() >= 2)
                return {(*it)[0].get<float>(), (*it)[1].get<float>()};
            return {};
        }

        glm::vec3 getVec3(const std::string& name) const
        {
            if (auto it = data->find(name); it != data->end() && it->is_array() && it->size() >= 3)
                return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
            return {};
        }

        glm::vec4 getVec4(const std::string& name) const
        {
            if (auto it = data->find(name); it != data->end() && it->is_array() && it->size() >= 4)
                return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>()};
            return {};
        }

        glm::vec4 getColor(const std::string& name) const
        {
            return getVec4(name);
        }

        // Setters
        void setInt(const std::string& name, int value) { (*data)[name] = value; }
        void setFloat(const std::string& name, float value) { (*data)[name] = value; }
        void setBool(const std::string& name, bool value) { (*data)[name] = value; }
        void setString(const std::string& name, const std::string& value) { (*data)[name] = value; }

        void setVec2(const std::string& name, glm::vec2 value)
        {
            (*data)[name] = nlohmann::json::array({value.x, value.y});
        }

        void setVec3(const std::string& name, glm::vec3 value)
        {
            (*data)[name] = nlohmann::json::array({value.x, value.y, value.z});
        }

        void setVec4(const std::string& name, glm::vec4 value)
        {
            (*data)[name] = nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        void setColor(const std::string& name, glm::vec4 value) { setVec4(name, value); }

        // Array accessors
        size_t getArraySize(const std::string& name) const
        {
            if (auto it = data->find(name); it != data->end() && it->is_array())
                return it->size();
            return 0;
        }

        PluginComponentData getArrayElement(const std::string& name, size_t index)
        {
            if (auto it = data->find(name); it != data->end() && it->is_array() && index < it->size())
                return PluginComponentData(&(*it)[index]);
            return PluginComponentData(nullptr);
        }

        void addArrayElement(const std::string& name, const nlohmann::json& element = nlohmann::json::object())
        {
            if (!data->contains(name) || !(*data)[name].is_array())
                (*data)[name] = nlohmann::json::array();
            (*data)[name].push_back(element);
        }

        void removeArrayElement(const std::string& name, size_t index)
        {
            if (auto it = data->find(name); it != data->end() && it->is_array() && index < it->size())
                it->erase(it->begin() + static_cast<nlohmann::json::difference_type>(index));
        }

        // Object accessor — returns a wrapper for a nested object field
        PluginComponentData getObject(const std::string& name)
        {
            if (auto it = data->find(name); it != data->end() && it->is_object())
                return PluginComponentData(&(*it));
            return PluginComponentData(nullptr);
        }

        // Raw JSON access (for advanced use)
        nlohmann::json* getRawJson() { return data; }
        const nlohmann::json* getRawJson() const { return data; }

        bool isValid() const { return data != nullptr; }

    private:
        nlohmann::json* data;
    };
}
