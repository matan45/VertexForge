#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include "protocol/ArgReader.hpp"
#include "protocol/ResourceRegistry.hpp"
#include "tools/MTypeApiDoc.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace doc = mcp::tools::mtypedoc;

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    bool hasMember(const doc::ApiType& type, const std::string& member)
    {
        return std::find(type.members.begin(), type.members.end(), member) != type.members.end();
    }

    bool anyMemberContains(const doc::ApiType& type, const std::string& needle)
    {
        return std::any_of(type.members.begin(), type.members.end(),
                           [&](const std::string& member) { return contains(member, needle); });
    }

    const doc::ApiType* findType(const std::vector<doc::ApiType>& types, const std::string& name)
    {
        for (const doc::ApiType& type : types)
        {
            if (type.name == name)
            {
                return &type;
            }
        }
        return nullptr;
    }

    std::optional<fs::path> scriptsRoot()
    {
        auto root = repo_scan::findRepoRoot();
        if (!root.has_value())
        {
            return std::nullopt;
        }
        return *root / "assets" / "scripts";
    }

    std::vector<doc::ApiType> parseRepoFile(const fs::path& scripts, const char* relative)
    {
        const fs::path file = scripts / "lib" / relative;
        REQUIRE_MESSAGE(fs::exists(file), "missing " << file.string());
        return doc::parseApi(repo_scan::readFile(file));
    }
}

TEST_SUITE("mType API doc")
{
    TEST_CASE("stripComments removes line and block comments but keeps strings and line breaks")
    {
        const std::string source =
            "int a = 1; // public class Hidden {\n"
            "/* public function gone(): void;\n   still a comment */ string url = \"http://x/*y*/\";\n";
        const std::string stripped = doc::stripComments(source);

        CHECK_FALSE(contains(stripped, "Hidden"));
        CHECK_FALSE(contains(stripped, "gone"));
        CHECK_FALSE(contains(stripped, "still"));
        CHECK(contains(stripped, "int a = 1;"));
        CHECK(contains(stripped, "\"http://x/*y*/\""));
        CHECK(std::count(stripped.begin(), stripped.end(), '\n') == 3);
    }

    TEST_CASE("commented-out code (Behaviour.mt header style) is not parsed")
    {
        const std::string source =
            "//   @Script\n"
            "//   public class PlayerMovement extends Behaviour {\n"
            "//       public function onUpdate(float deltaTime): void { }\n"
            "//   }\n"
            "/* public class Block { public function f(): void; } */\n"
            "public class Real {\n"
            "    public function run(): void {\n"
            "        // public function notAMember(): void { }\n"
            "    }\n"
            "}\n";
        const std::vector<doc::ApiType> types = doc::parseApi(source);

        REQUIRE(types.size() == 1);
        CHECK(types[0].name == "Real");
        CHECK(types[0].members == std::vector<std::string>{"public function run(): void;"});
    }

    TEST_CASE("multi-line signatures are joined and normalised")
    {
        const std::string source =
            "public class EQS {\n"
            "    public static function submitQuery(string queryName, int querierEntityId,\n"
            "                                        Vec3f position, Vec3f forward): int {\n"
            "        return 0;\n"
            "    }\n"
            "    public static function calculate(\n"
            "        Vec3f a,\n"
            "        float b\n"
            "    ): Vec3f {\n"
            "        return a;\n"
            "    }\n"
            "}\n";
        const std::vector<doc::ApiType> types = doc::parseApi(source);

        REQUIRE(types.size() == 1);
        CHECK(hasMember(types[0], "public static function submitQuery(string queryName, int querierEntityId, "
                                  "Vec3f position, Vec3f forward): int;"));
        CHECK(hasMember(types[0], "public static function calculate(Vec3f a, float b): Vec3f;"));
        CHECK(types[0].members.size() == 2);  // bodies (return ...) are skipped
    }

    TEST_CASE("generic methods, constructors with super() and annotations")
    {
        const std::string source =
            "@Script\n"
            "public class Mover extends Behaviour {\n"
            "    public constructor() : super() {\n"
            "    }\n"
            "    public static function <T> serialize(T obj): string { return \"\"; }\n"
            "    @Override\n"
            "    public function onUpdate(float deltaTime): void { }\n"
            "}\n";
        const std::vector<doc::ApiType> types = doc::parseApi(source);

        REQUIRE(types.size() == 1);
        CHECK(types[0].header == "public class Mover extends Behaviour");
        CHECK_FALSE(types[0].isInterface);
        CHECK(hasMember(types[0], "public constructor();"));
        CHECK(hasMember(types[0], "public static function <T> serialize(T obj): string;"));
        CHECK(hasMember(types[0], "public function onUpdate(float deltaTime): void;"));
    }

    TEST_CASE("interface methods end in ';' and need no modifier")
    {
        const std::string source =
            "interface ICollisionListener {\n"
            "    // Called when this entity starts colliding\n"
            "    function onCollisionEnter(int otherEntityId): void;\n"
            "    function onCollisionExit(int otherEntityId): void;\n"
            "}\n"
            "interface Collection<T> extends Iterable<T> {\n"
            "    function add(T item): bool;\n"
            "}\n";
        const std::vector<doc::ApiType> types = doc::parseApi(source);

        REQUIRE(types.size() == 2);
        CHECK(types[0].isInterface);
        CHECK(types[0].header == "interface ICollisionListener");
        CHECK(types[0].members == std::vector<std::string>{
            "function onCollisionEnter(int otherEntityId): void;",
            "function onCollisionExit(int otherEntityId): void;"
        });
        CHECK(types[1].name == "Collection");
        CHECK(types[1].header == "interface Collection<T> extends Iterable<T>");
        CHECK(hasMember(types[1], "function add(T item): bool;"));
    }

    TEST_CASE("Key-style constants and fields are kept; private and protected members are skipped")
    {
        const std::string source =
            "public value class Sample {\n"
            "    public static final int W = 87;\n"
            "    public static final string NAME = \"a{b};c\";\n"
            "    public float x;\n"
            "    private static final float RAD_TO_DEG = 57.29;\n"
            "    private int hidden = -1;\n"
            "    protected function inner(): void { }\n"
            "    private function helper(): void { }\n"
            "    int packageField;\n"
            "    public function after(): int { return 1; }\n"
            "}\n"
            "private class Secret {\n"
            "    public function leak(): void { }\n"
            "}\n";
        const std::vector<doc::ApiType> types = doc::parseApi(source);

        REQUIRE(types.size() == 1);
        CHECK(types[0].header == "public value class Sample");
        CHECK(types[0].members == std::vector<std::string>{
            "public static final int W = 87;",
            "public static final string NAME = \"a{b};c\";",
            "public float x;",
            "public function after(): int;"
        });
    }

    TEST_CASE("extractApi renders one mtype block per type")
    {
        const std::string markdown = doc::extractApi(
            "public class A {\n    public function f(): void { }\n}\n");
        CHECK(markdown == "```mtype\npublic class A {\n    public function f(): void;\n}\n```\n\n");
        CHECK(doc::extractApi("// nothing\n") == "_No public declarations._\n\n");
    }

    TEST_CASE("module ids are validated")
    {
        CHECK(doc::isValidModuleId("engine/Physics"));
        CHECK(doc::isValidModuleId("engine/oop/Behaviour"));
        CHECK(doc::isValidModuleId("math/Quaternion"));
        CHECK_FALSE(doc::isValidModuleId(""));
        CHECK_FALSE(doc::isValidModuleId("../engine/Physics"));
        CHECK_FALSE(doc::isValidModuleId("/engine/Physics"));
        CHECK_FALSE(doc::isValidModuleId("engine/"));
        CHECK_FALSE(doc::isValidModuleId("engine//Physics"));
        CHECK_FALSE(doc::isValidModuleId("engine\\Physics"));
        CHECK_FALSE(doc::isValidModuleId("C:/Windows/x"));
    }

    TEST_CASE("the primer embeds game/examples/PlayerMovement.mt verbatim")
    {
        auto scripts = scriptsRoot();
        REQUIRE_MESSAGE(scripts.has_value(), "repo root not found from Tests.exe location or CWD");

        std::string example = repo_scan::readFile(*scripts / "game" / "examples" / "PlayerMovement.mt");
        REQUIRE_FALSE(example.empty());
        example.erase(std::remove(example.begin(), example.end(), '\r'), example.end());
        while (!example.empty() && example.back() == '\n')
        {
            example.pop_back();
        }
        CHECK(contains(doc::primer(), example));
        CHECK(contains(doc::primer(), "public constructor() : super() {"));

        // Section 4 quotes game/examples/Projectile.mt verbatim as well.
        std::string projectile = repo_scan::readFile(*scripts / "game" / "examples" / "Projectile.mt");
        REQUIRE_FALSE(projectile.empty());
        projectile.erase(std::remove(projectile.begin(), projectile.end(), '\r'), projectile.end());
        while (!projectile.empty() && projectile.back() == '\n')
        {
            projectile.pop_back();
        }
        CHECK(contains(doc::primer(), projectile));
    }

    TEST_CASE("extractApi over the real engine scripts")
    {
        auto scripts = scriptsRoot();
        REQUIRE_MESSAGE(scripts.has_value(), "repo root not found from Tests.exe location or CWD");

        SUBCASE("Behaviour: commented header code skipped, private state hidden")
        {
            const auto types = parseRepoFile(*scripts, "engine/oop/Behaviour.mt");
            REQUIRE(types.size() == 1);
            CHECK(types[0].header == "public class Behaviour");
            CHECK(hasMember(types[0], "public constructor();"));
            CHECK(hasMember(types[0], "public function onStart(): void;"));
            CHECK(hasMember(types[0], "public function onUpdate(float deltaTime): void;"));
            CHECK(hasMember(types[0], "public function onDestroy(): void;"));
            CHECK(hasMember(types[0], "public function transform(): Transform;"));
            CHECK(hasMember(types[0], "public function log(string message): void;"));
            CHECK_FALSE(anyMemberContains(types[0], "vfEntityId"));
            CHECK(findType(types, "PlayerMovement") == nullptr);
        }

        SUBCASE("Transform: public final field, static helper, private constants hidden")
        {
            const auto types = parseRepoFile(*scripts, "engine/oop/Transform.mt");
            const doc::ApiType* transform = findType(types, "Transform");
            REQUIRE(transform != nullptr);
            CHECK(hasMember(*transform, "public final int entityId;"));
            CHECK(hasMember(*transform, "public constructor(int entityId);"));
            CHECK(hasMember(*transform, "public function translate(Vec3f delta): void;"));
            CHECK(hasMember(*transform, "public function forward(): Vec3f;"));
            CHECK(hasMember(*transform, "public static function quaternionFromEulerDegrees(Vec3f eulerDegrees): Quaternion;"));
            CHECK_FALSE(anyMemberContains(*transform, "RAD_TO_DEG"));
        }

        SUBCASE("Input (comment examples with code) and Key constants")
        {
            const auto input = parseRepoFile(*scripts, "engine/Input.mt");
            REQUIRE(input.size() == 1);
            CHECK(hasMember(input[0], "public static function isKeyDown(int keyCode): bool;"));
            CHECK_FALSE(anyMemberContains(input[0], "move forward"));

            const auto key = parseRepoFile(*scripts, "engine/Key.mt");
            REQUIRE(key.size() == 1);
            CHECK(hasMember(key[0], "public static final int W = 87;"));
            CHECK(hasMember(key[0], "public static final int SPACE = 32;"));
        }

        SUBCASE("multi-line signatures in EQS and HandIK")
        {
            const auto eqs = parseRepoFile(*scripts, "engine/EQS.mt");
            const doc::ApiType* eqsType = findType(eqs, "EQS");
            REQUIRE(eqsType != nullptr);
            CHECK(hasMember(*eqsType, "public static function submitQuery(string queryName, int querierEntityId, "
                                      "Vec3f position, Vec3f forward): int;"));

            const auto handIk = parseRepoFile(*scripts, "engine/HandIK.mt");
            REQUIRE(findType(handIk, "HandIKResult") != nullptr);
            const doc::ApiType* hand = findType(handIk, "HandIK");
            REQUIRE(hand != nullptr);
            CHECK(hasMember(*hand, "public static function calculateHandTarget(Vec3f handWorldPos, "
                                   "Vec3f shoulderWorldPos, Vec3f targetWorldPos, Quaternion targetRotation, "
                                   "float maxReachDistance, float gripRotationBlend): HandIKResult;"));
        }

        SUBCASE("interfaces and value classes")
        {
            const auto listener = parseRepoFile(*scripts, "engine/ICollisionListener.mt");
            REQUIRE(listener.size() == 1);
            CHECK(listener[0].isInterface);
            CHECK(hasMember(listener[0], "function onCollisionEnter(int otherEntityId): void;"));

            const auto vec = parseRepoFile(*scripts, "math/Vec3f.mt");
            REQUIRE(vec.size() == 1);
            CHECK(vec[0].header == "public value class Vec3f");
            CHECK(hasMember(vec[0], "public float x;"));
            CHECK(hasMember(vec[0], "public static function zero(): Vec3f;"));
            CHECK(hasMember(vec[0], "public function normalize(): Vec3f;"));
        }

        SUBCASE("generic static methods in core/json/Json.mt")
        {
            const auto json = parseRepoFile(*scripts, "core/json/Json.mt");
            REQUIRE(json.size() == 1);
            CHECK(hasMember(json[0], "public static function <T> serialize(T obj): string;"));
        }
    }

    TEST_CASE("buildMTypeApiDoc and buildModuleDoc over the real scripts root")
    {
        auto scripts = scriptsRoot();
        REQUIRE_MESSAGE(scripts.has_value(), "repo root not found from Tests.exe location or CWD");

        const std::string full = doc::buildMTypeApiDoc(*scripts);
        CHECK(full.rfind(doc::primer(), 0) == 0);
        CHECK(contains(full, "## engine/oop/Behaviour"));
        CHECK(contains(full, "`import * from \"../lib/engine/oop/Behaviour.mt\";`"));
        CHECK(contains(full, "public function onUpdate(float deltaTime): void;"));
        CHECK(contains(full, "- `engine/Physics`: Physics: "));
        CHECK_FALSE(contains(full, "- `engine/oop/Behaviour`:"));  // core modules are not repeated
        CHECK_FALSE(contains(full, "signatures unavailable"));

        const std::string physics = doc::buildModuleDoc(*scripts, "engine/Physics");
        CHECK(contains(physics, "# mType module `engine/Physics`"));
        CHECK(contains(physics, "public class Physics"));
        CHECK(doc::buildModuleDoc(*scripts, "engine/Physics.mt") == physics);

        CHECK_THROWS_AS(doc::buildModuleDoc(*scripts, "../game/examples/PlayerMovement"), mcp::ArgError);
        CHECK_THROWS_AS(doc::buildModuleDoc(*scripts, "C:/Windows/win"), mcp::ArgError);
        CHECK_THROWS_AS(doc::buildModuleDoc(*scripts, "engine/DoesNotExist"), mcp::ResourceNotFound);
    }

    TEST_CASE("buildMTypeApiDoc without scripts/lib still serves the primer")
    {
        const fs::path root = fs::temp_directory_path() / "VertexForge_MTypeDoc_NoLib";
        fs::remove_all(root);
        fs::create_directories(root);

        const std::string text = doc::buildMTypeApiDoc(root);
        CHECK(text.rfind(doc::primer(), 0) == 0);
        CHECK(contains(text, "signatures unavailable: project has no scripts/lib"));
        CHECK_THROWS_AS(doc::buildModuleDoc(root, "engine/Physics"), mcp::ResourceNotFound);

        fs::remove_all(root);
    }
}
