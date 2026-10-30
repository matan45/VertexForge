#include "CoreTools.hpp"

#include "navigation/volumetric/VolumetricTypes.hpp"

#include <string>
#include <utility>

// VK-1660: vf://docs/navigation, the agent's reference for moving characters: the navmesh (bake, agents, paths),
// patrol routes, volumetric volumes and flying agents, the overlays and a flying agent's behavior tree. Static text
// (Worker affinity); every rule in it follows the engine code it names.
namespace mcp::tools
{
    namespace
    {
        std::string navigationDoc()
        {
            std::string text =
                "# Navigation\n\n"
                "Ground agents walk the navmesh (the NavmeshAgent component); flying agents fly through voxel volumes "
                "(VolumetricNavVolume + VolumetricAgent). A behavior tree moves both: MoveTo goes to or chases a "
                "target, FollowRoute patrols a route. Trees, the node catalogue and the blackboard: vf://docs/bt. "
                "Script APIs: vf://docs/mtype-api.\n\n"
                "## Workflow (ground agents)\n\n"
                "1. `navmesh_bake {agentRadius: 0.4, agentHeight: 2}`: it bakes AND saves the navmesh. Check `polygons` "
                "and `warnings`, then `viewport_screenshot` (the overlay is on).\n"
                "2. `entity_create` at the scene root, then `component_add {entity, type: \"NavmeshAgent\", data: "
                "{radius: 0.4, height: 2, baseOffset: 1}}` (plus a Mesh). `component_get` NavmeshAgent: `warnings` "
                "should be empty.\n"
                "3. `navmesh_find_path {start: <agent id>, end: <target id>}`: `found` should be true.\n"
                "4. `route_create {name: \"Patrol\", points: [[0, 0, 0], [12, 0, 0], [12, 0, 12], [0, 0, 12]]}`, then "
                "`route_get`.\n"
                "5. `bt_create` a tree with MoveTo / FollowRoute (vf://docs/bt), then `bt_assign {entity, tree, "
                "blackboard: {target: <player id>, route: <route id>}}`.\n"
                "6. `scene_save`, `play_start`, `bt_debug_state`, `play_stop`.\n\n";

            text +=
                "## The navmesh\n\n"
                "The walkable surface Recast bakes from the scene's static geometry, in square tiles of tileSize x "
                "cellSize metres (128 x 0.25 = 32 m by default). Agents walk on it and paths are searched on it.\n\n"
                "- Geometry (navmesh_bake include): terrain; staticMeshes = every Mesh entity; colliders = Box, "
                "ConvexMesh and TriangleMesh colliders of entities without a Mesh (Sphere and Capsule colliders are not "
                "rasterized); foliage = nav-contributing foliage types on loaded terrain tiles. NavmeshAgent, "
                "VolumetricAgent and Controller entities, Dynamic rigid bodies and trigger colliders never count, so "
                "nothing that moves is baked under itself. A parented entity bakes at its world transform.\n"
                "- Settings: agentRadius (the walkable area keeps this far from walls), agentHeight (the headroom an "
                "agent needs), maxSlope (degrees), stepHeight (the highest step; it is rounded down to whole cellHeight "
                "cells, so it must be at least cellHeight), cellSize and cellHeight (the voxel size: smaller is more "
                "precise and slower; cellSize about agentRadius / 2). Bake with agentRadius / agentHeight at least the "
                "largest agent's radius / height. Keys not given keep the last bake's values (navmesh_get_info "
                "settings).\n"
                "- bounds {min, max} bakes only the whole tiles the box overlaps (entities whose origin lies within "
                "50 m of them count); on Y the box is a floor and a ceiling. Agents outside those tiles have no "
                "navmesh.\n"
                "- Saving: navmesh_bake writes index.vfNavIndex plus one file per tile into 'directory', else the "
                "folder of the scene's current navmesh, else navmesh/<scene file name> (untitled for a scene "
                "that was never saved), and points the scene root's NavmeshComponent at the index. scene_save keeps "
                "that reference; scene_load and play_stop then load the navmesh from the files, while a new scene or "
                "another scene drops the live one. A bake started in the editor's Navigation window is not on disk "
                "until it is saved there: navmesh_get_info reports asset.unsavedChanges, and scene_save / play_start "
                "warn.\n"
                "- Refusals and failures: navmesh_bake is refused in Play mode, in World mode (a world navmesh bakes "
                "sector by sector in the Navigation window), while a scene load is pending and while another bake "
                "runs; a scene with nothing to rasterize is refused with what counts. A bake that produces no "
                "polygons is an error and saves nothing: the ground is steeper than maxSlope, the space is too tight "
                "for agentRadius / agentHeight, include leaves the ground out, or bounds miss it.\n"
                "- navmesh_get_info describes the navmesh, its settings, the saved index, every agent (with its "
                "warnings) and the routes; navmesh_clear drops the live navmesh and the scene's reference to it (the "
                "files stay: scene_save makes it permanent).\n\n";

            text +=
                "## Agents (NavmeshAgent)\n\n"
                "NavmeshAgent is a component_add / component_set type (NavAgent in scripts: this.gameObject()."
                "navAgent()). Fields: radius, height, baseOffset, maxSpeed, maxAcceleration, turnSpeed (0 = faces its "
                "travel direction at once), stoppingDistance, avoidanceQuality (0-3: the crowd's obstacle-avoidance "
                "preset, 3 = best and the default), rootMotionDriven, rootMotionSpeedScale; inCrowd and warnings are "
                "read-only. component_get reports a warning for each rule below that the agent breaks.\n\n"
                "- Root level: the crowd reads and writes the entity's LOCAL position as a world position, so keep "
                "agents at the scene root. Under a moved, rotated or scaled parent the agent is drawn away from where "
                "it walks.\n"
                "- baseOffset: the crowd moves a point ON the navmesh surface and the transform gets that point plus "
                "baseOffset. Use 1 for a 2 m capsule whose pivot is its centre and 0 for a mesh whose pivot is at its "
                "feet; a wrong value sinks the agent into the ground or floats it (warning: off the navmesh).\n"
                "- Shape: radius at most the bake's agentRadius and height at most its agentHeight; a larger agent "
                "clips walls and walks under ceilings lower than itself. navmesh_bake warns about every agent its "
                "settings are too small for.\n"
                "- rootMotionDriven needs an Animator with applyRootMotion (component_set Animator {applyRootMotion: "
                "true}); without root motion the agent moves at maxSpeed.\n"
                "- Moving: an agent moves only in Play mode, when something gives it a destination: a behavior tree's "
                "MoveTo or FollowRoute, or a script (Navmesh::setDestination / NavAgent.setDestination). It joins the "
                "crowd at its first destination (inCrowd); a destination more than 100 m from the navmesh is dropped. "
                "It stops within stoppingDistance of its destination, and MoveTo stops it on arrival (stopOnArrival).\n"
                "- Play mode: component_set applies a change at once; play_stop restores the saved values.\n\n";

            text +=
                "## Finding a path (navmesh_find_path)\n\n"
                "start and end are [x, y, z] or entity ids (an entity's world position; a NavmeshAgent's feet, its "
                "world position minus baseOffset). Each end snaps to the nearest navmesh point within snapRadius "
                "(default 5 m, at most 20); agentRadius / agentHeight default to the bake's. found = a complete path; "
                "otherwise reason says why:\n\n"
                "- there is no navmesh: run navmesh_bake;\n"
                "- an end is off the navmesh: nothing walkable within snapRadius (move it, raise snapRadius, or bake a "
                "navmesh that covers it);\n"
                "- no path connects them: the two ends lie on parts of the navmesh that do not connect (a wall, a gap, "
                "a step above stepHeight or a slope above maxSlope);\n"
                "- partial: the end cannot be reached; the path stops at the reachable point closest to it.\n\n"
                "An end snapped more than 1 m adds a warning. Each end reports onNavmesh, nearest and offset (metres to "
                "the nearest navmesh point).\n\n";

            text +=
                "## Routes (patrols)\n\n"
                "A route is an entity with a NavRoute component whose ordered children are its markers. route_create "
                "makes one at the origin with an identity transform and markers WP0, WP1, ... (snapToNavmesh, default "
                "true, puts the points on the navmesh); route_set moves, adds or removes markers and renames the "
                "route; route_get reports the markers' world positions and the followers. Both edits are one undo step "
                "each; markers can also be moved one by one (entity_set_transform). scene_save saves routes like any "
                "entity.\n\n"
                "- FollowRoute (a behavior tree task) walks the markers in child order, using their WORLD positions. "
                "Params: routeKey (an Entity key, or an Int key holding an entity id; default \"route\"), mode "
                "(Loop, PingPong or Once), arrivalDistance (1 m), dwell (seconds at each marker), startAtNearest "
                "(true), legTimeout (30 s; 0 = never). Loop and PingPong run until aborted; Once succeeds at the last "
                "marker. It fails when a leg times out, the route has no markers or the entity has no agent.\n"
                "- The markers are read each time the node starts (a moved marker counts from the next start) and "
                "snapped onto the navmesh within 5, 25 or 100 m; a flying agent's markers snap to the nearest "
                "navigable voxel within 2 m.\n"
                "- Giving a route to an entity: bt_assign {entity, tree, blackboard: {route: <route id>}} stores the "
                "route as a start value (its UUID and name, resolved when Play starts); in Play mode bt_set_blackboard "
                "{entity, values: {route: <route id>}, live: true} changes the running tree.\n"
                "- Scripts that find a route by name and read the markers' LOCAL positions agree with FollowRoute only "
                "while the route entity keeps its identity transform (route_get warns otherwise).\n\n";

            text +=
                "## Flying agents (volumetric navigation)\n\n"
                "- Volumes: a VolumetricNavVolume is an axis-aligned box around its entity's WORLD position: centre = "
                "position + centerOffset, size = 2 x halfExtents; the entity's rotation and scale do not change it. "
                "Each volume bakes its own voxel grid; a path must stay inside one volume (the smallest that holds "
                "both ends), so make one volume cover a flyer's whole area.\n"
                "- Voxel size, clearance and the cap: the grid has ceil(2 x halfExtents / voxelSize) voxels per axis, "
                "at most " + std::to_string(::volumetric::kMaxVoxels) + " in all (2^24): halfExtents [30, 15, 30] at "
                "voxelSize 1 is 60 x 30 x 60 = 108000 voxels, at 0.25 it is 6912000. A bigger grid is refused with the "
                "smallest voxelSize that fits. agentClearance is the free space kept around geometry: blocked voxels "
                "grow by ceil(agentClearance / voxelSize) voxels, so a passage stays open only when it is at least "
                "2 x ceil(agentClearance / voxelSize) + 1 voxels wide. Set agentClearance to the largest flyer's radius "
                "and voxelSize at most agentClearance.\n"
                "- Geometry: includeTerrain (everything below the terrain surface is solid), includeStaticMeshes "
                "(meshes and nav-contributing foliage), includeColliders (Box / ConvexMesh / TriangleMesh colliders as "
                "triangles, Sphere / Capsule colliders as solid shapes). Movers never count. Space that geometry "
                "encloses (unreachable from the volume's outer faces) is blocked; the outer faces are not walls.\n"
                "- Bake and persistence: volumetric_bake {volume?} (default: every volume) bakes in the background (the "
                "editor keeps running) and saves each grid to <the folder of the volume's current file, else "
                "volnav/<scene file name>>/<volume name>_<uuid>.vfVolNav, which the volume's bakedRef points "
                "at; run scene_save afterwards to keep that reference. A scene load reloads the grid and play_stop "
                "keeps it. volumetric_bake is refused in Play mode, while a scene load is pending, in World mode, "
                "without a volume, over the voxel cap and while a volume is already baking. volumetric_clear {volume?} "
                "unloads a grid and clears its bakedRef (the file stays on disk; run scene_save afterwards); it is "
                "refused in Play mode and while a scene load is pending. "
                "volumetric_get_info {volume?} reports each volume (worldBounds, dims, navigablePercent 0-100, file, "
                "baked, stale, bake progress, warnings) and each flying agent (registered, state, failReason, inVolume, "
                "warnings); stale = the loaded grid no longer matches the volume's box or settings: bake it again.\n"
                "- Flying agents: an entity with a VolumetricAgent and no NavmeshAgent flies. MoveTo and FollowRoute "
                "route it through the volumes in Play mode: arrival is 3D (its stoppingDistance; MoveTo also accepts "
                "the agent's Arrived state, so keep MoveTo's arrivalDistance at least the agent's stoppingDistance), "
                "and Failed (no path, stuck) fails the task. Create a flyer's route at flying height with route_create "
                "{snapToNavmesh: false}. Range checks: the TargetDistance service measures on the XZ plane unless "
                "horizontal is false; set {horizontal: false} for flyers, so a target far below does not read as close.\n"
                "- volumetric_find_path {start, end (an entity id or [x, y, z]), snapRadius (0-10 m, default 2), show, "
                "showSeconds}: show keeps the path drawn in the viewport for showSeconds (default 10) so "
                "viewport_screenshot can capture it. Its status is Found, Partial (the end was not reached), NoVolume "
                "(nothing baked), OutsideVolume, StartBlocked / EndBlocked (no navigable voxel within snapRadius), "
                "DifferentVolumes or NoPath.\n"
                "- One agent kind per entity: component_add refuses a NavmeshAgent on an entity with a VolumetricAgent, "
                "and the other way round.\n\n";

            text +=
                "### A flying agent's behavior tree\n\n"
                "Patrol a route at altitude; dive at the target when it is within 15 m in 3D (bt_create format: "
                "vf://docs/bt):\n\n"
                "```json\n"
                "{\"name\": \"DroneAI\",\n"
                " \"blackboard\": [{\"name\": \"target\", \"type\": \"Entity\"}, {\"name\": \"route\", \"type\": "
                "\"Entity\"},\n"
                "                {\"name\": \"targetDistance\", \"type\": \"Float\", \"default\": 1000000.0}],\n"
                " \"root\": {\"type\": \"Service\", \"name\": \"Sense Target\",\n"
                "          \"params\": {\"serviceType\": \"TargetDistance\", \"interval\": 0.2, \"runOnActivation\": "
                "true,\n"
                "                     \"targetKey\": \"target\", \"distanceKey\": \"targetDistance\", \"horizontal\": "
                "false},\n"
                "          \"child\": {\"type\": \"Selector\", \"name\": \"Brain\", \"children\": [\n"
                "            {\"type\": \"BlackboardCondition\", \"name\": \"In Dive Range\",\n"
                "             \"params\": {\"key\": \"targetDistance\", \"compareOp\": \"<=\", \"compareValue\": 15.0, "
                "\"abortMode\": \"Both\"},\n"
                "             \"child\": {\"type\": \"MoveTo\", \"name\": \"Dive\",\n"
                "                       \"params\": {\"targetKey\": \"target\", \"arrivalDistance\": 3.0, "
                "\"repathDistance\": 2.0, \"stopOnArrival\": true}}},\n"
                "            {\"type\": \"FollowRoute\", \"name\": \"Patrol\",\n"
                "             \"params\": {\"routeKey\": \"route\", \"mode\": \"Loop\", \"arrivalDistance\": 1.5, "
                "\"dwell\": 0.5,\n"
                "                        \"startAtNearest\": true, \"legTimeout\": 30.0}}]}}}\n"
                "```\n\n"
                "Setup: a VolumetricNavVolume covering the patrol and the target's area, volumetric_bake, a Drone "
                "entity with a Mesh and a VolumetricAgent (no NavmeshAgent; agentClearance at least its radius, its "
                "stoppingDistance at most 3), route_create {snapToNavmesh: false} with the markers at flying height, "
                "then bt_assign {entity, tree, blackboard: {target: <player id>, route: <route id>}}.\n\n";

            text +=
                "## Overlays\n\n"
                "The navmesh overlay (navmesh_bake showDebug, on by default; the editor's Debug > Show Navmesh) draws "
                "the navmesh and, while it is on, the routes (a green sphere on the first marker, magenta spheres and "
                "legs on the rest; the leg from the last marker back to the first is not drawn), the volumetric "
                "volumes (cyan = baked, orange = stale, gray = not baked) and each flying agent's remaining path "
                "(magenta). volumetric_find_path {show: true} keeps one 3D path drawn for showSeconds. "
                "viewport_screenshot shows what the viewport draws.\n\n"
                "## Persistence, undo and modes\n\n"
                "- Saved with the scene (scene_save): the agents, routes, volumes and the behavior trees' start "
                "values; the navmesh and the volume grids live in their own files, which the scene references.\n"
                "- Undo: component_add / component_set / component_remove of NavmeshAgent, VolumetricNavVolume and "
                "VolumetricAgent, route_create and route_set are undo steps; navmesh_bake, navmesh_clear, "
                "volumetric_bake and volumetric_clear are not.\n"
                "- Modes: navmesh_bake, navmesh_clear, route_create and route_set are refused in Play mode and while a "
                "scene load is pending (navmesh_bake and the route edits also in World mode); navmesh_get_info, "
                "navmesh_find_path and route_get work in any mode.\n\n"
                "## Scripts\n\n"
                "Navmesh::findPath(start, end) (waypoints), Navmesh::isPointOnNavmesh, Navmesh::getClosestPoint, "
                "Navmesh::setDestination(entityId, target), Navmesh::stopAgent(entityId) and the NavAgent wrapper "
                "(this.gameObject().navAgent()) drive ground agents; VolumetricNav::findPath3D, "
                "VolumetricNav::setDestination, VolumetricNav::stopAgent and VolumetricNav::getAgentState drive flying "
                "ones. vf://docs/mtype-api has the signatures.\n";
            return text;
        }
    }

    void registerNavigationResources(ResourceRegistry& registry, const ToolContext&)
    {
        ResourceDef resource;
        resource.uri = "vf://docs/navigation";
        resource.name = "navigation";
        resource.title = "Navigation reference";
        resource.description =
            "How characters move: baking and saving the navmesh, NavmeshAgent rules (root level, baseOffset, radius "
            "within the bake), navmesh_find_path's reasons, patrol routes and FollowRoute, volumetric volumes and "
            "flying agents (voxel size, clearance, the voxel cap, persistence), the overlays and a flying agent's "
            "behavior tree.";
        resource.mimeType = "text/markdown";
        resource.affinity = ThreadAffinity::Worker;
        resource.reader = []() -> ResourceContents
        {
            return {"vf://docs/navigation", "text/markdown", navigationDoc()};
        };
        registry.add(std::move(resource));
    }
}
