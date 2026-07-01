#include <doctest.h>

#include <vfx/VFXForceConfigLoader.hpp>
#include <vfx/VFXShapeConfigLoader.hpp>
#include <vfx/VFXShapeProperties.hpp>
#include <vfx/VFXTypes.hpp>

#include <string>
#include <utility>

namespace
{
    vfx::VFXNode makeNode(uint32_t id, vfx::VFXNodeType type, const std::string& name)
    {
        vfx::VFXNode node;
        node.id = id;
        node.type = type;
        node.name = name;
        return node;
    }

    void setFloat(vfx::VFXNode& node, const std::string& name, float value, float min = 0.0f, float max = 1.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Float, value, min, max};
    }

    void setInt(vfx::VFXNode& node, const std::string& name, int32_t value, float min = 0.0f, float max = 1.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Int, value, min, max};
    }

    void setBool(vfx::VFXNode& node, const std::string& name, bool value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Bool, value, 0.0f, 1.0f};
    }

    void setString(vfx::VFXNode& node, const std::string& name, const std::string& value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::String, value, 0.0f, 1.0f};
    }

    void setVec3(vfx::VFXNode& node, const std::string& name, const glm::vec3& value,
                 float min = 0.0f, float max = 1.0f)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Vec3, value, min, max};
    }

    vfx::VFXNodeLink makeLink(uint32_t id, uint32_t source, uint32_t target, const std::string& targetPin = "Input")
    {
        vfx::VFXNodeLink link;
        link.id = id;
        link.sourceNodeId = source;
        link.targetNodeId = target;
        link.sourcePin = "Output";
        link.targetPin = targetPin;
        return link;
    }

    vfx::VFXGraph makeShapeGraph(vfx::VFXNode shape)
    {
        vfx::VFXGraph graph;
        graph.nodes.push_back(makeNode(1, vfx::VFXNodeType::Emitter, "Emitter"));
        graph.nodes.push_back(std::move(shape));
        graph.links.push_back(makeLink(1, 2, 1, "Shape"));
        graph.nextNodeId = 3;
        graph.nextLinkId = 2;
        return graph;
    }

    void checkVec3(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == doctest::Approx(expected.x));
        CHECK(actual.y == doctest::Approx(expected.y));
        CHECK(actual.z == doctest::Approx(expected.z));
    }
}

TEST_SUITE("VFXForceShapeProperties")
{
    TEST_CASE("applyShapeTypeProperties rebuilds only active dimension keys")
    {
        vfx::VFXNode node = makeNode(1, vfx::VFXNodeType::Shape, "Shape");
        setString(node, "emitFrom", "Surface");
        setBool(node, "randomDirection", true);

        vfx::applyShapeTypeProperties(node, vfx::ShapeType::Cone);
        REQUIRE(node.properties.count("radius") == 1);
        REQUIRE(node.properties.count("height") == 1);
        REQUIRE(node.properties.count("angle") == 1);

        vfx::applyShapeTypeProperties(node, vfx::ShapeType::Sphere);

        REQUIRE(node.properties.count("shapeType") == 1);
        CHECK(std::get<std::string>(node.properties.at("shapeType").value) == "Sphere");
        REQUIRE(node.properties.count("radius") == 1);
        CHECK(std::get<float>(node.properties.at("radius").value) == doctest::Approx(vfx::ShapeDefaults::SPHERE_RADIUS));
        CHECK(node.properties.count("height") == 0);
        CHECK(node.properties.count("angle") == 0);
        CHECK(node.properties.count("halfExtents") == 0);
        CHECK(node.properties.count("majorRadius") == 0);
        CHECK(node.properties.count("minorRadius") == 0);
        CHECK(std::get<std::string>(node.properties.at("emitFrom").value) == "Surface");
        CHECK(std::get<bool>(node.properties.at("randomDirection").value));
    }

    TEST_CASE("force loader extracts authored chain properties in graph order")
    {
        vfx::VFXGraph graph;
        graph.nodes.push_back(makeNode(1, vfx::VFXNodeType::Emitter, "Emitter"));

        vfx::VFXNode gravity = makeNode(2, vfx::VFXNodeType::ForceGravity, "Gravity");
        setVec3(gravity, "direction", glm::vec3(0.0f, -2.0f, 0.0f));
        setFloat(gravity, "strength", 12.0f);
        setBool(gravity, "localSpace", true);
        graph.nodes.push_back(gravity);

        vfx::VFXNode wind = makeNode(3, vfx::VFXNodeType::ForceWind, "Wind");
        setVec3(wind, "direction", glm::vec3(2.0f, 0.0f, 0.0f));
        setFloat(wind, "strength", 3.0f);
        setFloat(wind, "noiseStrength", 0.4f);
        setFloat(wind, "noiseFrequency", 2.5f);
        setBool(wind, "localSpace", false);
        graph.nodes.push_back(wind);

        vfx::VFXNode turbulence = makeNode(4, vfx::VFXNodeType::ForceTurbulence, "Turbulence");
        setFloat(turbulence, "strength", 5.0f);
        setFloat(turbulence, "frequency", 6.0f);
        setFloat(turbulence, "scrollSpeed", 7.0f);
        setInt(turbulence, "octaves", 3, 1.0f, 4.0f);
        setBool(turbulence, "localSpace", true);
        graph.nodes.push_back(turbulence);

        vfx::VFXNode vortex = makeNode(5, vfx::VFXNodeType::ForceVortex, "Vortex");
        setVec3(vortex, "axis", glm::vec3(0.0f, 0.0f, 1.0f));
        setVec3(vortex, "center", glm::vec3(1.0f, 2.0f, 3.0f));
        setFloat(vortex, "strength", 8.0f);
        setFloat(vortex, "radialPull", -2.0f);
        setBool(vortex, "localSpace", true);
        graph.nodes.push_back(vortex);

        graph.nodes.push_back(makeNode(6, vfx::VFXNodeType::OutSystem, "Output"));
        graph.nodes.push_back(makeNode(7, vfx::VFXNodeType::Shape, "Shape"));
        graph.links.push_back(makeLink(1, 1, 7, "Shape"));
        graph.links.push_back(makeLink(2, 1, 2));
        graph.links.push_back(makeLink(3, 2, 3));
        graph.links.push_back(makeLink(4, 3, 4));
        graph.links.push_back(makeLink(5, 4, 5));
        graph.links.push_back(makeLink(6, 5, 6));
        graph.nextNodeId = 8;
        graph.nextLinkId = 7;

        const vfx::VFXForceChain chain = vfx::VFXForceConfigLoader::fromGraph(graph);
        REQUIRE(chain.forces.size() == 4);

        const auto& gravityConfig = std::get<vfx::GravityForceConfig>(chain.forces[0]);
        checkVec3(gravityConfig.direction, glm::vec3(0.0f, -2.0f, 0.0f));
        CHECK(gravityConfig.strength == doctest::Approx(12.0f));
        CHECK(gravityConfig.space == vfx::ForceSpace::Local);

        const auto& windConfig = std::get<vfx::WindForceConfig>(chain.forces[1]);
        checkVec3(windConfig.direction, glm::vec3(2.0f, 0.0f, 0.0f));
        CHECK(windConfig.strength == doctest::Approx(3.0f));
        CHECK(windConfig.noiseStrength == doctest::Approx(0.4f));
        CHECK(windConfig.noiseFrequency == doctest::Approx(2.5f));
        CHECK(windConfig.space == vfx::ForceSpace::World);

        const auto& turbulenceConfig = std::get<vfx::TurbulenceForceConfig>(chain.forces[2]);
        CHECK(turbulenceConfig.strength == doctest::Approx(5.0f));
        CHECK(turbulenceConfig.frequency == doctest::Approx(6.0f));
        CHECK(turbulenceConfig.scrollSpeed == doctest::Approx(7.0f));
        CHECK(turbulenceConfig.octaves == 3);
        CHECK(turbulenceConfig.space == vfx::ForceSpace::Local);

        const auto& vortexConfig = std::get<vfx::VortexForceConfig>(chain.forces[3]);
        checkVec3(vortexConfig.axis, glm::vec3(0.0f, 0.0f, 1.0f));
        checkVec3(vortexConfig.center, glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(vortexConfig.strength == doctest::Approx(8.0f));
        CHECK(vortexConfig.radialPull == doctest::Approx(-2.0f));
        CHECK(vortexConfig.space == vfx::ForceSpace::Local);
    }

    TEST_CASE("shape loader extracts active shape dimensions from Shape pin")
    {
        vfx::VFXNode shape = makeNode(2, vfx::VFXNodeType::Shape, "Shape");
        setString(shape, "emitFrom", "Surface");
        setBool(shape, "randomDirection", true);

        SUBCASE("Sphere")
        {
            setString(shape, "shapeType", "Sphere");
            setFloat(shape, "radius", 4.0f);
            const vfx::ShapeConfig config = vfx::VFXShapeConfigLoader::fromGraph(makeShapeGraph(shape));
            CHECK(config.type == vfx::ShapeType::Sphere);
            CHECK(config.emitFrom == vfx::EmitFrom::Surface);
            CHECK(config.randomDirection);
            CHECK(config.dimensions.x == doctest::Approx(4.0f));
        }

        SUBCASE("Cone")
        {
            setString(shape, "shapeType", "Cone");
            setFloat(shape, "radius", 2.0f);
            setFloat(shape, "height", 5.0f);
            setFloat(shape, "angle", 0.75f);
            const vfx::ShapeConfig config = vfx::VFXShapeConfigLoader::fromGraph(makeShapeGraph(shape));
            CHECK(config.type == vfx::ShapeType::Cone);
            CHECK(config.dimensions.x == doctest::Approx(2.0f));
            CHECK(config.dimensions.y == doctest::Approx(5.0f));
            CHECK(config.dimensions.z == doctest::Approx(0.75f));
        }

        SUBCASE("Box")
        {
            setString(shape, "shapeType", "Box");
            setVec3(shape, "halfExtents", glm::vec3(1.0f, 2.0f, 3.0f));
            const vfx::ShapeConfig config = vfx::VFXShapeConfigLoader::fromGraph(makeShapeGraph(shape));
            CHECK(config.type == vfx::ShapeType::Box);
            CHECK(config.dimensions.x == doctest::Approx(1.0f));
            CHECK(config.dimensions.y == doctest::Approx(2.0f));
            CHECK(config.dimensions.z == doctest::Approx(3.0f));
        }

        SUBCASE("Torus")
        {
            setString(shape, "shapeType", "Torus");
            setFloat(shape, "majorRadius", 6.0f);
            setFloat(shape, "minorRadius", 0.5f);
            const vfx::ShapeConfig config = vfx::VFXShapeConfigLoader::fromGraph(makeShapeGraph(shape));
            CHECK(config.type == vfx::ShapeType::Torus);
            CHECK(config.dimensions.x == doctest::Approx(6.0f));
            CHECK(config.dimensions.y == doctest::Approx(0.5f));
        }
    }
}
