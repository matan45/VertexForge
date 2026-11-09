#include "ComponentBinding.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "VolumetricToolSupport.hpp"

#include "data/DTOs.hpp"
#include "data/VolumetricNavData.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// VK-1660: the "VolumetricNavVolume" and "VolumetricAgent" component bindings. A volume is the airspace flying
// agents path through, turned into a voxel grid by volumetric_bake; an agent flies through the baked volumes
// instead of walking the navmesh. The writable fields and their ranges are the tables of VolumetricToolSupport: a
// value the agent sets must lie in them, a value the bindings report passes back unchanged even outside them
// (env::dropUnchanged), and an undo restores whatever the engine stored (the stored tables, checked by the
// engine's Set*Data commands). An entity carries one agent kind, so a VolumetricAgent is not added next to a
// NavmeshAgent.
namespace mcp::tools
{
    namespace
    {
        constexpr std::array<std::string_view, 8> volumeReadOnlyFields{
            "baked", "stale", "dims", "voxels", "navigablePercent", "bakedFile", "worldBounds", "warnings"};
        constexpr std::array<std::string_view, 3> agentReadOnlyFields{"registered", "state", "warnings"};

        constexpr const char* volumeReadOnlyHint =
            "Writable fields: centerOffset, halfExtents, voxelSize, connectivity, agentClearance, includeTerrain, "
            "includeStaticMeshes, includeColliders. The bake fields change with volumetric_bake and volumetric_clear";
        constexpr const char* agentReadOnlyHint =
            "Writable fields: radius, maxSpeed, maxAcceleration, stoppingDistance, turnSpeed, alignPitch. registered "
            "and state are the agent's Play-mode status (volumetric_get_info lists every flying agent)";

        // Who wrote the data a write applies: the agent (component_add / component_set), or an undo replaying a
        // get() snapshot of what the engine stored (ComponentBinding::restore).
        enum class Source
        {
            Agent,
            Snapshot
        };

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }

        // ------------------------------------------------------------------
        // VolumetricNavVolume
        // ------------------------------------------------------------------

        std::optional<services::VolumetricNavVolumeData> volumeData(const services::EntityHandle& entity)
        {
            events::volumetric::GetVolumetricNavVolumeDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        std::optional<nlohmann::json> getVolume(const services::EntityHandle& entity)
        {
            const std::optional<services::VolumetricNavVolumeData> data = volumeData(entity);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            nlohmann::json out = env::toJson(volumetrictool::volumeTable(), *data);
            out["baked"] = data->baked;
            out["stale"] = data->stale;
            out["dims"] = volumetrictool::ivec3Json(data->dims);
            out["voxels"] = data->voxels;
            out["navigablePercent"] = env::floatJson(data->navigablePercent);
            out["bakedFile"] = volumetrictool::reportedFile(data->bakedFile);

            const std::optional<services::VolumetricVolumeStatus> status = volumetrictool::volumeStatus(entity);
            out["worldBounds"] = status.has_value() ? volumetrictool::boundsJson(status->worldMin, status->worldMax)
                                                    : nlohmann::json(nullptr);
            out["warnings"] = status.has_value() ? volumetrictool::volumeWarnings(*status) : std::vector<std::string>{};
            return out;
        }

        bool addVolume(const services::EntityHandle& entity)
        {
            events::scene::AddVolumetricNavVolumeComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        bool removeVolume(const services::EntityHandle& entity)
        {
            events::scene::RemoveVolumetricNavVolumeComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        // Keys already holding their value are dropped first, so a reported value outside a range passes back;
        // everything left is validated, then ONE SetVolumetricNavVolumeDataCommand writes the result (none when
        // nothing changes).
        std::optional<nlohmann::json> writeVolume(const services::EntityHandle& entity, const nlohmann::json& patch,
                                                  Source source)
        {
            const std::optional<services::VolumetricNavVolumeData> current = volumeData(entity);
            if (!current.has_value())
            {
                return std::nullopt;
            }

            const env::SectionTable<services::VolumetricNavVolumeData>& table =
                source == Source::Agent ? volumetrictool::volumeTable() : volumetrictool::storedVolumeTable();
            const nlohmann::json changed = env::dropUnchanged(table, *current, patch, "data");
            if (!changed.is_object() || !changed.empty())
            {
                events::volumetric::SetVolumetricNavVolumeDataCommand command;
                command.entity = entity;
                command.data = env::strictPatch(table, *current, changed, "data");
                if (!events::EventDispatcher::instance().execute(command))
                {
                    throw std::runtime_error("The engine rejected the VolumetricNavVolume data of entity " + idText(entity) +
                                             " (halfExtents and voxelSize must be positive, agentClearance not negative, "
                                             "connectivity 6 or 26)");
                }
            }

            std::optional<nlohmann::json> written = getVolume(entity);
            if (!written.has_value())
            {
                throw std::runtime_error("Entity " + idText(entity) + " lost its VolumetricNavVolume component");
            }
            return written;
        }

        std::optional<nlohmann::json> patchVolume(const services::EntityHandle& entity, const nlohmann::json& patch)
        {
            return writeVolume(entity, patch, Source::Agent);
        }

        std::optional<nlohmann::json> restoreVolume(const services::EntityHandle& entity, const nlohmann::json& snapshot)
        {
            return writeVolume(entity, snapshot, Source::Snapshot);
        }

        // ------------------------------------------------------------------
        // VolumetricAgent
        // ------------------------------------------------------------------

        std::optional<services::VolumetricAgentData> agentData(const services::EntityHandle& entity)
        {
            events::volumetric::GetVolumetricAgentDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        std::optional<nlohmann::json> getAgent(const services::EntityHandle& entity)
        {
            const std::optional<services::VolumetricAgentData> data = agentData(entity);
            if (!data.has_value())
            {
                return std::nullopt;
            }

            events::volumetric::GetVolumetricAgentStateQuery stateQuery;
            stateQuery.entity = entity;
            const std::optional<services::VolumetricAgentState> state =
                events::EventDispatcher::instance().query(stateQuery);
            const services::VolumetricAgentMoveState moveState =
                state.has_value() ? state->state : services::VolumetricAgentMoveState::Idle;

            nlohmann::json out = env::toJson(volumetrictool::agentTable(), *data);
            out["registered"] = data->registered;
            out["state"] = std::string(volumetrictool::moveStateName(moveState));

            std::vector<std::string> warnings;
            if (const std::optional<services::EntityData> owner = findEntity(entity))
            {
                warnings = volumetrictool::agentWarnings(*owner, data->radius, volumetrictool::listVolumes());
            }
            if (moveState == services::VolumetricAgentMoveState::Failed)
            {
                warnings.push_back("its last move failed: " +
                                   (state->failReason.empty() ? std::string("no reason given") : state->failReason));
            }
            out["warnings"] = warnings;
            return out;
        }

        // One agent kind per entity: refused next to a NavmeshAgent (the binding's addRequirement says so).
        bool addAgent(const services::EntityHandle& entity)
        {
            const std::optional<services::EntityData> owner = findEntity(entity);
            if (owner.has_value() && owner->hasComponent(services::ComponentTypeId::NavmeshAgent))
            {
                return false;
            }
            events::scene::AddVolumetricAgentComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        bool removeAgent(const services::EntityHandle& entity)
        {
            events::scene::RemoveVolumetricAgentComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        // As writeVolume, through SetVolumetricAgentDataCommand.
        std::optional<nlohmann::json> writeAgent(const services::EntityHandle& entity, const nlohmann::json& patch,
                                                 Source source)
        {
            const std::optional<services::VolumetricAgentData> current = agentData(entity);
            if (!current.has_value())
            {
                return std::nullopt;
            }

            const env::SectionTable<services::VolumetricAgentData>& table =
                source == Source::Agent ? volumetrictool::agentTable() : volumetrictool::storedAgentTable();
            const nlohmann::json changed = env::dropUnchanged(table, *current, patch, "data");
            if (!changed.is_object() || !changed.empty())
            {
                events::volumetric::SetVolumetricAgentDataCommand command;
                command.entity = entity;
                command.data = env::strictPatch(table, *current, changed, "data");
                if (!events::EventDispatcher::instance().execute(command))
                {
                    throw std::runtime_error("The engine rejected the VolumetricAgent data of entity " + idText(entity) +
                                             " (radius must be positive; maxSpeed, maxAcceleration, stoppingDistance and "
                                             "turnSpeed not negative)");
                }
            }

            std::optional<nlohmann::json> written = getAgent(entity);
            if (!written.has_value())
            {
                throw std::runtime_error("Entity " + idText(entity) + " lost its VolumetricAgent component");
            }
            return written;
        }

        std::optional<nlohmann::json> patchAgent(const services::EntityHandle& entity, const nlohmann::json& patch)
        {
            return writeAgent(entity, patch, Source::Agent);
        }

        std::optional<nlohmann::json> restoreAgent(const services::EntityHandle& entity, const nlohmann::json& snapshot)
        {
            return writeAgent(entity, snapshot, Source::Snapshot);
        }
    }

    ComponentBinding makeVolumetricNavVolumeBinding()
    {
        ComponentBinding binding;
        binding.name = "VolumetricNavVolume";
        binding.fieldHelp =
            "the airspace flying agents (VolumetricAgent) path through, baked into a voxel grid by volumetric_bake. Its "
            "box is axis-aligned around the entity's WORLD position (rotation and scale are ignored): centre = "
            "position + centerOffset ([x,y,z] metres, +/-10000), half size halfExtents ([x,y,z] 0.5-5000). voxelSize "
            "(0.1-10 m: smaller is finer and slower; a grid holds at most 16777216 voxels), connectivity (6 = steps "
            "through voxel faces only, 26 = also through edges and corners), agentClearance (0-10 m of free space kept "
            "around geometry: at least the largest radius of the agents that fly here), includeTerrain / "
            "includeStaticMeshes (meshes and foliage) / includeColliders (bool: what the bake treats as solid). "
            "Read-only: baked (a grid is loaded), stale (the box or settings changed since that bake), dims, voxels, "
            "navigablePercent (0-100), bakedFile (\"\" = never saved), worldBounds {min, max}, warnings (e.g. the "
            "voxel limit). A change takes effect at the next volumetric_bake.";
        binding.add = &addVolume;
        binding.remove = &removeVolume;
        binding.get = &getVolume;
        binding.patch = &patchVolume;
        binding.restore = &restoreVolume;
        for (std::string_view field : volumeReadOnlyFields)
        {
            binding.readOnlyFields.emplace_back(field);
        }
        binding.readOnlyHint = volumeReadOnlyHint;
        return binding;
    }

    ComponentBinding makeVolumetricAgentBinding()
    {
        ComponentBinding binding;
        binding.name = "VolumetricAgent";
        binding.fieldHelp =
            "a flying agent: in Play it paths through the baked VolumetricNavVolumes instead of the navmesh, moved by "
            "a behavior tree's MoveTo / FollowRoute or the VolumetricNav script module. radius (0.1-5 m: keep it <= "
            "the agentClearance of the volume it flies in), maxSpeed (0-50 m/s), maxAcceleration (0-100 m/s^2), "
            "stoppingDistance (0-10 m: it arrives within this 3D distance), turnSpeed (0-1440 degrees/s toward the "
            "travel direction, 0 = instantly), alignPitch (bool: also pitch toward the travel direction, else yaw "
            "only). Read-only: registered (the agent manager drives it), state (idle | moving | arrived | failed), "
            "warnings (e.g. outside every baked volume). An entity has one agent kind: not added next to a "
            "NavmeshAgent.";
        binding.add = &addAgent;
        binding.remove = &removeAgent;
        binding.get = &getAgent;
        binding.patch = &patchAgent;
        binding.restore = &restoreAgent;
        for (std::string_view field : agentReadOnlyFields)
        {
            binding.readOnlyFields.emplace_back(field);
        }
        binding.readOnlyHint = agentReadOnlyHint;
        binding.addRequirement =
            "the entity has a NavmeshAgent, and an entity is driven by one agent kind (NavmeshAgent walks the navmesh, "
            "VolumetricAgent flies through VolumetricNavVolumes): component_remove the NavmeshAgent first";
        return binding;
    }
}
