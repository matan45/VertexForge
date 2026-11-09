#include <doctest.h>
#include <behaviortree/ScriptTaskScan.hpp>

#include <optional>
#include <string>
#include <string_view>

// ============================================================
// VK-1660: behaviortree::scanScriptTask - recognising a behavior-tree ScriptTask from its source.
// ============================================================

namespace
{
    namespace bt = behaviortree;

    // The plan's example ScriptTask (assets/scripts/game/examples/MeleeAttack.mt), verbatim.
    constexpr std::string_view meleeAttackSource = R"mt(// MeleeAttack - Example of a behavior tree ScriptTask (VK-1660).
//
// A ScriptTask node runs this class through the behavior tree, not through
// onUpdate: the tree calls onStart once, then tick(deltaTime) every frame
// while the node runs ("running", "success", anything else = failure),
// onAbort() when a condition or a higher-priority branch interrupts it, and
// onEnd(success) when tick finished it. One instance per entity lives for the
// whole Play session, so per-attack state is reset in onAbort and onEnd.
//
// Blackboard: target (Entity) = who to hit, hits (Int) = hits landed so far.
// Guard the node with a BlackboardCondition on the distance to the target
// (abortMode Both): the tree stops this task when the target leaves range.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Blackboard.mt";
import * from "../../lib/engine/Entity.mt";
import * from "../../lib/engine/Navmesh.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class MeleeAttack extends Behaviour {
    private float swingInterval = 1.0;
    private float cooldown = 0.0;
    private bool engaged = false;

    public constructor() : super() {
    }

    public function tick(float deltaTime): string {
        int selfId = this.entityId();
        int target = Blackboard::getEntity(selfId, "target");
        if (!Entity::isValid(target)) {
            return "failure";
        }
        if (!this.engaged) {
            this.engaged = true;
            this.cooldown = 0.0;
            Navmesh::stopAgent(selfId);
        }
        Vec3f selfPos = this.transform().worldPosition();
        Vec3f targetPos = Entity::getWorldPosition(target);
        this.transform().lookAt(new Vec3f(targetPos.x, selfPos.y, targetPos.z));

        this.cooldown = this.cooldown - deltaTime;
        if (this.cooldown <= 0.0) {
            this.cooldown = this.swingInterval;
            int hits = Blackboard::getInt(selfId, "hits") + 1;
            Blackboard::setInt(selfId, "hits", hits);
            this.log("hits " + Entity::getName(target) + " (" + parsePrimitive(hits) + ")");
        }
        return "running";
    }

    public function onAbort(): void {
        this.engaged = false;
    }

    public function onEnd(bool success): void {
        this.engaged = false;
    }
}
)mt";

    bool isScriptTask(std::string_view source)
    {
        return bt::scanScriptTask(source).has_value();
    }
}

TEST_SUITE("BTScriptTaskScan")
{
    TEST_CASE("the MeleeAttack example is a ScriptTask with onAbort and onEnd")
    {
        const std::optional<bt::ScriptTaskInfo> info = bt::scanScriptTask(meleeAttackSource);
        REQUIRE(info.has_value());
        CHECK(info->className == "MeleeAttack");
        CHECK(info->hasOnStart == false); // "onStart" appears only in the header comment
        CHECK(info->hasOnAbort);
        CHECK(info->hasOnEnd);

        CHECK(info->summary.starts_with("MeleeAttack - Example of a behavior tree ScriptTask (VK-1660).\n\n"
                                        "A ScriptTask node runs this class through the behavior tree, not through\n"
                                        "onUpdate:"));
        CHECK(info->summary.ends_with("\n(abortMode Both): the tree stops this task when the target leaves range."));
        CHECK(info->summary.find("import") == std::string::npos);
        CHECK(info->summary.find("//") == std::string::npos);
    }

    TEST_CASE("an @Override onStart is reported")
    {
        const auto info = bt::scanScriptTask(R"mt(
@Script
public class Opener extends Behaviour {
    @Override
    public function onStart(): void {
    }
    public function tick(float dt): string { return "success"; }
}
)mt");
        REQUIRE(info.has_value());
        CHECK(info->className == "Opener");
        CHECK(info->hasOnStart);
        CHECK_FALSE(info->hasOnAbort);
        CHECK_FALSE(info->hasOnEnd);
        CHECK(info->summary.empty());
    }

    TEST_CASE("@Script matches with or without public, across lines")
    {
        const auto plain = bt::scanScriptTask("@Script class T { public function tick(float dt): string { return \"\"; } }");
        REQUIRE(plain.has_value());
        CHECK(plain->className == "T");

        const auto spread = bt::scanScriptTask("@Script\n\n  public\n  class\n  Spread\n"
                                               "{ function tick ( float dt ): string { return \"\"; } }");
        REQUIRE(spread.has_value());
        CHECK(spread->className == "Spread");
    }

    TEST_CASE("files that are not ScriptTasks")
    {
        SUBCASE("no @Script annotation")
        {
            CHECK_FALSE(isScriptTask("public class Plain { public function tick(float dt): string { return \"running\"; } }"));
        }
        SUBCASE("an @Script class without tick")
        {
            CHECK_FALSE(isScriptTask("@Script\npublic class Mover extends Behaviour {\n"
                                     "    public function onUpdate(float dt): void {}\n}\n"));
        }
        SUBCASE("tick takes no float")
        {
            CHECK_FALSE(isScriptTask("@Script\nclass T { public function tick(int frames): string { return \"\"; } }"));
            CHECK_FALSE(isScriptTask("@Script\nclass T { public function tick(floatValue v): string { return \"\"; } }"));
            CHECK_FALSE(isScriptTask("@Script\nclass T { public function tick(): string { return \"\"; } }"));
        }
        SUBCASE("tick only inside comments and strings")
        {
            CHECK_FALSE(isScriptTask(R"mt(
@Script
public class Fake extends Behaviour {
    // public function tick(float dt): string {
    /* public function tick(float dt): string { */
    public function onStart(): void {
        string s = "public function tick(float dt): string {";
    }
}
)mt"));
        }
        SUBCASE("annotation spellings ScriptingAdapter does not match")
        {
            CHECK_FALSE(isScriptTask("@ScriptTask\nclass T { public function tick(float dt): string { return \"\"; } }"));
            CHECK_FALSE(isScriptTask("@Script private class T { public function tick(float dt): string { return \"\"; } }"));
        }
    }

    TEST_CASE("only the first @Script class counts - the one ScriptingAdapter instantiates")
    {
        CHECK_FALSE(isScriptTask(R"mt(
@Script
public class Helper {
    public function onStart(): void {}
}

@Script
public class Real {
    public function tick(float dt): string { return "running"; }
}
)mt"));

        // Commented-out declarations are not the first class.
        const auto info = bt::scanScriptTask(R"mt(
// @Script public class Commented { }
/* @Script
public class AlsoCommented { public function tick(float dt): string { return ""; } } */
@Script
public class Real {
    public function tick(float dt): string { return "running"; }
}
)mt");
        REQUIRE(info.has_value());
        CHECK(info->className == "Real");
    }

    TEST_CASE("only members directly in the class body are seen")
    {
        CHECK_FALSE(isScriptTask(R"mt(
@Script
public class Nested {
    public function onStart(): void {
        if (true) {
            function tick(float dt): string { return "x"; }
        }
    }
}
)mt"));

        // Braces and quotes inside (interpolated) strings do not upset the body depth.
        const auto info = bt::scanScriptTask(R"mt(
@Script
public class Talker extends Behaviour {
    public function onStart(): void {
        this.log($"opened {this.label("}")} with {count} \"quoted\" }");
        string s = "function onAbort(";
    }
    public function tick(float dt): string {
        return "running";
    }
}
public class After {
    public function onEnd(bool ok): void {}
}
)mt");
        REQUIRE(info.has_value());
        CHECK(info->className == "Talker");
        CHECK(info->hasOnStart);
        CHECK_FALSE(info->hasOnAbort); // only inside a string
        CHECK_FALSE(info->hasOnEnd);   // declared by another class
    }

    TEST_CASE("summary: the leading // block only, CRLF and BOM tolerant")
    {
        const std::string source = "\xEF\xBB\xBF\r\n"
                                   "  // Patrol helper.  \r\n"
                                   "///   Walks the route.\r\n"
                                   "//no space\r\n"
                                   "//\r\n"
                                   "\r\n"
                                   "// not part of the summary\r\n"
                                   "@Script\r\n"
                                   "public class Patrol {\r\n"
                                   "    public function tick(float dt): string { return \"running\"; }\r\n"
                                   "}\r\n";
        const auto info = bt::scanScriptTask(source);
        REQUIRE(info.has_value());
        CHECK(info->className == "Patrol");
        CHECK(info->summary == "Patrol helper.\n  Walks the route.\nno space");
    }
}
