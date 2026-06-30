#include <doctest.h>

// VK-816 (Gameplay Ability System) — hierarchical gameplay tag matching + tag table.
// Pure-logic core under plugins/GameplayAbilitySystem/gas/core, included by relative
// path so these compile straight into Tests.exe with no engine dependency.

#include "../../plugins/GameplayAbilitySystem/gas/core/Tags.hpp"

using namespace gas;

TEST_SUITE("GAS.Tags")
{
    TEST_CASE("a broad query is satisfied by an owned descendant tag")
    {
        TagContainer c;
        c.addTag("Character.Stunned");

        // Query "Character" is satisfied because the owner has a descendant of it.
        CHECK(c.hasTag("Character"));
        // Exact descendant query also matches.
        CHECK(c.hasTag("Character.Stunned"));
    }

    TEST_CASE("owning a parent does NOT satisfy a query for a child")
    {
        TagContainer c;
        c.addTag("Character");

        CHECK(c.hasTag("Character"));            // exact
        CHECK_FALSE(c.hasTag("Character.Stunned")); // parent does not grant child
    }

    TEST_CASE("matching respects dotted boundaries (no substring match)")
    {
        TagContainer c;
        c.addTag("Character");

        CHECK_FALSE(c.hasTag("Char"));   // "Char" is not a dotted prefix of "Character"
        CHECK_FALSE(c.hasTag("Charac")); //  ditto

        TagContainer d;
        d.addTag("CharacterClass.Mage");
        // "Character" must NOT match "CharacterClass.Mage" (boundary is a letter, not '.').
        CHECK_FALSE(d.hasTag("Character"));
    }

    TEST_CASE("exact match and explicit hasTagExact")
    {
        TagContainer c;
        c.addTag("State.Buff.Haste");

        CHECK(c.hasTagExact("State.Buff.Haste"));
        CHECK_FALSE(c.hasTagExact("State.Buff"));  // exact only — no hierarchy
        CHECK(c.hasTag("State.Buff"));             // but hierarchical query matches
        CHECK(c.hasTag("State"));
    }

    TEST_CASE("hasAll / hasAny truth tables")
    {
        TagContainer c;
        c.addTag("Character.CanCast");
        c.addTag("Team.Player");

        CHECK(c.hasAll({"Character", "Team.Player"}));
        CHECK_FALSE(c.hasAll({"Character.CanCast", "Team.Enemy"}));

        CHECK(c.hasAny({"Team.Enemy", "Character"}));
        CHECK_FALSE(c.hasAny({"Team.Enemy", "State.Dead"}));

        // Empty queries: hasAll vacuously true, hasAny false.
        CHECK(c.hasAll({}));
        CHECK_FALSE(c.hasAny({}));
    }

    TEST_CASE("add and remove (exact removal leaves descendants alone)")
    {
        TagContainer c;
        c.addTag("Character.Stunned");
        c.addTag("Character");

        CHECK(c.size() == 2);

        c.removeTag("Character");
        CHECK_FALSE(c.hasTagExact("Character"));
        // The descendant is untouched, so a hierarchical query still matches.
        CHECK(c.hasTag("Character"));
        CHECK(c.hasTagExact("Character.Stunned"));

        c.removeTag("Character.Stunned");
        CHECK_FALSE(c.hasTag("Character"));
        CHECK(c.empty());

        // Empty tags are ignored.
        c.addTag("");
        CHECK(c.empty());
    }

    TEST_CASE("TagTable declares, validates, lists children and merges")
    {
        TagTable a;
        a.declare("Character", "root");
        a.declare("Character.Stunned");
        a.declare("Character.Dead");
        a.declare("Character.Stunned"); // duplicate ignored

        CHECK(a.contains("Character.Stunned"));
        CHECK_FALSE(a.contains("Character.Frozen"));
        CHECK(a.size() == 3);

        // Immediate children of "Character" (one level down only).
        auto kids = a.childrenOf("Character");
        REQUIRE(kids.size() == 2);
        CHECK(kids[0] == "Character.Dead");   // sorted
        CHECK(kids[1] == "Character.Stunned");

        // Top-level (empty prefix) returns dot-free roots.
        auto roots = a.childrenOf("");
        REQUIRE(roots.size() == 1);
        CHECK(roots[0] == "Character");

        // Merge a second table — union, de-duplicated.
        TagTable b;
        b.declare("Character.Frozen");
        b.declare("Ability.Fireball");
        a.merge(b);

        CHECK(a.contains("Character.Frozen"));
        CHECK(a.contains("Ability.Fireball"));
        CHECK(a.childrenOf("Character").size() == 3);
    }
}
