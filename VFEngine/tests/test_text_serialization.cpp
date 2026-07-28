// CPU-only serialization coverage for the world-space TextComponent's VK-1637
// fields (rectHeight, horizontalAlignment, verticalAlignment, overflow, wordWrap).
//   1. Backward compatibility: a .vfScene written before those fields existed must
//      load with Left / Top / Overflow / wrapping and rectHeight 0 - i.e. exactly
//      the behaviour world text had before the ticket. deserializeText uses
//      j.value(key, default), so a missing key falls back and older scenes keep
//      rendering identically.
//   2. Round-trip: non-default values must save and reload unchanged. Catches a
//      key-name mismatch between serialize and deserialize.
//   3. Clip round-trips as Clip even though the world text pipeline renders it as
//      Overflow - the enum is shared with UILabelComponent, and collapsing the
//      value at the serializer would lose a UI-meaningful setting.
//
// serialize/deserializeText are PRIVATE; the public seam is saveScene /
// loadSceneInto over a SceneGraphSystem, exactly as test_billboard_serialization.cpp
// and test_decal_serialization.cpp drive their components. No graphics layer.

#include <doctest.h>

#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <asset/AssetDatabase.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    fs::path textTestRoot()
    {
        return fs::temp_directory_path() / "vf_text_serialization_tests";
    }

    void resetTextTestRoot()
    {
        std::error_code ec;
        fs::remove_all(textTestRoot(), ec);
        fs::create_directories(textTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    // Writes a .vfScene whose "text" block is exactly `textBlock`.
    fs::path writeScene(const fs::path& path, json textBlock)
    {
        json sceneJson;
        sceneJson["version"] = "1.0";
        sceneJson["root"] = {
            {"name", "Root"},
            {"isActive", true},
            {"components", {{"text", std::move(textBlock)}}},
            {"children", json::array()}
        };

        std::ofstream file(path);
        REQUIRE(file.is_open());
        file << sceneJson.dump(2);
        file.close();
        return path;
    }

    const components::TextComponent& loadText(scene::SceneGraphSystem& into, const fs::path& path)
    {
        REQUIRE(serialization::SceneSerialization::loadSceneInto(path.string(), into));
        REQUIRE(into.GetRoot().hasComponent<components::TextComponent>());
        return into.GetRoot().getComponent<components::TextComponent>();
    }
}

TEST_SUITE("TextSerialization")
{
    TEST_CASE("VK-1637 fields save and reload through a scene round-trip")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "Round trip";
        text.maxWidth = 240.0f;
        text.rectHeight = 120.0f;
        // Every one non-default, so a key mismatch cannot hide behind a default.
        text.horizontalAlignment = components::HorizontalAlignment::Center;
        text.verticalAlignment = components::VerticalAlignment::Bottom;
        text.overflow = components::TextOverflow::Ellipsis;
        text.wordWrap = false;

        fs::path scenePath = textTestRoot() / "AlignedText.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        const auto& after = loadText(loaded, scenePath);

        CHECK(after.maxWidth == doctest::Approx(240.0f));
        CHECK(after.rectHeight == doctest::Approx(120.0f));
        CHECK(after.horizontalAlignment == components::HorizontalAlignment::Center);
        CHECK(after.verticalAlignment == components::VerticalAlignment::Bottom);
        CHECK(after.overflow == components::TextOverflow::Ellipsis);
        CHECK(after.wordWrap == false);
    }

    TEST_CASE("legacy text without the VK-1637 keys loads with pre-ticket behaviour")
    {
        resetTextTestRoot();

        // A pre-VK-1637 .vfScene: the text block carries only the original keys.
        fs::path scenePath = writeScene(textTestRoot() / "LegacyText.vfScene", {
            {"text", "Legacy"},
            {"fontSize", 48.0f},
            {"color", json::array({1.0f, 1.0f, 1.0f, 1.0f})},
            {"lineSpacing", 1.0f},
            {"letterSpacing", 0.0f},
            {"maxWidth", 200.0f},
            {"fontStyle", "normal"}
        });

        scene::SceneGraphSystem loaded;
        const auto& text = loadText(loaded, scenePath);

        // These five defaults are the whole pixel-identity claim: they resolve to a
        // no-op alignment and no ellipsis, which is what world text did before.
        CHECK(text.rectHeight == doctest::Approx(0.0f));
        CHECK(text.horizontalAlignment == components::HorizontalAlignment::Left);
        CHECK(text.verticalAlignment == components::VerticalAlignment::Top);
        CHECK(text.overflow == components::TextOverflow::Overflow);
        CHECK(text.wordWrap == true);   // missing key -> the historical "maxWidth wraps"

        // Original fields still load alongside the defaults.
        CHECK(text.text == "Legacy");
        CHECK(text.fontSize == doctest::Approx(48.0f));
        CHECK(text.maxWidth == doctest::Approx(200.0f));
    }

    TEST_CASE("Clip round-trips losslessly even though world text renders it as Overflow")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.overflow = components::TextOverflow::Clip;

        fs::path scenePath = textTestRoot() / "ClipText.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        // The serializer must not collapse Clip to Overflow: the editor combo is
        // 3-wide for the same reason, so an authored value survives an unrelated edit.
        scene::SceneGraphSystem loaded;
        CHECK(loadText(loaded, scenePath).overflow == components::TextOverflow::Clip);
    }

    TEST_CASE("unknown enum strings fall back rather than corrupting the component")
    {
        resetTextTestRoot();

        fs::path scenePath = writeScene(textTestRoot() / "GarbageEnums.vfScene", {
            {"text", "Garbage"},
            {"horizontalAlignment", "sideways"},
            {"verticalAlignment", "diagonal"},
            {"overflow", "explode"},
            {"wordWrap", true}
        });

        scene::SceneGraphSystem loaded;
        const auto& text = loadText(loaded, scenePath);

        // stringToHorizontalAlignment / stringToVerticalAlignment / stringToTextOverflow
        // are total functions; deserializeText relies on that rather than validating.
        CHECK(text.horizontalAlignment == components::HorizontalAlignment::Left);
        CHECK(text.verticalAlignment == components::VerticalAlignment::Top);
        CHECK(text.overflow == components::TextOverflow::Overflow);
    }

    TEST_CASE("a saved scene reloads to itself (load -> save -> load is a fixed point)")
    {
        resetTextTestRoot();

        // The five keys are written unconditionally, so a legacy scene gains them on
        // its first re-save. That one-time delta must then be stable.
        fs::path legacyPath = writeScene(textTestRoot() / "LegacyForResave.vfScene", {
            {"text", "Resave"},
            {"maxWidth", 200.0f}
        });

        scene::SceneGraphSystem first;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(legacyPath.string(), first));

        fs::path resavedPath = textTestRoot() / "Resaved.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(first, resavedPath.string()));

        scene::SceneGraphSystem second;
        const auto& text = loadText(second, resavedPath);
        CHECK(text.rectHeight == doctest::Approx(0.0f));
        CHECK(text.horizontalAlignment == components::HorizontalAlignment::Left);
        CHECK(text.verticalAlignment == components::VerticalAlignment::Top);
        CHECK(text.overflow == components::TextOverflow::Overflow);
        CHECK(text.wordWrap == true);
        CHECK(text.maxWidth == doctest::Approx(200.0f));
    }

    // ---- VK-1638: text effects persist what was AUTHORED, not what is rendering ----
    //
    // serializeTextEffects used to gate each group on hasOutline()/hasShadow()/hasGlow(),
    // which are alpha-gated. Dragging a colour's alpha to 0 to compare "with / without"
    // therefore deleted the authored distance on the next save.

    TEST_CASE("an outline width authored with a transparent colour survives a save")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "Effects";
        text.effects.outlineWidth = 3.0f;
        text.effects.outlineColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        REQUIRE_FALSE(text.effects.hasOutline()); // inert right now, but authored

        fs::path scenePath = textTestRoot() / "OutlineAlphaZero.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        const auto& reloaded = loadText(loaded, scenePath);
        CHECK(reloaded.effects.outlineWidth == doctest::Approx(3.0f));
        CHECK(reloaded.effects.outlineColor.a == doctest::Approx(0.0f));
    }

    TEST_CASE("a shadow offset authored with a transparent colour survives a save")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "Effects";
        text.effects.shadowOffset = glm::vec2(2.0f, -3.0f);
        text.effects.shadowColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        REQUIRE_FALSE(text.effects.hasShadow());

        fs::path scenePath = textTestRoot() / "ShadowAlphaZero.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        const auto& reloaded = loadText(loaded, scenePath);
        CHECK(reloaded.effects.shadowOffset.x == doctest::Approx(2.0f));
        CHECK(reloaded.effects.shadowOffset.y == doctest::Approx(-3.0f));
        CHECK(reloaded.effects.shadowColor.r == doctest::Approx(1.0f));
    }

    TEST_CASE("a glow range authored with a transparent colour survives a save")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "Effects";
        text.effects.glowRange = 4.0f;
        text.effects.glowColor = glm::vec4(0.2f, 0.4f, 0.6f, 0.0f);
        REQUIRE_FALSE(text.effects.hasGlow());

        fs::path scenePath = textTestRoot() / "GlowAlphaZero.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        const auto& reloaded = loadText(loaded, scenePath);
        CHECK(reloaded.effects.glowRange == doctest::Approx(4.0f));
        CHECK(reloaded.effects.glowColor.b == doctest::Approx(0.6f));
    }

    TEST_CASE("a text component with no authored effects still writes no effects key")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "No effects";

        fs::path scenePath = textTestRoot() / "NoEffects.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        std::ifstream file(scenePath);
        REQUIRE(file.is_open());
        json saved;
        file >> saved;
        CHECK_FALSE(saved["root"]["components"]["text"].contains("effects"));
    }

    TEST_CASE("an active effect still round-trips unchanged")
    {
        resetTextTestRoot();

        scene::SceneGraphSystem source;
        auto& text = source.GetRoot().addOrReplaceComponent<components::TextComponent>();
        text.text = "Effects";
        text.effects.outlineWidth = 2.0f;
        text.effects.outlineColor = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
        REQUIRE(text.effects.hasOutline());

        fs::path scenePath = textTestRoot() / "ActiveOutline.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        const auto& reloaded = loadText(loaded, scenePath);
        CHECK(reloaded.effects.outlineWidth == doctest::Approx(2.0f));
        CHECK(reloaded.effects.outlineColor.g == doctest::Approx(0.5f));
        CHECK(reloaded.effects.outlineColor.a == doctest::Approx(1.0f));
    }
}
