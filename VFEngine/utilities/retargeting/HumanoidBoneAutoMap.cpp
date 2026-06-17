#include "HumanoidBoneAutoMap.hpp"
#include "../resource/Types.hpp"

#include <glm/gtc/quaternion.hpp>
#include <array>
#include <algorithm>
#include <cctype>

namespace retargeting
{
    namespace
    {
        enum class Side : uint8_t { None, Left, Right };

        // Lowercase, drop separators, strip known armature prefixes.
        // "mixamorig:LeftForeArm" -> "leftforearm"; "Bip01 L Forearm" -> "lforearm".
        std::string normalize(const std::string& raw)
        {
            std::string s;
            s.reserve(raw.size());
            for (char c : raw)
            {
                if (c == '_' || c == '-' || c == ':' || c == '.' || c == '|' || c == ' ')
                    continue;
                s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }

            // Strip leading DCC prefixes (after separator removal).
            static constexpr std::array<const char*, 4> prefixes{"mixamorig", "bip001", "bip01", "bip"};
            for (const char* p : prefixes)
            {
                const std::string pref(p);
                if (s.size() > pref.size() && s.compare(0, pref.size(), pref) == 0)
                {
                    s.erase(0, pref.size());
                    break;
                }
            }
            return s;
        }

        bool contains(const std::string& hay, const char* needle) noexcept
        {
            return hay.find(needle) != std::string::npos;
        }

        // Side detection from the raw (separator-bearing) lowercased name.
        Side detectSide(const std::string& raw, const std::string& norm)
        {
            std::string low;
            low.reserve(raw.size());
            for (char c : raw) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            if (contains(low, "left")) return Side::Left;
            if (contains(low, "right")) return Side::Right;

            // Isolated l/r token (Arm_L, .R, Bip01 L Forearm) — split on separators.
            std::string token;
            auto flush = [&](Side& out) {
                if (token == "l") out = Side::Left;
                else if (token == "r") out = Side::Right;
                token.clear();
            };
            Side side = Side::None;
            for (char c : low)
            {
                if (c == '_' || c == '-' || c == ':' || c == '.' || c == '|' || c == ' ')
                    flush(side);
                else
                    token.push_back(c);
            }
            flush(side);
            if (side != Side::None) return side;

            // Trailing-char fallback ("UpperArmL"/"UpperArmR") on the normalized core.
            // Skip centerline bones that legitimately end in l/r ("Spine_Roll" ->
            // "spineroll", "pelvis" ... ): they are never sided, and mis-siding them
            // drops them from the centerline (spine) resolution pass and leaves a
            // required role unmapped.
            auto isCenterlineCore = [](const std::string& c) {
                static constexpr std::array<const char*, 9> mid{
                    "spine", "chest", "torso", "neck", "head", "hip", "pelvis", "jaw", "root"};
                for (const char* m : mid)
                    if (contains(c, m)) return true;
                return false;
            };
            if (!norm.empty() && !isCenterlineCore(norm))
            {
                if (norm.back() == 'l') return Side::Left;
                if (norm.back() == 'r') return Side::Right;
            }
            return Side::None;
        }

        struct RolePattern
        {
            HumanoidBoneRole baseRole;          // sided patterns use Left* as the base
            bool sided;
            std::vector<const char*> synonyms;  // matched in the normalized core
            std::vector<const char*> negatives; // disqualify if any present
        };

        // Spine/Chest/UpperChest are resolved separately by hierarchy depth.
        const std::vector<RolePattern>& patterns()
        {
            static const std::vector<RolePattern> p = {
                {HumanoidBoneRole::Hips,         false, {"hips", "pelvis"}, {}},
                {HumanoidBoneRole::Neck,         false, {"neck"}, {}},
                {HumanoidBoneRole::Head,         false, {"head"}, {"headtop", "end"}},
                {HumanoidBoneRole::Jaw,          false, {"jaw"}, {}},
                {HumanoidBoneRole::LeftEye,      true,  {"eye"}, {}},

                {HumanoidBoneRole::LeftShoulder, true,  {"shoulder", "clavicle", "collar"}, {}},
                {HumanoidBoneRole::LeftLowerArm, true,  {"forearm", "lowerarm"}, {"hand", "finger", "thumb"}},
                {HumanoidBoneRole::LeftUpperArm, true,  {"upperarm", "arm", "uparm"},
                                                        {"fore", "lower", "hand", "finger", "thumb", "lowerarm"}},
                {HumanoidBoneRole::LeftHand,     true,  {"hand", "wrist"},
                                                        {"finger", "thumb", "index", "middle", "ring", "pinky"}},

                {HumanoidBoneRole::LeftUpperLeg, true,  {"upperleg", "upleg", "thigh"}, {"fore"}},
                {HumanoidBoneRole::LeftLowerLeg, true,  {"lowerleg", "calf", "shin"}, {}},
                {HumanoidBoneRole::LeftFoot,     true,  {"foot", "ankle"}, {"toe", "ball"}},
                {HumanoidBoneRole::LeftToes,     true,  {"toebase", "toe", "ball", " toes"}, {}},
            };
            return p;
        }

        // LowerLeg uses bare "leg" which also appears in "upleg"; gate it after the
        // table so UpperLeg claims "upleg" first.
        bool matchesLowerLeg(const std::string& core)
        {
            if (contains(core, "up") || contains(core, "thigh")) return false;
            return contains(core, "leg");
        }

        HumanoidBoneRole sideRole(HumanoidBoneRole leftBase, Side side)
        {
            if (side == Side::Right)
            {
                // Left* and Right* roles are laid out as matching ordered blocks;
                // shift by the constant Left->Right delta for arm/leg/eye groups.
                switch (leftBase)
                {
                case HumanoidBoneRole::LeftShoulder: return HumanoidBoneRole::RightShoulder;
                case HumanoidBoneRole::LeftUpperArm: return HumanoidBoneRole::RightUpperArm;
                case HumanoidBoneRole::LeftLowerArm: return HumanoidBoneRole::RightLowerArm;
                case HumanoidBoneRole::LeftHand:     return HumanoidBoneRole::RightHand;
                case HumanoidBoneRole::LeftUpperLeg: return HumanoidBoneRole::RightUpperLeg;
                case HumanoidBoneRole::LeftLowerLeg: return HumanoidBoneRole::RightLowerLeg;
                case HumanoidBoneRole::LeftFoot:     return HumanoidBoneRole::RightFoot;
                case HumanoidBoneRole::LeftToes:     return HumanoidBoneRole::RightToes;
                case HumanoidBoneRole::LeftEye:      return HumanoidBoneRole::RightEye;
                default:                             return leftBase;
                }
            }
            return leftBase;
        }

        int boneDepth(const resource::SkeletonData& skel, int index)
        {
            int depth = 0;
            int idx = index;
            while (idx >= 0 && depth < 256)
            {
                idx = skel.bones[static_cast<size_t>(idx)].parentIndex;
                ++depth;
            }
            return depth;
        }
    }

    glm::quat localBindRotation(const glm::mat4& offsetMatrix)
    {
        glm::mat3 m(offsetMatrix);
        for (int c = 0; c < 3; ++c)
        {
            float len = glm::length(m[c]);
            if (len > 1e-6f) m[c] /= len;
        }
        return glm::normalize(glm::quat_cast(m));
    }

    std::vector<HumanoidBoneBinding> autoMapHumanoidBones(const resource::SkeletonData& skeleton)
    {
        const size_t boneCount = skeleton.bones.size();
        std::vector<std::string> cores(boneCount);
        std::vector<Side> sides(boneCount);
        for (size_t i = 0; i < boneCount; ++i)
        {
            cores[i] = normalize(skeleton.bones[i].name);
            sides[i] = detectSide(skeleton.bones[i].name, cores[i]);
        }

        std::vector<bool> boneClaimed(boneCount, false);
        // role index -> chosen bone (-1 none) and the score that won it.
        std::array<int, humanoidBoneRoleCount> roleBone;
        std::array<int, humanoidBoneRoleCount> roleScore;
        roleBone.fill(-1);
        roleScore.fill(-1);

        auto tryAssign = [&](HumanoidBoneRole role, size_t bone, int score) {
            const auto r = static_cast<size_t>(role);
            if (score > roleScore[r])
            {
                roleScore[r] = score;
                roleBone[r] = static_cast<int>(bone);
            }
        };

        // --- Generic synonym pass (everything except the spine chain) ---
        for (const auto& pat : patterns())
        {
            for (size_t i = 0; i < boneCount; ++i)
            {
                const std::string& core = cores[i];
                if (core.empty()) continue;

                bool negated = false;
                for (const char* neg : pat.negatives)
                    if (contains(core, neg)) { negated = true; break; }
                if (negated) continue;

                int best = -1;
                for (const char* syn : pat.synonyms)
                    if (contains(core, syn))
                        best = std::max(best, static_cast<int>(std::string(syn).size()));
                if (best < 0) continue;

                HumanoidBoneRole role = pat.baseRole;
                if (pat.sided)
                {
                    if (sides[i] == Side::None) continue;     // sided role needs a side
                    role = sideRole(pat.baseRole, sides[i]);
                }
                tryAssign(role, i, best);
            }
        }

        // --- LowerLeg special "leg" gate ---
        for (size_t i = 0; i < boneCount; ++i)
        {
            if (cores[i].empty() || sides[i] == Side::None) continue;
            if (matchesLowerLeg(cores[i]))
                tryAssign(sideRole(HumanoidBoneRole::LeftLowerLeg, sides[i]), i, 3 /* "leg" */);
        }

        // --- Spine chain by hierarchy depth: lowest -> Spine, then Chest, UpperChest ---
        {
            std::vector<size_t> spineBones;
            for (size_t i = 0; i < boneCount; ++i)
            {
                const std::string& core = cores[i];
                if (sides[i] != Side::None) continue;
                if (contains(core, "spine") || contains(core, "chest") || contains(core, "torso"))
                    spineBones.push_back(i);
            }
            std::sort(spineBones.begin(), spineBones.end(),
                      [&](size_t a, size_t b) {
                          return boneDepth(skeleton, static_cast<int>(a)) <
                                 boneDepth(skeleton, static_cast<int>(b));
                      });
            const std::array<HumanoidBoneRole, 3> spineRoles{
                HumanoidBoneRole::Spine, HumanoidBoneRole::Chest, HumanoidBoneRole::UpperChest};
            for (size_t k = 0; k < spineBones.size() && k < spineRoles.size(); ++k)
                tryAssign(spineRoles[k], spineBones[k], 100 /* authoritative */);
        }

        // --- Resolve greedily by descending score so a bone is never double-claimed ---
        std::vector<std::pair<int, size_t>> ranked; // (score, roleIndex)
        for (size_t r = 0; r < humanoidBoneRoleCount; ++r)
            if (roleBone[r] >= 0)
                ranked.emplace_back(roleScore[r], r);
        std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });

        std::vector<HumanoidBoneBinding> result;
        for (const auto& [score, r] : ranked)
        {
            const int bone = roleBone[r];
            if (bone < 0 || boneClaimed[static_cast<size_t>(bone)]) continue;
            boneClaimed[static_cast<size_t>(bone)] = true;

            HumanoidBoneBinding binding;
            binding.role = static_cast<HumanoidBoneRole>(r);
            binding.boneName = skeleton.bones[static_cast<size_t>(bone)].name;
            binding.referenceLocalRotation = localBindRotation(skeleton.bones[static_cast<size_t>(bone)].offsetMatrix);
            binding.retargetTranslation = (binding.role == HumanoidBoneRole::Hips);
            result.push_back(std::move(binding));
        }
        return result;
    }
}
