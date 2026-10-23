#pragma once

#include "../protocol/ArgReader.hpp"

#include "asset/AssetRef.hpp"
#include "data/EntityHandle.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// The curated (built-in) component bindings behind component_add / component_get / component_set /
// component_remove (ComponentTools.cpp) and the JSON field helpers they share. VK-1656 moved them out of
// ComponentTools.cpp so a binding can live in its own TU (AnimatorBinding.cpp). Every function that
// dispatches runs on the main thread, like the tools.
namespace mcp::tools
{
    struct ComponentBinding
    {
        std::string name;
        std::string fieldHelp;
        std::function<bool(const services::EntityHandle&)> add;
        std::function<bool(const services::EntityHandle&)> remove;
        // nullopt = the entity has no such component.
        std::function<std::optional<nlohmann::json>(const services::EntityHandle&)> get;
        // Overlays `patch` onto the current data and writes it back. Returns the
        // data read back afterwards, or nullopt when the component is missing.
        std::function<std::optional<nlohmann::json>(const services::EntityHandle&, const nlohmann::json&)> patch;

        // VK-1656. Keys get() reports that component_add / component_set refuse (rejectReadOnlyFields).
        // patchBuiltinComponent drops them, so an undo can replay a full get() snapshot.
        std::vector<std::string> readOnlyFields;
        // Appended to the read-only rejection: the writable fields and where the others are edited.
        std::string readOnlyHint;
        // Appended to component_add's error when add() returns false: what the entity needs first.
        std::string addRequirement;
        // component_add only, never undo: runs after add() and before the patch, consumes creation-only
        // keys (read-only fields) and returns the data to patch with. Throws ArgError for bad input.
        std::function<nlohmann::json(const services::EntityHandle&, const nlohmann::json& data)> prepareAdd;
        // VK-1656: a binding stored in fields of another built-in component (the Animator lives in the
        // Mesh's animatorRef / retargetRef / applyRootMotion). Undoing an add patches those fields back
        // (builtinHostFields) instead of removing the component, which would also clear values the host
        // held before the add. Empty for a component of its own.
        std::string hostType;
        std::vector<std::string> hostFields;
        // VK-1657, undo only (restoreBuiltinComponent): writes back a full get() snapshot. A snapshot holds
        // values the engine stored, which patch() may refuse as agent input (an effect outside the project,
        // a value outside a tool's range set in a scene file); restore checks them against the engine's own
        // rules instead. Empty = patch.
        std::function<std::optional<nlohmann::json>(const services::EntityHandle&, const nlohmann::json&)> restore;
    };

    // ArgError when the object `data` sets one of the binding's read-only fields.
    inline void rejectReadOnlyFields(const ComponentBinding& binding, const nlohmann::json& data)
    {
        if (!data.is_object())
        {
            return;
        }
        for (const std::string& field : binding.readOnlyFields)
        {
            if (data.contains(field))
            {
                std::string message = "field '" + field + "' of " + binding.name + " is read-only";
                if (!binding.readOnlyHint.empty())
                {
                    message += ". " + binding.readOnlyHint;
                }
                throw ArgError(message);
            }
        }
    }

    // `data` without the binding's read-only fields.
    inline nlohmann::json withoutReadOnlyFields(const ComponentBinding& binding, const nlohmann::json& data)
    {
        if (!data.is_object() || binding.readOnlyFields.empty())
        {
            return data;
        }
        nlohmann::json writable = data;
        for (const std::string& field : binding.readOnlyFields)
        {
            writable.erase(field);
        }
        return writable;
    }

    // ------------------------------------------------------------------
    // JSON <-> DTO field helpers
    // ------------------------------------------------------------------

    // Assets are exchanged as paths: absolute, or relative to the project working
    // directory. "guid:<hex>" is accepted too (and returned for assets whose
    // file can no longer be resolved). "" clears the reference.
    inline nlohmann::json assetToJson(const asset::AssetRef& ref)
    {
        if (!ref.isValid())
        {
            return "";
        }
        const std::string& path = ref.resolve();
        return path.empty() ? "guid:" + ref.toHexString() : path;
    }

    inline asset::AssetRef assetFromJson(const nlohmann::json& value, const char* key)
    {
        if (value.is_null())
        {
            return asset::AssetRef::invalid();
        }
        if (!value.is_string())
        {
            throw ArgError(std::string("field '") + key + "' must be an asset path string");
        }
        const std::string text = value.get<std::string>();
        if (text.empty())
        {
            return asset::AssetRef::invalid();
        }

        static constexpr std::string_view guidPrefix = "guid:";
        asset::AssetRef ref = text.starts_with(guidPrefix)
            ? asset::AssetRef::fromHexString(text.substr(guidPrefix.size()))
            : asset::AssetRef::fromPath(text);
        if (!ref.isValid())
        {
            throw ArgError(std::string("field '") + key + "': asset not found or not an engine asset: " + text +
                           " (use assets_list to find imported assets)");
        }
        return ref;
    }

    template <typename T>
    void readField(const nlohmann::json& object, const char* key, T& out)
    {
        auto it = object.find(key);
        if (it == object.end())
        {
            return;
        }
        const nlohmann::json& value = *it;

        if constexpr (std::is_same_v<T, bool>)
        {
            if (!value.is_boolean())
            {
                throw ArgError(std::string("field '") + key + "' must be a boolean");
            }
            out = value.get<bool>();
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (!value.is_number())
            {
                throw ArgError(std::string("field '") + key + "' must be a number");
            }
            out = value.get<float>();
        }
        else if constexpr (std::is_integral_v<T>)
        {
            int64_t number = 0;
            if (value.is_number_integer())
            {
                number = value.get<int64_t>();
            }
            else if (value.is_number_float() &&
                     value.get<double>() == static_cast<double>(static_cast<int64_t>(value.get<double>())))
            {
                number = static_cast<int64_t>(value.get<double>());
            }
            else
            {
                throw ArgError(std::string("field '") + key + "' must be an integer");
            }
            if (number < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
                number > static_cast<int64_t>(std::numeric_limits<T>::max()))
            {
                throw ArgError(std::string("field '") + key + "' is out of range [" +
                               std::to_string(static_cast<int64_t>(std::numeric_limits<T>::min())) + ", " +
                               std::to_string(static_cast<int64_t>(std::numeric_limits<T>::max())) + "]");
            }
            out = static_cast<T>(number);
        }
        else if constexpr (std::is_same_v<T, glm::vec3>)
        {
            out = ArgReader::toVec3(value, key);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!value.is_string())
            {
                throw ArgError(std::string("field '") + key + "' must be a string");
            }
            out = value.get<std::string>();
        }
        else if constexpr (std::is_same_v<T, asset::AssetRef>)
        {
            out = assetFromJson(value, key);
        }
        else if constexpr (std::is_same_v<T, std::vector<asset::AssetRef>>)
        {
            if (!value.is_array())
            {
                throw ArgError(std::string("field '") + key + "' must be an array of asset paths");
            }
            std::vector<asset::AssetRef> refs;
            refs.reserve(value.size());
            for (const nlohmann::json& item : value)
            {
                refs.push_back(assetFromJson(item, key));
            }
            out = std::move(refs);
        }
        else
        {
            static_assert(!sizeof(T*), "unsupported component field type");
        }
    }

    // ------------------------------------------------------------------
    // Bindings defined outside ComponentTools.cpp
    // ------------------------------------------------------------------

    // VK-1656 (AnimatorBinding.cpp): "Animator", a view over the Mesh component's animator fields.
    // Building it dispatches nothing.
    ComponentBinding makeAnimatorBinding();

    // VK-1657 (VFXBinding.cpp): "VFX", an effect inside the project plus its per-entity parameters.
    // Building it dispatches nothing.
    ComponentBinding makeVFXBinding();
}
