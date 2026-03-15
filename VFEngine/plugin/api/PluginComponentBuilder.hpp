#pragma once
#include "components/PluginPropertyTypes.hpp"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace plugin
{
    class PluginContext;

    // Builder for declaring plugin component properties.
    // All methods are virtual — memory operations happen in the exe's address space,
    // avoiding DLL heap mismatch issues.
    class ComponentBuilder
    {
    public:
        virtual ~ComponentBuilder() = default;

        virtual ComponentBuilder& addInt(const std::string& name, int defaultVal, float min = 0, float max = 0) = 0;
        virtual ComponentBuilder& addFloat(const std::string& name, float defaultVal, float min = 0, float max = 0) = 0;
        virtual ComponentBuilder& addBool(const std::string& name, bool defaultVal) = 0;
        virtual ComponentBuilder& addString(const std::string& name, const std::string& defaultVal) = 0;
        virtual ComponentBuilder& addVec2(const std::string& name, glm::vec2 defaultVal) = 0;
        virtual ComponentBuilder& addVec3(const std::string& name, glm::vec3 defaultVal) = 0;
        virtual ComponentBuilder& addVec4(const std::string& name, glm::vec4 defaultVal) = 0;
        virtual ComponentBuilder& addColor(const std::string& name, glm::vec4 defaultVal) = 0;

        // Add a dynamic-length array property.
        // Use the returned builder to define the element schema (one element's properties).
        // Example: addArray("items").addString("name", "").addInt("count", 1).endArray();
        virtual ComponentBuilder& addArray(const std::string& name) = 0;

        // End the current array element definition and return to the parent builder.
        virtual ComponentBuilder& endArray() = 0;

        // Add a nested object property (fixed struct with named fields).
        // Use the returned builder to define the object's fields.
        // Example: addObject("stats").addInt("str", 10).addInt("dex", 10).endObject();
        virtual ComponentBuilder& addObject(const std::string& name) = 0;

        // End the current object definition and return to the parent builder.
        virtual ComponentBuilder& endObject() = 0;

        // Optional: provide a custom ImGui inspector callback.
        // Only used in Editor (ignored in Runtime).
        // The callback receives mutable JSON data and returns true if any value was modified.
        virtual ComponentBuilder& setInspector(std::function<bool(nlohmann::json&)> inspectorCallback) = 0;

        // Finalizes registration.
        virtual void build() = 0;
    };
}
