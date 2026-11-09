#include <doctest.h>
#include <ostream> // doctest stringifies std::string, std::string_view and nlohmann::json through operator<<

#include "protocol/ArgReader.hpp"
#include "tools/SettingsTable.hpp"
#include "tools/ToolHelpers.hpp"
#include "tools/UIToolSupport.hpp"
#include "undo/EntityIdRemap.hpp"
#include "undo/UIUndo.hpp"

#include <asset/AssetDatabase.hpp>
#include <data/DTOs.hpp>
#include <data/EntityConversion.hpp>
#include <data/EntityHandle.hpp>
#include <events/EventDispatcher.hpp>
#include <impl/components/UIComponentService.hpp>
#include <impl/scene/EntityQueryService.hpp>
#include <scene/Entity.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <ui/UIRectLayout.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

// VK-1659: the UI section tables of the MCP UI tools (tools/UIToolSupport) against the real UIComponentService and
// the scene serializer. Every settable key of every section, at both ends of its range (every value of an enum, ""
// and a value for a string, "" and a GUID for an asset field), goes in through patchSection + writeSection the way
// ui_set writes it, out through scene save + reload, and must read back exactly as ui_get reported it before the
// save; every such value that differs from the default is a key of the saved component, and every saved key is a
// table key or deliberately not exposed. One step past a bound, a wrong type and a broken cross-field rule are
// refused naming the key; a read-only key passes back only unchanged; a stored value outside its range passes back
// as an echo. Then defaultSection against Add*Component, copyAuthored, UISectionUndo and the helpers (section keys,
// widget types, placement, resolved rects, the hierarchy and ui_get's widget report).
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;
    namespace uitool = mcp::tools::uitool;
    namespace layout = utilities::ui;
    using json = nlohmann::json;
    using uitool::Section;

    // A GUID no asset database knows: assignable, and reported back as given, with or without a project.
    const std::string unknownAsset = "guid:00c0ffee12345678";

    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    // The UI and entity-query services on the global dispatcher, their scene, a scratch directory, and a clean asset
    // database and id remap. The scene is cleared at the end, so no entity outlives the test.
    struct UIRangesFixture
    {
        DispatcherScope dispatcherScope;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::EntityQueryService entityQuery{sceneGraph};
        services::UIComponentService ui{sceneGraph};
        fs::path root = fs::temp_directory_path() / "vf_mcp_ui_ranges";

        UIRangesFixture()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            asset::AssetDatabase::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            events::EventDispatcher& dispatcher = events::EventDispatcher::instance();
            entityQuery.registerEventHandlers(dispatcher);
            ui.registerEventHandlers(dispatcher);
        }

        ~UIRangesFixture()
        {
            sceneGraph->clearScene();
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }

        UIRangesFixture(const UIRangesFixture&) = delete;
        UIRangesFixture& operator=(const UIRangesFixture&) = delete;

        scene::Entity make(const std::string& name)
        {
            scene::Entity entity(name);
            sceneGraph->addChild(sceneGraph->GetRoot(), entity);
            return entity;
        }

        scene::Entity make(const std::string& name, scene::Entity& parent)
        {
            scene::Entity entity(name);
            sceneGraph->addChild(parent, entity);
            return entity;
        }
    };

    // A loaded scene's entities live in the shared registry too; clear them when the case ends.
    struct SceneCleanup
    {
        scene::SceneGraphSystem& scene;

        ~SceneCleanup()
        {
            scene.clearScene();
        }
    };

    services::EntityHandle handleOf(const scene::Entity& entity)
    {
        return services::internal::toHandle(entity.getHandle());
    }

    uint32_t idOf(const scene::Entity& entity)
    {
        return mcp::tools::toId(handleOf(entity));
    }

    services::EntityData dataOf(const scene::Entity& entity)
    {
        const std::optional<services::EntityData> data = mcp::tools::findEntity(handleOf(entity));
        REQUIRE(data.has_value());
        return *data;
    }

    json readJson(const fs::path& path)
    {
        std::ifstream file(path);
        REQUIRE(file.is_open());
        json value;
        file >> value;
        return value;
    }

    // The ArgError patchSection throws, "" when it accepts the patch.
    std::string patchError(const uitool::SectionData& base, const json& patch, const std::string& prefix)
    {
        try
        {
            uitool::patchSection(base, patch, prefix);
        }
        catch (const mcp::ArgError& error)
        {
            return error.what();
        }
        return {};
    }

    // Every value a ranges entry names: both ends of a number, every enum value, false and true, "" and a value for a
    // string ("" and a GUID for an asset), the shortest and the longest list.
    std::vector<json> boundValues(const json& entry)
    {
        const std::string type = entry.at("type").get<std::string>();
        if (type == "boolean")
        {
            return {json(false), json(true)};
        }
        if (type == "number" || type == "integer")
        {
            return {entry.at("min"), entry.at("max")};
        }
        if (type == "enum")
        {
            return entry.at("values").get<std::vector<json>>();
        }
        if (type == "string")
        {
            return {json(""), entry.contains("extension") ? json(unknownAsset) : json("Score: 0 / 100")};
        }
        if (type == "vec2" || type == "vec4" || type == "color")
        {
            const std::size_t count = type == "vec2" ? 2 : 4;
            return {json(std::vector<json>(count, entry.at("min"))), json(std::vector<json>(count, entry.at("max")))};
        }
        if (type == "stringList")
        {
            std::vector<json> out;
            for (const char* side : {"minItems", "maxItems"})
            {
                json list = json::array();
                for (int64_t i = 0; i < entry.at(side).get<int64_t>(); ++i)
                {
                    list.push_back("Option " + std::to_string(i));
                }
                out.push_back(std::move(list));
            }
            return out;
        }
        throw std::logic_error("unknown range type '" + type + "'");
    }

    float floatBelow(float min)
    {
        // A clearly negative value below a 0 minimum: the next float below 0 is a denormal.
        return min == 0.0f ? -1.0e-6f : std::nextafter(min, -std::numeric_limits<float>::infinity());
    }

    float floatAbove(float max)
    {
        return std::nextafter(max, std::numeric_limits<float>::infinity());
    }

    // Values one step past a ranges entry, or of the wrong type or shape.
    std::vector<json> outsideValues(const json& entry)
    {
        const std::string type = entry.at("type").get<std::string>();
        if (type == "boolean")
        {
            return {json(1), json("true")};
        }
        if (type == "integer")
        {
            return {json(entry.at("min").get<int64_t>() - 1), json(entry.at("max").get<int64_t>() + 1), json(1.5),
                    json("1")};
        }
        if (type == "number")
        {
            return {env::floatJson(floatBelow(entry.at("min").get<float>())),
                    env::floatJson(floatAbove(entry.at("max").get<float>())), json("1"), json(true)};
        }
        if (type == "enum")
        {
            return {json("notAValue"), json(0)};
        }
        if (type == "string")
        {
            if (entry.contains("extension"))
            {
                return {json(5), json("guid:xyz"), json("ui/wrong.ext")};
            }
            return {json(5), json::array()};
        }
        if (type == "vec2" || type == "vec4")
        {
            const std::size_t count = type == "vec2" ? 2 : 4;
            const json low = env::floatJson(floatBelow(entry.at("min").get<float>()));
            const json high = env::floatJson(floatAbove(entry.at("max").get<float>()));
            return {json(std::vector<json>(count, low)), json(std::vector<json>(count, high)),
                    json(std::vector<json>(count - 1, entry.at("min"))), json("0, 0")};
        }
        if (type == "color")
        {
            const json low = env::floatJson(floatBelow(entry.at("min").get<float>()));
            const json high = env::floatJson(floatAbove(entry.at("max").get<float>()));
            return {json::array({high, 0, 0}), json::array({0, 0, 0, low}), json("#GG0000"), json("#FFF"),
                    json::array({1, 1})};
        }
        if (type == "stringList")
        {
            json tooMany = json::array();
            for (int64_t i = 0; i <= entry.at("maxItems").get<int64_t>(); ++i)
            {
                tooMany.push_back("x");
            }
            return {tooMany, json::array({"a", 1}), json("a")};
        }
        throw std::logic_error("unknown range type '" + type + "'");
    }

    // A value of a read-only entry's kind that differs from `current`.
    json otherValue(const json& entry, const json& current)
    {
        const std::string type = entry.at("type").get<std::string>();
        if (type == "boolean")
        {
            return !current.get<bool>();
        }
        if (type == "integer")
        {
            const int64_t value = current.get<int64_t>();
            return value == entry.at("max").get<int64_t>() ? value - 1 : value + 1;
        }
        if (type == "number")
        {
            return current.get<double>() + 1.0;
        }
        if (type == "enum")
        {
            for (const json& value : entry.at("values"))
            {
                if (value != current)
                {
                    return value;
                }
            }
            return "notAValue"; // a one-value enum (renderMode)
        }
        if (type == "string")
        {
            return current.get<std::string>() + "x";
        }
        throw std::logic_error("no other value for a read-only '" + type + "'");
    }

    // Keys a case sets along with its own, for the cross-field rules: minValue < maxValue, minValue <= value <=
    // maxValue, stepSize <= maxValue - minValue, selectedIndex < the option count.
    json contextFor(Section section)
    {
        switch (section)
        {
        case Section::Slider:
            return {{"minValue", -1.0e6}, {"maxValue", 1.0e6}, {"value", 0.0}, {"stepSize", 0.0}};
        case Section::ProgressBar:
            return {{"minValue", -1.0e6}, {"maxValue", 1.0e6}, {"value", 0.0}};
        case Section::Dropdown:
        {
            json options = json::array();
            for (int i = 0; i < 64; ++i)
            {
                options.push_back("Option " + std::to_string(i));
            }
            json out = json::object();
            out["options"] = std::move(options);
            return out;
        }
        default:
            return json::object();
        }
    }

    // minValue's maximum and maxValue's minimum cannot be set: minValue < maxValue, both within +/-1e6.
    bool unreachable(Section section, const std::string& key, const json& value, const json& entry)
    {
        if (section != Section::Slider && section != Section::ProgressBar)
        {
            return false;
        }
        return (key == "minValue" && value == entry.at("max")) || (key == "maxValue" && value == entry.at("min"));
    }

    // The saved scene's key of a section's component: "uiProgressBar" for UIProgressBar.
    std::string savedComponentKey(Section section)
    {
        const std::string_view name = uitool::sectionComponents[static_cast<std::size_t>(section)];
        return "ui" + std::string(name.substr(2));
    }

    // Saved component keys no table holds (the VK-1659 plan, "not exposed"): the canvas' theme reference (reported
    // read-only as "theme") and a label's text effects.
    bool notExposed(Section section, const std::string& key)
    {
        if (section == Section::Canvas && (key == "themeRef" || key == "themeRefPath"))
        {
            return true;
        }
        return section == Section::Label && key == "effects";
    }

    // "<key>Path": the path fallback the serializer writes next to the GUID of a resolvable asset field.
    bool isAssetPathKey(const json& ranges, const std::string& key)
    {
        constexpr std::string_view suffix = "Path";
        if (key.size() <= suffix.size() || !key.ends_with(suffix))
        {
            return false;
        }
        const std::string assetKey = key.substr(0, key.size() - suffix.size());
        return ranges.contains(assetKey) && ranges.at(assetKey).contains("extension");
    }
}

TEST_CASE("mcp ui ranges: every settable value at each end of its range survives ui_set, scene save and reload")
{
    UIRangesFixture fixture;

    struct Case
    {
        Section section = Section::Rect;
        std::string key;
        json value;
        std::string name;
        json report; // sectionJson after the write: what ui_get reports
    };
    std::vector<Case> cases;

    for (std::size_t s = 0; s < uitool::sectionCount; ++s)
    {
        const Section section = static_cast<Section>(s);
        const std::string prefix(uitool::sectionKey(section));
        const json ranges = uitool::sectionRanges(section);
        for (auto entry = ranges.begin(); entry != ranges.end(); ++entry)
        {
            if (entry->contains("readOnly"))
            {
                continue;
            }
            const std::vector<json> values = boundValues(*entry);
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (unreachable(section, entry.key(), values[i], *entry))
                {
                    continue;
                }
                Case c;
                c.section = section;
                c.key = entry.key();
                c.value = values[i];
                c.name = prefix + "." + c.key + "#" + std::to_string(i);
                INFO(c.name << " = " << c.value.dump());

                scene::Entity entity = fixture.make(c.name);
                const services::EntityHandle handle = handleOf(entity);
                uitool::addSection(handle, section);
                const std::optional<uitool::SectionData> live = uitool::readSection(handle, section);
                REQUIRE(live.has_value());

                json patch = contextFor(section);
                patch[c.key] = c.value;
                uitool::SectionData staged;
                REQUIRE_NOTHROW(staged = uitool::patchSection(*live, patch, prefix));
                uitool::writeSection(handle, uitool::copyAuthored(*live, staged));

                const std::optional<uitool::SectionData> written = uitool::readSection(handle, section);
                REQUIRE(written.has_value());
                c.report = uitool::sectionJson(*written);
                CHECK(c.report.at(c.key) == c.value);
                CHECK(uitool::sameAuthored(*written, staged));
                cases.push_back(std::move(c));
            }
        }
    }
    CHECK(cases.size() > 100);

    const fs::path scenePath = fixture.root / "UIRanges.vfScene";
    REQUIRE(serialization::SceneSerialization::saveScene(*fixture.sceneGraph, scenePath.string()));
    std::map<std::string, json> savedComponents;
    {
        const json sceneJson = readJson(scenePath);
        for (const json& child : sceneJson.at("root").at("children"))
        {
            savedComponents.emplace(child.at("name").get<std::string>(), child.at("components"));
        }
    }

    asset::AssetDatabase::instance().clear();
    scene::SceneGraphSystem loaded;
    SceneCleanup loadedCleanup{loaded};
    REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
    std::map<std::string, scene::Entity> reloaded;
    for (const scene::Entity& child : loaded.GetRoot().getChildren())
    {
        reloaded.emplace(child.getName(), child);
    }

    for (const Case& c : cases)
    {
        INFO(c.name << " = " << c.value.dump());

        // ui_get after the reload reports what it reported before the save, except a progress bar's displayValue:
        // runtime state that is never saved. The loader starts it at the value (SceneSerializeUIWidgets), while a
        // bar written in Edit mode keeps the displayValue it had (only the Play overlay moves it).
        const auto entity = reloaded.find(c.name);
        REQUIRE(entity != reloaded.end());
        const std::optional<uitool::SectionData> after = uitool::readSection(handleOf(entity->second), c.section);
        REQUIRE(after.has_value());
        json expected = c.report;
        if (c.section == Section::ProgressBar)
        {
            expected["displayValue"] = expected.at("value");
        }
        CHECK(uitool::sectionJson(*after) == expected);

        // A value that differs from the default is a key of the saved component ...
        const auto components = savedComponents.find(c.name);
        REQUIRE(components != savedComponents.end());
        const std::string componentKey = savedComponentKey(c.section);
        REQUIRE(components->second.contains(componentKey));
        const json& block = components->second.at(componentKey);
        const json defaults = uitool::sectionJson(uitool::defaultSection(c.section));
        if (c.value != defaults.at(c.key))
        {
            CHECK(block.contains(c.key));
        }

        // ... and every saved key is a table key or deliberately not exposed.
        const json ranges = uitool::sectionRanges(c.section);
        for (auto key = block.begin(); key != block.end(); ++key)
        {
            INFO("saved key " << key.key());
            CHECK((ranges.contains(key.key()) || notExposed(c.section, key.key()) || isAssetPathKey(ranges, key.key())));
        }
    }
}

TEST_CASE("mcp ui ranges: one step past a bound, a wrong type, an unknown key or a broken cross-field rule is refused")
{
    UIRangesFixture fixture; // a clean asset database for the asset fields

    for (std::size_t s = 0; s < uitool::sectionCount; ++s)
    {
        const Section section = static_cast<Section>(s);
        const std::string prefix(uitool::sectionKey(section));
        const uitool::SectionData base = uitool::defaultSection(section);
        const json ranges = uitool::sectionRanges(section);
        for (auto entry = ranges.begin(); entry != ranges.end(); ++entry)
        {
            if (entry->contains("readOnly"))
            {
                continue;
            }
            for (const json& bad : outsideValues(*entry))
            {
                INFO(prefix << "." << entry.key() << " = " << bad.dump());
                json patch = contextFor(section);
                patch[entry.key()] = bad;
                CHECK(patchError(base, patch, prefix).find(prefix + "." + entry.key()) != std::string::npos);
            }
        }

        INFO(prefix);
        CHECK(patchError(base, json{{"bogus", 1}}, prefix).find("unknown " + prefix + " key 'bogus'") !=
              std::string::npos);
        CHECK(patchError(base, json::array(), prefix).find("'" + prefix + "' must be an object") != std::string::npos);
    }

    // The cross-field rules name the section.
    const auto refused = [](Section section, const json& patch)
    {
        return patchError(uitool::defaultSection(section), patch, std::string(uitool::sectionKey(section)));
    };
    CHECK(refused(Section::Rect, {{"anchorMin", json::array({0.6, 0})}, {"anchorMax", json::array({0.5, 1})}})
              .find("rect: anchorMin") != std::string::npos);
    CHECK(refused(Section::Slider, {{"minValue", 1}}).find("slider: minValue (") != std::string::npos);
    CHECK(refused(Section::Slider, {{"value", 2}}).find("slider: value (") != std::string::npos);
    CHECK(refused(Section::Slider, {{"stepSize", 2}}).find("slider: stepSize (") != std::string::npos);
    CHECK(refused(Section::ProgressBar, {{"minValue", 2}}).find("progressBar: minValue (") != std::string::npos);
    CHECK(refused(Section::ProgressBar, {{"value", -1}}).find("progressBar: value (") != std::string::npos);
    CHECK(refused(Section::Dropdown, {{"selectedIndex", 0}})
              .find("dropdown: selectedIndex 0 is out of range for 0 options") != std::string::npos);
    CHECK(refused(Section::Dropdown, {{"options", json::array({"a"})}, {"selectedIndex", 1}})
              .find("dropdown: selectedIndex 1 is out of range for 1 options") != std::string::npos);

    // ... and a consistent combination passes.
    CHECK(refused(Section::Dropdown, {{"options", json::array({"a"})}, {"selectedIndex", 0}}).empty());
    CHECK(refused(Section::Slider, {{"minValue", -5}, {"maxValue", 5}, {"value", -5}, {"stepSize", 10}}).empty());
    CHECK(refused(Section::Rect, {{"anchorMin", json::array({0.5, 0.5})}, {"anchorMax", json::array({0.5, 0.5})}})
              .empty());
}

TEST_CASE("mcp ui ranges: a read-only key passes back only unchanged, and its hint says what sets it")
{
    UIRangesFixture fixture;
    std::size_t readOnlyKeys = 0;
    for (std::size_t s = 0; s < uitool::sectionCount; ++s)
    {
        const Section section = static_cast<Section>(s);
        const std::string prefix(uitool::sectionKey(section));
        const uitool::SectionData base = uitool::defaultSection(section);
        const json report = uitool::sectionJson(base);
        const json ranges = uitool::sectionRanges(section);
        for (auto entry = ranges.begin(); entry != ranges.end(); ++entry)
        {
            if (!entry->contains("readOnly"))
            {
                continue;
            }
            ++readOnlyKeys;
            const std::string key = entry.key();
            INFO(prefix << "." << key);

            uitool::SectionData echoed;
            REQUIRE_NOTHROW(echoed = uitool::patchSection(base, json{{key, report.at(key)}}, prefix));
            CHECK(uitool::sectionJson(echoed) == report);

            const json other = otherValue(*entry, report.at(key));
            const std::string error = patchError(base, json{{key, other}}, prefix);
            CHECK(error.find("'" + prefix + "." + key + "' is read-only here: " +
                             entry->at("readOnly").get<std::string>()) != std::string::npos);
        }
    }
    // canvas renderMode + theme; image sourceWidth, sourceHeight, renderTextureSourceName; the runtime state of a
    // button, checkbox, text input (currentState), slider (currentState, isDragging), progress bar (displayValue),
    // dropdown (currentState, isOpen, hoveredOptionIndex) and list view (selectedIndex).
    CHECK(readOnlyKeys == 15);
}

TEST_CASE("mcp ui ranges: a stored value outside its range passes back as an echo and survives other edits")
{
    UIRangesFixture fixture;

    scene::Entity labelEntity = fixture.make("Legacy Label");
    const services::EntityHandle labelHandle = handleOf(labelEntity);
    uitool::addSection(labelHandle, Section::Label);
    services::UILabelData stored = std::get<services::UILabelData>(*uitool::readSection(labelHandle, Section::Label));
    stored.fontSize = 500.0f; // a script or an older scene wrote it: the table takes 1-200
    stored.fontStyle = 9;     // not a FontStyle
    uitool::writeSection(labelHandle, stored);

    const uitool::SectionData label = *uitool::readSection(labelHandle, Section::Label);
    const json labelReport = uitool::sectionJson(label);
    CHECK(labelReport.at("fontSize") == 500.0);
    CHECK(labelReport.at("fontStyle") == 9);

    uitool::SectionData echoed;
    REQUIRE_NOTHROW(echoed = uitool::patchSection(label, labelReport, "label"));
    CHECK(uitool::sameAuthored(echoed, label));

    const uitool::SectionData edited = uitool::patchSection(label, json{{"fontSize", 500}, {"wordWrap", false}}, "label");
    CHECK(std::get<services::UILabelData>(edited).fontSize == 500.0f);
    CHECK(std::get<services::UILabelData>(edited).fontStyle == 9);
    CHECK_FALSE(std::get<services::UILabelData>(edited).wordWrap);
    CHECK_THROWS_AS(uitool::patchSection(label, json{{"fontSize", 501}}, "label"), mcp::ArgError);
    CHECK_THROWS_AS(uitool::patchSection(label, json{{"fontStyle", 10}}, "label"), mcp::ArgError);

    // A stored cross-field violation passes back as an echo; any edit re-checks the rules, so the agent fixes it in
    // the same call.
    scene::Entity sliderEntity = fixture.make("Legacy Slider");
    const services::EntityHandle sliderHandle = handleOf(sliderEntity);
    uitool::addSection(sliderHandle, Section::Slider);
    services::UISliderData storedSlider =
        std::get<services::UISliderData>(*uitool::readSection(sliderHandle, Section::Slider));
    storedSlider.value = 5.0f; // outside 0-1
    uitool::writeSection(sliderHandle, storedSlider);

    const uitool::SectionData slider = *uitool::readSection(sliderHandle, Section::Slider);
    REQUIRE_NOTHROW(echoed = uitool::patchSection(slider, uitool::sectionJson(slider), "slider"));
    CHECK(uitool::sameAuthored(echoed, slider));
    const std::string error = patchError(slider, json{{"interactable", false}}, "slider");
    CHECK(error.find("slider: value (5") != std::string::npos);
    CHECK(error.find("must lie between minValue (0") != std::string::npos);
    const uitool::SectionData fixedSlider =
        uitool::patchSection(slider, json{{"interactable", false}, {"value", 1}}, "slider");
    CHECK(std::get<services::UISliderData>(fixedSlider).value == 1.0f);
    CHECK_FALSE(std::get<services::UISliderData>(fixedSlider).interactable);
}

TEST_CASE("mcp ui ranges: copyAuthored takes every authored member and keeps every runtime member the engine owns")
{
    services::UIDropdownData liveDropdown;
    liveDropdown.currentState = 2;
    liveDropdown.isOpen = true;
    liveDropdown.hoveredOptionIndex = 3;
    services::UIDropdownData authoredDropdown;
    authoredDropdown.placeholderText = "Pick one";
    const auto dropdown = std::get<services::UIDropdownData>(uitool::copyAuthored(liveDropdown, authoredDropdown));
    CHECK(dropdown.placeholderText == "Pick one");
    CHECK(dropdown.currentState == 2);
    CHECK(dropdown.isOpen);
    CHECK(dropdown.hoveredOptionIndex == 3);

    services::UISliderData liveSlider;
    liveSlider.currentState = 2;
    liveSlider.isDragging = true;
    services::UISliderData authoredSlider;
    authoredSlider.value = 0.25f;
    const auto slider = std::get<services::UISliderData>(uitool::copyAuthored(liveSlider, authoredSlider));
    CHECK(slider.value == 0.25f);
    CHECK(slider.currentState == 2);
    CHECK(slider.isDragging);

    services::UIProgressBarData liveProgress;
    liveProgress.displayValue = 0.7f;
    services::UIProgressBarData authoredProgress;
    authoredProgress.value = 0.25f;
    const auto progress = std::get<services::UIProgressBarData>(uitool::copyAuthored(liveProgress, authoredProgress));
    CHECK(progress.value == 0.25f);
    CHECK(progress.displayValue == 0.7f);

    services::UIListViewData liveList;
    liveList.selectedIndex = 4;
    services::UIListViewData authoredList;
    authoredList.itemCount = 9;
    const auto list = std::get<services::UIListViewData>(uitool::copyAuthored(liveList, authoredList));
    CHECK(list.itemCount == 9);
    CHECK(list.selectedIndex == 4);

    services::UIImageData liveImage;
    liveImage.externalTextureKey = "__plugintex_7__";
    liveImage.renderTextureSource.id = 42;
    services::UIImageData authoredImage;
    authoredImage.colorTint = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    const auto image = std::get<services::UIImageData>(uitool::copyAuthored(liveImage, authoredImage));
    CHECK(image.colorTint == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(image.externalTextureKey == "__plugintex_7__");
    CHECK(image.renderTextureSource.id == 42);

    services::UIButtonData liveButton;
    liveButton.currentState = 3;
    services::UIButtonData authoredButton;
    authoredButton.interactable = false;
    const auto button = std::get<services::UIButtonData>(uitool::copyAuthored(liveButton, authoredButton));
    CHECK_FALSE(button.interactable);
    CHECK(button.currentState == 3);

    services::UICheckboxData liveCheckbox;
    liveCheckbox.currentState = 1;
    services::UICheckboxData authoredCheckbox;
    authoredCheckbox.isChecked = true;
    const auto checkbox = std::get<services::UICheckboxData>(uitool::copyAuthored(liveCheckbox, authoredCheckbox));
    CHECK(checkbox.isChecked);
    CHECK(checkbox.currentState == 1);

    services::UITextInputData liveInput;
    liveInput.currentState = 2;
    services::UITextInputData authoredInput;
    authoredInput.text = "Player";
    const auto input = std::get<services::UITextInputData>(uitool::copyAuthored(liveInput, authoredInput));
    CHECK(input.text == "Player");
    CHECK(input.currentState == 2);

    uitool::CanvasValues liveCanvas;
    liveCanvas.theme = "ui/dark.vfTheme";
    uitool::CanvasValues authoredCanvas;
    authoredCanvas.data.referenceWidth = 1280.0f;
    authoredCanvas.theme = "ui/other.vfTheme";
    const auto canvas = std::get<uitool::CanvasValues>(uitool::copyAuthored(liveCanvas, authoredCanvas));
    CHECK(canvas.data.referenceWidth == 1280.0f);
    CHECK(canvas.theme == "ui/dark.vfTheme");

    CHECK_THROWS_AS(uitool::copyAuthored(uitool::SectionData(services::UIRectData{}),
                                         uitool::SectionData(services::UIImageData{})),
                    std::logic_error);
}

TEST_CASE("mcp ui ranges: defaultSection is what Add*Component creates; add, remove and write refuse what they cannot do")
{
    UIRangesFixture fixture;
    for (std::size_t s = 0; s < uitool::sectionCount; ++s)
    {
        const Section section = static_cast<Section>(s);
        const std::string prefix(uitool::sectionKey(section));
        INFO(prefix);
        scene::Entity entity = fixture.make("Default." + prefix);
        const services::EntityHandle handle = handleOf(entity);

        CHECK_FALSE(uitool::hasSection(handle, section));
        CHECK_FALSE(uitool::readSection(handle, section).has_value());
        CHECK_THROWS_AS(uitool::writeSection(handle, uitool::defaultSection(section)), std::runtime_error);
        CHECK_THROWS_AS(uitool::removeSection(handle, section), std::runtime_error);

        uitool::addSection(handle, section);
        CHECK(uitool::hasSection(handle, section));
        const std::vector<Section> present = uitool::presentSections(handle);
        CHECK(std::find(present.begin(), present.end(), section) != present.end());
        CHECK(std::is_sorted(present.begin(), present.end()));
        const std::optional<uitool::SectionData> value = uitool::readSection(handle, section);
        REQUIRE(value.has_value());
        CHECK(uitool::sectionOf(*value) == section);
        CHECK(uitool::sectionJson(*value) == uitool::sectionJson(uitool::defaultSection(section)));
        CHECK_THROWS_AS(uitool::addSection(handle, section), std::runtime_error);

        uitool::removeSection(handle, section);
        CHECK_FALSE(uitool::hasSection(handle, section));
    }
}

TEST_CASE("mcp ui ranges: a section's schema and ranges list every key it reports, and the whole report passes back")
{
    UIRangesFixture fixture; // a clean asset database
    for (std::size_t s = 0; s < uitool::sectionCount; ++s)
    {
        const Section section = static_cast<Section>(s);
        const std::string prefix(uitool::sectionKey(section));
        INFO(prefix);
        const uitool::SectionData base = uitool::defaultSection(section);
        const json report = uitool::sectionJson(base);
        const json ranges = uitool::sectionRanges(section);
        const std::string description = "The " + prefix + " section.";
        const json schema = uitool::sectionSchema(section, description);

        CHECK(schema.at("type") == "object");
        CHECK(schema.at("additionalProperties") == false);
        CHECK(schema.at("description") == description);
        CHECK(ranges.size() == report.size());
        const json& properties = schema.at("properties");
        CHECK(properties.size() == report.size());
        for (auto it = report.begin(); it != report.end(); ++it)
        {
            INFO(prefix << "." << it.key());
            REQUIRE(ranges.contains(it.key()));
            REQUIRE(properties.contains(it.key()));
            const bool readOnly = ranges.at(it.key()).contains("readOnly");
            const std::string help = properties.at(it.key()).at("description").get<std::string>();
            CHECK((help.rfind("Read-only", 0) == 0) == readOnly);
        }

        // authoredJson is the report without its read-only keys.
        const json authored = uitool::authoredJson(base);
        for (auto it = report.begin(); it != report.end(); ++it)
        {
            INFO(prefix << "." << it.key());
            CHECK(authored.contains(it.key()) == !ranges.at(it.key()).contains("readOnly"));
        }

        uitool::SectionData echoed;
        REQUIRE_NOTHROW(echoed = uitool::patchSection(base, report, prefix));
        CHECK(uitool::sectionJson(echoed) == report);
        CHECK(uitool::sameAuthored(echoed, base));
    }
}

TEST_CASE("mcp ui ranges: UISectionUndo restores or removes each section, re-applies it on redo, never rewinds runtime state")
{
    UIRangesFixture fixture;
    scene::Entity pause = fixture.make("Pause");
    const services::EntityHandle handle = handleOf(pause);
    uitool::addSection(handle, Section::Button); // + UIRect, UIImage, UILabel
    REQUIRE_FALSE(uitool::hasSection(handle, Section::Mask));

    // The call: a button edit and an attached mask, recorded the way ui_set records them.
    const uitool::SectionData buttonBefore = *uitool::readSection(handle, Section::Button);
    const uitool::SectionData buttonAfter =
        uitool::patchSection(buttonBefore, json{{"normalColor", "#FF0000"}, {"interactable", false}}, "button");
    uitool::writeSection(handle, uitool::copyAuthored(buttonBefore, buttonAfter));
    uitool::addSection(handle, Section::Mask);
    const uitool::SectionData maskLive = *uitool::readSection(handle, Section::Mask);
    const uitool::SectionData maskAfter = uitool::patchSection(maskLive, json{{"alphaThreshold", 0.25}}, "mask");
    uitool::writeSection(handle, uitool::copyAuthored(maskLive, maskAfter));

    std::vector<mcp::undo::UISectionChange> changes;
    changes.push_back({Section::Button, buttonBefore, buttonAfter});
    changes.push_back({Section::Mask, std::nullopt, maskAfter});
    mcp::undo::UISectionUndo undo(idOf(pause), "Pause", std::move(changes));
    CHECK(undo.getDescription() == "MCP: Set UI (Pause)");
    CHECK(undo.getMemoryFootprint() >= sizeof(mcp::undo::UISectionUndo) + 2 * sizeof(mcp::undo::UISectionChange));

    // The engine moves the button on after the call (pressed).
    services::UIButtonData pressed = std::get<services::UIButtonData>(*uitool::readSection(handle, Section::Button));
    pressed.currentState = 2;
    uitool::writeSection(handle, pressed);

    undo.undo();
    CHECK_FALSE(uitool::hasSection(handle, Section::Mask));
    CHECK(uitool::hasSection(handle, Section::Rect));
    const auto undone = std::get<services::UIButtonData>(*uitool::readSection(handle, Section::Button));
    CHECK(undone.normalColor == std::get<services::UIButtonData>(buttonBefore).normalColor);
    CHECK(undone.interactable);
    CHECK(undone.currentState == 2);

    undo.execute();
    REQUIRE(uitool::hasSection(handle, Section::Mask));
    CHECK(std::get<services::UIMaskData>(*uitool::readSection(handle, Section::Mask)).alphaThreshold == 0.25f);
    const auto redone = std::get<services::UIButtonData>(*uitool::readSection(handle, Section::Button));
    CHECK(redone.normalColor == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK_FALSE(redone.interactable);
    CHECK(redone.currentState == 2);

    // A re-created entity (entity_delete, then undo) is reached through EntityIdRemap.
    scene::Entity replacement = fixture.make("Pause");
    const services::EntityHandle replacementHandle = handleOf(replacement);
    uitool::addSection(replacementHandle, Section::Button);
    services::UIButtonData moved = std::get<services::UIButtonData>(*uitool::readSection(replacementHandle, Section::Button));
    moved.normalColor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    moved.interactable = false;
    uitool::writeSection(replacementHandle, moved);
    mcp::undo::EntityIdRemap::instance().record(idOf(pause), idOf(replacement));
    undo.undo();
    const auto remapped = std::get<services::UIButtonData>(*uitool::readSection(replacementHandle, Section::Button));
    CHECK(remapped.normalColor == std::get<services::UIButtonData>(buttonBefore).normalColor);
    CHECK(remapped.interactable);
    CHECK_FALSE(uitool::hasSection(replacementHandle, Section::Mask));
    // The original entity is no longer the target.
    CHECK(std::get<services::UIButtonData>(*uitool::readSection(handle, Section::Button)).normalColor ==
          glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // A gone entity, or a section it no longer has, throws (McpBatchUndoCommand logs it).
    mcp::undo::UISectionUndo gone(999999u, "Ghost", {});
    CHECK_THROWS_AS(gone.undo(), std::runtime_error);
    CHECK_THROWS_AS(gone.execute(), std::runtime_error);

    scene::Entity bare = fixture.make("Bare");
    std::vector<mcp::undo::UISectionChange> labelChange;
    labelChange.push_back(
        {Section::Label, uitool::defaultSection(Section::Label), uitool::defaultSection(Section::Label)});
    mcp::undo::UISectionUndo missing(idOf(bare), "Bare", std::move(labelChange));
    CHECK_THROWS_AS(missing.undo(), std::runtime_error);
    CHECK_THROWS_AS(missing.execute(), std::runtime_error);
}

TEST_CASE("mcp ui support: section keys, attachable sections, widget type names and the type ui_get reports")
{
    for (std::size_t i = 0; i < uitool::sectionCount; ++i)
    {
        const Section section = static_cast<Section>(i);
        INFO(uitool::sectionKey(section));
        CHECK(uitool::sectionFromKey(uitool::sectionKey(section)) == section);
        CHECK(uitool::sectionOf(uitool::defaultSection(section)) == section);
    }
    CHECK_FALSE(uitool::sectionFromKey("Image").has_value()); // exact, like every argument key
    CHECK_FALSE(uitool::sectionFromKey("tabs").has_value());
    CHECK_FALSE(uitool::sectionFromKey("").has_value());

    const std::vector<Section> attachable{Section::Image, Section::Label, Section::LayoutGroup, Section::Mask,
                                          Section::Scroll};
    for (std::size_t i = 0; i < uitool::sectionCount; ++i)
    {
        const Section section = static_cast<Section>(i);
        INFO(uitool::sectionKey(section));
        CHECK(uitool::isAttachable(section) ==
              (std::find(attachable.begin(), attachable.end(), section) != attachable.end()));
    }

    for (std::size_t i = 0; i < uitool::widgetTypeNames.size(); ++i)
    {
        INFO(uitool::widgetTypeNames[i]);
        CHECK(uitool::widgetTypeFromName(uitool::widgetTypeNames[i]) == static_cast<uitool::WidgetType>(i));
    }
    CHECK(uitool::widgetTypeFromName("progressbar") == uitool::WidgetType::ProgressBar);
    CHECK(uitool::widgetTypeFromName("LAYOUTGROUP") == uitool::WidgetType::LayoutGroup);
    CHECK_FALSE(uitool::widgetTypeFromName("Canvas").has_value());
    CHECK_FALSE(uitool::widgetTypeFromName("Toggle").has_value());

    using services::ComponentTypeId;
    const auto typeOf = [](std::vector<ComponentTypeId> components)
    {
        services::EntityData data;
        data.components = std::move(components);
        return uitool::detectType(data);
    };
    CHECK(typeOf({ComponentTypeId::UICanvas, ComponentTypeId::UIRect, ComponentTypeId::UIImage}) == "Canvas");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UILabel, ComponentTypeId::UIButton}) ==
          "Button");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UICheckbox}) == "Checkbox");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UISlider}) == "Slider");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIProgressBar}) == "ProgressBar");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UILabel, ComponentTypeId::UIDropdown}) ==
          "Dropdown");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UITextInput}) == "TextInput");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UILayoutGroup, ComponentTypeId::UIListView}) == "ListView");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UILayoutGroup, ComponentTypeId::UITabs}) == "Tabs");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UIWindow}) == "Window");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UILayoutGroup}) == "LayoutGroup");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage, ComponentTypeId::UILabel}) == "Label");
    CHECK(typeOf({ComponentTypeId::UIRect, ComponentTypeId::UIImage}) == "Image");
    CHECK(typeOf({ComponentTypeId::UIRect}) == "Rect");
    CHECK(typeOf({ComponentTypeId::UIScroll}).empty());
    CHECK(typeOf({ComponentTypeId::Transform}).empty());
    CHECK(typeOf({}).empty());
}

TEST_CASE("mcp ui support: placement arguments, the rect they produce and its resolved pixels at two resolutions")
{
    const uitool::PlacementArgs defaults =
        uitool::readPlacement(json::object(), glm::vec2(160.0f, 40.0f), "placement");
    CHECK(defaults.placement.h == layout::HAlign::Center);
    CHECK(defaults.placement.v == layout::VAlign::Middle);
    CHECK(defaults.placement.size == glm::vec2(160.0f, 40.0f));
    CHECK(defaults.placement.margin == glm::vec4(0.0f));
    CHECK(defaults.placement.offset == glm::vec2(0.0f));
    CHECK_FALSE(defaults.relativeToCanvas);

    const uitool::PlacementArgs topLeft = uitool::readPlacement(
        json{{"preset", "TOPLEFT"}, {"size", json::array({200, 50})}, {"margin", 16}, {"relativeTo", "Canvas"}},
        glm::vec2(160.0f, 40.0f), "placement");
    CHECK(topLeft.placement.h == layout::HAlign::Left);
    CHECK(topLeft.placement.v == layout::VAlign::Top);
    CHECK(topLeft.placement.size == glm::vec2(200.0f, 50.0f));
    CHECK(topLeft.placement.margin == glm::vec4(16.0f));
    CHECK(topLeft.relativeToCanvas);

    CHECK(uitool::readPlacement(json{{"margin", json::array({8, 4})}}, glm::vec2(1.0f), "placement").placement.margin ==
          glm::vec4(8.0f, 8.0f, 4.0f, 4.0f));
    CHECK(uitool::readPlacement(json{{"margin", json{{"left", 1}, {"bottom", 4}}}}, glm::vec2(1.0f), "placement")
              .placement.margin == glm::vec4(1.0f, 0.0f, 0.0f, 4.0f));
    CHECK(uitool::readPlacement(json{{"offset", json::array({-5, 12})}}, glm::vec2(1.0f), "placement").placement.offset ==
          glm::vec2(-5.0f, 12.0f));
    CHECK(uitool::readPlacement(json{{"preset", nullptr}}, glm::vec2(1.0f), "placement").placement.h ==
          layout::HAlign::Center);

    for (const json& bad : {json{{"preset", "middle"}}, json{{"preset", 3}}, json{{"size", json::array({-1, 10})}},
                            json{{"size", json::array({10})}}, json{{"margin", -1}}, json{{"margin", json{{"inner", 1}}}},
                            json{{"margin", "8"}}, json{{"offset", json::array({0, 20000})}},
                            json{{"relativeTo", "screen"}}, json{{"anchor", "top"}}, json("topLeft")})
    {
        INFO(bad.dump());
        CHECK_THROWS_AS(uitool::readPlacement(bad, glm::vec2(10.0f), "placement"), mcp::ArgError);
    }
    try
    {
        uitool::readPlacement(json{{"size", json::array({-1, 10})}}, glm::vec2(10.0f), "placement");
        FAIL("a negative size was accepted");
    }
    catch (const mcp::ArgError& error)
    {
        CHECK(std::string(error.what()).find("'placement.size[0]'") != std::string::npos);
    }

    // topLeft, 200 x 50, margin 16: 16 px from the top-left corner at the reference size, 32 px at twice it.
    services::UIRectData current;
    current.blocksRaycast = false;
    const services::UIRectData rect = uitool::placeRect(current, topLeft, layout::fullViewportRect());
    CHECK(rect.pivot == glm::vec2(0.5f, 0.5f));
    CHECK_FALSE(rect.blocksRaycast);
    services::UICanvasData canvas; // 1920 x 1080, scaleWithScreenSize
    CHECK(uitool::resolvedJson(rect, canvas, std::nullopt) ==
          json{{"x", 16.0}, {"y", 16.0}, {"w", 200.0}, {"h", 50.0}, {"viewport", json::array({1920.0, 1080.0})},
               {"scale", 1.0}});
    CHECK(uitool::resolvedJson(rect, canvas, glm::vec2(3840.0f, 2160.0f)) ==
          json{{"x", 32.0}, {"y", 32.0}, {"w", 400.0}, {"h", 100.0}, {"viewport", json::array({3840.0, 2160.0})},
               {"scale", 2.0}});

    // topRight keeps its margin from the right edge at every resolution.
    const services::UIRectData topRight = uitool::placeRect(
        services::UIRectData{},
        uitool::readPlacement(json{{"preset", "topRight"}, {"size", json::array({200, 50})}, {"margin", 16}},
                              glm::vec2(1.0f), "placement"),
        layout::fullViewportRect());
    CHECK(uitool::resolvedJson(topRight, canvas, std::nullopt).at("x") == 1704.0);
    CHECK(uitool::resolvedJson(topRight, canvas, glm::vec2(3840.0f, 2160.0f)).at("x") == 3408.0);

    // stretch with a margin of 10 fills the canvas less the margin.
    const services::UIRectData stretched = uitool::placeRect(
        services::UIRectData{}, uitool::readPlacement(json{{"preset", "stretch"}, {"margin", 10}}, glm::vec2(1.0f), "placement"),
        layout::fullViewportRect());
    CHECK(uitool::resolvedJson(stretched, canvas, std::nullopt) ==
          json{{"x", 10.0}, {"y", 10.0}, {"w", 1900.0}, {"h", 1060.0}, {"viewport", json::array({1920.0, 1080.0})},
               {"scale", 1.0}});

    // An offset moves the placed rect, +y DOWN.
    const services::UIRectData nudged = uitool::placeRect(
        services::UIRectData{},
        uitool::readPlacement(json{{"offset", json::array({10, 20})}}, glm::vec2(160.0f, 40.0f), "placement"),
        layout::fullViewportRect());
    const json nudgedPixels = uitool::resolvedJson(nudged, canvas, std::nullopt);
    CHECK(nudgedPixels.at("x") == 890.0);
    CHECK(nudgedPixels.at("y") == 540.0);

    // constantPixelSize: reference pixels are screen pixels at any viewport.
    canvas.scaleMode = 0;
    const json constant = uitool::resolvedJson(rect, canvas, glm::vec2(3840.0f, 2160.0f));
    CHECK(constant.at("scale") == 1.0);
    CHECK(constant.at("x") == 16.0);
    CHECK(constant.at("w") == 200.0);

    const json schema = uitool::placementSchema("Where the widget goes.");
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("additionalProperties") == false);
    CHECK(schema.at("description") == "Where the widget goes.");
    const json& properties = schema.at("properties");
    CHECK(properties.at("preset").at("enum").size() == layout::anchorPresets.size());
    CHECK(properties.at("relativeTo").at("enum") == json::array({"parent", "canvas"}));
    CHECK(properties.at("size").at("minItems") == 2);
    CHECK(properties.at("size").at("maxItems") == 2);
    CHECK(properties.at("margin").at("anyOf").size() == 3);
    CHECK(properties.at("offset").at("items").at("minimum") == -16384.0);
}

TEST_CASE("mcp ui support: the placement frame, the hierarchy helpers and ui_get's widget report on real entities")
{
    UIRangesFixture fixture;
    scene::Entity canvas = fixture.make("HUD");
    uitool::addSection(handleOf(canvas), Section::Canvas);

    // A 400 x 300 panel at the canvas' top left: a canvas root places in the whole canvas.
    scene::Entity panel = fixture.make("Panel", canvas);
    uitool::addSection(handleOf(panel), Section::Rect);
    const layout::AffineRect canvasFrame = uitool::placementFrame(dataOf(canvas), false);
    CHECK(canvasFrame.left.f == 0.0f);
    CHECK(canvasFrame.right.f == 1.0f);
    CHECK(canvasFrame.top.f == 0.0f);
    CHECK(canvasFrame.bottom.f == 1.0f);
    const uitool::PlacementArgs panelArgs = uitool::readPlacement(
        json{{"preset", "topLeft"}, {"size", json::array({400, 300})}}, glm::vec2(1.0f), "placement");
    uitool::writeSection(handleOf(panel),
                         uitool::placeRect(std::get<services::UIRectData>(*uitool::readSection(handleOf(panel), Section::Rect)),
                                           panelArgs, canvasFrame));

    // A 100 x 50 badge at the panel's bottom right, margin 10: inside the panel ...
    scene::Entity badge = fixture.make("Badge", panel);
    uitool::addSection(handleOf(badge), Section::Image);
    uitool::addSection(handleOf(badge), Section::Rect);
    const uitool::PlacementArgs badgeArgs = uitool::readPlacement(
        json{{"preset", "bottomRight"}, {"size", json::array({100, 50})}, {"margin", 10}}, glm::vec2(1.0f), "placement");
    const services::UIRectData inPanel =
        uitool::placeRect(services::UIRectData{}, badgeArgs, uitool::placementFrame(dataOf(panel), false));
    CHECK(uitool::resolvedJson(inPanel, services::UICanvasData{}, std::nullopt) ==
          json{{"x", 290.0}, {"y", 240.0}, {"w", 100.0}, {"h", 50.0}, {"viewport", json::array({1920.0, 1080.0})},
               {"scale", 1.0}});
    // ... or at the canvas' bottom right with relativeTo "canvas".
    const services::UIRectData inCanvas =
        uitool::placeRect(services::UIRectData{}, badgeArgs, uitool::placementFrame(dataOf(panel), true));
    CHECK(uitool::resolvedJson(inCanvas, services::UICanvasData{}, std::nullopt) ==
          json{{"x", 1810.0}, {"y", 1020.0}, {"w", 100.0}, {"h", 50.0}, {"viewport", json::array({1920.0, 1080.0})},
               {"scale", 1.0}});

    // Hierarchy.
    const services::EntityData canvasData = dataOf(canvas);
    const services::EntityData badgeData = dataOf(badge);
    const std::optional<services::EntityData> badgeCanvas = uitool::findCanvas(badgeData);
    REQUIRE(badgeCanvas.has_value());
    CHECK(badgeCanvas->handle.id == canvasData.handle.id);
    const std::optional<services::EntityData> ownCanvas = uitool::findCanvas(canvasData);
    REQUIRE(ownCanvas.has_value());
    CHECK(ownCanvas->handle.id == canvasData.handle.id);
    CHECK(uitool::hasUIAncestor(badgeData));
    CHECK_FALSE(uitool::hasUIAncestor(canvasData));
    CHECK(uitool::isUIEntity(badgeData));
    CHECK_FALSE(uitool::isUIEntity(dataOf(fixture.sceneGraph->GetRoot())));
    CHECK(uitool::detectType(badgeData) == "Image");
    CHECK(uitool::uiComponentNames(badgeData) == std::vector<std::string>{"UIRect", "UIImage"});
    CHECK_FALSE(uitool::isListItem(handleOf(badge)));
    CHECK(uitool::countEntitiesNamed("Badge") == 1);
    scene::Entity secondBadge = fixture.make("Badge", panel);
    CHECK(uitool::countEntitiesNamed(secondBadge.getName()) == 2);
    CHECK(uitool::countEntitiesNamed("Nobody") == 0);

    // A checkbox with its label part.
    scene::Entity sound = fixture.make("Sound", panel);
    uitool::addSection(handleOf(sound), Section::Checkbox); // + UIRect, UIImage
    scene::Entity soundLabel = fixture.make("Sound Label", sound);
    uitool::addSection(handleOf(soundLabel), Section::Label); // + UIRect
    const services::EntityData soundData = dataOf(sound);
    const std::optional<services::EntityHandle> labelPart = uitool::checkboxLabelPart(soundData);
    REQUIRE(labelPart.has_value());
    CHECK(labelPart->id == handleOf(soundLabel).id);
    CHECK_FALSE(uitool::checkboxLabelPart(badgeData).has_value());

    const json report = uitool::widgetReport(soundData, uitool::ReportOptions{});
    CHECK(report.at("entity") == idOf(sound));
    CHECK(report.at("name") == "Sound");
    CHECK(report.at("type") == "Checkbox");
    CHECK(report.at("active") == true);
    CHECK(report.at("effectivelyActive") == true);
    CHECK(report.at("parent") == idOf(panel));
    CHECK(report.at("canvasId") == idOf(canvas));
    CHECK(report.at("children") == json::array({idOf(soundLabel)}));
    CHECK(report.at("parts") == json{{"label", idOf(soundLabel)}});
    CHECK(report.at("layoutControlled") == false);
    CHECK(report.at("listItem") == false);
    CHECK(report.at("components") == json::array({"UIRect", "UIImage", "UICheckbox"}));
    CHECK(report.contains("rect"));
    CHECK(report.contains("image"));
    CHECK(report.contains("checkbox"));
    CHECK_FALSE(report.contains("label"));
    CHECK_FALSE(report.contains("styleKey"));
    CHECK_FALSE(report.contains("ranges"));
    REQUIRE(report.contains("resolved"));
    CHECK(report.at("resolved").at("viewport") == json::array({1920.0, 1080.0}));
    if (report.contains("validation"))
    {
        CHECK(report.at("validation").at("ok") == false);
    }
    for (const std::string_view key : uitool::reportIdentityKeys)
    {
        INFO(key);
        CHECK((report.contains(std::string(key)) || key == "styleKey"));
    }
    CHECK(uitool::widgetReport(badgeData, uitool::ReportOptions{}).at("parts") == json::object());

    uitool::ReportOptions detailed;
    detailed.ranges = true;
    detailed.viewport = glm::vec2(3840.0f, 2160.0f);
    const json detailedReport = uitool::widgetReport(soundData, detailed);
    REQUIRE(detailedReport.contains("ranges"));
    CHECK(detailedReport.at("ranges").size() == 3);
    CHECK(detailedReport.at("ranges").at("checkbox") == uitool::sectionRanges(Section::Checkbox));
    CHECK(detailedReport.at("resolved").at("scale") == 2.0);

    // A layout group on the parent takes over the position.
    uitool::addSection(handleOf(panel), Section::LayoutGroup);
    CHECK(uitool::widgetReport(dataOf(sound), uitool::ReportOptions{}).at("layoutControlled") == true);
}
