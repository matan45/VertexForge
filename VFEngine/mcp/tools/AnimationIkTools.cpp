#include "AnimationRigSupport.hpp"
#include "CoreTools.hpp"
#include "EnvironmentToolSupport.hpp"
#include "SettingsTable.hpp"
#include "ToolHelpers.hpp"
#include "../protocol/ArgReader.hpp"
#include "../undo/IkStateUndo.hpp"
#include "../undo/McpUndo.hpp"

#include "events/EventDispatcher.hpp"
#include "events/physics/IKEvents.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// VK-1656 IK tools: author an entity's IK chains (its IKTargetComponent, saved with the scene) and aim
// them at runtime targets. The FABRIK solver runs on top of the animated pose, so IK only moves an entity
// that has an Animator. A chain edit checks every bone against the mesh's skeleton (read fresh from the
// .vfMesh) before anything changes, replaces the whole chain list with ONE SetIKChainsCommand and pushes
// one undo step (IkStateUndo). Targets are runtime-only.
namespace mcp::tools
{
    namespace
    {
        using animator::ik::IKChainConfig;
        using animator::ik::JointConstraint;
        using animator::ik::JointConstraintType;

        // Indexed by JointConstraintType.
        constexpr std::array<std::string_view, 4> constraintTypeNames{"None", "Hinge", "Cone", "BallAndSocket"};

        // The ranges of the editor's constraint sliders (AnimationIKChainPanel, IKDrawer), in degrees.
        constexpr double maxConeDegrees = 180.0;
        constexpr double maxSwingDegrees = 180.0;
        constexpr double minTwistDegrees = -180.0; // twistMin lies in [-180, 0]
        constexpr double maxTwistDegrees = 180.0;  // twistMax lies in [0, 180]

        constexpr int64_t maxChainJoints = 256;
        // Bounds that only keep vectors finite.
        constexpr double maxVectorComponent = 1.0e6;
        constexpr float minAxisLength = 1.0e-6f;

        constexpr const char* bonesHint = "animation_ik_get {entity, includeBones:true} lists the bones";

        // Bone lookup over a skeleton read with GetMeshSkeletonQuery. The first bone with a name wins, as in
        // SkeletonData::getBoneIndex, which the solver resolves chains with.
        class SkeletonBones
        {
        public:
            explicit SkeletonBones(const services::MeshSkeletonInfo& skeleton)
                : names(skeleton.boneNames), parents(skeleton.parents)
            {
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    byName.emplace(names[i], static_cast<int>(i));
                }
            }

            int find(const std::string& name) const
            {
                const auto it = byName.find(name);
                return it == byName.end() ? -1 : it->second;
            }

            // -1 for a root (or a parent index outside the skeleton).
            int parentOf(int bone) const
            {
                if (bone < 0 || static_cast<std::size_t>(bone) >= parents.size())
                {
                    return -1;
                }
                const int32_t parent = parents[static_cast<std::size_t>(bone)];
                return parent >= 0 && static_cast<std::size_t>(parent) < names.size() ? static_cast<int>(parent) : -1;
            }

            const std::string& nameOf(int bone) const
            {
                return names[static_cast<std::size_t>(bone)];
            }

            std::size_t size() const
            {
                return names.size();
            }

        private:
            std::vector<std::string> names;
            std::vector<int32_t> parents;
            std::unordered_map<std::string, int> byName;
        };

        struct SkinnedRig
        {
            services::MeshData mesh;
            std::string label; // the quoted agent path, for messages
            services::MeshSkeletonInfo skeleton;
        };

        std::string entityLabel(const services::EntityData& entity)
        {
            return "Entity " + std::to_string(toId(entity.handle)) + " ('" + entity.name + "')";
        }

        std::string noAnimatorWarning(const services::EntityData& entity)
        {
            return entityLabel(entity) + " has no Animator: IK is applied on top of the animated pose, so it never "
                   "runs without one (add it with component_add {type:\"Animator\", data:{defaultClip}}; "
                   "animation_list_clips lists the clips that fit).";
        }

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
            {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        }

        // Agent input echoed in an error; replace: it may hold invalid UTF-8.
        std::string valueText(const nlohmann::json& value)
        {
            return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        // The value of `key`; null counts as absent, like ArgReader::has.
        const nlohmann::json* member(const nlohmann::json& object, const char* key)
        {
            const auto it = object.find(key);
            return it == object.end() || it->is_null() ? nullptr : &*it;
        }

        std::string requiredString(const nlohmann::json& object, const char* key, const std::string& path)
        {
            const nlohmann::json* value = member(object, key);
            if (value == nullptr)
            {
                throw ArgError("'" + path + "." + key + "' is required");
            }
            return env::requireString(*value, path + "." + key);
        }

        std::string chainNames(const std::vector<IKChainConfig>& chains)
        {
            std::string out;
            for (const IKChainConfig& chain : chains)
            {
                out += (out.empty() ? "" : ", ") + chain.chainName;
            }
            return out.empty() ? std::string("none") : out;
        }

        // The entity's Mesh with a readable skeleton, or std::runtime_error saying what is missing.
        SkinnedRig requireSkinnedRig(const services::EntityData& entity)
        {
            const std::optional<services::MeshData> mesh = rig::meshData(entity.handle);
            if (!mesh.has_value() || !mesh->meshRef.isValid())
            {
                throw std::runtime_error(entityLabel(entity) + " has no mesh: IK chains need a skinned Mesh "
                                         "(component_add {type:\"Mesh\"} with an imported .vfMesh).");
            }
            const std::string path = rig::meshPath(*mesh);
            if (path.empty())
            {
                throw std::runtime_error("The mesh asset of " + entityLabel(entity) + " cannot be resolved (guid:" +
                                         mesh->meshRef.toHexString() + ").");
            }

            SkinnedRig target;
            target.mesh = *mesh;
            target.label = "'" + rig::agentPath(path) + "'";
            target.skeleton = rig::readSkeleton(path);
            if (!target.skeleton.success)
            {
                throw std::runtime_error("Cannot read the skeleton of " + target.label + ": " + target.skeleton.error);
            }
            if (!target.skeleton.skinned || target.skeleton.boneNames.empty())
            {
                throw std::runtime_error(target.label + " (the mesh of " + entityLabel(entity) +
                                         ") has no skeleton; IK needs a skinned mesh.");
            }
            return target;
        }

        // ------------------------------------------------------------------
        // Agent JSON -> chains (every check throws ArgError naming the value)
        // ------------------------------------------------------------------

        JointConstraintType constraintType(const nlohmann::json& value, const std::string& path)
        {
            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                for (std::size_t i = 0; i < constraintTypeNames.size(); ++i)
                {
                    if (equalsIgnoreCase(text, constraintTypeNames[i]))
                    {
                        return static_cast<JointConstraintType>(i);
                    }
                }
            }
            throw ArgError("'" + path + "' must be one of None, Hinge, Cone, BallAndSocket (got " + valueText(value) + ")");
        }

        // Angles arrive in degrees and are stored in radians; omitted values keep JointConstraint's defaults.
        JointConstraint parseConstraint(const nlohmann::json& value, const std::string& path)
        {
            if (!value.is_object())
            {
                throw ArgError("'" + path + "' must be an object such as {\"type\":\"Hinge\",\"hingeAxis\":[1,0,0]} (got " +
                               valueText(value) + ")");
            }
            const nlohmann::json* type = member(value, "type");
            if (type == nullptr)
            {
                throw ArgError("'" + path + ".type' is required: one of None, Hinge, Cone, BallAndSocket");
            }

            JointConstraint constraint;
            constraint.type = constraintType(*type, path + ".type");
            const std::string keysPath = path + " (" + animator::ik::constraintTypeToString(constraint.type) + ")";
            switch (constraint.type)
            {
            case JointConstraintType::None:
                env::rejectUnknownKeys(value, {"type"}, keysPath);
                break;

            case JointConstraintType::Hinge:
                env::rejectUnknownKeys(value, {"type", "hingeAxis"}, keysPath);
                if (const nlohmann::json* axis = member(value, "hingeAxis"))
                {
                    const glm::vec3 raw = env::requireVec3(*axis, -maxVectorComponent, maxVectorComponent,
                                                           path + ".hingeAxis");
                    const float length = glm::length(raw);
                    if (!(length > minAxisLength))
                    {
                        throw ArgError("'" + path + ".hingeAxis' must not be the zero vector");
                    }
                    constraint.hingeAxis = raw / length;
                }
                break;

            case JointConstraintType::Cone:
                env::rejectUnknownKeys(value, {"type", "coneAngle"}, keysPath);
                if (const nlohmann::json* angle = member(value, "coneAngle"))
                {
                    constraint.coneAngle = glm::radians(env::requireFloat(*angle, 0.0, maxConeDegrees, path + ".coneAngle"));
                }
                break;

            case JointConstraintType::BallAndSocket:
                env::rejectUnknownKeys(value, {"type", "swingAngle", "twistMin", "twistMax"}, keysPath);
                if (const nlohmann::json* angle = member(value, "swingAngle"))
                {
                    constraint.swingAngle =
                        glm::radians(env::requireFloat(*angle, 0.0, maxSwingDegrees, path + ".swingAngle"));
                }
                if (const nlohmann::json* angle = member(value, "twistMin"))
                {
                    constraint.twistMin = glm::radians(env::requireFloat(*angle, minTwistDegrees, 0.0, path + ".twistMin"));
                }
                if (const nlohmann::json* angle = member(value, "twistMax"))
                {
                    constraint.twistMax = glm::radians(env::requireFloat(*angle, 0.0, maxTwistDegrees, path + ".twistMax"));
                }
                break;
            }
            return constraint;
        }

        // chainLength: walks the parents up from the tip, tip included, and returns the bones root first
        // (the Animation preview window's IK chain panel builds chains the same way).
        std::vector<std::string> bonesFromLength(const SkeletonBones& bones, int tip, const nlohmann::json& value,
                                                 const std::string& path)
        {
            const int64_t length = env::detail::checkedInteger(value, 2.0, static_cast<double>(maxChainJoints), path);
            std::vector<std::string> chain;
            std::vector<bool> visited(bones.size(), false);
            for (int bone = tip; bone >= 0 && static_cast<int64_t>(chain.size()) < length; bone = bones.parentOf(bone))
            {
                if (visited[static_cast<std::size_t>(bone)])
                {
                    throw std::runtime_error("the parent links of the skeleton form a loop at bone '" + bones.nameOf(bone) + "'");
                }
                visited[static_cast<std::size_t>(bone)] = true;
                chain.push_back(bones.nameOf(bone));
            }
            if (static_cast<int64_t>(chain.size()) < length)
            {
                throw ArgError("'" + path + "' is " + std::to_string(length) + ", but tip bone '" + bones.nameOf(tip) +
                               "' has only " + std::to_string(chain.size()) + " bone(s) up to the root (tip included)");
            }
            std::reverse(chain.begin(), chain.end());
            return chain;
        }

        // chainBones: a parent-linked path, root first, ending with the tip or with its parent. The tip is
        // appended in the second case, so a stored chain always ends with its tip, as the solver resolves it
        // (IKPostProcessor::initializeChainRuntime appends a missing tip).
        std::vector<std::string> bonesFromList(const SkeletonBones& bones, int tip, const nlohmann::json& value,
                                               const std::string& path, const std::string& meshLabel)
        {
            if (!value.is_array() || value.empty())
            {
                throw ArgError("'" + path + "' must be a non-empty array of bone names, root first");
            }
            std::vector<int> chain;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                const std::string itemPath = path + "[" + std::to_string(i) + "]";
                const std::string name = env::requireString(value[i], itemPath);
                const int bone = bones.find(name);
                if (bone < 0)
                {
                    throw ArgError("'" + itemPath + "' names '" + name + "', which is not a bone of " + meshLabel + " (" +
                                   bonesHint + ")");
                }
                if (!chain.empty() && bones.parentOf(bone) != chain.back())
                {
                    throw ArgError("'" + path + "' must be a parent-linked path, root first: '" + name +
                                   "' is not a child of '" + bones.nameOf(chain.back()) + "'");
                }
                chain.push_back(bone);
            }
            if (chain.back() != tip)
            {
                if (bones.parentOf(tip) != chain.back())
                {
                    throw ArgError("'" + path + "' must end with the tip bone '" + bones.nameOf(tip) +
                                   "' or its parent (it ends with '" + bones.nameOf(chain.back()) + "')");
                }
                chain.push_back(tip);
            }
            if (chain.size() < 2)
            {
                throw ArgError("'" + path + "' gives a single joint; an IK chain needs at least 2 joints (the tip and "
                               "at least one bone above it)");
            }

            std::vector<std::string> names;
            names.reserve(chain.size());
            for (int bone : chain)
            {
                names.push_back(bones.nameOf(bone));
            }
            return names;
        }

        IKChainConfig parseChain(const nlohmann::json& value, const std::string& path, const SkeletonBones& bones,
                                 const std::string& meshLabel)
        {
            env::rejectUnknownKeys(value, {"name", "tipBone", "chainBones", "chainLength", "weight", "enabled",
                                           "constraints"}, path);

            IKChainConfig chain;
            chain.chainName = requiredString(value, "name", path);
            if (chain.chainName.empty())
            {
                throw ArgError("'" + path + ".name' must not be empty");
            }

            chain.tipBoneName = requiredString(value, "tipBone", path);
            const int tip = bones.find(chain.tipBoneName);
            if (tip < 0)
            {
                throw ArgError("'" + path + ".tipBone' names '" + chain.tipBoneName + "', which is not a bone of " +
                               meshLabel + " (" + bonesHint + ")");
            }

            const nlohmann::json* explicitBones = member(value, "chainBones");
            const nlohmann::json* length = member(value, "chainLength");
            if ((explicitBones == nullptr) == (length == nullptr))
            {
                throw ArgError("'" + path + "' needs exactly one of 'chainBones' (bone names, root first) or "
                               "'chainLength' (joints counted up from the tip, tip included)");
            }
            chain.chainBoneNames = explicitBones != nullptr
                ? bonesFromList(bones, tip, *explicitBones, path + ".chainBones", meshLabel)
                : bonesFromLength(bones, tip, *length, path + ".chainLength");

            if (const nlohmann::json* weight = member(value, "weight"))
            {
                chain.weight = env::requireFloat(*weight, 0.0, 1.0, path + ".weight");
            }
            if (const nlohmann::json* enabled = member(value, "enabled"))
            {
                chain.enabled = env::requireBool(*enabled, path + ".enabled");
            }

            if (const nlohmann::json* constraints = member(value, "constraints"))
            {
                if (!constraints->is_array())
                {
                    throw ArgError("'" + path + ".constraints' must be an array (one constraint per joint, root first)");
                }
                if (constraints->size() > chain.chainBoneNames.size())
                {
                    std::string joints;
                    for (const std::string& bone : chain.chainBoneNames)
                    {
                        joints += (joints.empty() ? "" : ", ") + bone;
                    }
                    throw ArgError("'" + path + ".constraints' has " + std::to_string(constraints->size()) +
                                   " entries but the chain has " + std::to_string(chain.chainBoneNames.size()) +
                                   " joints (" + joints + "): at most one constraint per joint, root first");
                }
                for (std::size_t i = 0; i < constraints->size(); ++i)
                {
                    chain.constraints.push_back(
                        parseConstraint((*constraints)[i], path + ".constraints[" + std::to_string(i) + "]"));
                }
            }
            return chain;
        }

        bool sameConstraint(const JointConstraint& a, const JointConstraint& b)
        {
            return a.type == b.type && a.hingeAxis == b.hingeAxis && a.coneAngle == b.coneAngle &&
                   a.swingAngle == b.swingAngle && a.twistMin == b.twistMin && a.twistMax == b.twistMax;
        }

        bool sameChain(const IKChainConfig& a, const IKChainConfig& b)
        {
            return a.chainName == b.chainName && a.tipBoneName == b.tipBoneName &&
                   a.chainBoneNames == b.chainBoneNames && a.weight == b.weight && a.enabled == b.enabled &&
                   std::equal(a.constraints.begin(), a.constraints.end(), b.constraints.begin(), b.constraints.end(),
                              sameConstraint);
        }

        bool sameChains(const std::vector<IKChainConfig>& a, const std::vector<IKChainConfig>& b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameChain);
        }

        // ------------------------------------------------------------------
        // Reports
        // ------------------------------------------------------------------

        nlohmann::json constraintJson(const JointConstraint& constraint)
        {
            nlohmann::json out{{"type", animator::ik::constraintTypeToString(constraint.type)}};
            switch (constraint.type)
            {
            case JointConstraintType::Hinge:
                out["hingeAxis"] = env::vec3Json(constraint.hingeAxis);
                break;
            case JointConstraintType::Cone:
                out["coneAngle"] = rig::degreesJson(glm::degrees(constraint.coneAngle));
                break;
            case JointConstraintType::BallAndSocket:
                out["swingAngle"] = rig::degreesJson(glm::degrees(constraint.swingAngle));
                out["twistMin"] = rig::degreesJson(glm::degrees(constraint.twistMin));
                out["twistMax"] = rig::degreesJson(glm::degrees(constraint.twistMax));
                break;
            case JointConstraintType::None:
                break;
            }
            return out;
        }

        // The same shape animation_ik_set takes, so a reported chain can be edited and sent back.
        nlohmann::json chainJson(const IKChainConfig& chain)
        {
            nlohmann::json constraints = nlohmann::json::array();
            for (const JointConstraint& constraint : chain.constraints)
            {
                constraints.push_back(constraintJson(constraint));
            }
            return {
                {"name", chain.chainName},
                {"tipBone", chain.tipBoneName},
                {"chainBones", chain.chainBoneNames},
                {"weight", env::floatJson(chain.weight)},
                {"enabled", chain.enabled},
                {"constraints", std::move(constraints)}
            };
        }

        nlohmann::json chainsJson(const std::vector<IKChainConfig>& chains)
        {
            nlohmann::json out = nlohmann::json::array();
            for (const IKChainConfig& chain : chains)
            {
                out.push_back(chainJson(chain));
            }
            return out;
        }

        nlohmann::json bonesJson(const SkeletonBones& bones)
        {
            nlohmann::json out = nlohmann::json::array();
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                const int parent = bones.parentOf(static_cast<int>(i));
                out.push_back({
                    {"name", bones.nameOf(static_cast<int>(i))},
                    {"parent", parent >= 0 ? nlohmann::json(bones.nameOf(parent)) : nlohmann::json(nullptr)}
                });
            }
            return out;
        }

        // What the solver makes of each chain (IKPostProcessor::initializeChainRuntime / applyIK): a bone the
        // skeleton lacks skips the chain and logs a warning every frame; fewer than 2 joints are never solved.
        void addChainWarnings(const std::vector<IKChainConfig>& chains, const SkeletonBones& bones,
                              const std::string& meshLabel, nlohmann::json& warnings)
        {
            for (const IKChainConfig& chain : chains)
            {
                const std::string label = "IK chain '" + chain.chainName + "'";
                bool missing = false;
                int previous = -1;
                std::size_t joints = 0;
                for (const std::string& name : chain.chainBoneNames)
                {
                    const int bone = bones.find(name);
                    if (bone < 0)
                    {
                        warnings.push_back(label + ": bone '" + name + "' is not in the skeleton of " + meshLabel +
                                           "; the chain is skipped and logs a warning every frame");
                        missing = true;
                        continue;
                    }
                    if (previous >= 0 && bones.parentOf(bone) != previous)
                    {
                        warnings.push_back(label + ": '" + name + "' is not a child of '" + bones.nameOf(previous) +
                                           "' (chain bones should form a parent-linked path, root first)");
                    }
                    previous = bone;
                    ++joints;
                }

                const int tip = bones.find(chain.tipBoneName);
                if (tip < 0)
                {
                    warnings.push_back(label + ": tip bone '" + chain.tipBoneName + "' is not in the skeleton of " +
                                       meshLabel + "; the chain is skipped and logs a warning every frame");
                    continue;
                }
                if (missing)
                {
                    continue;
                }
                if (previous != tip)
                {
                    if (previous >= 0 && bones.parentOf(tip) != previous)
                    {
                        warnings.push_back(label + ": tip bone '" + chain.tipBoneName + "' is not a child of '" +
                                           bones.nameOf(previous) + "'");
                    }
                    ++joints; // the solver appends the tip
                }
                if (joints < 2)
                {
                    warnings.push_back(label + " has fewer than 2 joints and is never solved");
                }
            }
        }

        std::string warningCount(const nlohmann::json& warnings)
        {
            return warnings.empty() ? std::string() : " " + std::to_string(warnings.size()) + " warning(s).";
        }

        // A finite [x, y, z] (also {"x":..,"y":..,"z":..}) within +/-maxVectorComponent.
        glm::vec3 finiteVec3(const ArgReader& reader, const char* name)
        {
            const glm::vec3 value = reader.requireVec3(name);
            for (glm::length_t i = 0; i < 3; ++i)
            {
                if (!std::isfinite(value[i]) || std::fabs(value[i]) > maxVectorComponent)
                {
                    throw ArgError(std::string("argument '") + name + "' must hold finite numbers within +/-1e6");
                }
            }
            return value;
        }

        // ------------------------------------------------------------------
        // Schemas
        // ------------------------------------------------------------------

        nlohmann::json constraintSchema()
        {
            std::vector<std::string> types;
            for (std::string_view name : constraintTypeNames)
            {
                types.emplace_back(name);
            }
            return closedObject({
                {"type", schema::enumString("Joint constraint type", types)},
                {"hingeAxis", schema::vec3("Hinge: rotation axis [x,y,z] in the joint's local space (normalised; "
                                           "default [0,1,0])")},
                {"coneAngle", schema::numberRange("Cone: maximum swing in DEGREES (default 45)", 0.0, maxConeDegrees)},
                {"swingAngle", schema::numberRange("BallAndSocket: maximum swing in DEGREES (default 45)", 0.0,
                                                   maxSwingDegrees)},
                {"twistMin", schema::numberRange("BallAndSocket: minimum twist in DEGREES (default -45)",
                                                 minTwistDegrees, 0.0)},
                {"twistMax", schema::numberRange("BallAndSocket: maximum twist in DEGREES (default 45)", 0.0,
                                                 maxTwistDegrees)}
            }, {"type"});
        }

        nlohmann::json chainSchema()
        {
            return closedObject({
                {"name", schema::string("Chain name, unique on the entity (an existing chain of that name is replaced "
                                        "in place)")},
                {"tipBone", schema::string("The bone that reaches for the target, e.g. a hand or a foot")},
                {"chainBones", schema::array(schema::string("Bone name"),
                                             "Bone names root first, each the parent of the next, ending with tipBone "
                                             "or its parent (the tip is then appended). Use this OR chainLength.")},
                {"chainLength", schema::integerRange("Joints counted up from tipBone, tip included (3 = e.g. upper "
                                                     "arm, forearm, hand). Use this OR chainBones.", 2, maxChainJoints)},
                {"weight", schema::numberRange("Blend weight between the animation (0) and the IK result (1). "
                                               "Default 1.", 0.0, 1.0)},
                {"enabled", schema::boolean("Default true")},
                {"constraints", schema::array(constraintSchema(), "Joint limits, one per joint, root first (fewer = "
                                                                  "the rest unconstrained); angles in DEGREES")}
            }, {"name", "tipBone"});
        }

        // ------------------------------------------------------------------
        // Tools
        // ------------------------------------------------------------------

        void registerIkGet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_ik_get";
            tool.title = "Get IK chains";
            tool.description =
                "Read an entity's IK setup: hasComponent, hasAnimator (IK only runs on an entity with an Animator), "
                "mesh, skinned, boneCount, chains on its IK component [{name, tipBone, chainBones (root first, "
                "ending with the tip), weight, enabled, constraints (angles in DEGREES)}] in solve order, meshChains "
                "(the chains authored into the .vfMesh, which a new IK component starts from) and warnings (a bone "
                "the skeleton lacks skips its chain). includeBones=true adds the skeleton's bones [{name, parent}] "
                "to pick tipBone / chainBones from. A reported chain can be edited and sent back to "
                "animation_ik_set. Works in Play mode.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity()},
                {"includeBones", schema::boolean("Also list the skeleton's bones [{name, parent}]. Default false.")}
            }, {"entity"});
            tool.readOnly = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "includeBones"}, "animation_ik_get");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const bool includeBones = reader.optBool("includeBones", false);
                auto& dispatcher = events::EventDispatcher::instance();

                events::ik::HasIKComponentQuery hasQuery;
                hasQuery.entity = entity.handle;
                const bool hasComponent = dispatcher.query(hasQuery);
                std::vector<IKChainConfig> chains;
                if (hasComponent)
                {
                    events::ik::GetIKChainConfigsQuery chainsQuery;
                    chainsQuery.entity = entity.handle;
                    chains = dispatcher.query(chainsQuery);
                }

                const std::optional<services::MeshData> mesh = rig::meshData(entity.handle);
                const bool hasAnimator = mesh.has_value() && mesh->animatorRef.isValid();
                nlohmann::json warnings = nlohmann::json::array();
                nlohmann::json meshChains = nlohmann::json::array();
                std::optional<SkeletonBones> bones;
                std::string meshLabel;
                if (!mesh.has_value() || !mesh->meshRef.isValid())
                {
                    warnings.push_back(entityLabel(entity) + " has no mesh: IK needs a skinned Mesh.");
                }
                else if (const std::string path = rig::meshPath(*mesh); path.empty())
                {
                    warnings.push_back("The mesh asset (guid:" + mesh->meshRef.toHexString() + ") cannot be resolved.");
                }
                else
                {
                    meshLabel = "'" + rig::agentPath(path) + "'";
                    const services::MeshSkeletonInfo skeleton = rig::readSkeleton(path);
                    if (!skeleton.success)
                    {
                        warnings.push_back("Cannot read the skeleton of " + meshLabel + ": " + skeleton.error);
                    }
                    else if (!skeleton.skinned || skeleton.boneNames.empty())
                    {
                        warnings.push_back(meshLabel + " has no skeleton: IK needs a skinned mesh.");
                    }
                    else
                    {
                        bones.emplace(skeleton);
                        meshChains = chainsJson(skeleton.ikChains);
                    }
                }

                if (!hasAnimator)
                {
                    warnings.push_back(noAnimatorWarning(entity));
                }
                if (hasComponent && chains.empty())
                {
                    warnings.push_back("The IK component has no chains; add them with animation_ik_set.");
                }
                if (bones.has_value())
                {
                    addChainWarnings(chains, *bones, meshLabel, warnings);
                }

                nlohmann::json out{
                    {"entity", entityId(entity.handle)},
                    {"name", entity.name},
                    {"hasComponent", hasComponent},
                    {"hasAnimator", hasAnimator},
                    {"mesh", mesh.has_value() ? rig::meshJson(*mesh) : nlohmann::json(nullptr)},
                    {"skinned", bones.has_value()},
                    {"boneCount", bones.has_value() ? bones->size() : std::size_t{0}},
                    {"chains", chainsJson(chains)},
                    {"meshChains", std::move(meshChains)}
                };
                if (includeBones)
                {
                    out["bones"] = bones.has_value() ? bonesJson(*bones) : nlohmann::json::array();
                }
                out["warnings"] = warnings;

                std::string summary = entityLabel(entity) + ": " +
                    (hasComponent ? std::to_string(chains.size()) + " IK chain(s) (" + chainNames(chains) + ")."
                                  : std::string("no IK component."));
                summary += warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerIkSet(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_ik_set";
            tool.title = "Set IK chains";
            tool.description =
                "Create, replace or remove the IK chains of an entity with a skinned Mesh (FABRIK, solved on top of "
                "the animated pose, so it needs an Animator to show). Each chain: {name, tipBone, chainLength "
                "(joints counted up from the tip, tip included) | chainBones (bone names root first, each the parent "
                "of the next, ending with the tip or its parent), weight 0..1 (default 1), enabled (default true), "
                "constraints: one per joint, root first, [{type None|Hinge|Cone|BallAndSocket, hingeAxis [x,y,z] "
                "(Hinge), coneAngle 0..180 (Cone), swingAngle 0..180, twistMin -180..0, twistMax 0..180 "
                "(BallAndSocket)}] with angles in DEGREES}. Chains are upserted by name (an existing chain keeps its "
                "place, new ones are appended and solved last); 'remove' drops chains by name; replaceAll=true makes "
                "'chains' the whole list. Without an IK component one is added first, starting from the chains "
                "authored in the .vfMesh. Every bone is checked against the skeleton before anything changes. "
                "Setting the chains clears their runtime targets (animation_ik_set_target). Saved with the scene on "
                "scene_save; one undo step; refused in Play mode. Returns {changed, componentAdded, chains, "
                "warnings}.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity("Entity with a skinned Mesh")},
                {"chains", schema::array(chainSchema(), "Chains to create or replace, matched by name")},
                {"remove", schema::array(schema::string("Chain name"), "Names of chains to remove")},
                {"replaceAll", schema::boolean("true: 'chains' becomes the whole chain list. Default false (upsert).")}
            }, {"entity"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("edit IK chains"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"entity", "chains", "remove", "replaceAll"}, "animation_ik_set");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const bool replaceAll = reader.optBool("replaceAll", false);

                std::vector<std::string> removeNames;
                if (reader.has("remove"))
                {
                    for (std::string& name : reader.requireStringArray("remove"))
                    {
                        if (std::find(removeNames.begin(), removeNames.end(), name) == removeNames.end())
                        {
                            removeNames.push_back(std::move(name));
                        }
                    }
                }
                if (replaceAll && !removeNames.empty())
                {
                    throw ArgError("'remove' cannot be combined with replaceAll=true: replaceAll already drops every "
                                   "chain 'chains' does not list");
                }
                const nlohmann::json* chainArgs = reader.has("chains") ? &reader.raw("chains") : nullptr;
                if (chainArgs != nullptr && !chainArgs->is_array())
                {
                    throw ArgError("argument 'chains' must be an array of chain objects");
                }

                auto& dispatcher = events::EventDispatcher::instance();
                undo::IkState before;
                events::ik::HasIKComponentQuery hasQuery;
                hasQuery.entity = entity.handle;
                before.hasComponent = dispatcher.query(hasQuery);
                if (before.hasComponent && chainArgs == nullptr && removeNames.empty() && !replaceAll)
                {
                    throw ArgError("nothing to change: pass 'chains', 'remove' or replaceAll (" + entityLabel(entity) +
                                   " already has an IK component)");
                }

                const SkinnedRig target = requireSkinnedRig(entity);
                const SkeletonBones bones(target.skeleton);

                std::vector<IKChainConfig> requested;
                if (chainArgs != nullptr)
                {
                    for (std::size_t i = 0; i < chainArgs->size(); ++i)
                    {
                        IKChainConfig chain =
                            parseChain((*chainArgs)[i], "chains[" + std::to_string(i) + "]", bones, target.label);
                        for (const IKChainConfig& earlier : requested)
                        {
                            if (earlier.chainName == chain.chainName)
                            {
                                throw ArgError("'chains' names the chain '" + chain.chainName + "' twice");
                            }
                        }
                        if (std::find(removeNames.begin(), removeNames.end(), chain.chainName) != removeNames.end())
                        {
                            throw ArgError("the chain '" + chain.chainName + "' is both in 'chains' and in 'remove'");
                        }
                        requested.push_back(std::move(chain));
                    }
                }

                if (before.hasComponent)
                {
                    events::ik::GetIKChainConfigsQuery chainsQuery;
                    chainsQuery.entity = entity.handle;
                    before.chains = dispatcher.query(chainsQuery);
                }

                // Without a component the edit starts from what AddIKComponent copies: the mesh's own chains.
                std::vector<IKChainConfig> chains;
                if (!replaceAll)
                {
                    chains = before.hasComponent ? before.chains : target.skeleton.ikChains;
                }
                const std::string available = chainNames(chains);
                for (const std::string& name : removeNames)
                {
                    const auto removed = std::erase_if(chains, [&name](const IKChainConfig& chain)
                    {
                        return chain.chainName == name;
                    });
                    if (removed == 0)
                    {
                        throw ArgError("there is no IK chain named '" + name + "' to remove (chains: " + available + ")");
                    }
                }
                for (IKChainConfig& chain : requested)
                {
                    const auto existing = std::find_if(chains.begin(), chains.end(), [&chain](const IKChainConfig& c)
                    {
                        return c.chainName == chain.chainName;
                    });
                    if (existing != chains.end())
                    {
                        *existing = std::move(chain);
                    }
                    else
                    {
                        chains.push_back(std::move(chain));
                    }
                }

                const bool componentAdded = !before.hasComponent;
                const bool changed = componentAdded || !sameChains(before.chains, chains);
                if (changed)
                {
                    undo::UndoRecorder recorder("MCP: Set IK chains (" + entity.name + ")");
                    recorder.add(std::make_unique<undo::IkStateUndo>(toId(entity.handle), entity.name, before,
                                                                     undo::IkState{true, chains}));
                    try
                    {
                        if (componentAdded)
                        {
                            events::ik::AddIKComponentCommand add;
                            add.entity = entity.handle;
                            if (!dispatcher.execute(add))
                            {
                                throw std::runtime_error("AddIKComponent was refused");
                            }
                        }
                        events::ik::SetIKChainsCommand set;
                        set.entity = entity.handle;
                        set.chains = chains;
                        if (!dispatcher.execute(set))
                        {
                            throw std::runtime_error("SetIKChains was refused");
                        }
                    }
                    catch (const std::exception& e)
                    {
                        recorder.rollback();
                        return ToolResult::error(std::string("The IK chains were not changed (rolled back): ") + e.what());
                    }
                    recorder.push();
                }

                events::ik::GetIKChainConfigsQuery storedQuery;
                storedQuery.entity = entity.handle;
                const std::vector<IKChainConfig> stored = dispatcher.query(storedQuery);

                nlohmann::json warnings = nlohmann::json::array();
                const bool hasAnimator = target.mesh.animatorRef.isValid();
                if (!hasAnimator)
                {
                    warnings.push_back(noAnimatorWarning(entity));
                }
                addChainWarnings(stored, bones, target.label, warnings);

                nlohmann::json out{
                    {"entity", entityId(entity.handle)},
                    {"name", entity.name},
                    {"changed", changed},
                    {"componentAdded", componentAdded},
                    {"chains", chainsJson(stored)},
                    {"hasAnimator", hasAnimator},
                    {"warnings", warnings}
                };
                std::string summary = (changed ? "Set the IK chains of " : "Nothing changed on ") + entityLabel(entity) +
                    ": " + std::to_string(stored.size()) + " chain(s) (" + chainNames(stored) + ").";
                if (componentAdded)
                {
                    summary += " Added the IK component.";
                }
                summary += warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerIkRemove(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_ik_remove";
            tool.title = "Remove IK component";
            tool.description =
                "Remove an entity's IK component with all of its chains and targets. One undo step (undo restores "
                "the component and its chains); refused in Play mode. Returns the removed chains.";
            tool.inputSchema = closedObject({{"entity", schema::entity()}}, {"entity"});
            tool.destructive = true;
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                if (auto blocked = env::editBlocker("remove an IK component"))
                {
                    return ToolResult::error(*blocked);
                }
                env::rejectUnknownArgs(args, {"entity"}, "animation_ik_remove");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                auto& dispatcher = events::EventDispatcher::instance();

                events::ik::HasIKComponentQuery hasQuery;
                hasQuery.entity = entity.handle;
                if (!dispatcher.query(hasQuery))
                {
                    throw std::runtime_error(entityLabel(entity) + " has no IK component.");
                }
                events::ik::GetIKChainConfigsQuery chainsQuery;
                chainsQuery.entity = entity.handle;
                undo::IkState before{true, dispatcher.query(chainsQuery)};

                events::ik::RemoveIKComponentCommand remove;
                remove.entity = entity.handle;
                if (!dispatcher.execute(remove))
                {
                    return ToolResult::error("RemoveIKComponent failed for " + entityLabel(entity));
                }

                nlohmann::json out{
                    {"entity", entityId(entity.handle)},
                    {"name", entity.name},
                    {"removedChains", chainsJson(before.chains)}
                };
                const std::string summary = "Removed the IK component of " + entityLabel(entity) + " (" +
                    std::to_string(before.chains.size()) + " chain(s)).";

                undo::UndoRecorder recorder("MCP: Remove IK component (" + entity.name + ")");
                recorder.add(std::make_unique<undo::IkStateUndo>(toId(entity.handle), entity.name, std::move(before),
                                                                 undo::IkState{}));
                recorder.push();
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }

        void registerIkSetTarget(ToolRegistry& registry)
        {
            ToolDef tool;
            tool.name = "animation_ik_set_target";
            tool.title = "Set IK target";
            tool.description =
                "Aim an IK chain: its tip bone reaches for 'position' (WORLD space, metres) and, when 'rotation' is "
                "given, takes that WORLD rotation (Euler DEGREES, the entity rotationEuler convention). Runtime "
                "only: targets are not saved and not undoable, animation_ik_set clears them, and they stay where they "
                "are when the entity moves - in a game a script updates them every frame with lib/engine/IK.mt "
                "(IK::setTarget(entityId, chain, position)). 'weight' (0..1) first sets the chain's blend weight, "
                "which is the chain's saved weight (not undone by undo; animation_ik_set changes it undoably). Works "
                "in edit mode (the viewport shows the pose) and in Play mode. IK only runs on an entity with an "
                "Animator.";
            tool.inputSchema = closedObject({
                {"entity", schema::entity()},
                {"chain", schema::string("IK chain name (animation_ik_get lists them)")},
                {"position", schema::vec3("Target position [x,y,z] in WORLD space (metres)")},
                {"rotation", schema::vec3("Optional WORLD rotation for the tip, Euler DEGREES [x,y,z]")},
                {"weight", schema::numberRange("Optional chain weight 0..1, set before the target", 0.0, 1.0)}
            }, {"entity", "chain", "position"});
            tool.handler = [](const nlohmann::json& args) -> ToolResult
            {
                env::rejectUnknownArgs(args, {"entity", "chain", "position", "rotation", "weight"},
                                       "animation_ik_set_target");
                const ArgReader reader(args);
                const services::EntityData entity = requireEntity(reader, "entity");
                const std::string chainName = reader.requireString("chain");
                if (chainName.empty())
                {
                    throw ArgError("argument 'chain' must not be empty");
                }
                const glm::vec3 position = finiteVec3(reader, "position");
                std::optional<glm::vec3> rotation;
                if (reader.has("rotation"))
                {
                    rotation = finiteVec3(reader, "rotation");
                }
                std::optional<float> weight;
                if (reader.has("weight"))
                {
                    weight = static_cast<float>(reader.requireNumberInRange("weight", 0.0, 1.0));
                }

                // SetIKTarget / SetIKChainWeight silently ignore an unknown chain: check it first.
                auto& dispatcher = events::EventDispatcher::instance();
                events::ik::HasIKComponentQuery hasQuery;
                hasQuery.entity = entity.handle;
                if (!dispatcher.query(hasQuery))
                {
                    throw std::runtime_error(entityLabel(entity) +
                                             " has no IK component; create its chains with animation_ik_set first.");
                }
                events::ik::GetIKChainConfigsQuery chainsQuery;
                chainsQuery.entity = entity.handle;
                const std::vector<IKChainConfig> chains = dispatcher.query(chainsQuery);
                const auto chain = std::find_if(chains.begin(), chains.end(), [&chainName](const IKChainConfig& c)
                {
                    return c.chainName == chainName;
                });
                if (chain == chains.end())
                {
                    throw std::runtime_error(entityLabel(entity) + " has no IK chain '" + chainName + "' (chains: " +
                                             chainNames(chains) + ").");
                }

                if (weight.has_value())
                {
                    events::ik::SetIKChainWeightCommand setWeight;
                    setWeight.entity = entity.handle;
                    setWeight.chainName = chainName;
                    setWeight.weight = *weight;
                    dispatcher.execute(setWeight);
                }
                events::ik::SetIKTargetCommand setTarget;
                setTarget.entity = entity.handle;
                setTarget.chainName = chainName;
                setTarget.targetPosition = position;
                if (rotation.has_value())
                {
                    setTarget.targetRotation = rig::rotationFromEulerDegrees(*rotation);
                }
                dispatcher.execute(setTarget);

                const std::optional<services::MeshData> mesh = rig::meshData(entity.handle);
                const bool hasAnimator = mesh.has_value() && mesh->animatorRef.isValid();
                const float effectiveWeight = weight.value_or(chain->weight);
                nlohmann::json warnings = nlohmann::json::array();
                if (!hasAnimator)
                {
                    warnings.push_back(noAnimatorWarning(entity));
                }
                if (!chain->enabled)
                {
                    warnings.push_back("IK chain '" + chainName + "' is disabled; enable it with animation_ik_set.");
                }
                if (effectiveWeight <= 0.0f)
                {
                    warnings.push_back("IK chain '" + chainName + "' has weight 0: the target has no visible effect.");
                }

                nlohmann::json out{
                    {"entity", entityId(entity.handle)},
                    {"chain", chainName},
                    {"position", env::vec3Json(position)},
                    {"rotationEuler", rotation.has_value() ? rig::degreesJson(*rotation) : nlohmann::json(nullptr)},
                    {"weight", env::floatJson(effectiveWeight)},
                    {"enabled", chain->enabled},
                    {"hasAnimator", hasAnimator},
                    {"warnings", warnings}
                };
                const std::string summary = "Aimed IK chain '" + chainName + "' of " + entityLabel(entity) + "." +
                    warningCount(warnings);
                return reply(std::move(out), summary);
            };
            registry.add(std::move(tool));
        }
    }

    void registerAnimationIkTools(ToolRegistry& registry, const ToolContext&)
    {
        registerIkGet(registry);
        registerIkSet(registry);
        registerIkRemove(registry);
        registerIkSetTarget(registry);
    }
}
