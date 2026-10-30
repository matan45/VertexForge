#include "ComponentBinding.hpp"
#include "NavigationToolSupport.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"

#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// VK-1660: the "NavmeshAgent" component binding (NavAgent in scripts). Its writable fields are navtool::agentTable()
// over services::NavmeshAgentData, with the inspector's ranges; a value get() reported passes back unchanged even
// outside its range (a scene file or a script stored it), and an undo restores whatever the engine stored
// (restoreAgent: the engine's own rules). SetNavmeshAgentDataCommand also pushes the shape, speed and avoidance to
// an agent already in the crowd, so an edit in Play mode applies at once; play_stop restores the saved values.
namespace mcp::tools
{
    namespace
    {
        constexpr std::array<std::string_view, 2> readOnlyKeys{"inCrowd", "warnings"};

        constexpr const char* readOnlyHint =
            "inCrowd (the agent is registered with the crowd: it got a destination in Play mode) and warnings are "
            "reported, never set. The writable fields are radius, height, baseOffset, maxSpeed, maxAcceleration, "
            "turnSpeed, stoppingDistance, avoidanceQuality, rootMotionDriven and rootMotionSpeedScale";

        std::string idText(const services::EntityHandle& entity)
        {
            return std::to_string(toId(entity));
        }

        std::optional<services::NavmeshAgentData> agentData(const services::EntityHandle& entity)
        {
            events::scene::GetNavmeshAgentDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        nlohmann::json agentJson(const services::EntityHandle& entity, const services::NavmeshAgentData& data)
        {
            nlohmann::json out = env::toJson(navtool::agentTable(), data);
            out["inCrowd"] = data.inCrowd;
            const std::optional<services::EntityData> owner = findEntity(entity);
            out["warnings"] = owner.has_value() ? navtool::agentWarnings(*owner, data) : std::vector<std::string>{};
            return out;
        }

        std::optional<nlohmann::json> getAgent(const services::EntityHandle& entity)
        {
            const std::optional<services::NavmeshAgentData> data = agentData(entity);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            return agentJson(entity, *data);
        }

        // One agent kind per entity: MoveTo and FollowRoute fly an entity only while it has no NavmeshAgent.
        bool addAgent(const services::EntityHandle& entity)
        {
            const std::optional<services::EntityData> owner = findEntity(entity);
            if (owner.has_value() && owner->hasComponent(services::ComponentTypeId::VolumetricAgent))
            {
                return false;
            }
            events::scene::AddNavmeshAgentComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        bool removeAgent(const services::EntityHandle& entity)
        {
            events::scene::RemoveNavmeshAgentComponentCommand command;
            command.entity = entity;
            return events::EventDispatcher::instance().execute(command);
        }

        // Who wrote the data writeAgent applies: the agent (component_set / component_add), or an undo replaying a
        // get() snapshot of what the engine stored (ComponentBinding::restore).
        enum class Source
        {
            Agent,
            Snapshot
        };

        // A get() snapshot written back by an undo: any finite value of the field's kind, not the tool's range (a
        // scene file may hold radius 7). SetNavmeshAgentDataCommand still refuses what the crowd cannot use.
        services::NavmeshAgentData restoredData(const services::NavmeshAgentData& current, const nlohmann::json& snapshot)
        {
            const env::SectionTable<services::NavmeshAgentData>& table = navtool::agentTable();
            services::NavmeshAgentData data = current;
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it)
            {
                const env::FieldDesc<services::NavmeshAgentData>* field = env::detail::findField(table, it.key());
                if (field == nullptr)
                {
                    throw ArgError("unknown NavmeshAgent field '" + it.key() + "'; valid fields: " +
                                   env::detail::settableKeys(table));
                }
                const double bound = field->kind == env::FieldKind::Int
                    ? static_cast<double>(std::numeric_limits<int>::max())
                    : static_cast<double>(std::numeric_limits<float>::max());
                field->set(data, env::detail::canonicalValue(field->kind, -bound, bound, field->enumNames, it.value(),
                                                             env::detail::pathOf(table.name, field->key)));
            }
            return data;
        }

        // Everything is validated first; then ONE SetNavmeshAgentDataCommand writes the result.
        std::optional<nlohmann::json> writeAgent(const services::EntityHandle& entity, const nlohmann::json& patch,
                                                 Source source)
        {
            if (!patch.is_object())
            {
                throw ArgError("the NavmeshAgent data must be an object of field -> value");
            }
            for (std::string_view key : readOnlyKeys)
            {
                if (patch.contains(std::string(key)))
                {
                    throw ArgError("field '" + std::string(key) + "' of NavmeshAgent is read-only. " + readOnlyHint);
                }
            }
            const std::optional<services::NavmeshAgentData> current = agentData(entity);
            if (!current.has_value())
            {
                return std::nullopt;
            }

            const env::SectionTable<services::NavmeshAgentData>& table = navtool::agentTable();
            const services::NavmeshAgentData data = source == Source::Agent
                ? env::strictPatch(table, *current, env::dropUnchanged(table, *current, patch, table.name), table.name)
                : restoredData(*current, patch);

            events::scene::SetNavmeshAgentDataCommand command;
            command.entity = entity;
            command.data = data;
            if (!events::EventDispatcher::instance().execute(command))
            {
                throw std::runtime_error("The engine rejected the NavmeshAgent data of entity " + idText(entity) +
                                         " (radius and height must be above 0; maxSpeed, maxAcceleration, turnSpeed, "
                                         "stoppingDistance and rootMotionSpeedScale at least 0; avoidanceQuality "
                                         "0-3)");
            }
            std::optional<nlohmann::json> written = getAgent(entity);
            if (!written.has_value())
            {
                throw std::runtime_error("Entity " + idText(entity) + " lost its NavmeshAgent component");
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

    ComponentBinding makeNavmeshAgentBinding()
    {
        ComponentBinding binding;
        binding.name = "NavmeshAgent";
        binding.fieldHelp =
            "the entity walks the navmesh with crowd steering and avoidance (NavAgent in scripts: "
            "this.gameObject().navAgent(); a behavior tree moves it with MoveTo / FollowRoute). radius (0.1-5 m) and "
            "height (0.5-10 m) are its shape: keep them at most the navmesh_bake agentRadius / agentHeight. "
            "baseOffset (-5 to 5 m) is the height of its pivot above the ground the crowd walks: 1 for a 2 m capsule "
            "centred on its pivot, 0 for a mesh with its pivot at the feet (a wrong value sinks or floats it). "
            "maxSpeed (0-50 m/s), maxAcceleration (0-100 m/s^2), turnSpeed (0-1440 deg/s, 0 = faces its travel "
            "direction at once), stoppingDistance (0-5 m: it stops this close to its destination), avoidanceQuality "
            "(0-3, 3 = best and costliest), rootMotionDriven (bool: the animation's root motion sets its speed; needs "
            "an Animator with applyRootMotion), rootMotionSpeedScale (0-8). Read-only: inCrowd (registered with the "
            "crowd: it got a destination in Play mode) and warnings (a parented agent, a shape larger than the bake, "
            "missing root motion, feet off the navmesh, no navmesh). Keep agents at the scene root. An entity has one "
            "agent kind: not both a NavmeshAgent and a VolumetricAgent. In Play mode an edit applies at once and "
            "play_stop restores the saved values. vf://docs/navigation has the details.";
        binding.add = &addAgent;
        binding.remove = &removeAgent;
        binding.get = &getAgent;
        binding.patch = &patchAgent;
        binding.restore = &restoreAgent;
        binding.readOnlyFields = {std::string(readOnlyKeys[0]), std::string(readOnlyKeys[1])};
        binding.readOnlyHint = readOnlyHint;
        binding.addRequirement =
            "the entity must not have a VolumetricAgent (a flying agent): an entity has one agent kind, so "
            "component_remove the VolumetricAgent first";
        return binding;
    }
}
