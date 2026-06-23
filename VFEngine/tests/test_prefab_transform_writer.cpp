#include <doctest.h>

// VK-1433 (Save Transforms to Prefab) — CPU coverage for the entt-free JSON writer that bakes the
// rig-preview gizmo transforms into a .vfPrefab. Verifies:
//   * the k-th MESH-BEARING node (DFS pre-order) is treated as part k (matches buildPrefabRigDescDTO)
//   * newLocal = oldLocal * previewTransform, decomposed to the TransformComponent TRS schema
//     ([x,y,z] arrays, rotation = XYZ euler degrees) — a pure translate writes the expected position
//   * identity previewTransform is a no-op (part not rewritten)
//   * skipParts (root) is honored
//   * the round-trip preserves EVERY untouched field (components, isStatic, sibling nodes, version)

#include "windows/preview/PrefabTransformWriter.hpp"
#include "math/TransformUtils.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <set>
#include <string>

using nlohmann::json;
using windows::prefabtransform::applyPreviewTransforms;

namespace
{
    // A 2-part prefab: root mesh "Body" (skeletal) with a child mesh "Weapon" (static). Both carry
    // a transform + a mesh component (so both are mesh-bearing parts). The child also has an
    // extra component + isStatic to prove untouched fields survive the round-trip.
    json makeTwoPartPrefab()
    {
        json weapon = {
            {"name", "Weapon"},
            {"transform", {
                {"position", {1.0f, 0.0f, 0.0f}},
                {"rotation", {0.0f, 0.0f, 0.0f}},
                {"scale", {1.0f, 1.0f, 1.0f}},
                {"isStatic", true}
            }},
            {"components", {
                {"mesh", {{"meshRefPath", "weapon.vfMesh"}}},
                {"socketAttachment", {{"parentEntityName", "Body"}, {"socketName", "Hand"}}}
            }},
            {"children", json::array()}
        };

        json body = {
            {"name", "Body"},
            {"transform", {
                {"position", {0.0f, 0.0f, 0.0f}},
                {"rotation", {0.0f, 0.0f, 0.0f}},
                {"scale", {1.0f, 1.0f, 1.0f}},
                {"isStatic", false}
            }},
            {"components", {
                {"mesh", {{"meshRefPath", "body.vfMesh"}}}
            }},
            {"children", json::array({weapon})}
        };

        return json{
            {"version", "1.0"},
            {"prefab", {{"name", "TestRig"}, {"entity", body}}}
        };
    }

    // Read back a node's transform position array.
    glm::vec3 readPos(const json& transform)
    {
        return glm::vec3(transform["position"][0].get<float>(),
                         transform["position"][1].get<float>(),
                         transform["position"][2].get<float>());
    }
}

TEST_SUITE("PrefabTransformWriter")
{
    // -----------------------------------------------------------------------
    // A pure-translate preview on the CHILD (part 1) writes the expected position
    // and leaves the ROOT (part 0) untouched when root is skipped.
    // -----------------------------------------------------------------------
    TEST_CASE("child translate bakes into the child node transform; root skipped")
    {
        json prefab = makeTwoPartPrefab();

        std::map<int, glm::mat4> preview;
        preview[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)); // child +2 Y

        std::set<int> skip = {0}; // skip root by default

        const int written = applyPreviewTransforms(prefab, preview, skip);
        CHECK(written == 1);

        // Child (part 1) = body.children[0]. oldLocal pos (1,0,0) * translate(0,2,0) => (1,2,0).
        const json& child = prefab["prefab"]["entity"]["children"][0];
        const glm::vec3 pos = readPos(child["transform"]);
        CHECK(pos.x == doctest::Approx(1.0f));
        CHECK(pos.y == doctest::Approx(2.0f));
        CHECK(pos.z == doctest::Approx(0.0f));

        // Root (part 0) position is unchanged (skipped).
        const glm::vec3 rootPos = readPos(prefab["prefab"]["entity"]["transform"]);
        CHECK(rootPos == glm::vec3(0.0f));
    }

    // -----------------------------------------------------------------------
    // Identity preview transform is a no-op (the node is not rewritten / counted).
    // -----------------------------------------------------------------------
    TEST_CASE("identity preview transform is a no-op")
    {
        json prefab = makeTwoPartPrefab();
        std::map<int, glm::mat4> preview;
        preview[1] = glm::mat4(1.0f); // identity

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 0);
    }

    // -----------------------------------------------------------------------
    // Root is written ONLY when not skipped (include-root opt-in).
    // -----------------------------------------------------------------------
    TEST_CASE("root part is written when not skipped")
    {
        json prefab = makeTwoPartPrefab();
        std::map<int, glm::mat4> preview;
        preview[0] = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 1);

        const glm::vec3 rootPos = readPos(prefab["prefab"]["entity"]["transform"]);
        CHECK(rootPos.x == doctest::Approx(5.0f));
    }

    // -----------------------------------------------------------------------
    // Round-trip preserves untouched fields: version, prefab name, the child's
    // extra component (socketAttachment), and isStatic on both nodes.
    // -----------------------------------------------------------------------
    TEST_CASE("round-trip preserves all untouched fields")
    {
        json prefab = makeTwoPartPrefab();
        const json before = prefab; // deep copy

        std::map<int, glm::mat4> preview;
        preview[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
        applyPreviewTransforms(prefab, preview, /*skip*/ {0});

        // Top-level + structural fields untouched.
        CHECK(prefab["version"] == before["version"]);
        CHECK(prefab["prefab"]["name"] == before["prefab"]["name"]);

        const json& body = prefab["prefab"]["entity"];
        const json& child = body["children"][0];

        // Components untouched on both nodes.
        CHECK(body["components"] == before["prefab"]["entity"]["components"]);
        CHECK(child["components"] == before["prefab"]["entity"]["children"][0]["components"]);

        // isStatic survives the transform rewrite (only position/rotation/scale change).
        CHECK(child["transform"]["isStatic"] == true);
        CHECK(body["transform"]["isStatic"] == false);

        // The ONLY changed node is the child's transform position; root transform unchanged.
        CHECK(body["transform"] == before["prefab"]["entity"]["transform"]);
        CHECK(child["transform"]["position"] != before["prefab"]["entity"]["children"][0]["transform"]["position"]);
    }

    // -----------------------------------------------------------------------
    // Decompose sanity: a rotate+scale gizmo edit on a node that already has a
    // non-trivial rotation must bake to a transform whose composed matrix equals
    // oldLocal * previewTransform. Euler is non-unique, so we re-compose the
    // WRITTEN TRS via the SAME convention (math::composeMatrix == getMatrix) and
    // assert the matrix matches — this guards the XYZ euler-order round-trip.
    //
    // Scale is kept UNIFORM on both oldLocal and the preview: the TRS schema
    // (TransformComponent = translate * R-xyz * diagonal-scale) cannot represent
    // the shear that non-uniform-scale ∘ rotation introduces, so a shear-free
    // (rotation + uniform scale + translation) composition is the meaningful
    // round-trip to validate.
    // -----------------------------------------------------------------------
    TEST_CASE("rotate+scale edit bakes to a TRS that recomposes to oldLocal * preview")
    {
        json prefab = makeTwoPartPrefab();

        // Give the child a non-trivial starting rotation + uniform scale so the compose
        // path (not just a translation column) is exercised.
        json& childIn = prefab["prefab"]["entity"]["children"][0];
        childIn["transform"]["position"] = json::array({1.0f, 0.0f, 0.0f});
        childIn["transform"]["rotation"] = json::array({30.0f, 45.0f, 60.0f}); // deg, XYZ
        childIn["transform"]["scale"]    = json::array({2.0f, 2.0f, 2.0f});

        const glm::vec3 oldPos(1.0f, 0.0f, 0.0f);
        const glm::vec3 oldRot(30.0f, 45.0f, 60.0f);
        const glm::vec3 oldScale(2.0f, 2.0f, 2.0f);
        const glm::mat4 oldLocal = math::composeMatrix(oldPos, oldRot, oldScale);

        // Gizmo preview: rotate 20deg about Y, uniform scale 1.5x, translate +1 on Z (local).
        glm::mat4 preview(1.0f);
        preview = glm::translate(preview, glm::vec3(0.0f, 0.0f, 1.0f));
        preview = glm::rotate(preview, glm::radians(20.0f), glm::vec3(0, 1, 0));
        preview = glm::scale(preview, glm::vec3(1.5f, 1.5f, 1.5f));

        const glm::mat4 expected = oldLocal * preview;

        std::map<int, glm::mat4> previewMap;
        previewMap[1] = preview;
        const int written = applyPreviewTransforms(prefab, previewMap, /*skip*/ {0});
        CHECK(written == 1);

        // Re-compose the WRITTEN TRS and compare to the expected matrix element-wise.
        const json& child = prefab["prefab"]["entity"]["children"][0]["transform"];
        const glm::vec3 wPos = readPos(child);
        const glm::vec3 wRot(child["rotation"][0].get<float>(),
                             child["rotation"][1].get<float>(),
                             child["rotation"][2].get<float>());
        const glm::vec3 wScale(child["scale"][0].get<float>(),
                               child["scale"][1].get<float>(),
                               child["scale"][2].get<float>());
        const glm::mat4 recomposed = math::composeMatrix(wPos, wRot, wScale);

        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(recomposed[c][r] == doctest::Approx(expected[c][r]).epsilon(0.0001));
    }

    // -----------------------------------------------------------------------
    // A node with NO mesh component is not counted as a part (index mapping must
    // skip it, exactly like PrefabRigDescBuilder's mesh-bearing filter).
    // -----------------------------------------------------------------------
    TEST_CASE("non-mesh nodes are skipped in the part index mapping")
    {
        // Tree: Root(mesh, part0) -> Empty(no mesh) -> Weapon(mesh, part1).
        json weapon = {
            {"name", "Weapon"},
            {"transform", {{"position", {3.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {{"mesh", {{"meshRefPath", "w.vfMesh"}}}}},
            {"children", json::array()}
        };
        json empty = {
            {"name", "Empty"},
            {"components", json::object()}, // no mesh
            {"children", json::array({weapon})}
        };
        json root = {
            {"name", "Root"},
            {"transform", {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {{"mesh", {{"meshRefPath", "r.vfMesh"}}}}},
            {"children", json::array({empty})}
        };
        json prefab = {{"version", "1.0"}, {"prefab", {{"name", "T"}, {"entity", root}}}};

        // Move part 1 (= Weapon, the 2nd mesh-bearing node — the empty node is NOT part 1).
        std::map<int, glm::mat4> preview;
        preview[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 4.0f));

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 1);

        // Weapon's position z: old (3,0,0) * translate(0,0,4) => (3,0,4).
        const json& w = prefab["prefab"]["entity"]["children"][0]["children"][0];
        const glm::vec3 pos = readPos(w["transform"]);
        CHECK(pos.x == doctest::Approx(3.0f));
        CHECK(pos.z == doctest::Approx(4.0f));
    }

    // -----------------------------------------------------------------------
    // VK-1433 gap: the node->part mapping is PURELY POSITIONAL (k-th mesh-bearing
    // node in DFS pre-order), it never reads node names. DUPLICATE node names must
    // therefore NOT confuse the mapping: two sibling mesh nodes both named "Part"
    // are part 1 and part 2 by ORDER, and moving part 2 leaves part 1 untouched.
    //
    // (PrefabRigDescBuilder's nameToPart is "last-wins" but that map only resolves
    // socket PARENTS — the part INDEX is the desc.parts push order, which the writer
    // mirrors. This test locks that the writer's positional index is name-agnostic.)
    // -----------------------------------------------------------------------
    TEST_CASE("duplicate node names do not confuse the positional part mapping")
    {
        auto meshNode = [](const char* name, float x) {
            return json{
                {"name", name},
                {"transform", {{"position", {x, 0.0f, 0.0f}},
                               {"rotation", {0.0f, 0.0f, 0.0f}},
                               {"scale", {1.0f, 1.0f, 1.0f}}}},
                {"components", {{"mesh", {{"meshRefPath", "p.vfMesh"}}}}},
                {"children", json::array()}
            };
        };

        // Root(mesh, part0) with two children BOTH named "Part" (part1 @x=1, part2 @x=2).
        json root = {
            {"name", "Root"},
            {"transform", {{"position", {0.0f, 0.0f, 0.0f}},
                           {"rotation", {0.0f, 0.0f, 0.0f}},
                           {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {{"mesh", {{"meshRefPath", "r.vfMesh"}}}}},
            {"children", json::array({meshNode("Part", 1.0f), meshNode("Part", 2.0f)})}
        };
        json prefab = {{"version", "1.0"}, {"prefab", {{"name", "Dup"}, {"entity", root}}}};
        const json before = prefab;

        // Move ONLY part 2 (the SECOND "Part" sibling, x=2).
        std::map<int, glm::mat4> preview;
        preview[2] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 7.0f));

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 1);

        // Part 2 (children[1]) moved: old (2,0,0) * translate(0,0,7) => (2,0,7).
        const json& second = prefab["prefab"]["entity"]["children"][1];
        CHECK(readPos(second["transform"]).x == doctest::Approx(2.0f));
        CHECK(readPos(second["transform"]).z == doctest::Approx(7.0f));

        // Part 1 (the FIRST same-named sibling) and the root are untouched.
        CHECK(prefab["prefab"]["entity"]["children"][0]["transform"]
              == before["prefab"]["entity"]["children"][0]["transform"]);
        CHECK(prefab["prefab"]["entity"]["transform"]
              == before["prefab"]["entity"]["transform"]);
    }

    // -----------------------------------------------------------------------
    // VK-1433 gap: a DEEP nested chain (depth >= 3, four mesh nodes) resolves part
    // indices by DFS pre-order. Moving the DEEPEST part (3) must land on the leaf
    // and leave every ancestor part untouched.
    // -----------------------------------------------------------------------
    TEST_CASE("a deeply nested child (depth >= 3) maps to the correct part index")
    {
        auto chainNode = [](const char* name, float x, json child) {
            json children = child.is_null() ? json::array() : json::array({child});
            return json{
                {"name", name},
                {"transform", {{"position", {x, 0.0f, 0.0f}},
                               {"rotation", {0.0f, 0.0f, 0.0f}},
                               {"scale", {1.0f, 1.0f, 1.0f}}}},
                {"components", {{"mesh", {{"meshRefPath", "m.vfMesh"}}}}},
                {"children", children}
            };
        };

        // Root(part0,x=0) -> A(part1,x=1) -> B(part2,x=2) -> C(part3,x=3), all mesh-bearing.
        json c = chainNode("C", 3.0f, json());
        json b = chainNode("B", 2.0f, c);
        json a = chainNode("A", 1.0f, b);
        json root = chainNode("Root", 0.0f, a);
        json prefab = {{"version", "1.0"}, {"prefab", {{"name", "Deep"}, {"entity", root}}}};
        const json before = prefab;

        // Move ONLY the deepest part (3 = C): old (3,0,0) * translate(0,8,0) => (3,8,0).
        std::map<int, glm::mat4> preview;
        preview[3] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 8.0f, 0.0f));

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 1);

        const json& leaf = prefab["prefab"]["entity"]["children"][0]["children"][0]["children"][0];
        CHECK(leaf["name"] == "C");
        CHECK(readPos(leaf["transform"]).x == doctest::Approx(3.0f));
        CHECK(readPos(leaf["transform"]).y == doctest::Approx(8.0f));

        // Every ancestor (Root, A, B) is untouched.
        const json& aOut = prefab["prefab"]["entity"]["children"][0];
        const json& bOut = aOut["children"][0];
        CHECK(prefab["prefab"]["entity"]["transform"] == before["prefab"]["entity"]["transform"]);
        CHECK(aOut["transform"] == before["prefab"]["entity"]["children"][0]["transform"]);
        CHECK(bOut["transform"]
              == before["prefab"]["entity"]["children"][0]["children"][0]["transform"]);
    }

    // -----------------------------------------------------------------------
    // VK-1433 gap: MULTIPLE moved parts in ONE save. Both targeted parts are
    // rewritten in the single call (written == 2) and the untouched part stays
    // byte-for-byte identical.
    // -----------------------------------------------------------------------
    TEST_CASE("multiple moved parts bake in a single call; untouched parts stay identical")
    {
        auto meshNode = [](const char* name, float x) {
            return json{
                {"name", name},
                {"transform", {{"position", {x, 0.0f, 0.0f}},
                               {"rotation", {0.0f, 0.0f, 0.0f}},
                               {"scale", {1.0f, 1.0f, 1.0f}}}},
                {"components", {{"mesh", {{"meshRefPath", "p.vfMesh"}}}}},
                {"children", json::array()}
            };
        };

        // Root(part0) with three children: part1, part2, part3.
        json root = {
            {"name", "Root"},
            {"transform", {{"position", {0.0f, 0.0f, 0.0f}},
                           {"rotation", {0.0f, 0.0f, 0.0f}},
                           {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {{"mesh", {{"meshRefPath", "r.vfMesh"}}}}},
            {"children", json::array({meshNode("A", 1.0f), meshNode("B", 2.0f), meshNode("C", 3.0f)})}
        };
        json prefab = {{"version", "1.0"}, {"prefab", {{"name", "Multi"}, {"entity", root}}}};
        const json before = prefab;

        // Move parts 1 and 3 in ONE call; leave part 2 alone.
        std::map<int, glm::mat4> preview;
        preview[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // A +1 Y
        preview[3] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // C +1 Z

        const int written = applyPreviewTransforms(prefab, preview, /*skip*/ {});
        CHECK(written == 2);

        const json& a = prefab["prefab"]["entity"]["children"][0];
        const json& b = prefab["prefab"]["entity"]["children"][1];
        const json& cNode = prefab["prefab"]["entity"]["children"][2];

        CHECK(readPos(a["transform"]).x == doctest::Approx(1.0f));
        CHECK(readPos(a["transform"]).y == doctest::Approx(1.0f));
        CHECK(readPos(cNode["transform"]).x == doctest::Approx(3.0f));
        CHECK(readPos(cNode["transform"]).z == doctest::Approx(1.0f));

        // Part 2 (B) and the root are untouched.
        CHECK(b["transform"] == before["prefab"]["entity"]["children"][1]["transform"]);
        CHECK(prefab["prefab"]["entity"]["transform"] == before["prefab"]["entity"]["transform"]);
    }

    // -----------------------------------------------------------------------
    // VK-1433 gap: translate + a MULTI-AXIS rotation (no scale) must recompose to
    // oldLocal * preview. The existing rotate+scale case rotates about a single axis
    // (Y) with uniform scale; this exercises a different XYZ-euler decompose path
    // (a non-trivial rotation about all three axes) to guard the euler-order
    // round-trip more broadly. No scale keeps the product shear-free, so the TRS
    // representation is exact.
    // -----------------------------------------------------------------------
    TEST_CASE("translate + multi-axis rotation recomposes to oldLocal * preview (no scale)")
    {
        json prefab = makeTwoPartPrefab();

        json& childIn = prefab["prefab"]["entity"]["children"][0];
        childIn["transform"]["position"] = json::array({-2.0f, 1.0f, 0.5f});
        childIn["transform"]["rotation"] = json::array({15.0f, -25.0f, 40.0f}); // deg, XYZ
        childIn["transform"]["scale"]    = json::array({1.0f, 1.0f, 1.0f});      // no scale

        const glm::vec3 oldPos(-2.0f, 1.0f, 0.5f);
        const glm::vec3 oldRot(15.0f, -25.0f, 40.0f);
        const glm::vec3 oldScale(1.0f, 1.0f, 1.0f);
        const glm::mat4 oldLocal = math::composeMatrix(oldPos, oldRot, oldScale);

        // Preview: translate + rotate about all three axes, NO scale.
        glm::mat4 preview(1.0f);
        preview = glm::translate(preview, glm::vec3(0.3f, -0.7f, 1.2f));
        preview = glm::rotate(preview, glm::radians(35.0f), glm::vec3(1, 0, 0));
        preview = glm::rotate(preview, glm::radians(-20.0f), glm::vec3(0, 1, 0));
        preview = glm::rotate(preview, glm::radians(50.0f), glm::vec3(0, 0, 1));

        const glm::mat4 expected = oldLocal * preview;

        std::map<int, glm::mat4> previewMap;
        previewMap[1] = preview;
        const int written = applyPreviewTransforms(prefab, previewMap, /*skip*/ {0});
        CHECK(written == 1);

        const json& child = prefab["prefab"]["entity"]["children"][0]["transform"];
        const glm::vec3 wPos = readPos(child);
        const glm::vec3 wRot(child["rotation"][0].get<float>(),
                             child["rotation"][1].get<float>(),
                             child["rotation"][2].get<float>());
        const glm::vec3 wScale(child["scale"][0].get<float>(),
                               child["scale"][1].get<float>(),
                               child["scale"][2].get<float>());
        const glm::mat4 recomposed = math::composeMatrix(wPos, wRot, wScale);

        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                CHECK(recomposed[col][row] == doctest::Approx(expected[col][row]).epsilon(0.0001));
    }
}
