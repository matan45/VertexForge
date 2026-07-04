#include <doctest.h>

#include <render/vfx/lut/VFXLUTBaker.hpp>
#include <render/vfx/compute/GPUVFXTypes.hpp>
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXModifierConfigLoader.hpp>
#include <vfx/VFXSpeedRemap.hpp>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXCurveTypes.hpp>

#include <glm/glm.hpp>

#include <string>
#include <variant>

// VK-1473 / VK-1474: LUT channel layout (5 -> 9), by-speed / over-trail bake, the pure
// speed->t remap, and the modifier loader round-trip for the two new node types.

using render::vfx::VFXLUTBaker;
using render::vfx::LUTBakeResult;
namespace GPUC = render::vfx::GPUVFXConstants;
namespace LUTCh = render::vfx::LUTChannel;
namespace LUTFl = render::vfx::LUTFlags;

namespace
{
    // data[channel*RES + i] holds the value at t = i/(RES-1). Sample the exact stored texel.
    glm::vec4 texel(const LUTBakeResult& r, uint32_t channel, uint32_t i)
    {
        const uint32_t idx = channel * GPUC::LUT_RESOLUTION + i;
        REQUIRE(idx < r.data.size());
        return r.data[idx];
    }

    constexpr uint32_t LAST = 63; // LUT_RESOLUTION - 1

    vfx::VFXNode makeNode(uint32_t id, vfx::VFXNodeType type, const std::string& name)
    {
        vfx::VFXNode node;
        node.id = id;
        node.type = type;
        node.name = name;
        return node;
    }

    void setFloat(vfx::VFXNode& node, const std::string& name, float value)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Float, value, 0.0f, 100.0f};
    }
    void setCurve(vfx::VFXNode& node, const std::string& name, const vfx::VFXCurve& c)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Curve, c, 0.0f, 10.0f};
    }
    void setGradient(vfx::VFXNode& node, const std::string& name, const vfx::VFXGradient& g)
    {
        node.properties[name] = vfx::VFXProperty{name, vfx::VFXPropertyType::Gradient, g, 0.0f, 1.0f};
    }
    vfx::VFXNodeLink makeLink(uint32_t id, uint32_t source, uint32_t target)
    {
        vfx::VFXNodeLink link;
        link.id = id;
        link.sourceNodeId = source;
        link.targetNodeId = target;
        link.sourcePin = "Output";
        link.targetPin = "Input";
        return link;
    }
}

TEST_SUITE("VFXLUTChannelLayout")
{
    TEST_CASE("channel count and flag bits are derived and consistent")
    {
        CHECK(LUTCh::Count == 9u);
        CHECK(GPUC::LUT_CHANNELS == 9u);
        CHECK(GPUC::LUT_RESOLUTION == 64u);

        // Existing bits preserved, new bits appended.
        CHECK(LUTFl::Color == (1u << LUTCh::Color));
        CHECK(LUTFl::Size == (1u << LUTCh::Size));
        CHECK(LUTFl::Speed == (1u << LUTCh::Speed));
        CHECK(LUTFl::Rotation == (1u << LUTCh::Rotation));
        CHECK(LUTFl::Glow == (1u << LUTCh::Glow));
        CHECK(LUTFl::SizeBySpeed == (1u << LUTCh::SizeBySpeed));
        CHECK(LUTFl::ColorBySpeed == (1u << LUTCh::ColorBySpeed));
        CHECK(LUTFl::RibbonWidth == (1u << LUTCh::RibbonWidth));
        CHECK(LUTFl::RibbonTailGradient == (1u << LUTCh::RibbonTailGradient));

        // Legacy numeric values must not have shifted (GPU/GLSL contract).
        CHECK(LUTFl::Color == 1u);
        CHECK(LUTFl::Glow == 16u);
        CHECK(LUTFl::SizeBySpeed == 32u);
        CHECK(LUTFl::ColorBySpeed == 64u);
        CHECK(LUTFl::RibbonWidth == 128u);
        CHECK(LUTFl::RibbonTailGradient == 256u);
    }
}

TEST_SUITE("VFXLUTBaker")
{
    TEST_CASE("empty chain bakes a dense 9-channel table of no-op defaults, no flags")
    {
        vfx::VFXModifierChain empty;
        LUTBakeResult r = VFXLUTBaker::bake(empty);

        CHECK(r.data.size() == GPUC::LUT_CHANNELS * GPUC::LUT_RESOLUTION);
        CHECK(r.totalEntries == r.data.size());
        CHECK(r.lutFlags == 0u);

        // Multiply/tint no-op defaults for each channel (see VFXLUTBaker::bake fillDefault).
        CHECK(texel(r, LUTCh::Color, 0) == glm::vec4(1.0f));
        CHECK(texel(r, LUTCh::Size, 0) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::Speed, 0) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::Rotation, 0) == glm::vec4(0.0f));
        CHECK(texel(r, LUTCh::Glow, 0) == glm::vec4(0.0f));
        CHECK(texel(r, LUTCh::SizeBySpeed, LAST) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::ColorBySpeed, LAST) == glm::vec4(1.0f));
        CHECK(texel(r, LUTCh::RibbonWidth, LAST) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::RibbonTailGradient, LAST) == glm::vec4(1.0f));
    }

    TEST_CASE("SizeBySpeed curve bakes into channel 5 at t=0/0.5/1; other channels untouched")
    {
        vfx::VFXModifierChain chain;
        vfx::SizeBySpeedConfig cfg;
        cfg.curve = vfx::VFXCurve::fromStartEnd(0.0f, 2.0f);
        cfg.speedMin = 1.0f;
        cfg.speedMax = 20.0f;
        chain.modifiers.push_back(cfg);

        LUTBakeResult r = VFXLUTBaker::bake(chain);
        CHECK((r.lutFlags & LUTFl::SizeBySpeed) != 0u);
        CHECK((r.lutFlags & LUTFl::Color) == 0u);

        CHECK(texel(r, LUTCh::SizeBySpeed, 0).x == doctest::Approx(0.0f));
        CHECK(texel(r, LUTCh::SizeBySpeed, LAST).x == doctest::Approx(2.0f));
        CHECK(texel(r, LUTCh::SizeBySpeed, 31).x == doctest::Approx(1.0f).epsilon(0.05f));

        // Channels 0-4 remain their untouched defaults.
        CHECK(texel(r, LUTCh::Color, 10) == glm::vec4(1.0f));
        CHECK(texel(r, LUTCh::Size, 10) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::Rotation, 10) == glm::vec4(0.0f));
    }

    TEST_CASE("ColorBySpeed gradient bakes into channel 6")
    {
        vfx::VFXModifierChain chain;
        vfx::ColorBySpeedConfig cfg;
        cfg.gradient = vfx::VFXGradient::fromStartEnd(
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        chain.modifiers.push_back(cfg);

        LUTBakeResult r = VFXLUTBaker::bake(chain);
        CHECK((r.lutFlags & LUTFl::ColorBySpeed) != 0u);

        CHECK(texel(r, LUTCh::ColorBySpeed, 0).r == doctest::Approx(0.0f));
        CHECK(texel(r, LUTCh::ColorBySpeed, LAST).r == doctest::Approx(1.0f));
        CHECK(texel(r, LUTCh::ColorBySpeed, LAST).a == doctest::Approx(1.0f));
    }

    TEST_CASE("ribbon width curve + tail gradient bake into channels 7/8 via RibbonLUTInputs")
    {
        vfx::VFXModifierChain empty;
        vfx::VFXCurve widthCurve = vfx::VFXCurve::fromStartEnd(1.0f, 0.0f); // taper to zero at tail
        vfx::VFXGradient tail = vfx::VFXGradient::fromStartEnd(
            glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)); // fade alpha at tail

        VFXLUTBaker::RibbonLUTInputs inputs;
        inputs.widthCurve = &widthCurve;
        inputs.tailGradient = &tail;

        LUTBakeResult r = VFXLUTBaker::bake(empty, inputs);
        CHECK((r.lutFlags & LUTFl::RibbonWidth) != 0u);
        CHECK((r.lutFlags & LUTFl::RibbonTailGradient) != 0u);

        CHECK(texel(r, LUTCh::RibbonWidth, 0).x == doctest::Approx(1.0f));
        CHECK(texel(r, LUTCh::RibbonWidth, LAST).x == doctest::Approx(0.0f));
        CHECK(texel(r, LUTCh::RibbonTailGradient, 0).a == doctest::Approx(1.0f));
        CHECK(texel(r, LUTCh::RibbonTailGradient, LAST).a == doctest::Approx(0.0f));
    }

    TEST_CASE("absent ribbon inputs leave channels 7/8 default with flags off")
    {
        vfx::VFXModifierChain empty;
        LUTBakeResult r = VFXLUTBaker::bake(empty); // no ribbon inputs
        CHECK((r.lutFlags & LUTFl::RibbonWidth) == 0u);
        CHECK((r.lutFlags & LUTFl::RibbonTailGradient) == 0u);
        CHECK(texel(r, LUTCh::RibbonWidth, 0) == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        CHECK(texel(r, LUTCh::RibbonTailGradient, 0) == glm::vec4(1.0f));
    }

    TEST_CASE("over-lifetime channels still bake at indices 0-4 alongside new channels")
    {
        vfx::VFXModifierChain chain;
        vfx::ColorOverLifetimeConfig color;
        color.gradient = vfx::VFXGradient::fromStartEnd(glm::vec4(1.0f), glm::vec4(0.0f));
        vfx::SizeOverLifetimeConfig size;
        size.curve = vfx::VFXCurve::fromStartEnd(2.0f, 0.5f);
        chain.modifiers.push_back(color);
        chain.modifiers.push_back(size);

        LUTBakeResult r = VFXLUTBaker::bake(chain);
        CHECK((r.lutFlags & LUTFl::Color) != 0u);
        CHECK((r.lutFlags & LUTFl::Size) != 0u);
        CHECK((r.lutFlags & LUTFl::SizeBySpeed) == 0u);

        CHECK(texel(r, LUTCh::Color, 0).a == doctest::Approx(1.0f));
        CHECK(texel(r, LUTCh::Color, LAST).a == doctest::Approx(0.0f));
        CHECK(texel(r, LUTCh::Size, 0).x == doctest::Approx(2.0f));
        CHECK(texel(r, LUTCh::Size, LAST).x == doctest::Approx(0.5f));
    }
}

TEST_SUITE("VFXSpeedRemap")
{
    TEST_CASE("normalizedSpeed01 clamps at 0 and >= max and remaps linearly")
    {
        CHECK(vfx::normalizedSpeed01(0.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
        CHECK(vfx::normalizedSpeed01(10.0f, 0.0f, 10.0f) == doctest::Approx(1.0f));
        CHECK(vfx::normalizedSpeed01(5.0f, 0.0f, 10.0f) == doctest::Approx(0.5f));
        CHECK(vfx::normalizedSpeed01(-5.0f, 0.0f, 10.0f) == doctest::Approx(0.0f)); // clamp low
        CHECK(vfx::normalizedSpeed01(20.0f, 0.0f, 10.0f) == doctest::Approx(1.0f)); // clamp >= max
        CHECK(vfx::normalizedSpeed01(3.0f, 2.0f, 6.0f) == doctest::Approx(0.25f));   // offset range
        CHECK(vfx::normalizedSpeed01(5.0f, 5.0f, 5.0f) == doctest::Approx(0.0f));    // degenerate
        CHECK(vfx::normalizedSpeed01(5.0f, 8.0f, 2.0f) == doctest::Approx(0.0f));    // inverted (hi<lo)
    }
}

TEST_SUITE("VFXBySpeedLoader")
{
    TEST_CASE("fromGraph round-trips SizeBySpeed and ColorBySpeed configs")
    {
        vfx::VFXGraph graph;

        vfx::VFXNode emitter = makeNode(1, vfx::VFXNodeType::Emitter, "Emitter");

        vfx::VFXNode sizeNode = makeNode(2, vfx::VFXNodeType::SizeBySpeed, "SizeBySpeed");
        setCurve(sizeNode, "curve", vfx::VFXCurve::fromStartEnd(0.3f, 1.0f));
        setFloat(sizeNode, "speedMin", 1.0f);
        setFloat(sizeNode, "speedMax", 20.0f);

        vfx::VFXNode colorNode = makeNode(3, vfx::VFXNodeType::ColorBySpeed, "ColorBySpeed");
        setGradient(colorNode, "gradient", vfx::VFXGradient::fromStartEnd(
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));
        setFloat(colorNode, "speedMin", 2.0f);
        setFloat(colorNode, "speedMax", 8.0f);

        vfx::VFXNode out = makeNode(4, vfx::VFXNodeType::OutSystem, "OutSystem");

        graph.nodes = {emitter, sizeNode, colorNode, out};
        graph.links = {makeLink(1, 1, 2), makeLink(2, 2, 3), makeLink(3, 3, 4)};

        vfx::VFXModifierChain chain = vfx::VFXModifierConfigLoader::fromGraph(graph);
        REQUIRE(chain.modifiers.size() == 2);

        const auto* sizeCfg = std::get_if<vfx::SizeBySpeedConfig>(&chain.modifiers[0]);
        REQUIRE(sizeCfg != nullptr);
        CHECK(sizeCfg->speedMin == doctest::Approx(1.0f));
        CHECK(sizeCfg->speedMax == doctest::Approx(20.0f));
        CHECK(sizeCfg->curve.evaluate(0.0f) == doctest::Approx(0.3f));
        CHECK(sizeCfg->curve.evaluate(1.0f) == doctest::Approx(1.0f));

        const auto* colorCfg = std::get_if<vfx::ColorBySpeedConfig>(&chain.modifiers[1]);
        REQUIRE(colorCfg != nullptr);
        CHECK(colorCfg->speedMin == doctest::Approx(2.0f));
        CHECK(colorCfg->speedMax == doctest::Approx(8.0f));
        CHECK(colorCfg->gradient.evaluate(0.0f).r == doctest::Approx(1.0f));
        CHECK(colorCfg->gradient.evaluate(1.0f).b == doctest::Approx(1.0f));
    }
}
