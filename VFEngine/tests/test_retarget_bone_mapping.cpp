#include <doctest.h>

#include "retargeting/RetargetBoneMapping.hpp"

#include <string>
#include <utility>
#include <vector>

// VK-1656: retargeting::RetargetBoneMapping, the single source of truth for which source-rig bone drives
// a target-skeleton bone (RetargetContext::build at runtime, animation_list_clips' coverage in MCP).
namespace
{
    using retargeting::HumanoidBoneRole;

    retargeting::HumanoidRigData rig(std::vector<std::pair<HumanoidBoneRole, std::string>> bindings)
    {
        retargeting::HumanoidRigData out;
        for (auto& [role, bone] : bindings)
        {
            retargeting::HumanoidBoneBinding binding;
            binding.role = role;
            binding.boneName = std::move(bone);
            out.bindings.push_back(std::move(binding));
        }
        return out;
    }

    // A Mixamo-named source rig and a target skeleton with its own bone names.
    retargeting::HumanoidRigData mixamo()
    {
        return rig({{HumanoidBoneRole::Hips, "mixamorig:Hips"},
                    {HumanoidBoneRole::Spine, "mixamorig:Spine"},
                    {HumanoidBoneRole::Chest, "mixamorig:Spine1"},
                    {HumanoidBoneRole::Head, "mixamorig:Head"}});
    }

    retargeting::HumanoidRigData hero()
    {
        return rig({{HumanoidBoneRole::Hips, "pelvis"},
                    {HumanoidBoneRole::Spine, "spine_01"},
                    {HumanoidBoneRole::Head, "head"},
                    {HumanoidBoneRole::LeftHand, "hand_l"}});
    }
}

TEST_SUITE("RetargetBoneMapping")
{
    TEST_CASE("a bound target bone maps through its role to the source rig's bone")
    {
        const retargeting::HumanoidRigData source = mixamo();
        const retargeting::HumanoidRigData target = hero();
        const retargeting::RetargetMapData map{};
        const retargeting::RetargetBoneMapping mapping(source, target, map);

        const auto pelvis = mapping.match("pelvis");
        REQUIRE(pelvis.has_value());
        REQUIRE(pelvis->target != nullptr);
        REQUIRE(pelvis->source != nullptr);
        CHECK(pelvis->target->role == HumanoidBoneRole::Hips);
        CHECK(pelvis->target->boneName == "pelvis");
        CHECK(pelvis->source->role == HumanoidBoneRole::Hips);
        CHECK(pelvis->source->boneName == "mixamorig:Hips");
        // The bindings are the rigs' own entries, not copies.
        CHECK(pelvis->source == &source.bindings[0]);
        CHECK(pelvis->target == &target.bindings[0]);

        CHECK(mapping.match("head")->source->boneName == "mixamorig:Head");
    }

    TEST_CASE("an unbound target bone holds its bind pose")
    {
        const retargeting::HumanoidRigData source = mixamo();
        const retargeting::HumanoidRigData target = hero();
        const retargeting::RetargetMapData map{};
        const retargeting::RetargetBoneMapping mapping(source, target, map);

        CHECK_FALSE(mapping.match("spine_02").has_value());
        CHECK_FALSE(mapping.match("mixamorig:Hips").has_value()); // a source name is not a target bone
        CHECK_FALSE(mapping.match("").has_value());
    }

    TEST_CASE("a role disabled by a map override is not driven; an enabled override changes nothing")
    {
        const retargeting::HumanoidRigData source = mixamo();
        const retargeting::HumanoidRigData target = hero();
        retargeting::RetargetMapData map;
        map.overrides.push_back({HumanoidBoneRole::Spine, false, false});
        map.overrides.push_back({HumanoidBoneRole::Head, true, true});
        const retargeting::RetargetBoneMapping mapping(source, target, map);

        CHECK_FALSE(mapping.match("spine_01").has_value());
        REQUIRE(mapping.match("head").has_value());
        CHECK(mapping.match("head")->source->boneName == "mixamorig:Head");
        CHECK(mapping.match("pelvis").has_value());
    }

    TEST_CASE("a role the source rig does not bind is not driven")
    {
        const retargeting::HumanoidRigData source = mixamo(); // no LeftHand
        const retargeting::HumanoidRigData target = hero();
        const retargeting::RetargetMapData map{};
        const retargeting::RetargetBoneMapping mapping(source, target, map);

        CHECK_FALSE(mapping.match("hand_l").has_value());
    }

    TEST_CASE("a bone name the target rig binds twice resolves to its last binding")
    {
        const retargeting::HumanoidRigData source = mixamo();
        const retargeting::HumanoidRigData target = rig({{HumanoidBoneRole::Spine, "spine"},
                                                         {HumanoidBoneRole::Chest, "spine"}});
        const retargeting::RetargetMapData map{};
        const retargeting::RetargetBoneMapping mapping(source, target, map);

        const auto spine = mapping.match("spine");
        REQUIRE(spine.has_value());
        CHECK(spine->target->role == HumanoidBoneRole::Chest);
        CHECK(spine->source->boneName == "mixamorig:Spine1");
    }
}
