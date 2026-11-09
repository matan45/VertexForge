#include "ComponentBinding.hpp"
#include "AssetFieldSupport.hpp"
#include "PathSandbox.hpp"
#include "ToolHelpers.hpp"
#include "VFXToolSupport.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentMediaEvents.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

// VK-1657: the "VFX" component binding. A VFX component plays one .vfVFX effect (one emitter and its node
// graph) at its entity, with per-entity parameters (VFXData::overrides) that are saved with the scene and
// applied to every instance the component spawns. An effect the agent assigns must lie inside the project (an
// exported game carries the project's assets only) and its parameters within the tools' ranges; an undo
// restores whatever the engine stored (restoreVFX). The parameters' keys, ranges and JSON live in VFXToolSupport.
namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view effectExtension = ".vfVFX";

        constexpr std::array<std::string_view, 3> componentFields{"vfxRef", "autoPlay", "loop"};

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }

        std::optional<services::VFXData> vfxData(const services::EntityHandle& entity)
        {
            events::scene::GetVFXDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        std::string validFields()
        {
            std::string fields;
            for (std::string_view field : componentFields)
            {
                fields += fields.empty() ? "" : ", ";
                fields += field;
            }
            return fields + ", " + vfxtool::paramKeyList(true);
        }

        bool isComponentField(std::string_view key)
        {
            for (std::string_view field : componentFields)
            {
                if (field == key)
                {
                    return true;
                }
            }
            return false;
        }

        // Unknown keys are refused before anything is read or written.
        void checkFields(const nlohmann::json& patch)
        {
            if (!patch.is_object())
            {
                throw ArgError("the VFX data must be an object of field -> value");
            }
            for (auto it = patch.begin(); it != patch.end(); ++it)
            {
                if (!isComponentField(it.key()) && vfxtool::findParam(it.key()) == nullptr &&
                    it.key() != vfxtool::otherParamsKey)
                {
                    throw ArgError("unknown VFX field '" + it.key() + "'; valid fields: " + validFields());
                }
            }
        }

        // True when the agent's value names a .vfVFXSequence: a path, or a GUID registered for one.
        bool namesCombo(const nlohmann::json& value)
        {
            if (!value.is_string())
            {
                return false;
            }
            const std::string& text = value.get_ref<const std::string&>();
            static constexpr std::string_view guidPrefix = "guid:";
            if (text.starts_with(guidPrefix))
            {
                const asset::AssetRef ref = asset::AssetRef::fromHexString(text.substr(guidPrefix.size()));
                return ref.isValid() && vfxtool::isComboPath(pathToUtf8(fs::path(ref.resolve())));
            }
            return vfxtool::isComboPath(text);
        }

        // Who wrote the data writeVFX applies: the agent (component_set / component_add / the vfx tools), or an
        // undo replaying a get() snapshot of what the engine stored (ComponentBinding::restore).
        enum class Source
        {
            Agent,
            Snapshot
        };

        // vfxRef: from the agent a .vfVFX inside the project (AssetScope::InsideProject); a snapshot may also
        // hold one outside it (assigned in the inspector). The value the field reports now is accepted
        // unchanged either way.
        asset::AssetRef vfxRefFromJson(const nlohmann::json& value, const asset::AssetRef& current, Source source)
        {
            if (value == assetFieldJson(current, AssetScope::InsideProject))
            {
                return current;
            }
            if (source == Source::Snapshot)
            {
                return assetFieldFromJson(value, "vfxRef", effectExtension, AssetScope::Anywhere);
            }
            if (namesCombo(value))
            {
                throw ArgError("field 'vfxRef': '" + value.get<std::string>() + "' is a combo (.vfVFXSequence): combos are "
                               "listed by vfx_list_assets but cannot be attached in this version; attach one of its "
                               ".vfVFX effects instead");
            }
            return assetFieldFromJson(value, "vfxRef", effectExtension, AssetScope::InsideProject);
        }

        std::optional<nlohmann::json> getVFX(const services::EntityHandle& entity)
        {
            const std::optional<services::VFXData> data = vfxData(entity);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            nlohmann::json out{
                {"vfxRef", assetFieldJson(data->vfxRef, AssetScope::InsideProject)},
                {"autoPlay", data->autoPlay},
                {"loop", data->loop}
            };
            vfxtool::writeParams(out, data->overrides);
            return out;
        }

        bool addVFX(const services::EntityHandle& entity)
        {
            events::scene::AddVFXComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        bool removeVFX(const services::EntityHandle& entity)
        {
            events::scene::RemoveVFXComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        // Everything is validated first; then ONE SetVFXDataCommand writes the result.
        std::optional<nlohmann::json> writeVFX(const services::EntityHandle& entity, const nlohmann::json& patch,
                                               Source source)
        {
            checkFields(patch);
            const std::optional<services::VFXData> current = vfxData(entity);
            if (!current.has_value())
            {
                return std::nullopt;
            }

            services::VFXData data = *current;
            if (const auto it = patch.find("vfxRef"); it != patch.end())
            {
                data.vfxRef = vfxRefFromJson(*it, current->vfxRef, source);
            }
            readField(patch, "autoPlay", data.autoPlay);
            readField(patch, "loop", data.loop);

            nlohmann::json params = nlohmann::json::object();
            for (auto it = patch.begin(); it != patch.end(); ++it)
            {
                if (!isComponentField(it.key()))
                {
                    params[it.key()] = it.value();
                }
            }
            if (!params.empty())
            {
                data.overrides = vfxtool::patchOverrides(current->overrides, params, "",
                                                         source == Source::Agent ? vfxtool::ValueRules::Tool
                                                                                 : vfxtool::ValueRules::Stored);
            }

            events::scene::SetVFXDataCommand command;
            command.entity = entity;
            command.vfxData = data;
            if (!events::EventDispatcher::instance().execute(command))
            {
                throw std::runtime_error("The engine rejected the VFX data of entity " + idText(entity));
            }
            std::optional<nlohmann::json> written = getVFX(entity);
            if (!written.has_value())
            {
                throw std::runtime_error("Entity " + idText(entity) + " lost its VFX component");
            }
            return written;
        }

        std::optional<nlohmann::json> patchVFX(const services::EntityHandle& entity, const nlohmann::json& patch)
        {
            return writeVFX(entity, patch, Source::Agent);
        }

        std::optional<nlohmann::json> restoreVFX(const services::EntityHandle& entity, const nlohmann::json& snapshot)
        {
            return writeVFX(entity, snapshot, Source::Snapshot);
        }
    }

    ComponentBinding makeVFXBinding()
    {
        ComponentBinding binding;
        binding.name = "VFX";
        binding.fieldHelp =
            "a particle effect played at the entity (its world transform places it). vfxRef (.vfVFX path inside "
            "the project: one emitter and its node graph; vfx_list_assets lists them; a .vfVFXSequence combo cannot "
            "be attached), autoPlay (bool: plays when Play mode starts, else a script's VFX::play starts it), loop "
            "(bool). Per-entity parameters, null = as authored in the .vfVFX (the file is never written): " +
            vfxtool::paramsHelp(true) +
            ". otherParams.startColor is overwritten every frame by the effect's colour-over-lifetime gradient when it "
            "has one: recolour with color instead. The parameters are saved with the scene, prefab or world sector and "
            "apply to the edit preview, Play mode and exported games. vfx_add_emitter adds the component with its "
            "parameters, vfx_set_params changes them (authored vs effective values), vfx_play previews it in edit "
            "mode.";
        binding.add = &addVFX;
        binding.remove = &removeVFX;
        binding.get = &getVFX;
        binding.patch = &patchVFX;
        binding.restore = &restoreVFX;
        return binding;
    }
}
