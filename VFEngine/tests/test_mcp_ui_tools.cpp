#include "test_mcp_ui_fixture.hpp"

#include "ui/UIRectMath.hpp"

#include "events/ui/UIEvents.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// VK-1659: the MCP UI tools (ui_list_widget_types, ui_create_canvas, ui_add_widget, ui_get, ui_set,
// ui_save_prefab, ui_instantiate_prefab, ui_click) against the real scene / UI / persistence services
// (test_mcp_ui_fixture.hpp). Placement numbers are canvas reference pixels on the default 1920 x 1080
// scaleWithScreenSize canvas. Entity names are unique (McpUi*): the registry and some queries are process-wide.

using namespace mcp_ui_test;

namespace
{
    const components::UIRectComponent& rectOf(uint32_t id)
    {
        return reg().get<components::UIRectComponent>(enttOf(id));
    }

    // The rect as the runtime resolves it for a width x height screen (scaleWithScreenSize on 1920 x 1080).
    utilities::ui::PixelRect resolvedAt(uint32_t id, float width, float height)
    {
        const float scale = std::min(width / 1920.0f, height / 1080.0f);
        return utilities::ui::resolvePixelRect(rectOf(id), width, height, scale);
    }

    void checkRect(const utilities::ui::PixelRect& rect, float x, float y, float w, float h)
    {
        CHECK(rect.x == doctest::Approx(x).epsilon(1e-4));
        CHECK(rect.y == doctest::Approx(y).epsilon(1e-4));
        CHECK(rect.w == doctest::Approx(w).epsilon(1e-4));
        CHECK(rect.h == doctest::Approx(h).epsilon(1e-4));
    }

    void checkResolvedJson(const nlohmann::json& resolved, float x, float y, float w, float h)
    {
        REQUIRE(resolved.is_object());
        CHECK(resolved.at("x").get<float>() == doctest::Approx(x).epsilon(1e-4));
        CHECK(resolved.at("y").get<float>() == doctest::Approx(y).epsilon(1e-4));
        CHECK(resolved.at("w").get<float>() == doctest::Approx(w).epsilon(1e-4));
        CHECK(resolved.at("h").get<float>() == doctest::Approx(h).epsilon(1e-4));
    }

    void checkVec2(const glm::vec2& value, float x, float y)
    {
        CHECK(value.x == doctest::Approx(x));
        CHECK(value.y == doctest::Approx(y));
    }

    void checkColor(const glm::vec4& value, float r, float g, float b, float a)
    {
        CHECK(value.r == doctest::Approx(r));
        CHECK(value.g == doctest::Approx(g));
        CHECK(value.b == doctest::Approx(b));
        CHECK(value.a == doctest::Approx(a));
    }

    nlohmann::json placement(const char* preset, float w, float h, nlohmann::json margin = 0)
    {
        return {{"preset", preset}, {"size", nlohmann::json::array({w, h})}, {"margin", std::move(margin)}};
    }

    std::string nameOf(uint32_t id)
    {
        return reg().get<components::NameComponent>(enttOf(id)).name;
    }

    // The child of `parent` named `name` (entt::null when none).
    entt::entity childNamed(const UIToolFixture& fixture, uint32_t parent, const std::string& name)
    {
        for (const entt::entity child : fixture.childrenOf(parent))
        {
            if (reg().valid(child) && reg().get<components::NameComponent>(child).name == name)
            {
                return child;
            }
        }
        return entt::null;
    }
}

TEST_SUITE("McpUiTools")
{
    TEST_CASE("registering the UI tools dispatches nothing")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerUITools(registry, context));
        CHECK(registry.size() == 8);
    }

    TEST_CASE("the UI tools run on the main thread with closed schemas and their annotations")
    {
        UIToolFixture fixture("VertexForge_McpUi_Registration");

        struct Expected
        {
            const char* name;
            bool readOnly;
            bool destructive;
            long long timeoutMs;
        };
        for (const Expected& entry : {Expected{"ui_list_widget_types", true, false, 10000},
                                      Expected{"ui_create_canvas", false, false, 10000},
                                      Expected{"ui_add_widget", false, false, 10000},
                                      Expected{"ui_get", true, false, 10000},
                                      Expected{"ui_set", false, false, 10000},
                                      Expected{"ui_save_prefab", false, true, 30000},
                                      Expected{"ui_instantiate_prefab", false, false, 30000},
                                      Expected{"ui_click", false, false, 10000}})
        {
            CAPTURE(entry.name);
            const mcp::ToolDef& def = fixture.tool(entry.name);
            CHECK(def.affinity == mcp::ThreadAffinity::Main);
            CHECK(def.readOnly == entry.readOnly);
            CHECK(def.destructive == entry.destructive);
            CHECK(def.timeout.count() == entry.timeoutMs);
            CHECK(def.inputSchema.at("type") == "object");
            CHECK(def.inputSchema.at("additionalProperties") == false);
        }
        CHECK(fixture.tool("ui_add_widget").inputSchema.at("required") == nlohmann::json::array({"type", "parent"}));
        CHECK(fixture.tool("ui_set").inputSchema.at("required") == nlohmann::json::array({"entity"}));
        CHECK(fixture.tool("ui_save_prefab").inputSchema.at("required") == nlohmann::json::array({"entity", "path"}));
        CHECK(fixture.tool("ui_instantiate_prefab").inputSchema.at("required") == nlohmann::json::array({"path"}));
        CHECK(fixture.tool("ui_click").inputSchema.at("required") == nlohmann::json::array({"entity"}));

        // ui_set lists every section and the read-only report keys it accepts back.
        const nlohmann::json& setProperties = fixture.tool("ui_set").inputSchema.at("properties");
        for (const char* key : {"canvas", "rect", "image", "label", "button", "checkbox", "slider", "progressBar",
                                "dropdown", "textInput", "layoutGroup", "listView", "mask", "scroll", "placement",
                                "moveDescendants", "type", "resolved", "validation"})
        {
            CAPTURE(key);
            CHECK(setProperties.contains(key));
        }
        // A widget is never a canvas.
        CHECK_FALSE(fixture.tool("ui_add_widget").inputSchema.at("properties").contains("canvas"));
    }

    TEST_CASE("ui_list_widget_types describes every widget type, section and placement preset")
    {
        UIToolFixture fixture("VertexForge_McpUi_List");

        nlohmann::json all = fixture.callOk("ui_list_widget_types", nlohmann::json::object());
        REQUIRE(all["widgets"].size() == 11);
        CHECK(all["sections"].size() == 14);
        CHECK(all["placement"]["presets"].size() == 16);
        CHECK(all["canvas"]["renderMode"] == "screenSpaceOverlay");

        nlohmann::json button;
        for (const nlohmann::json& widget : all["widgets"])
        {
            CAPTURE(widget.dump());
            CHECK(widget.at("sections").size() >= 1);
            CHECK(widget.at("defaultSize").size() == 2);
            if (widget.at("type") == "Button")
            {
                button = widget;
            }
        }
        REQUIRE(button.is_object());
        CHECK(button["text"] == "label.text");
        CHECK(button["starterStyle"]["button"].contains("normalColor"));
        CHECK(button["components"] == nlohmann::json::array({"UIRect", "UIImage", "UILabel", "UIButton"}));

        // Narrowed to one type (case-insensitive): its sections and the attachable ones.
        nlohmann::json one = fixture.callOk("ui_list_widget_types", {{"type", "progressbar"}});
        REQUIRE(one["widgets"].size() == 1);
        CHECK(one["widgets"][0]["type"] == "ProgressBar");
        CHECK(one["sections"].contains("progressBar"));
        CHECK(one["sections"].contains("rect"));
        CHECK(one["sections"].contains("mask"));
        CHECK_FALSE(one["sections"].contains("button"));

        CHECK(contains(fixture.argError("ui_list_widget_types", {{"type", "Knob"}}), "must be one of"));
        CHECK(contains(fixture.argError("ui_list_widget_types", {{"kind", "Button"}}), "unknown argument"));
    }

    TEST_CASE("ui_create_canvas makes a screen-space canvas with a rect in one undo step")
    {
        UIToolFixture fixture("VertexForge_McpUi_Canvas");

        nlohmann::json out = fixture.callOk("ui_create_canvas", {{"name", "McpUiHud"}});
        const uint32_t canvas = out["entity"].get<uint32_t>();
        CHECK(out["renderMode"] == "screenSpaceOverlay");
        REQUIRE(reg().all_of<components::UICanvasComponent, components::UIRectComponent>(enttOf(canvas)));
        const auto& data = reg().get<components::UICanvasComponent>(enttOf(canvas));
        CHECK(data.referenceWidth == 1920.0f);
        CHECK(data.referenceHeight == 1080.0f);
        CHECK(data.scaleMode == components::UIScaleMode::ScaleWithScreenSize);
        CHECK(reg().get<components::ParentComponent>(enttOf(canvas)).parent == enttOf(fixture.rootId()));
        REQUIRE(fixture.undoStack.size() == 1);
        CHECK(batchSize(fixture.undoStack.back()) == 1);

        const uint32_t menu = fixture.callOk("ui_create_canvas", {{"name", "McpUiMenu"},
                                                                  {"referenceResolution", {1280, 720}},
                                                                  {"scaleMode", "constantPixelSize"},
                                                                  {"pixelsPerUnit", 50},
                                                                  {"sortOrder", 5},
                                                                  {"renderMode", "ScreenSpaceOverlay"}})["entity"]
                                  .get<uint32_t>();
        const auto& menuData = reg().get<components::UICanvasComponent>(enttOf(menu));
        CHECK(menuData.referenceWidth == 1280.0f);
        CHECK(menuData.referenceHeight == 720.0f);
        CHECK(menuData.scaleMode == components::UIScaleMode::ConstantPixelSize);
        CHECK(menuData.pixelsPerUnit == 50.0f);
        CHECK(menuData.sortOrder == 5);

        // Undo removes it, redo re-creates it under a new id that the remap resolves.
        fixture.undoLast();
        CHECK_FALSE(alive(menu));
        fixture.redoLast();
        REQUIRE(alive(UIToolFixture::live(menu)));
        CHECK(reg().get<components::UICanvasComponent>(enttOf(UIToolFixture::live(menu))).referenceWidth == 1280.0f);

        // Strict arguments: nothing is created.
        const std::size_t rootChildren = fixture.childrenOf(fixture.rootId()).size();
        CHECK(contains(fixture.argError("ui_create_canvas", {{"renderMode", "worldSpace"}}), "screenSpaceOverlay"));
        CHECK(contains(fixture.argError("ui_create_canvas", {{"referenceResolution", {0, 720}}}), "referenceResolution"));
        CHECK(contains(fixture.argError("ui_create_canvas", {{"referenceResolution", {1280}}}), "[width, height]"));
        CHECK(contains(fixture.argError("ui_create_canvas", {{"scaleMode", "stretch"}}), "scaleMode"));
        CHECK(fixture.childrenOf(fixture.rootId()).size() == rootChildren);
    }

    TEST_CASE("ui_create_canvas refuses UI parents, Play mode, a pending load and World mode")
    {
        UIToolFixture fixture("VertexForge_McpUi_CanvasRefusals");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t panel = fixture.addWidget({{"type", "Rect"}, {"parent", canvas}, {"name", "McpUiPanel"}});
        const std::size_t stack = fixture.undoStack.size();

        CHECK(contains(fixture.failure("ui_create_canvas", {{"parent", panel}}), "drawn twice"));
        CHECK(contains(fixture.failure("ui_create_canvas", {{"parent", canvas}}), "drawn twice"));

        fixture.playMode = true;
        CHECK(contains(fixture.failure("ui_create_canvas", nlohmann::json::object()), "Play mode"));
        fixture.playMode = false;
        fixture.loadPending = true;
        CHECK(contains(fixture.failure("ui_create_canvas", nlohmann::json::object()), "scene load"));
        fixture.loadPending = false;
        fixture.worldMode = true;
        CHECK(contains(fixture.failure("ui_create_canvas", nlohmann::json::object()), "World mode"));

        CHECK(fixture.undoStack.size() == stack);
    }

    TEST_CASE("ui_add_widget builds every widget type from its recipe")
    {
        UIToolFixture fixture("VertexForge_McpUi_Recipes");
        const uint32_t canvas = fixture.makeCanvas();

        auto add = [&](const char* type, const char* name)
        {
            nlohmann::json out = fixture.callOk("ui_add_widget", {{"type", type}, {"parent", canvas}, {"name", name}});
            CHECK(out["type"] == type);
            CHECK(out["canvasId"] == canvas);
            return out["entity"].get<uint32_t>();
        };

        // Without a placement every widget is centred at its default size (pivot 0.5).
        auto checkDefaultRect = [](uint32_t id, float w, float h)
        {
            const auto& rect = rectOf(id);
            checkVec2(rect.anchorMin, 0.5f, 0.5f);
            checkVec2(rect.anchorMax, 0.5f, 0.5f);
            checkVec2(rect.pivot, 0.5f, 0.5f);
            checkVec2(rect.sizeDelta, w, h);
            checkVec2(rect.anchoredPosition, 0.0f, 0.0f);
        };

        const uint32_t rect = add("Rect", "McpUiRect");
        CHECK(reg().all_of<components::UIRectComponent>(enttOf(rect)));
        CHECK_FALSE(reg().any_of<components::UIImageComponent, components::UILabelComponent>(enttOf(rect)));
        checkDefaultRect(rect, 400.0f, 300.0f);

        const uint32_t image = add("Image", "McpUiImage");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent>(enttOf(image)));
        checkColor(reg().get<components::UIImageComponent>(enttOf(image)).colorTint, 0.10f, 0.10f, 0.12f, 0.85f);
        checkDefaultRect(image, 256.0f, 256.0f);

        const uint32_t label = add("Label", "McpUiLabel");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UILabelComponent>(enttOf(label)));
        const auto& labelComp = reg().get<components::UILabelComponent>(enttOf(label));
        CHECK(labelComp.text == "Label");
        CHECK(labelComp.fontSize == 28.0f);
        CHECK(labelComp.horizontalAlignment == components::HorizontalAlignment::Left);
        CHECK(labelComp.verticalAlignment == components::VerticalAlignment::Middle);
        checkColor(labelComp.color, 1.0f, 1.0f, 1.0f, 1.0f);

        const uint32_t button = add("Button", "McpUiButton");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UILabelComponent,
                             components::UIButtonComponent>(enttOf(button)));
        const auto& buttonComp = reg().get<components::UIButtonComponent>(enttOf(button));
        checkColor(buttonComp.normalColor, 0.16f, 0.42f, 0.86f, 1.0f);
        checkColor(buttonComp.pressedColor, 0.10f, 0.30f, 0.66f, 1.0f);
        const auto& buttonLabel = reg().get<components::UILabelComponent>(enttOf(button));
        CHECK(buttonLabel.text == "Button");
        CHECK(buttonLabel.fontSize == 24.0f);
        CHECK(buttonLabel.horizontalAlignment == components::HorizontalAlignment::Center);
        checkColor(buttonLabel.color, 1.0f, 1.0f, 1.0f, 1.0f);
        CHECK(fixture.childrenOf(button).empty()); // the label is on the button itself
        checkDefaultRect(button, 200.0f, 60.0f);

        nlohmann::json checkboxOut =
            fixture.callOk("ui_add_widget", {{"type", "Checkbox"}, {"parent", canvas}, {"name", "McpUiOption"}});
        const uint32_t checkbox = checkboxOut["entity"].get<uint32_t>();
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UICheckboxComponent>(
            enttOf(checkbox)));
        const auto& checkboxComp = reg().get<components::UICheckboxComponent>(enttOf(checkbox));
        CHECK(checkboxComp.labelToggle);
        checkColor(checkboxComp.uncheckedColor, 0.85f, 0.85f, 0.88f, 1.0f);
        checkColor(checkboxComp.checkedColor, 0.20f, 0.70f, 0.35f, 1.0f);
        checkDefaultRect(checkbox, 32.0f, 32.0f);
        // The label part: "<name>Label", 12 px right of the box, 260 x 40, vertically centred on it.
        const entt::entity part = childNamed(fixture, checkbox, "McpUiOptionLabel");
        REQUIRE(part != entt::null);
        CHECK(checkboxOut["parts"]["label"] == static_cast<uint32_t>(part));
        REQUIRE(reg().all_of<components::UIRectComponent, components::UILabelComponent>(part));
        CHECK(reg().get<components::UILabelComponent>(part).text == "McpUiOption");
        checkRect(resolvedAt(checkbox, 1920.0f, 1080.0f), 944.0f, 524.0f, 32.0f, 32.0f);
        checkRect(utilities::ui::resolvePixelRect(reg().get<components::UIRectComponent>(part), 1920.0f, 1080.0f, 1.0f),
                  988.0f, 520.0f, 260.0f, 40.0f);

        const uint32_t slider = add("Slider", "McpUiSlider");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UISliderComponent>(
            enttOf(slider)));
        checkColor(reg().get<components::UISliderComponent>(enttOf(slider)).fillColor, 0.16f, 0.55f, 0.95f, 1.0f);
        checkColor(reg().get<components::UIImageComponent>(enttOf(slider)).colorTint, 0.15f, 0.15f, 0.18f, 1.0f);

        const uint32_t bar = add("ProgressBar", "McpUiBar");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIProgressBarComponent>(enttOf(bar)));
        const auto& barComp = reg().get<components::UIProgressBarComponent>(enttOf(bar));
        CHECK(barComp.value == 1.0f);
        checkColor(barComp.fillColor, 0.20f, 0.80f, 0.30f, 1.0f);
        checkColor(barComp.trackColor, 0.10f, 0.10f, 0.12f, 0.85f);

        const uint32_t dropdown = add("Dropdown", "McpUiDropdown");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UILabelComponent,
                             components::UIDropdownComponent>(enttOf(dropdown)));
        CHECK(reg().get<components::UIDropdownComponent>(enttOf(dropdown)).options.size() == 3);
        CHECK(reg().get<components::UILabelComponent>(enttOf(dropdown)).text == "Select...");

        const uint32_t list = add("ListView", "McpUiList");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UILayoutGroupComponent,
                             components::UIListViewComponent>(enttOf(list)));
        CHECK(reg().get<components::UILayoutGroupComponent>(enttOf(list)).spacing == 4.0f);

        const uint32_t input = add("TextInput", "McpUiInput");
        CHECK(reg().all_of<components::UIRectComponent, components::UIImageComponent, components::UITextInputComponent>(
            enttOf(input)));

        const uint32_t layout = add("LayoutGroup", "McpUiLayout");
        REQUIRE(reg().all_of<components::UIRectComponent, components::UILayoutGroupComponent>(enttOf(layout)));
        CHECK(reg().get<components::UILayoutGroupComponent>(enttOf(layout)).spacing == 8.0f);

        // `text` is the caption of the types that have one.
        const uint32_t pause = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiPause"},
                                                  {"text", "Pause"}});
        CHECK(reg().get<components::UILabelComponent>(enttOf(pause)).text == "Pause");
        const uint32_t pick = fixture.addWidget({{"type", "Dropdown"}, {"parent", canvas}, {"name", "McpUiPick"},
                                                 {"text", "Pick one"}});
        CHECK(reg().get<components::UIDropdownComponent>(enttOf(pick)).placeholderText == "Pick one");
        CHECK(reg().get<components::UILabelComponent>(enttOf(pick)).text == "Pick one");

        // One undo step per call: the canvas, the 11 types and the two captioned widgets.
        CHECK(fixture.undoStack.size() == 14);
    }

    TEST_CASE("the HUD of the acceptance lands on the planned reference rects")
    {
        UIToolFixture fixture("VertexForge_McpUi_Hud");
        const uint32_t canvas = fixture.makeCanvas("McpUiHudCanvas");

        nlohmann::json bar = fixture.callOk("ui_add_widget",
            {{"type", "ProgressBar"}, {"parent", canvas}, {"name", "McpUiHealthBar"},
             {"placement", placement("topLeft", 320.0f, 28.0f, 24)}});
        const uint32_t barId = bar["entity"].get<uint32_t>();
        checkVec2(rectOf(barId).anchorMin, 0.0f, 1.0f);
        checkVec2(rectOf(barId).anchorMax, 0.0f, 1.0f);
        checkVec2(rectOf(barId).pivot, 0.5f, 0.5f);
        checkVec2(rectOf(barId).sizeDelta, 320.0f, 28.0f);
        checkVec2(rectOf(barId).anchoredPosition, 184.0f, -38.0f);
        checkResolvedJson(bar["resolved"], 24.0f, 24.0f, 320.0f, 28.0f);
        CHECK(bar["widget"]["type"] == "ProgressBar");
        checkRect(resolvedAt(barId, 1920.0f, 1080.0f), 24.0f, 24.0f, 320.0f, 28.0f);
        // The margins scale with the canvas.
        checkRect(resolvedAt(barId, 1280.0f, 720.0f), 16.0f, 16.0f, 320.0f * 2.0f / 3.0f, 28.0f * 2.0f / 3.0f);
        checkRect(resolvedAt(barId, 2560.0f, 1080.0f), 24.0f, 24.0f, 320.0f, 28.0f);

        nlohmann::json score = fixture.callOk("ui_add_widget",
            {{"type", "Label"}, {"parent", canvas}, {"name", "McpUiScoreLabel"}, {"text", "Score: 0"},
             {"placement", placement("topRight", 300.0f, 48.0f, nlohmann::json::array({24, 20}))},
             {"label", {{"horizontalAlignment", "right"}}}});
        const uint32_t scoreId = score["entity"].get<uint32_t>();
        checkVec2(rectOf(scoreId).anchorMin, 1.0f, 1.0f);
        checkVec2(rectOf(scoreId).anchoredPosition, -174.0f, -44.0f);
        checkResolvedJson(score["resolved"], 1596.0f, 20.0f, 300.0f, 48.0f);
        CHECK(reg().get<components::UILabelComponent>(enttOf(scoreId)).text == "Score: 0");
        CHECK(reg().get<components::UILabelComponent>(enttOf(scoreId)).horizontalAlignment ==
              components::HorizontalAlignment::Right);

        nlohmann::json pause = fixture.callOk("ui_add_widget",
            {{"type", "Button"}, {"parent", canvas}, {"name", "McpUiPauseButton"}, {"text", "Pause"},
             {"placement", placement("top", 160.0f, 56.0f, nlohmann::json::array({0, 20}))}});
        const uint32_t pauseId = pause["entity"].get<uint32_t>();
        checkVec2(rectOf(pauseId).anchorMin, 0.5f, 1.0f);
        checkVec2(rectOf(pauseId).anchoredPosition, 0.0f, -48.0f);
        checkResolvedJson(pause["resolved"], 880.0f, 20.0f, 160.0f, 56.0f);
    }

    TEST_CASE("a widget placed inside another stays glued to it at every resolution")
    {
        UIToolFixture fixture("VertexForge_McpUi_Glue");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t panel = fixture.addWidget({{"type", "Image"}, {"parent", canvas}, {"name", "McpUiPanel"},
                                                  {"placement", placement("topRight", 400.0f, 200.0f, 40)}});
        const uint32_t inner = fixture.addWidget({{"type", "Label"}, {"parent", panel}, {"name", "McpUiInner"},
                                                  {"placement", {{"preset", "stretch"}, {"margin", 8}}}});
        const uint32_t corner = fixture.addWidget({{"type", "Image"}, {"parent", panel}, {"name", "McpUiCorner"},
                                                   {"placement", placement("bottomLeft", 30.0f, 20.0f, 5)}});

        struct Viewport
        {
            float w, h;
        };
        for (const Viewport viewport : {Viewport{1920.0f, 1080.0f}, Viewport{1280.0f, 720.0f}, Viewport{2560.0f, 1080.0f},
                                        Viewport{1080.0f, 1920.0f}, Viewport{800.0f, 600.0f}})
        {
            CAPTURE(viewport.w);
            CAPTURE(viewport.h);
            const float s = std::min(viewport.w / 1920.0f, viewport.h / 1080.0f);
            const utilities::ui::PixelRect p = resolvedAt(panel, viewport.w, viewport.h);
            checkRect(resolvedAt(inner, viewport.w, viewport.h), p.x + 8.0f * s, p.y + 8.0f * s, p.w - 16.0f * s,
                      p.h - 16.0f * s);
            checkRect(resolvedAt(corner, viewport.w, viewport.h), p.x + 5.0f * s, p.y + p.h - 5.0f * s - 20.0f * s,
                      30.0f * s, 20.0f * s);
        }

        // relativeTo canvas ignores the parent widget.
        const uint32_t loose = fixture.addWidget({{"type", "Rect"}, {"parent", panel}, {"name", "McpUiLoose"},
                                                  {"placement", {{"preset", "topLeft"}, {"size", {10, 10}},
                                                                 {"relativeTo", "canvas"}}}});
        checkRect(resolvedAt(loose, 1920.0f, 1080.0f), 0.0f, 0.0f, 10.0f, 10.0f);
    }

    TEST_CASE("ui_add_widget is strict: a bad argument creates nothing")
    {
        UIToolFixture fixture("VertexForge_McpUi_Strict");
        const uint32_t canvas = fixture.makeCanvas();
        const std::size_t children = fixture.childrenOf(canvas).size();
        const std::size_t stack = fixture.undoStack.size();

        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Knob"}, {"parent", canvas}}), "must be one of"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas},
                                                          {"label", {{"bogus", 1}}}}), "bogus"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas},
                                                          {"label", {{"fontSize", 5000}}}}), "fontSize"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Slider"}, {"parent", canvas}, {"text", "x"}}),
                       "text"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas},
                                                          {"button", {{"interactable", false}}}}), "no 'button' section"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas},
                                                          {"placement", placement("top", 10.0f, 10.0f)},
                                                          {"rect", {{"sizeDelta", {10, 10}}}}}), "not both"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas},
                                                          {"placement", {{"preset", "middle-ish"}}}}), "preset"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas}, {"name", ""}}),
                       "name"));
        CHECK(contains(fixture.argError("ui_add_widget", {{"type", "Label"}, {"parent", canvas}, {"colour", "red"}}),
                       "unknown argument"));
        // Not under a canvas.
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", fixture.rootId()}}), "canvas"));
        CHECK(fixture.childrenOf(canvas).size() == children);
        CHECK(fixture.undoStack.size() == stack);

        // An attachable section the type lacks is attached at creation; raw rect values override the placement.
        const uint32_t masked = fixture.addWidget({{"type", "Rect"}, {"parent", canvas}, {"name", "McpUiMasked"},
                                                   {"mask", {{"alphaThreshold", 0.25}}},
                                                   {"rect", {{"anchorMin", {0, 0}}, {"anchorMax", {1, 1}},
                                                             {"sizeDelta", {-20, -20}}}}});
        REQUIRE(reg().all_of<components::UIMaskComponent>(enttOf(masked)));
        CHECK(reg().get<components::UIMaskComponent>(enttOf(masked)).alphaThreshold == doctest::Approx(0.25f));
        checkRect(resolvedAt(masked, 1920.0f, 1080.0f), 10.0f, 10.0f, 1900.0f, 1060.0f);
        CHECK(fixture.undoStack.size() == stack + 1);
    }

    TEST_CASE("ui_get reports a widget and ui_set accepts the whole report back unchanged")
    {
        UIToolFixture fixture("VertexForge_McpUi_Echo");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiEcho"},
                                                   {"placement", placement("bottomRight", 200.0f, 60.0f, 30)}});

        nlohmann::json report = fixture.callOk("ui_get", {{"entity", button}});
        CHECK(report["entity"] == button);
        CHECK(report["type"] == "Button");
        CHECK(report["canvasId"] == canvas);
        CHECK(report["parent"] == canvas);
        for (const char* key : {"rect", "image", "label", "button", "resolved"})
        {
            CAPTURE(key);
            CHECK(report.contains(key));
        }
        checkResolvedJson(report["resolved"], 1690.0f, 990.0f, 200.0f, 60.0f);

        const std::size_t stack = fixture.undoStack.size();
        nlohmann::json out = fixture.callOk("ui_set", report);
        CHECK(out["changed"] == false);
        CHECK(fixture.undoStack.size() == stack);

        // With ranges (a derived key, ignored) and for the canvas too.
        CHECK(fixture.callOk("ui_set", fixture.callOk("ui_get", {{"entity", button}, {"ranges", true}}))["changed"] == false);
        CHECK(fixture.callOk("ui_set", fixture.callOk("ui_get", {{"entity", canvas}}))["changed"] == false);
        CHECK(fixture.undoStack.size() == stack);

        // An identity key that does not echo the current value is refused.
        nlohmann::json wrongType = report;
        wrongType["type"] = "Label";
        CHECK(contains(fixture.argError("ui_set", wrongType), "read-only"));
        nlohmann::json wrongParent = report;
        wrongParent["parent"] = fixture.rootId();
        CHECK(contains(fixture.argError("ui_set", wrongParent), "entity_set_parent"));
    }

    TEST_CASE("ui_set is strict, all-or-nothing and cross-checked")
    {
        UIToolFixture fixture("VertexForge_McpUi_SetStrict");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiStrict"}});
        const uint32_t bar = fixture.addWidget({{"type", "ProgressBar"}, {"parent", canvas}, {"name", "McpUiStrictBar"}});
        const std::size_t stack = fixture.undoStack.size();

        // The label is valid but the button colour is not: nothing changes.
        CHECK(contains(fixture.argError("ui_set", {{"entity", button}, {"label", {{"text", "Go"}}},
                                                   {"button", {{"normalColor", {2, 0, 0, 1}}}}}), "normalColor"));
        CHECK(reg().get<components::UILabelComponent>(enttOf(button)).text == "Button");
        CHECK(contains(fixture.argError("ui_set", {{"entity", button}, {"label", {{"textt", "Go"}}}}), "textt"));
        CHECK(contains(fixture.argError("ui_set", {{"entity", button}, {"label", {{"horizontalAlignment", "middle"}}}}),
                       "horizontalAlignment"));
        CHECK(contains(fixture.argError("ui_set", {{"entity", bar}, {"progressBar", {{"minValue", 5}, {"maxValue", 1}}}}),
                       "progressBar"));
        CHECK(contains(fixture.argError("ui_set", {{"entity", bar}, {"progressBar", {{"value", 3}}}}), "progressBar"));
        CHECK(contains(fixture.argError("ui_set", {{"entity", button}, {"name", ""}}), "name"));
        CHECK(fixture.undoStack.size() == stack);

        // A valid multi-section call is one undo step.
        nlohmann::json out = fixture.callOk("ui_set", {{"entity", button}, {"label", {{"text", "Go"}}},
                                                       {"button", {{"normalColor", "#336699"}}},
                                                       {"name", "McpUiGo"}});
        CHECK(out["changed"] == true);
        CHECK(reg().get<components::UILabelComponent>(enttOf(button)).text == "Go");
        checkColor(reg().get<components::UIButtonComponent>(enttOf(button)).normalColor, 0x33 / 255.0f, 0x66 / 255.0f,
                   0x99 / 255.0f, 1.0f);
        CHECK(nameOf(button) == "McpUiGo");
        REQUIRE(fixture.undoStack.size() == stack + 1);

        fixture.undoLast();
        CHECK(reg().get<components::UILabelComponent>(enttOf(button)).text == "Button");
        checkColor(reg().get<components::UIButtonComponent>(enttOf(button)).normalColor, 0.16f, 0.42f, 0.86f, 1.0f);
        CHECK(nameOf(button) == "McpUiStrict");
        fixture.redoLast();
        CHECK(reg().get<components::UILabelComponent>(enttOf(button)).text == "Go");
        CHECK(nameOf(button) == "McpUiGo");

        // Raising the max and the value together passes the cross-checks.
        fixture.callOk("ui_set", {{"entity", bar}, {"progressBar", {{"maxValue", 100}, {"value", 75}}}});
        CHECK(reg().get<components::UIProgressBarComponent>(enttOf(bar)).maxValue == 100.0f);
        CHECK(reg().get<components::UIProgressBarComponent>(enttOf(bar)).value == 75.0f);
    }

    TEST_CASE("ui_set attaches a section, and undo removes it again")
    {
        UIToolFixture fixture("VertexForge_McpUi_Attach");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t panel = fixture.addWidget({{"type", "Rect"}, {"parent", canvas}, {"name", "McpUiAttach"}});
        const std::size_t stack = fixture.undoStack.size();

        nlohmann::json out = fixture.callOk("ui_set", {{"entity", panel}, {"mask", {{"alphaThreshold", 0.3}}},
                                                       {"image", {{"colorTint", {0.2, 0.2, 0.2, 1}}}}});
        CHECK(out["changed"] == true);
        REQUIRE(reg().all_of<components::UIMaskComponent, components::UIImageComponent>(enttOf(panel)));
        CHECK(reg().get<components::UIMaskComponent>(enttOf(panel)).alphaThreshold == doctest::Approx(0.3f));
        REQUIRE(fixture.undoStack.size() == stack + 1);

        fixture.undoLast();
        CHECK_FALSE(reg().any_of<components::UIMaskComponent, components::UIImageComponent>(enttOf(panel)));
        fixture.redoLast();
        REQUIRE(reg().all_of<components::UIMaskComponent, components::UIImageComponent>(enttOf(panel)));
        CHECK(reg().get<components::UIMaskComponent>(enttOf(panel)).alphaThreshold == doctest::Approx(0.3f));

        // A section that cannot be attached.
        CHECK(contains(fixture.failure("ui_set", {{"entity", panel}, {"button", {{"interactable", false}}}}),
                       "has no UIButton"));
        CHECK(fixture.undoStack.size() == stack + 1);
    }

    TEST_CASE("ui_set never writes the state-driven image tint of a button (review fix)")
    {
        UIToolFixture fixture("VertexForge_McpUi_ButtonTint");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiTint"}});
        const glm::vec4 tint = reg().get<components::UIImageComponent>(enttOf(button)).colorTint;
        const std::size_t stack = fixture.undoStack.size();

        // A different tint is validated, then kept as the engine has it, with a pointer to the state colours.
        nlohmann::json out = fixture.callOk("ui_set", {{"entity", button}, {"image", {{"colorTint", {0.9, 0.1, 0.1, 1}}}}});
        CHECK(out["changed"] == false);
        REQUIRE(out["warnings"].size() == 1);
        CHECK(contains(out["warnings"][0].get<std::string>(), "button.normalColor"));
        checkColor(reg().get<components::UIImageComponent>(enttOf(button)).colorTint, tint.r, tint.g, tint.b, tint.a);
        CHECK(fixture.undoStack.size() == stack);

        // An invalid tint still fails the call.
        CHECK(contains(fixture.failure("ui_set", {{"entity", button}, {"image", {{"colorTint", {2, 0, 0, 1}}}}}),
                       "image.colorTint"));

        // The tint can drift mid-transition (the runtime lerps it every frame): echoing an old value is no change.
        reg().get<components::UIImageComponent>(enttOf(button)).colorTint = tint + glm::vec4(1e-5f, 0.0f, 0.0f, 0.0f);
        out = fixture.callOk("ui_set", {{"entity", button}, {"image", {{"colorTint", {tint.r, tint.g, tint.b, tint.a}}}}});
        CHECK(out["changed"] == false);
        CHECK(out["warnings"].empty());
        CHECK(fixture.undoStack.size() == stack);

        // Other image keys of a button still apply.
        out = fixture.callOk("ui_set", {{"entity", button}, {"image", {{"imageType", "tiled"}}}});
        CHECK(out["changed"] == true);
        CHECK(reg().get<components::UIImageComponent>(enttOf(button)).imageType == components::UIImageType::Tiled);
        CHECK(fixture.undoStack.size() == stack + 1);
    }

    TEST_CASE("ui_set placement moves the widget's descendants along in one undo step")
    {
        UIToolFixture fixture("VertexForge_McpUi_Move");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t panel = fixture.addWidget({{"type", "Image"}, {"parent", canvas}, {"name", "McpUiMovePanel"},
                                                  {"placement", placement("center", 400.0f, 300.0f)}});
        const uint32_t child = fixture.addWidget({{"type", "Label"}, {"parent", panel}, {"name", "McpUiMoveChild"},
                                                  {"placement", placement("topLeft", 100.0f, 30.0f, 10)}});
        checkRect(resolvedAt(panel, 1920.0f, 1080.0f), 760.0f, 390.0f, 400.0f, 300.0f);
        checkRect(resolvedAt(child, 1920.0f, 1080.0f), 770.0f, 400.0f, 100.0f, 30.0f);
        const components::UIRectComponent panelBefore = rectOf(panel);
        const components::UIRectComponent childBefore = rectOf(child);
        const std::size_t stack = fixture.undoStack.size();

        nlohmann::json out = fixture.callOk("ui_set", {{"entity", panel},
                                                       {"placement", placement("topLeft", 400.0f, 300.0f, 20)}});
        CHECK(out["movedDescendants"] == 1);
        checkRect(resolvedAt(panel, 1920.0f, 1080.0f), 20.0f, 20.0f, 400.0f, 300.0f);
        checkRect(resolvedAt(child, 1920.0f, 1080.0f), 30.0f, 30.0f, 100.0f, 30.0f);
        // Still glued at another resolution.
        const utilities::ui::PixelRect scaled = resolvedAt(panel, 1280.0f, 720.0f);
        checkRect(resolvedAt(child, 1280.0f, 720.0f), scaled.x + 10.0f * 2.0f / 3.0f, scaled.y + 10.0f * 2.0f / 3.0f,
                  100.0f * 2.0f / 3.0f, 30.0f * 2.0f / 3.0f);
        REQUIRE(fixture.undoStack.size() == stack + 1);

        fixture.undoLast();
        checkVec2(rectOf(panel).anchorMin, panelBefore.anchorMin.x, panelBefore.anchorMin.y);
        checkVec2(rectOf(panel).anchoredPosition, panelBefore.anchoredPosition.x, panelBefore.anchoredPosition.y);
        checkVec2(rectOf(child).anchorMin, childBefore.anchorMin.x, childBefore.anchorMin.y);
        checkVec2(rectOf(child).anchoredPosition, childBefore.anchoredPosition.x, childBefore.anchoredPosition.y);

        // Without moveDescendants only the widget moves; the size defaults to the current one.
        fixture.callOk("ui_set", {{"entity", panel}, {"placement", {{"preset", "bottom"}}}, {"moveDescendants", false}});
        checkRect(resolvedAt(panel, 1920.0f, 1080.0f), 760.0f, 780.0f, 400.0f, 300.0f);
        checkRect(resolvedAt(child, 1920.0f, 1080.0f), 770.0f, 400.0f, 100.0f, 30.0f);
    }

    TEST_CASE("ui_set keeps a dropdown's label in step and preserves runtime state")
    {
        UIToolFixture fixture("VertexForge_McpUi_Dropdown");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t dropdown = fixture.addWidget({{"type", "Dropdown"}, {"parent", canvas}, {"name", "McpUiDifficulty"}});

        fixture.callOk("ui_set", {{"entity", dropdown},
                                  {"dropdown", {{"options", nlohmann::json::array({"Easy", "Hard"})}, {"selectedIndex", 1}}}});
        CHECK(reg().get<components::UIDropdownComponent>(enttOf(dropdown)).options.size() == 2);
        CHECK(reg().get<components::UILabelComponent>(enttOf(dropdown)).text == "Hard");

        // An explicit label text wins.
        fixture.callOk("ui_set", {{"entity", dropdown}, {"dropdown", {{"selectedIndex", 0}}}, {"label", {{"text", "Pick"}}}});
        CHECK(reg().get<components::UILabelComponent>(enttOf(dropdown)).text == "Pick");
        CHECK(contains(fixture.argError("ui_set", {{"entity", dropdown}, {"dropdown", {{"selectedIndex", 5}}}}),
                       "selectedIndex"));

        // Runtime state survives a write of the authored values.
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiHover"}});
        reg().get<components::UIButtonComponent>(enttOf(button)).currentState = components::UIButtonState::Hovered;
        fixture.callOk("ui_set", {{"entity", button}, {"button", {{"normalColor", {0.5, 0.5, 0.5, 1}}}}});
        CHECK(reg().get<components::UIButtonComponent>(enttOf(button)).currentState == components::UIButtonState::Hovered);
        checkColor(reg().get<components::UIButtonComponent>(enttOf(button)).normalColor, 0.5f, 0.5f, 0.5f, 1.0f);
    }

    TEST_CASE("ui_set refuses a canvas rect, a checkbox root's label and non-UI entities")
    {
        UIToolFixture fixture("VertexForge_McpUi_SetRefusals");
        const uint32_t canvas = fixture.makeCanvas();
        CHECK(contains(fixture.failure("ui_set", {{"entity", canvas}, {"rect", {{"anchorMin", {0.1, 0.1}}}}}), "canvas"));
        CHECK(contains(fixture.failure("ui_set", {{"entity", canvas}, {"placement", {{"preset", "center"}}}}), "canvas"));

        nlohmann::json checkbox =
            fixture.callOk("ui_add_widget", {{"type", "Checkbox"}, {"parent", canvas}, {"name", "McpUiSound"}});
        const uint32_t part = checkbox["parts"]["label"].get<uint32_t>();
        CHECK(contains(fixture.failure("ui_set", {{"entity", checkbox["entity"]}, {"label", {{"text", "Sound"}}}}),
                       "entity " + std::to_string(part)));
        // The part itself takes the text.
        fixture.callOk("ui_set", {{"entity", part}, {"label", {{"text", "Sound"}}}});
        CHECK(reg().get<components::UILabelComponent>(enttOf(part)).text == "Sound");

        CHECK(contains(fixture.failure("ui_set", {{"entity", fixture.rootId()}, {"label", {{"text", "x"}}}}),
                       "not a UI entity"));
        CHECK(contains(fixture.failure("ui_get", {{"entity", fixture.rootId()}}), "not a UI entity"));
    }

    TEST_CASE("undo and redo of a created widget follow it through new ids")
    {
        UIToolFixture fixture("VertexForge_McpUi_UndoRedo");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiFire"},
                                                   {"text", "Fire"}, {"placement", placement("bottom", 160.0f, 50.0f, 40)}});
        const std::size_t stack = fixture.undoStack.size();

        fixture.undoLast();
        CHECK_FALSE(alive(button));
        fixture.redoLast();
        const uint32_t again = UIToolFixture::live(button);
        REQUIRE(alive(again));
        CHECK(reg().get<components::UILabelComponent>(enttOf(again)).text == "Fire");
        checkRect(resolvedAt(again, 1920.0f, 1080.0f), 880.0f, 990.0f, 160.0f, 50.0f);
        CHECK(fixture.undoStack.size() == stack);

        // A later edit recorded against the new id undoes against it.
        fixture.callOk("ui_set", {{"entity", again}, {"label", {{"text", "Shoot"}}}});
        fixture.undoLast();
        CHECK(reg().get<components::UILabelComponent>(enttOf(again)).text == "Fire");
    }

    TEST_CASE("ui_save_prefab writes a .vfPrefab inside the project and is not undoable")
    {
        UIToolFixture fixture("VertexForge_McpUi_SavePrefab");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiPrefabButton"}});
        const std::size_t stack = fixture.undoStack.size();

        nlohmann::json out = fixture.callOk("ui_save_prefab", {{"entity", button}, {"path", "ui/Btn.vfPrefab"}});
        CHECK(out["path"] == "ui/Btn.vfPrefab");
        CHECK(out["overwritten"] == false);
        CHECK(std::filesystem::is_regular_file(fixture.file("ui/Btn.vfPrefab")));
        REQUIRE(fixture.savedAssets.size() == 1);
        CHECK(contains(fixture.savedAssets.front(), "Btn.vfPrefab"));
        CHECK(reg().all_of<components::PrefabInstanceComponent>(enttOf(button)));
        CHECK(fixture.undoStack.size() == stack);

        CHECK(contains(fixture.failure("ui_save_prefab", {{"entity", button}, {"path", "ui/Btn.vfPrefab"}}), "overwrite"));
        CHECK(fixture.callOk("ui_save_prefab", {{"entity", button}, {"path", "ui/Btn.vfPrefab"},
                                                {"overwrite", true}})["overwritten"] == true);
        CHECK(contains(fixture.argError("ui_save_prefab", {{"entity", button}, {"path", "../Outside.vfPrefab"}}),
                       "outside"));
        CHECK(contains(fixture.argError("ui_save_prefab", {{"entity", button}, {"path", "ui/Btn.json"}}),
                       "must have the .vfPrefab extension"));
        CHECK(contains(fixture.failure("ui_save_prefab", {{"entity", fixture.rootId()}, {"path", "ui/Root.vfPrefab"}}),
                       "not a UI entity"));
        CHECK(fixture.undoStack.size() == stack);
    }

    TEST_CASE("ui_instantiate_prefab adds a widget prefab under a canvas in one undo step")
    {
        UIToolFixture fixture("VertexForge_McpUi_Instantiate");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiTemplate"},
                                                   {"text", "Buy"}});
        fixture.callOk("ui_save_prefab", {{"entity", button}, {"path", "ui/Buy.vfPrefab"}});
        const std::size_t stack = fixture.undoStack.size();

        nlohmann::json out = fixture.callOk("ui_instantiate_prefab",
            {{"path", "ui/Buy.vfPrefab"}, {"parent", canvas}, {"name", "McpUiBuyCopy"},
             {"placement", placement("bottomRight", 200.0f, 60.0f, 10)}});
        const uint32_t copy = out["entity"].get<uint32_t>();
        REQUIRE(alive(copy));
        CHECK(nameOf(copy) == "McpUiBuyCopy");
        CHECK(out["canvasId"] == canvas);
        CHECK(reg().get<components::UILabelComponent>(enttOf(copy)).text == "Buy");
        checkRect(resolvedAt(copy, 1920.0f, 1080.0f), 1710.0f, 1010.0f, 200.0f, 60.0f);
        REQUIRE(fixture.undoStack.size() == stack + 1);
        fixture.undoLast();
        CHECK_FALSE(alive(copy));
        fixture.redoLast();
        CHECK(alive(UIToolFixture::live(copy)));

        // A widget prefab needs a canvas; nothing is left behind.
        const std::size_t rootChildren = fixture.childrenOf(fixture.rootId()).size();
        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/Buy.vfPrefab"}}), "without a canvas"));
        CHECK(fixture.childrenOf(fixture.rootId()).size() == rootChildren);

        // A canvas prefab cannot go inside UI, but goes to the scene root.
        fixture.callOk("ui_save_prefab", {{"entity", canvas}, {"path", "ui/Hud.vfPrefab"}});
        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/Hud.vfPrefab"}, {"parent", button}}),
                       "drawn twice"));
        const uint32_t hud = fixture.callOk("ui_instantiate_prefab", {{"path", "ui/Hud.vfPrefab"}})["entity"].get<uint32_t>();
        CHECK(reg().all_of<components::UICanvasComponent>(enttOf(hud)));

        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/Missing.vfPrefab"}, {"parent", canvas}}),
                       "does not exist"));
        CHECK(fixture.undoStack.size() == stack + 2);
    }

    TEST_CASE("ListView rows are built from the template, refused for edits and rebuilt after undo / redo")
    {
        UIToolFixture fixture("VertexForge_McpUi_ListRows");
        const uint32_t canvas = fixture.makeCanvas("McpUiListCanvas");
        const uint32_t row = fixture.addWidget({{"type", "Label"}, {"parent", canvas}, {"name", "McpUiRow"},
                                                {"text", "Row"}});
        fixture.callOk("ui_save_prefab", {{"entity", row}, {"path", "ui/Row.vfPrefab"}});

        const uint32_t list = fixture.addWidget({{"type", "ListView"}, {"parent", canvas}, {"name", "McpUiRows"},
                                                 {"listView", {{"itemTemplateRef", "ui/Row.vfPrefab"}, {"itemCount", 3}}}});
        REQUIRE(fixture.listItemsUnder(list) == 3);

        const uint32_t item = static_cast<uint32_t>(fixture.childrenOf(list).front());
        CHECK(contains(fixture.failure("ui_set", {{"entity", item}, {"label", {{"text", "x"}}}}), "ListView row"));
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", list}}), "ListView"));

        // The rows are never part of the snapshot, so the redo rebuilds them from the template.
        fixture.undoLast();
        CHECK_FALSE(alive(list));
        fixture.redoLast();
        const uint32_t again = UIToolFixture::live(list);
        REQUIRE(alive(again));
        CHECK(fixture.listItemsUnder(again) == 3);

        // ... and so does a prefab instantiation.
        fixture.callOk("ui_save_prefab", {{"entity", canvas}, {"path", "ui/Menu.vfPrefab"}});
        nlohmann::json copy = fixture.callOk("ui_instantiate_prefab", {{"path", "ui/Menu.vfPrefab"}});
        CHECK(copy["listItemsBuilt"] == 3);
    }

    TEST_CASE("ui_click clicks buttons and toggles checkboxes in Play mode only")
    {
        UIToolFixture fixture("VertexForge_McpUi_Click");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiClickMe"}});
        nlohmann::json checkbox =
            fixture.callOk("ui_add_widget", {{"type", "Checkbox"}, {"parent", canvas}, {"name", "McpUiMusic"}});
        const uint32_t box = checkbox["entity"].get<uint32_t>();
        const uint32_t boxLabel = checkbox["parts"]["label"].get<uint32_t>();
        // The radio rule walks every checkbox of the registry: a group name no other suite uses.
        const nlohmann::json radio{{"groupName", "mcpUiClickGroup"}, {"allowUncheck", false}};
        const uint32_t radioA = fixture.addWidget({{"type", "Checkbox"}, {"parent", canvas}, {"name", "McpUiRadioA"},
                                                   {"checkbox", radio}});
        const uint32_t radioB = fixture.addWidget({{"type", "Checkbox"}, {"parent", canvas}, {"name", "McpUiRadioB"},
                                                   {"checkbox", radio}});
        const uint32_t locked = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiLocked"},
                                                   {"button", {{"interactable", false}}}});
        const uint32_t hidden = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiHidden"},
                                                   {"active", false}});
        const uint32_t text = fixture.addWidget({{"type", "Label"}, {"parent", canvas}, {"name", "McpUiJustText"}});
        const std::size_t stack = fixture.undoStack.size();

        CHECK(contains(fixture.failure("ui_click", {{"entity", button}}), "Play mode"));
        fixture.playMode = true;

        // Cleared with the dispatcher by the fixture, before anything else could publish.
        std::vector<std::string> published;
        auto& d = events::EventDispatcher::instance();
        (void)d.subscribe<events::ui::UIButtonPressedNotification>(
            [&published](const events::ui::UIButtonPressedNotification& n) { published.push_back("pressed:" + n.entityName); });
        (void)d.subscribe<events::ui::UIButtonReleasedNotification>(
            [&published](const events::ui::UIButtonReleasedNotification& n) { published.push_back("released:" + n.entityName); });
        (void)d.subscribe<events::ui::UIButtonClickedNotification>(
            [&published](const events::ui::UIButtonClickedNotification& n) { published.push_back("clicked:" + n.entityName); });

        nlohmann::json clicked = fixture.callOk("ui_click", {{"entity", button}});
        CHECK(clicked["kind"] == "button");
        CHECK(clicked["target"] == button);
        CHECK(clicked["published"].size() == 3);
        CHECK(published == std::vector<std::string>{"pressed:McpUiClickMe", "released:McpUiClickMe", "clicked:McpUiClickMe"});

        // A checkbox toggles; its label part clicks it too.
        nlohmann::json toggled = fixture.callOk("ui_click", {{"entity", box}});
        CHECK(toggled["kind"] == "checkbox");
        CHECK(reg().get<components::UICheckboxComponent>(enttOf(box)).isChecked);
        REQUIRE(toggled["toggles"].size() == 1);
        CHECK(toggled["toggles"][0]["checked"] == true);
        CHECK(toggled["toggles"][0]["previous"] == false);
        nlohmann::json viaLabel = fixture.callOk("ui_click", {{"entity", boxLabel}});
        CHECK(viaLabel["target"] == box);
        CHECK_FALSE(reg().get<components::UICheckboxComponent>(enttOf(box)).isChecked);

        // Radio group: checking B unchecks A (the peer is reported first); a checked strict radio stays checked.
        fixture.callOk("ui_click", {{"entity", radioA}});
        nlohmann::json radioClick = fixture.callOk("ui_click", {{"entity", radioB}});
        CHECK(reg().get<components::UICheckboxComponent>(enttOf(radioB)).isChecked);
        CHECK_FALSE(reg().get<components::UICheckboxComponent>(enttOf(radioA)).isChecked);
        REQUIRE(radioClick["toggles"].size() == 2);
        CHECK(radioClick["toggles"][0]["entity"] == radioA);
        CHECK(radioClick["toggles"][1]["entity"] == radioB);
        CHECK(fixture.callOk("ui_click", {{"entity", radioB}})["toggles"].empty());
        CHECK(reg().get<components::UICheckboxComponent>(enttOf(radioB)).isChecked);

        // The same checks as a real click.
        CHECK(contains(fixture.failure("ui_click", {{"entity", locked}}), "not interactable"));
        CHECK(contains(fixture.failure("ui_click", {{"entity", hidden}}), "inactive"));
        CHECK(contains(fixture.failure("ui_click", {{"entity", text}}), "neither a button"));

        fixture.loadPending = true;
        CHECK(contains(fixture.failure("ui_click", {{"entity", button}}), "scene load"));
        CHECK(fixture.undoStack.size() == stack);
    }

    TEST_CASE("the editing tools refuse in Play mode, World mode and during a scene load")
    {
        UIToolFixture fixture("VertexForge_McpUi_Modes");
        const uint32_t canvas = fixture.makeCanvas();
        const uint32_t button = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiModes"}});
        fixture.callOk("ui_save_prefab", {{"entity", button}, {"path", "ui/Modes.vfPrefab"}});
        const std::size_t stack = fixture.undoStack.size();

        fixture.playMode = true;
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", canvas}}), "Play mode"));
        CHECK(contains(fixture.failure("ui_set", {{"entity", button}, {"label", {{"text", "x"}}}}), "Play mode"));
        CHECK(contains(fixture.failure("ui_save_prefab", {{"entity", button}, {"path", "ui/Play.vfPrefab"}}), "Play mode"));
        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/Modes.vfPrefab"}, {"parent", canvas}}),
                       "Play mode"));
        // Reading works in Play.
        CHECK(fixture.callOk("ui_get", {{"entity", button}})["type"] == "Button");
        CHECK(fixture.callOk("ui_list_widget_types", nlohmann::json::object())["widgets"].size() == 11);
        fixture.playMode = false;

        fixture.worldMode = true;
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", canvas}}), "World mode"));
        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/Modes.vfPrefab"}, {"parent", canvas}}),
                       "World mode"));
        // Editing existing UI is fine in World mode.
        CHECK(fixture.callOk("ui_set", {{"entity", button}, {"label", {{"text", "World"}}}})["changed"] == true);
        fixture.worldMode = false;

        fixture.loadPending = true;
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", canvas}}), "scene load"));
        CHECK(contains(fixture.failure("ui_set", {{"entity", button}, {"label", {{"text", "y"}}}}), "scene load"));
        CHECK(fixture.undoStack.size() == stack + 1);
    }

    TEST_CASE("ui_get lists the canvases and reports a subtree")
    {
        UIToolFixture fixture("VertexForge_McpUi_Get");
        const uint32_t canvas = fixture.makeCanvas("McpUiGetCanvas");
        const uint32_t panel = fixture.addWidget({{"type", "Rect"}, {"parent", canvas}, {"name", "McpUiGetPanel"}});
        const uint32_t label = fixture.addWidget({{"type", "Label"}, {"parent", panel}, {"name", "McpUiGetLabel"}});

        // Every canvas of the registry is listed (other suites may have left some): ours is among them.
        nlohmann::json canvases = fixture.callOk("ui_get", nlohmann::json::object());
        bool found = false;
        for (const nlohmann::json& entry : canvases["canvases"])
        {
            if (entry.at("entity") == canvas)
            {
                found = true;
                CHECK(entry.at("name") == "McpUiGetCanvas");
                CHECK(entry.at("widgets") == 2);
                CHECK(entry.at("canvas").is_object());
            }
        }
        CHECK(found);

        nlohmann::json tree = fixture.callOk("ui_get", {{"entity", canvas}, {"recursive", true}});
        REQUIRE(tree["widgets"].size() == 3);
        CHECK(tree["widgets"][0]["entity"] == canvas);
        CHECK(tree["widgets"][1]["entity"] == panel);
        CHECK(tree["widgets"][2]["entity"] == label);
        CHECK(tree["count"] == 3);
        CHECK(tree["truncated"] == false);

        // Resolved at another viewport: the canvas scale follows it.
        nlohmann::json atViewport = fixture.callOk("ui_get", {{"entity", panel}, {"viewport", {1280, 720}}});
        CHECK(atViewport["resolved"]["scale"].get<float>() == doctest::Approx(2.0f / 3.0f));
        CHECK(contains(fixture.argError("ui_get", {{"entity", panel}, {"viewport", {0, 720}}}), "viewport"));
    }

    TEST_CASE("the UI Layer Builder's working copy is neither listed nor edited (review fix)")
    {
        UIToolFixture fixture("VertexForge_McpUi_Sandbox");
        const uint32_t canvas = fixture.makeCanvas("McpUiSceneCanvas");
        const uint32_t sceneButton = fixture.addWidget({{"type", "Button"}, {"parent", canvas}, {"name", "McpUiSceneButton"}});
        fixture.callOk("ui_save_prefab", {{"entity", sceneButton}, {"path", "ui/SandboxTest.vfPrefab"}});

        // What the builder opens: a canvas it tags as its preview sandbox, with widgets below it.
        const uint32_t sandbox = fixture.makeCanvas("McpUiBuilderLayer");
        const uint32_t inside = fixture.addWidget({{"type", "Button"}, {"parent", sandbox}, {"name", "McpUiBuilderButton"}});
        events::ui::MarkUIPreviewSandboxCommand mark;
        mark.entity = handleOf(sandbox);
        REQUIRE(events::EventDispatcher::instance().execute(mark));
        const std::size_t stack = fixture.undoStack.size();
        const std::size_t sandboxChildren = fixture.childrenOf(sandbox).size();

        bool sceneListed = false;
        bool sandboxListed = false;
        const nlohmann::json listing = fixture.callOk("ui_get", nlohmann::json::object());
        for (const nlohmann::json& entry : listing.at("canvases"))
        {
            sceneListed = sceneListed || entry.at("entity") == canvas;
            sandboxListed = sandboxListed || entry.at("entity") == sandbox;
        }
        CHECK(sceneListed);
        CHECK_FALSE(sandboxListed);

        const std::string refusal = "UI Layer Builder";
        CHECK(contains(fixture.failure("ui_get", {{"entity", sandbox}}), refusal));
        CHECK(contains(fixture.failure("ui_get", {{"entity", inside}, {"recursive", true}}), refusal));
        CHECK(contains(fixture.failure("ui_set", {{"entity", inside}, {"label", {{"text", "x"}}}}), refusal));
        CHECK(contains(fixture.failure("ui_add_widget", {{"type", "Label"}, {"parent", sandbox}}), refusal));
        CHECK(contains(fixture.failure("ui_save_prefab", {{"entity", inside}, {"path", "ui/Builder.vfPrefab"}}), refusal));
        CHECK(contains(fixture.failure("ui_instantiate_prefab", {{"path", "ui/SandboxTest.vfPrefab"}, {"parent", sandbox}}),
                       refusal));
        fixture.playMode = true;
        CHECK(contains(fixture.failure("ui_click", {{"entity", inside}}), refusal));
        fixture.playMode = false;

        CHECK(fixture.childrenOf(sandbox).size() == sandboxChildren);
        CHECK(reg().get<components::UILabelComponent>(enttOf(inside)).text != "x");
        CHECK_FALSE(std::filesystem::exists(fixture.file("ui/Builder.vfPrefab")));
        CHECK(fixture.undoStack.size() == stack);
    }
}
