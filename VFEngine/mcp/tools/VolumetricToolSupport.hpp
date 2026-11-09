#pragma once

#include "SettingsTable.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "data/VolumetricNavData.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1660 helpers shared by the volumetric tools (VolumetricTools.cpp) and the VolumetricNavVolume /
// VolumetricAgent component bindings (VolumetricBindings.cpp): the field tables (the single source of the
// bindings' writable keys and ranges), the names and paths the tools report, and the warnings both show. A
// volume is the axis-aligned box around its entity's WORLD position: centre = position + centerOffset, half
// size = halfExtents (the entity's rotation and scale do not affect it). Flying agents path through the baked
// volumes; where several hold a point, the one with the fewest voxels.
//
// Every function that dispatches runs on the main thread; the others say "Any thread".
namespace mcp::tools::volumetrictool
{
    // ------------------------------------------------------------------
    // Field tables
    // ------------------------------------------------------------------

    // Any thread. The writable fields of the bindings with the ranges an agent may set (component_add /
    // component_set). Keys = the scene serializer's keys; connectivity is 6 or 26 (the tables' crossCheck).
    const env::SectionTable<services::VolumetricNavVolumeData>& volumeTable();
    const env::SectionTable<services::VolumetricAgentData>& agentTable();

    // Any thread. The same fields bounded only by what a float holds, for an undo replaying values the engine
    // stored (ComponentBinding::restore): the engine's Set*Data commands check those against its own rules.
    const env::SectionTable<services::VolumetricNavVolumeData>& storedVolumeTable();
    const env::SectionTable<services::VolumetricAgentData>& storedAgentTable();

    // ------------------------------------------------------------------
    // Reported forms
    // ------------------------------------------------------------------

    // Any thread. "idle" | "moving" | "arrived" | "failed".
    std::string_view moveStateName(services::VolumetricAgentMoveState state);

    // Any thread. "none" (no bake this session) | "running" | "succeeded" | "failed" | "cancelled".
    std::string_view bakeStateName(services::VolumetricBakeState state);

    // Any thread. [x, y, z] of integers.
    nlohmann::json ivec3Json(const glm::ivec3& value);

    // Any thread. {min: [x, y, z], max: [x, y, z]}.
    nlohmann::json boundsJson(const glm::vec3& min, const glm::vec3& max);

    // An engine file path (narrow, as every engine path) as the tools report files: relative to the project's
    // asset root with '/' separators when it lies inside it, else the whole path; "" stays "".
    std::string reportedFile(const std::string& enginePath);

    // Any thread. "volume <id> ('<name>')".
    std::string volumeLabel(const services::VolumetricVolumeStatus& volume);

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    // ListVolumetricVolumesQuery: every VolumetricNavVolume of the scene.
    std::vector<services::VolumetricVolumeStatus> listVolumes();

    // GetVolumetricVolumeStatusQuery; nullopt when the entity has no VolumetricNavVolume.
    std::optional<services::VolumetricVolumeStatus> volumeStatus(const services::EntityHandle& volume);

    // The volume of `volumes` on entity `id`. Throws std::runtime_error naming volumetric_get_info when the
    // entity does not exist or has no VolumetricNavVolume; `argument` is the tool argument that named it.
    const services::VolumetricVolumeStatus& requireVolume(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                          uint32_t id, const char* argument);

    // ------------------------------------------------------------------
    // Geometry and warnings
    // ------------------------------------------------------------------

    // Any thread. `point` lies in the volume's box now (bounds inclusive).
    bool boxContains(const services::VolumetricVolumeStatus& volume, const glm::vec3& point);

    // Any thread. The baked volume a flying agent at `point` paths through, picked as the engine picks it (the
    // fewest voxels, then the lowest UUID) but by the volumes' boxes now; nullptr when no baked box holds it.
    const services::VolumetricVolumeStatus* bakedVolumeAt(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                          const glm::vec3& point);

    // Any thread. The volume whose entity UUID is `uuid` (a VolumePathResult::volumeId); nullptr for none.
    const services::VolumetricVolumeStatus* volumeByUuid(const std::vector<services::VolumetricVolumeStatus>& volumes,
                                                         uint64_t uuid);

    // Any thread. "" when a box baked at `voxelSize` fits under ::volumetric::kMaxVoxels, else how many voxels
    // it needs and the smallest voxelSize that fits (or that only a smaller box does).
    std::string voxelCapProblem(const glm::vec3& worldMin, const glm::vec3& worldMax, float voxelSize);

    // Any thread. What a volume's state asks of the agent: the voxel limit, a stale grid, a grid never saved.
    std::vector<std::string> volumeWarnings(const services::VolumetricVolumeStatus& volume);

    // Any thread. What keeps the VolumetricAgent on `entity` (body radius `radius`) from flying well: a
    // NavmeshAgent next to it, no baked volume holding its world position, a radius above that volume's
    // agentClearance.
    std::vector<std::string> agentWarnings(const services::EntityData& entity, float radius,
                                           const std::vector<services::VolumetricVolumeStatus>& volumes);
}
