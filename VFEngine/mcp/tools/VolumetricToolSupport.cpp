#include "VolumetricToolSupport.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "navigation/volumetric/VolumetricTypes.hpp"
#include "navigation/volumetric/VolumetricVoxelizer.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>

namespace mcp::tools::volumetrictool
{
    namespace
    {
        using VolumeData = services::VolumetricNavVolumeData;
        using AgentData = services::VolumetricAgentData;

        // The ranges an agent may set. Each lies inside what the engine accepts (VolumetricNavServiceImpl:
        // halfExtents and voxelSize positive, agentClearance not negative; radius positive, the rest not
        // negative), so a value set here always bakes and flies.
        constexpr double maxCenterOffset = 10000.0; // metres
        constexpr double minHalfExtent = 0.5;
        constexpr double maxHalfExtent = 5000.0;
        constexpr double minVoxelSize = 0.1;
        constexpr double maxVoxelSize = 10.0;
        constexpr double maxClearance = 10.0;

        constexpr double minRadius = 0.1;
        constexpr double maxRadius = 5.0;
        constexpr double maxAgentSpeed = 50.0;         // m/s
        constexpr double maxAgentAcceleration = 100.0; // m/s^2
        constexpr double maxStoppingDistance = 10.0;   // metres
        constexpr double maxTurnSpeed = 1440.0;        // degrees per second

        std::string numberText(float value)
        {
            return env::floatJson(value).dump();
        }

        std::string connectivityError(const VolumeData& value)
        {
            if (value.connectivity == 6 || value.connectivity == 26)
            {
                return {};
            }
            return "connectivity must be 6 (steps through voxel faces only) or 26 (also through edges and corners) "
                   "(got " + std::to_string(value.connectivity) + ")";
        }

        env::SectionTable<VolumeData> makeVolumeTable()
        {
            env::SectionTable<VolumeData> table;
            table.name = "VolumetricNavVolume";
            table.fields = {
                env::vec3Field<VolumeData, &VolumeData::centerOffset>(
                    "centerOffset", -maxCenterOffset, maxCenterOffset,
                    "Box centre relative to the entity's world position, metres (+/-10000)."),
                env::vec3Field<VolumeData, &VolumeData::halfExtents>(
                    "halfExtents", minHalfExtent, maxHalfExtent, "Half the box size per axis, metres (0.5-5000)."),
                env::floatField<VolumeData, &VolumeData::voxelSize>(
                    "voxelSize", minVoxelSize, maxVoxelSize,
                    "Voxel edge, metres (0.1-10): smaller is finer and slower; a grid holds at most 16777216 voxels."),
                env::intField<VolumeData, &VolumeData::connectivity>(
                    "connectivity", 6.0, 26.0, "6 = steps through voxel faces only, 26 = also through edges and corners."),
                env::floatField<VolumeData, &VolumeData::agentClearance>(
                    "agentClearance", 0.0, maxClearance,
                    "Free space kept around geometry, metres (0-10): at least the largest radius of the agents that fly here."),
                env::boolField<VolumeData, &VolumeData::includeTerrain>(
                    "includeTerrain", "The terrain (and everything below its surface) is solid."),
                env::boolField<VolumeData, &VolumeData::includeStaticMeshes>(
                    "includeStaticMeshes", "Meshes and navigation-contributing foliage are solid."),
                env::boolField<VolumeData, &VolumeData::includeColliders>(
                    "includeColliders", "Collider shapes are solid.")
            };
            table.crossCheck = &connectivityError;
            return table;
        }

        env::SectionTable<AgentData> makeAgentTable()
        {
            env::SectionTable<AgentData> table;
            table.name = "VolumetricAgent";
            table.fields = {
                env::floatField<AgentData, &AgentData::radius>(
                    "radius", minRadius, maxRadius,
                    "Body radius, metres (0.1-5): keep it <= the agentClearance of the volumes it flies in."),
                env::floatField<AgentData, &AgentData::maxSpeed>("maxSpeed", 0.0, maxAgentSpeed, "Top speed, m/s (0-50)."),
                env::floatField<AgentData, &AgentData::maxAcceleration>(
                    "maxAcceleration", 0.0, maxAgentAcceleration, "Acceleration, m/s^2 (0-100)."),
                env::floatField<AgentData, &AgentData::stoppingDistance>(
                    "stoppingDistance", 0.0, maxStoppingDistance,
                    "It arrives within this 3D distance of its destination, metres (0-10)."),
                env::floatField<AgentData, &AgentData::turnSpeed>(
                    "turnSpeed", 0.0, maxTurnSpeed,
                    "Degrees per second it turns toward the travel direction (0-1440, 0 = instantly)."),
                env::boolField<AgentData, &AgentData::alignPitch>(
                    "alignPitch", "Also pitch toward the travel direction (false = yaw only).")
            };
            return table;
        }

        // Every Float / Vec3 bound widened to what a float holds: the engine checks stored values itself.
        template <typename T>
        env::SectionTable<T> withFloatBounds(env::SectionTable<T> table)
        {
            constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
            for (env::FieldDesc<T>& field : table.fields)
            {
                if (field.kind == env::FieldKind::Float || field.kind == env::FieldKind::Vec3)
                {
                    field.min = -limit;
                    field.max = limit;
                }
            }
            return table;
        }

        const services::VolumetricVolumeStatus* unbakedVolumeAt(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                                const glm::vec3& point)
        {
            for (const services::VolumetricVolumeStatus& volume : volumes)
            {
                if (!volume.baked && boxContains(volume, point))
                {
                    return &volume;
                }
            }
            return nullptr;
        }
    }

    // ------------------------------------------------------------------
    // Field tables
    // ------------------------------------------------------------------

    const env::SectionTable<services::VolumetricNavVolumeData>& volumeTable()
    {
        static const env::SectionTable<VolumeData> table = makeVolumeTable();
        return table;
    }

    const env::SectionTable<services::VolumetricAgentData>& agentTable()
    {
        static const env::SectionTable<AgentData> table = makeAgentTable();
        return table;
    }

    const env::SectionTable<services::VolumetricNavVolumeData>& storedVolumeTable()
    {
        static const env::SectionTable<VolumeData> table = withFloatBounds(makeVolumeTable());
        return table;
    }

    const env::SectionTable<services::VolumetricAgentData>& storedAgentTable()
    {
        static const env::SectionTable<AgentData> table = withFloatBounds(makeAgentTable());
        return table;
    }

    // ------------------------------------------------------------------
    // Reported forms
    // ------------------------------------------------------------------

    std::string_view moveStateName(services::VolumetricAgentMoveState state)
    {
        switch (state)
        {
        case services::VolumetricAgentMoveState::Idle:
            return "idle";
        case services::VolumetricAgentMoveState::Moving:
            return "moving";
        case services::VolumetricAgentMoveState::Arrived:
            return "arrived";
        case services::VolumetricAgentMoveState::Failed:
            return "failed";
        }
        return "idle";
    }

    std::string_view bakeStateName(services::VolumetricBakeState state)
    {
        switch (state)
        {
        case services::VolumetricBakeState::Unknown:
            return "none";
        case services::VolumetricBakeState::Running:
            return "running";
        case services::VolumetricBakeState::Succeeded:
            return "succeeded";
        case services::VolumetricBakeState::Failed:
            return "failed";
        case services::VolumetricBakeState::Cancelled:
            return "cancelled";
        }
        return "none";
    }

    nlohmann::json ivec3Json(const glm::ivec3& value)
    {
        return nlohmann::json::array({value.x, value.y, value.z});
    }

    nlohmann::json boundsJson(const glm::vec3& min, const glm::vec3& max)
    {
        nlohmann::json out = nlohmann::json::object();
        out["min"] = env::vec3Json(min);
        out["max"] = env::vec3Json(max);
        return out;
    }

    std::string reportedFile(const std::string& enginePath)
    {
        if (enginePath.empty())
        {
            return {};
        }
        const std::optional<std::filesystem::path> root = tryTerrainProjectRoot();
        return projectRelativeUtf8(root, terrainEnginePath(root, enginePath));
    }

    std::string volumeLabel(const services::VolumetricVolumeStatus& volume)
    {
        return "volume " + std::to_string(toId(volume.entity)) + " ('" + volume.name + "')";
    }

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    std::vector<services::VolumetricVolumeStatus> listVolumes()
    {
        return events::EventDispatcher::instance().query(events::volumetric::ListVolumetricVolumesQuery{});
    }

    std::optional<services::VolumetricVolumeStatus> volumeStatus(const services::EntityHandle& volume)
    {
        events::volumetric::GetVolumetricVolumeStatusQuery query;
        query.volume = volume;
        return events::EventDispatcher::instance().query(query);
    }

    const services::VolumetricVolumeStatus& requireVolume(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                          uint32_t id, const char* argument)
    {
        for (const services::VolumetricVolumeStatus& volume : volumes)
        {
            if (volume.entity.id == static_cast<uint64_t>(id))
            {
                return volume;
            }
        }
        const std::optional<services::EntityData> entity = findEntity(toHandle(id));
        if (!entity.has_value())
        {
            throw std::runtime_error("Entity " + std::to_string(id) + " (argument '" + argument +
                                     "') does not exist; volumetric_get_info lists the volumes");
        }
        throw std::runtime_error("Entity " + std::to_string(id) + " ('" + entity->name + "', argument '" + argument +
                                 "') has no VolumetricNavVolume; volumetric_get_info lists the volumes and "
                                 "component_add VolumetricNavVolume makes one");
    }

    // ------------------------------------------------------------------
    // Geometry and warnings
    // ------------------------------------------------------------------

    bool boxContains(const services::VolumetricVolumeStatus& volume, const glm::vec3& point)
    {
        for (glm::length_t axis = 0; axis < 3; ++axis)
        {
            if (!(point[axis] >= volume.worldMin[axis] && point[axis] <= volume.worldMax[axis]))
            {
                return false;
            }
        }
        return true;
    }

    const services::VolumetricVolumeStatus* bakedVolumeAt(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                          const glm::vec3& point)
    {
        const services::VolumetricVolumeStatus* best = nullptr;
        int64_t bestVoxels = 0;
        for (const services::VolumetricVolumeStatus& volume : volumes)
        {
            if (!volume.baked || !boxContains(volume, point))
            {
                continue;
            }
            const int64_t voxels = static_cast<int64_t>(volume.dims.x) * volume.dims.y * volume.dims.z;
            if (best == nullptr || voxels < bestVoxels || (voxels == bestVoxels && volume.uuid < best->uuid))
            {
                best = &volume;
                bestVoxels = voxels;
            }
        }
        return best;
    }

    const services::VolumetricVolumeStatus* volumeByUuid(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                         uint64_t uuid)
    {
        if (uuid == 0)
        {
            return nullptr; // VolumePathResult: no volume was searched
        }
        for (const services::VolumetricVolumeStatus& volume : volumes)
        {
            if (volume.uuid == uuid)
            {
                return &volume;
            }
        }
        return nullptr;
    }

    std::string voxelCapProblem(const glm::vec3& worldMin, const glm::vec3& worldMax, float voxelSize)
    {
        const int64_t voxels = ::volumetric::voxelCountFor(worldMin, worldMax, voxelSize);
        if (voxels <= ::volumetric::kMaxVoxels)
        {
            return {};
        }
        const glm::ivec3 dims = ::volumetric::gridDimensionsFor(worldMin, worldMax, voxelSize);
        std::string text = std::format("the box needs {}x{}x{} = {} voxels at voxelSize {}, more than the bake limit of {}",
                                       dims.x, dims.y, dims.z, voxels, numberText(voxelSize), ::volumetric::kMaxVoxels);
        // The voxelizer's own hint: the smallest size that fits, rounded up to the centimetre.
        const float minimum = std::ceil(::volumetric::minVoxelSizeFor(worldMin, worldMax) * 100.0f) / 100.0f;
        if (minimum <= static_cast<float>(maxVoxelSize))
        {
            text += std::format(": raise voxelSize to >= {:.2f} or shrink halfExtents", minimum);
        }
        else
        {
            text += ": shrink halfExtents (even the largest voxelSize, " + numberText(static_cast<float>(maxVoxelSize)) +
                    ", is too fine for this box)";
        }
        return text;
    }

    std::vector<std::string> volumeWarnings(const services::VolumetricVolumeStatus& volume)
    {
        std::vector<std::string> warnings;
        const std::string cap = voxelCapProblem(volume.worldMin, volume.worldMax, volume.voxelSize);
        if (!cap.empty())
        {
            warnings.push_back("volumetric_bake refuses this volume: " + cap);
        }
        if (volume.baked && volume.stale)
        {
            warnings.emplace_back("stale: the box or the bake settings changed since the loaded grid was baked, and "
                                  "agents still fly that grid; volumetric_bake it again");
        }
        if (volume.baked && volume.file.empty())
        {
            warnings.emplace_back("the loaded grid was never saved to a .vfVolNav, so it is lost when the scene reloads; "
                                  "volumetric_bake saves it");
        }
        return warnings;
    }

    std::vector<std::string> agentWarnings(const services::EntityData& entity, float radius,
                                           const std::vector<services::VolumetricVolumeStatus>& volumes)
    {
        std::vector<std::string> warnings;
        if (entity.hasComponent(services::ComponentTypeId::NavmeshAgent))
        {
            warnings.emplace_back("the entity also has a NavmeshAgent: one agent kind drives an entity, so "
                                  "component_remove one of them");
        }

        const glm::vec3 position = entity.worldTransform.position;
        const services::VolumetricVolumeStatus* volume = bakedVolumeAt(volumes, position);
        if (volume == nullptr)
        {
            if (volumes.empty())
            {
                warnings.emplace_back("the scene has no VolumetricNavVolume, so it cannot fly: component_add one whose box "
                                      "holds the flight area, then volumetric_bake it");
            }
            else if (const services::VolumetricVolumeStatus* unbaked = unbakedVolumeAt(volumes, position))
            {
                warnings.push_back("outside every baked VolumetricNavVolume, so it cannot fly: " + volumeLabel(*unbaked) +
                                   " holds it but is not baked (volumetric_bake it)");
            }
            else
            {
                warnings.emplace_back("outside every baked VolumetricNavVolume, so it cannot fly: move it into a baked "
                                      "volume's box, or grow or add a volume (volumetric_get_info lists them)");
            }
        }
        else if (radius > volume->agentClearance)
        {
            warnings.push_back("radius " + numberText(radius) + " is larger than the agentClearance " +
                               numberText(volume->agentClearance) + " of " + volumeLabel(*volume) +
                               ": its paths keep only that much space from geometry, so the agent can clip obstacles; "
                               "raise the volume's agentClearance to >= " + numberText(radius) +
                               " (component_set VolumetricNavVolume) and volumetric_bake it again");
        }
        return warnings;
    }
}
