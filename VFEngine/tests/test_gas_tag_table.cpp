#include <doctest.h>
#include "../../plugins/GameplayAbilitySystem/gas/core/TagTableValidation.hpp"
#include <string>

// ============================================================
// VK-816 Phase 6 — pure tag-table editor helpers (engine-free):
// duplicate detection + hierarchical rename cascade. These power
// the Tag Table editor window's validation + rename assistance.
// ============================================================

TEST_CASE("gas::findDuplicateTags reports tags declared more than once")
{
    gas::TagTable t;
    t.definitions().push_back({"Character", ""});
    t.definitions().push_back({"Character.Stunned", ""});
    t.definitions().push_back({"Character", "duplicate"});
    t.definitions().push_back({"Ability", ""});

    const auto dups = gas::findDuplicateTags(t);
    REQUIRE(dups.size() == 1);
    CHECK(dups[0] == "Character");

    gas::TagTable clean;
    clean.declare("A");
    clean.declare("A.B");
    CHECK(gas::findDuplicateTags(clean).empty());
}

TEST_CASE("gas::renameTagCascade renames a tag and all hierarchical descendants")
{
    gas::TagTable t;
    t.declare("Character");
    t.declare("Character.Stunned");
    t.declare("Character.Combat.Dead");
    t.declare("Characters.NotAChild");   // boundary: must NOT be renamed
    t.declare("Ability");

    const int changed = gas::renameTagCascade(t, "Character", "Hero");
    CHECK(changed == 3);
    CHECK(t.contains("Hero"));
    CHECK(t.contains("Hero.Stunned"));
    CHECK(t.contains("Hero.Combat.Dead"));
    CHECK_FALSE(t.contains("Character"));
    CHECK(t.contains("Characters.NotAChild"));   // untouched (boundary-aware)
    CHECK(t.contains("Ability"));                // untouched

    SUBCASE("no-op cases")
    {
        CHECK(gas::renameTagCascade(t, "", "X") == 0);
        CHECK(gas::renameTagCascade(t, "Hero", "Hero") == 0);
        CHECK(gas::renameTagCascade(t, "Missing", "X") == 0);
    }
}
