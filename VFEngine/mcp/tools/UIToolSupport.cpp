#include "UIToolSupport.hpp"

#include "AssetFieldSupport.hpp"
#include "TerrainToolSupport.hpp"
#include "ToolHelpers.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/ui/UIEvents.hpp"
#include "resource/VfImageHeader.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mcp::tools::uitool
{
    namespace
    {
        namespace fs = std::filesystem;
        using json = nlohmann::json;
        using services::ComponentTypeId;

        using Rect = services::UIRectData;
        using Image = services::UIImageData;
        using Label = services::UILabelData;
        using Button = services::UIButtonData;
        using Checkbox = services::UICheckboxData;
        using Slider = services::UISliderData;
        using ProgressBar = services::UIProgressBarData;
        using Dropdown = services::UIDropdownData;
        using TextInput = services::UITextInputData;
        using LayoutGroup = services::UILayoutGroupData;
        using ListView = services::UIListViewData;
        using Mask = services::UIMaskData;
        using Scroll = services::UIScrollData;

        // Rect offsets and sizes, placement sizes / margins / offsets (canvas reference pixels).
        constexpr double maxReferencePixels = 16384.0;
        // Slider / progress bar values and steps.
        constexpr double maxValueMagnitude = 1.0e6;
        // services::UICanvasData::scaleMode: 0 = ConstantPixelSize, 1 = ScaleWithScreenSize.
        constexpr uint8_t scaleWithScreenSize = 1;
        // Guard for parent walks (a scene graph is a tree; this only stops a corrupt cycle).
        constexpr int maxHierarchyDepth = 4096;

        constexpr std::string_view runtimeHint =
            "runtime state the engine updates while the game runs (pointer, focus, open list); it is reported, never set";
        constexpr std::string_view imageSizeHint =
            "the texture's pixel size, read from its .vfImage header whenever textureRef changes";
        constexpr std::string_view renderTextureHint =
            "the render-texture source entity is chosen in the editor's UI Image inspector";
        constexpr std::string_view themeHint =
            "a theme writes into every styled widget of the canvas and is re-applied on each scene load; assign it "
            "in the editor's UI Canvas inspector";
        constexpr std::string_view renderModeHint =
            "a canvas always draws as a screen-space overlay in Play and Runtime (the editor's Edit mode shows a "
            "world-space preview quad on the canvas entity); there is no other render mode";

        events::EventDispatcher& dispatcher()
        {
            return events::EventDispatcher::instance();
        }

        // ------------------------------------------------------------------
        // Field builders the generic SettingsTable ones do not cover
        // ------------------------------------------------------------------

        // A compile-time string: the full argument path ("image.textureRef") of an asset field's error messages,
        // and its extension. Template parameter objects have static storage, so views into them stay valid.
        template <std::size_t N>
        struct Literal
        {
            char text[N]{};

            constexpr Literal(const char (&value)[N])
            {
                for (std::size_t i = 0; i < N; ++i)
                {
                    text[i] = value[i];
                }
            }

            constexpr std::string_view view() const
            {
                return std::string_view(text, N - 1);
            }
        };

        constexpr std::string_view afterLastDot(std::string_view path)
        {
            const std::size_t dot = path.rfind('.');
            return dot == std::string_view::npos ? path : path.substr(dot + 1);
        }

        // assetField's writer: a function template rather than a lambda, so the template parameter objects are only
        // named in a function template body.
        template <typename T, asset::AssetRef T::*Member, Literal Path, Literal Ext>
        void setAssetField(T& value, const json& v)
        {
            value.*Member = assetFieldFromJson(v, Path.text, Ext.view(), AssetScope::InsideProject);
        }

        // An asset reference exchanged as a project-relative path ("" = none, "guid:<hex>" for a registered asset
        // whose file cannot be found), checked by assetFieldFromJson: the right extension, inside the project, the
        // file exists. Main thread (the project root and the asset database).
        template <typename T, asset::AssetRef T::*Member, Literal Path, Literal Ext>
        env::FieldDesc<T> assetField(std::string_view help)
        {
            env::FieldDesc<T> field;
            field.key = afterLastDot(Path.view());
            field.kind = env::FieldKind::String;
            field.pathExtension = Ext.view();
            field.help = help;
            field.get = [](const T& value) -> json { return assetFieldJson(value.*Member, AssetScope::InsideProject); };
            field.set = &setAssetField<T, Member, Path, Ext>;
            return field;
        }

        // A uint8_t-coded enum of a DTO: reported by name (index = enumerator value); a stored value outside the
        // names is reported as its number, which then passes back unchanged.
        template <typename T, uint8_t T::*Member, typename Names>
        env::FieldDesc<T> codedEnumField(std::string_view key, std::string_view help)
        {
            env::FieldDesc<T> field;
            field.key = key;
            field.kind = env::FieldKind::Enum;
            field.enumNames = Names::names;
            field.help = help;
            field.get = [](const T& value) -> json
            {
                const std::span<const std::string_view> names = Names::names;
                const auto index = static_cast<std::size_t>(value.*Member);
                return index < names.size() ? json(std::string(names[index])) : json(static_cast<int64_t>(index));
            };
            field.set = [](T& value, const json& v)
            {
                const std::span<const std::string_view> names = Names::names;
                const std::string name = v.get<std::string>();
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (names[i] == name)
                    {
                        value.*Member = static_cast<uint8_t>(i);
                        return;
                    }
                }
                throw std::logic_error("'" + name + "' is not a canonical enum name");
            };
            return field;
        }

        // Canonical names, indexed like the engine enums (utilities/components/UIComponents.hpp).
        struct ScaleModeNames
        {
            static constexpr std::array<std::string_view, 2> names{"constantPixelSize", "scaleWithScreenSize"};
        };
        struct RenderModeNames
        {
            static constexpr std::array<std::string_view, 1> names{"screenSpaceOverlay"};
        };
        struct ImageTypeNames
        {
            static constexpr std::array<std::string_view, 3> names{"simple", "sliced", "tiled"};
        };
        struct FontStyleNames
        {
            static constexpr std::array<std::string_view, 4> names{"normal", "bold", "italic", "boldItalic"};
        };
        struct HorizontalAlignmentNames
        {
            static constexpr std::array<std::string_view, 3> names{"left", "center", "right"};
        };
        struct VerticalAlignmentNames
        {
            static constexpr std::array<std::string_view, 3> names{"top", "middle", "bottom"};
        };
        struct OverflowNames
        {
            static constexpr std::array<std::string_view, 3> names{"overflow", "clip", "ellipsis"};
        };
        // UIButtonState and UISliderState.
        struct PressStateNames
        {
            static constexpr std::array<std::string_view, 4> names{"normal", "hovered", "pressed", "disabled"};
        };
        struct CheckboxStateNames
        {
            static constexpr std::array<std::string_view, 3> names{"normal", "hovered", "disabled"};
        };
        struct DropdownStateNames
        {
            static constexpr std::array<std::string_view, 4> names{"normal", "hovered", "open", "disabled"};
        };
        struct TextInputStateNames
        {
            static constexpr std::array<std::string_view, 4> names{"normal", "hovered", "focused", "disabled"};
        };
        struct OrientationNames
        {
            static constexpr std::array<std::string_view, 2> names{"horizontal", "vertical"};
        };
        struct DirectionNames
        {
            static constexpr std::array<std::string_view, 3> names{"vertical", "horizontal", "grid"};
        };
        struct ChildAlignmentNames
        {
            static constexpr std::array<std::string_view, 3> names{"start", "center", "end"};
        };
        struct ScrollbarVisibilityNames
        {
            static constexpr std::array<std::string_view, 3> names{"auto", "alwaysVisible", "hidden"};
        };

        // ---- Canvas fields (nested in CanvasValues::data) ----

        template <float services::UICanvasData::*Member>
        env::FieldDesc<CanvasValues> canvasFloatField(std::string_view key, double min, double max, std::string_view help)
        {
            env::FieldDesc<CanvasValues> field;
            field.key = key;
            field.kind = env::FieldKind::Float;
            field.min = min;
            field.max = max;
            field.help = help;
            field.get = [](const CanvasValues& value) -> json { return env::floatJson(value.data.*Member); };
            field.set = [](CanvasValues& value, const json& v)
            {
                value.data.*Member = static_cast<float>(v.get<double>());
            };
            return field;
        }

        env::FieldDesc<CanvasValues> canvasScaleModeField()
        {
            env::FieldDesc<CanvasValues> field;
            field.key = "scaleMode";
            field.kind = env::FieldKind::Enum;
            field.enumNames = ScaleModeNames::names;
            field.help = "scaleWithScreenSize (default): everything scales by min(screenWidth / referenceWidth, "
                         "screenHeight / referenceHeight). constantPixelSize: reference pixels are screen pixels.";
            field.get = [](const CanvasValues& value) -> json
            {
                const std::span<const std::string_view> names = ScaleModeNames::names;
                const auto index = static_cast<std::size_t>(value.data.scaleMode);
                return index < names.size() ? json(std::string(names[index])) : json(static_cast<int64_t>(index));
            };
            field.set = [](CanvasValues& value, const json& v)
            {
                const std::span<const std::string_view> names = ScaleModeNames::names;
                const std::string name = v.get<std::string>();
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (names[i] == name)
                    {
                        value.data.scaleMode = static_cast<uint8_t>(i);
                        return;
                    }
                }
                throw std::logic_error("'" + name + "' is not a canonical scale mode");
            };
            return field;
        }

        env::FieldDesc<CanvasValues> canvasSortOrderField()
        {
            env::FieldDesc<CanvasValues> field;
            field.key = "sortOrder";
            field.kind = env::FieldKind::Int;
            field.min = -1000.0;
            field.max = 1000.0;
            field.help = "Draw order between canvases: higher draws later (on top). Labels of every canvas still draw "
                         "after the images of all canvases.";
            field.get = [](const CanvasValues& value) -> json { return static_cast<int64_t>(value.data.sortOrder); };
            field.set = [](CanvasValues& value, const json& v) { value.data.sortOrder = static_cast<int>(v.get<int64_t>()); };
            return field;
        }

        // Read-only and constant: the one render mode there is. Enum-typed so a different name is refused with the
        // hint instead of being silently accepted.
        env::FieldDesc<CanvasValues> canvasRenderModeField()
        {
            env::FieldDesc<CanvasValues> field;
            field.key = "renderMode";
            field.kind = env::FieldKind::Enum;
            field.enumNames = RenderModeNames::names;
            field.help = "Always screenSpaceOverlay.";
            field.setHint = renderModeHint;
            field.get = [](const CanvasValues&) -> json { return std::string(RenderModeNames::names[0]); };
            field.set = [](CanvasValues&, const json&) {};
            return field;
        }

        env::FieldDesc<CanvasValues> canvasThemeField()
        {
            env::FieldDesc<CanvasValues> field;
            field.key = "theme";
            field.kind = env::FieldKind::String;
            field.pathExtension = ".vfTheme";
            field.help = "The canvas' .vfTheme (project-relative), \"\" for none.";
            field.setHint = themeHint;
            field.get = [](const CanvasValues& value) -> json { return value.theme; };
            field.set = [](CanvasValues& value, const json& v) { value.theme = v.get<std::string>(); };
            return field;
        }

        // ---- Dropdown options: the option texts (each option keeps its icon by index) ----

        env::FieldDesc<Dropdown> dropdownOptionsField()
        {
            env::FieldDesc<Dropdown> field;
            field.key = "options";
            field.kind = env::FieldKind::StringList;
            field.min = 0.0;
            field.max = 64.0;
            field.help = "The option texts, top to bottom (0-64). An option keeps the icon it had at the same index.";
            field.get = [](const Dropdown& value) -> json
            {
                json out = json::array();
                for (const services::DropdownOptionData& option : value.options)
                {
                    out.push_back(option.text);
                }
                return out;
            };
            field.set = [](Dropdown& value, const json& v)
            {
                std::vector<services::DropdownOptionData> next;
                next.reserve(v.size());
                for (std::size_t i = 0; i < v.size(); ++i)
                {
                    services::DropdownOptionData option;
                    option.text = v.at(i).get<std::string>();
                    if (i < value.options.size())
                    {
                        option.iconRef = value.options[i].iconRef;
                    }
                    next.push_back(std::move(option));
                }
                value.options = std::move(next);
            };
            return field;
        }

        // ------------------------------------------------------------------
        // Cross-field checks and derived fields
        // ------------------------------------------------------------------

        std::string checkRect(const Rect& rect)
        {
            if (rect.anchorMin.x > rect.anchorMax.x || rect.anchorMin.y > rect.anchorMax.y)
            {
                return "anchorMin " + env::vec2Json(rect.anchorMin).dump() + " must not exceed anchorMax " +
                       env::vec2Json(rect.anchorMax).dump() + " on either axis";
            }
            return {};
        }

        template <typename T>
        std::string checkValueRange(const T& data)
        {
            if (!(data.minValue < data.maxValue))
            {
                return "minValue (" + env::floatJson(data.minValue).dump() + ") must be less than maxValue (" +
                       env::floatJson(data.maxValue).dump() + ")";
            }
            if (data.value < data.minValue || data.value > data.maxValue)
            {
                return "value (" + env::floatJson(data.value).dump() + ") must lie between minValue (" +
                       env::floatJson(data.minValue).dump() + ") and maxValue (" + env::floatJson(data.maxValue).dump() +
                       ")";
            }
            return {};
        }

        std::string checkSlider(const Slider& slider)
        {
            std::string error = checkValueRange(slider);
            if (!error.empty())
            {
                return error;
            }
            if (slider.stepSize > slider.maxValue - slider.minValue)
            {
                return "stepSize (" + env::floatJson(slider.stepSize).dump() + ") must not exceed maxValue - minValue (" +
                       env::floatJson(slider.maxValue - slider.minValue).dump() + ")";
            }
            return {};
        }

        std::string checkProgressBar(const ProgressBar& progressBar)
        {
            return checkValueRange(progressBar);
        }

        std::string checkDropdown(const Dropdown& dropdown)
        {
            if (dropdown.selectedIndex >= static_cast<int>(dropdown.options.size()))
            {
                return "selectedIndex " + std::to_string(dropdown.selectedIndex) + " is out of range for " +
                       std::to_string(dropdown.options.size()) + " options (-1 = nothing selected)";
            }
            return {};
        }

        // A new textureRef brings its pixel size (9-slicing needs it); clearing it clears the size.
        void deriveImageSourceSize(const Image& base, Image& staged, const json& patch)
        {
            if (!patch.contains("textureRef") || staged.textureRef == base.textureRef)
            {
                return;
            }
            staged.sourceWidth = 0;
            staged.sourceHeight = 0;
            if (!staged.textureRef.isValid())
            {
                return;
            }
            const std::string& path = staged.textureRef.resolve();
            if (path.empty())
            {
                return;
            }
            if (const std::optional<glm::uvec2> size = resource::readVfImageSize(fs::path(path)))
            {
                staged.sourceWidth = size->x;
                staged.sourceHeight = size->y;
            }
        }

        // ------------------------------------------------------------------
        // The section tables (function-local statics; any thread)
        // ------------------------------------------------------------------

        const env::SectionTable<CanvasValues>& canvasTable()
        {
            static const env::SectionTable<CanvasValues> table = []
            {
                env::SectionTable<CanvasValues> t;
                t.name = "canvas";
                t.fields = {
                    canvasFloatField<&services::UICanvasData::referenceWidth>(
                        "referenceWidth", 1.0, 7680.0,
                        "Design width in reference pixels; every rect, size and margin on the canvas is in these pixels."),
                    canvasFloatField<&services::UICanvasData::referenceHeight>(
                        "referenceHeight", 1.0, 4320.0, "Design height in reference pixels."),
                    canvasScaleModeField(),
                    canvasFloatField<&services::UICanvasData::pixelsPerUnit>(
                        "pixelsPerUnit", 1.0, 1000.0,
                        "Reference pixels per metre of the editor's Edit-mode preview quad (Play ignores it)."),
                    canvasSortOrderField(),
                    canvasRenderModeField(),
                    canvasThemeField()
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Rect>& rectTable()
        {
            static const env::SectionTable<Rect> table = []
            {
                env::SectionTable<Rect> t;
                t.name = "rect";
                t.fields = {
                    env::vec2Field<Rect, &Rect::anchorMin>(
                        "anchorMin", 0.0, 1.0,
                        "Lower-left anchor as fractions of the WHOLE canvas (never the parent), y up: [0, 0] = bottom "
                        "left. Prefer 'placement'."),
                    env::vec2Field<Rect, &Rect::anchorMax>(
                        "anchorMax", 0.0, 1.0, "Upper-right anchor (fractions of the whole canvas, y up)."),
                    env::vec2Field<Rect, &Rect::pivot>(
                        "pivot", 0.0, 1.0,
                        "The rect point anchoredPosition places: x from the LEFT edge, y from the TOP edge (opposite of "
                        "Unity). 'placement' always writes [0.5, 0.5]."),
                    env::vec2Field<Rect, &Rect::sizeDelta>(
                        "sizeDelta", -maxReferencePixels, maxReferencePixels,
                        "Size added to the anchor span (reference px): the size itself when anchorMin == anchorMax."),
                    env::vec2Field<Rect, &Rect::anchoredPosition>(
                        "anchoredPosition", -maxReferencePixels, maxReferencePixels,
                        "Offset of the pivot from the anchors' centre (reference px, y up)."),
                    env::boolField<Rect, &Rect::blocksRaycast>(
                        "blocksRaycast", "Counts for UI::isPointerOverUI (false for decorative overlays).")
                };
                t.crossCheck = &checkRect;
                return t;
            }();
            return table;
        }

        const env::SectionTable<Image>& imageTable()
        {
            static const env::SectionTable<Image> table = []
            {
                env::SectionTable<Image> t;
                t.name = "image";
                t.fields = {
                    assetField<Image, &Image::textureRef, "image.textureRef", ".vfImage">(
                        "A .vfImage (project-relative), \"\" for a plain colour quad."),
                    env::colorField<Image, &Image::colorTint>(
                        "colorTint",
                        "RGBA multiplier (the quad's colour without a texture). On a Button, Checkbox, TextInput or "
                        "Dropdown the state colours overwrite it every frame: set those instead."),
                    codedEnumField<Image, &Image::imageType, ImageTypeNames>(
                        "imageType", "simple, or sliced / tiled 9-slicing with 'border'."),
                    env::vec4Field<Image, &Image::border>(
                        "border", 0.0, 8192.0, "9-slice borders [left, right, top, bottom] in texture pixels."),
                    env::readOnly(env::uintField<Image, &Image::sourceWidth>(
                                      "sourceWidth", 0.0, 4294967295.0, "Texture width in pixels."),
                                  imageSizeHint),
                    env::readOnly(env::uintField<Image, &Image::sourceHeight>(
                                      "sourceHeight", 0.0, 4294967295.0, "Texture height in pixels."),
                                  imageSizeHint),
                    env::readOnly(env::stringField<Image, &Image::renderTextureSourceName>(
                                      "renderTextureSourceName", "", "The render-texture source entity's name."),
                                  renderTextureHint)
                };
                t.derive = &deriveImageSourceSize;
                return t;
            }();
            return table;
        }

        const env::SectionTable<Label>& labelTable()
        {
            static const env::SectionTable<Label> table = []
            {
                env::SectionTable<Label> t;
                t.name = "label";
                t.fields = {
                    env::stringField<Label, &Label::text>(
                        "text", "", "The text. With richText: [b], [i], [color=#RRGGBB] markup."),
                    assetField<Label, &Label::fontRef, "label.fontRef", ".vfFont">(
                        "The font slot: a .vfFont (project-relative), \"\" for the engine's default font."),
                    env::floatField<Label, &Label::fontSize>("fontSize", 1.0, 200.0, "Font size in reference pixels."),
                    codedEnumField<Label, &Label::fontStyle, FontStyleNames>("fontStyle", "normal, bold, italic, boldItalic."),
                    env::colorField<Label, &Label::color>("color", "Text colour RGBA."),
                    codedEnumField<Label, &Label::horizontalAlignment, HorizontalAlignmentNames>(
                        "horizontalAlignment", "Within the rect."),
                    codedEnumField<Label, &Label::verticalAlignment, VerticalAlignmentNames>(
                        "verticalAlignment", "Within the rect."),
                    codedEnumField<Label, &Label::overflow, OverflowNames>(
                        "overflow", "Text wider than the rect: overflow (drawn past it), clip or ellipsis."),
                    env::boolField<Label, &Label::wordWrap>("wordWrap", "Wrap at the rect width."),
                    env::floatField<Label, &Label::lineSpacing>("lineSpacing", 0.1, 5.0, "Line height multiplier."),
                    env::floatField<Label, &Label::letterSpacing>(
                        "letterSpacing", -10.0, 50.0, "Extra pixels between glyphs."),
                    env::boolField<Label, &Label::richText>("richText", "Parse [b] / [i] / [color] markup.")
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Button>& buttonTable()
        {
            static const env::SectionTable<Button> table = []
            {
                env::SectionTable<Button> t;
                t.name = "button";
                t.fields = {
                    env::colorField<Button, &Button::normalColor>(
                        "normalColor", "Background colour at rest (pushed into the button's image every frame)."),
                    env::colorField<Button, &Button::hoveredColor>("hoveredColor", "Background under the pointer."),
                    env::colorField<Button, &Button::pressedColor>("pressedColor", "Background while pressed."),
                    env::colorField<Button, &Button::disabledColor>("disabledColor", "Background while not interactable."),
                    assetField<Button, &Button::normalTextureRef, "button.normalTextureRef", ".vfImage">(
                        "Optional .vfImage at rest."),
                    assetField<Button, &Button::hoverTextureRef, "button.hoverTextureRef", ".vfImage">(
                        "Optional .vfImage under the pointer."),
                    assetField<Button, &Button::pressedTextureRef, "button.pressedTextureRef", ".vfImage">(
                        "Optional .vfImage while pressed."),
                    assetField<Button, &Button::disabledTextureRef, "button.disabledTextureRef", ".vfImage">(
                        "Optional .vfImage while not interactable."),
                    env::floatField<Button, &Button::colorTransitionDuration>(
                        "colorTransitionDuration", 0.0, 2.0, "Seconds to blend between state colours."),
                    env::boolField<Button, &Button::interactable>(
                        "interactable", "false = disabled: no clicks, disabledColor."),
                    env::readOnly(codedEnumField<Button, &Button::currentState, PressStateNames>(
                                      "currentState", "The pointer state."),
                                  runtimeHint)
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Checkbox>& checkboxTable()
        {
            static const env::SectionTable<Checkbox> table = []
            {
                env::SectionTable<Checkbox> t;
                t.name = "checkbox";
                t.fields = {
                    env::boolField<Checkbox, &Checkbox::isChecked>("isChecked", "Checked state."),
                    env::stringField<Checkbox, &Checkbox::groupName>(
                        "groupName", "", "Radio group: checking one box unchecks the others of the group (\"\" = independent)."),
                    env::boolField<Checkbox, &Checkbox::allowUncheck>(
                        "allowUncheck", "In a radio group: whether clicking the checked box unchecks it."),
                    env::colorField<Checkbox, &Checkbox::uncheckedColor>(
                        "uncheckedColor", "Box colour when unchecked (pushed into the box image every frame)."),
                    env::colorField<Checkbox, &Checkbox::checkedColor>("checkedColor", "Box colour when checked."),
                    env::colorField<Checkbox, &Checkbox::hoveredColor>("hoveredColor", "Box colour under the pointer."),
                    env::colorField<Checkbox, &Checkbox::disabledColor>(
                        "disabledColor", "Box colour while not interactable."),
                    assetField<Checkbox, &Checkbox::uncheckedTextureRef, "checkbox.uncheckedTextureRef", ".vfImage">(
                        "Optional .vfImage when unchecked."),
                    assetField<Checkbox, &Checkbox::checkedTextureRef, "checkbox.checkedTextureRef", ".vfImage">(
                        "Optional .vfImage when checked."),
                    assetField<Checkbox, &Checkbox::hoveredTextureRef, "checkbox.hoveredTextureRef", ".vfImage">(
                        "Optional .vfImage under the pointer."),
                    assetField<Checkbox, &Checkbox::disabledTextureRef, "checkbox.disabledTextureRef", ".vfImage">(
                        "Optional .vfImage while not interactable."),
                    env::floatField<Checkbox, &Checkbox::colorTransitionDuration>(
                        "colorTransitionDuration", 0.0, 2.0, "Seconds to blend between state colours."),
                    env::boolField<Checkbox, &Checkbox::interactable>("interactable", "false = disabled."),
                    env::boolField<Checkbox, &Checkbox::labelToggle>(
                        "labelToggle", "Clicking the checkbox's child UILabel toggles it too."),
                    env::readOnly(codedEnumField<Checkbox, &Checkbox::currentState, CheckboxStateNames>(
                                      "currentState", "The pointer state."),
                                  runtimeHint)
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Slider>& sliderTable()
        {
            static const env::SectionTable<Slider> table = []
            {
                env::SectionTable<Slider> t;
                t.name = "slider";
                t.fields = {
                    env::floatField<Slider, &Slider::minValue>(
                        "minValue", -maxValueMagnitude, maxValueMagnitude, "Value at the start of the track."),
                    env::floatField<Slider, &Slider::maxValue>(
                        "maxValue", -maxValueMagnitude, maxValueMagnitude, "Value at the end (> minValue)."),
                    env::floatField<Slider, &Slider::value>(
                        "value", -maxValueMagnitude, maxValueMagnitude, "Current value (minValue..maxValue)."),
                    env::floatField<Slider, &Slider::stepSize>(
                        "stepSize", 0.0, maxValueMagnitude, "Snap step (0 = continuous)."),
                    codedEnumField<Slider, &Slider::orientation, OrientationNames>("orientation", "horizontal or vertical."),
                    env::boolField<Slider, &Slider::clickTrackToSet>(
                        "clickTrackToSet", "Clicking the track jumps the handle there."),
                    env::floatField<Slider, &Slider::handleSizeRatio>(
                        "handleSizeRatio", 0.01, 0.5, "Handle length as a fraction of the track."),
                    env::colorField<Slider, &Slider::handleNormalColor>("handleNormalColor", "Handle colour at rest."),
                    env::colorField<Slider, &Slider::handleHoveredColor>("handleHoveredColor", "Handle under the pointer."),
                    env::colorField<Slider, &Slider::handlePressedColor>("handlePressedColor", "Handle while dragged."),
                    env::colorField<Slider, &Slider::handleDisabledColor>(
                        "handleDisabledColor", "Handle while not interactable."),
                    assetField<Slider, &Slider::handleNormalTextureRef, "slider.handleNormalTextureRef", ".vfImage">(
                        "Optional handle .vfImage at rest."),
                    assetField<Slider, &Slider::handleHoveredTextureRef, "slider.handleHoveredTextureRef", ".vfImage">(
                        "Optional handle .vfImage under the pointer."),
                    assetField<Slider, &Slider::handlePressedTextureRef, "slider.handlePressedTextureRef", ".vfImage">(
                        "Optional handle .vfImage while dragged."),
                    assetField<Slider, &Slider::handleDisabledTextureRef, "slider.handleDisabledTextureRef", ".vfImage">(
                        "Optional handle .vfImage while not interactable."),
                    env::colorField<Slider, &Slider::fillColor>("fillColor", "Colour of the filled part of the track."),
                    assetField<Slider, &Slider::fillTextureRef, "slider.fillTextureRef", ".vfImage">(
                        "Optional .vfImage of the filled part."),
                    env::floatField<Slider, &Slider::colorTransitionDuration>(
                        "colorTransitionDuration", 0.0, 2.0, "Seconds to blend between handle colours."),
                    env::boolField<Slider, &Slider::interactable>("interactable", "false = disabled."),
                    env::readOnly(codedEnumField<Slider, &Slider::currentState, PressStateNames>(
                                      "currentState", "The pointer state."),
                                  runtimeHint),
                    env::readOnly(env::boolField<Slider, &Slider::isDragging>("isDragging", "The handle is being dragged."),
                                  runtimeHint)
                };
                t.crossCheck = &checkSlider;
                return t;
            }();
            return table;
        }

        const env::SectionTable<ProgressBar>& progressBarTable()
        {
            static const env::SectionTable<ProgressBar> table = []
            {
                env::SectionTable<ProgressBar> t;
                t.name = "progressBar";
                t.fields = {
                    env::floatField<ProgressBar, &ProgressBar::minValue>(
                        "minValue", -maxValueMagnitude, maxValueMagnitude, "Empty value."),
                    env::floatField<ProgressBar, &ProgressBar::maxValue>(
                        "maxValue", -maxValueMagnitude, maxValueMagnitude, "Full value (> minValue)."),
                    env::floatField<ProgressBar, &ProgressBar::value>(
                        "value", -maxValueMagnitude, maxValueMagnitude, "Current value (minValue..maxValue)."),
                    codedEnumField<ProgressBar, &ProgressBar::orientation, OrientationNames>(
                        "orientation", "horizontal (fills left to right) or vertical (bottom to top)."),
                    env::boolField<ProgressBar, &ProgressBar::invertDirection>(
                        "invertDirection", "Fill from the other end."),
                    env::boolField<ProgressBar, &ProgressBar::smoothInterpolation>(
                        "smoothInterpolation", "Animate the fill toward a new value."),
                    env::floatField<ProgressBar, &ProgressBar::interpolationSpeed>(
                        "interpolationSpeed", 0.1, 50.0, "Fill animation speed (with smoothInterpolation)."),
                    env::colorField<ProgressBar, &ProgressBar::trackColor>("trackColor", "Colour of the empty track."),
                    assetField<ProgressBar, &ProgressBar::trackTextureRef, "progressBar.trackTextureRef", ".vfImage">(
                        "Optional .vfImage of the track."),
                    env::colorField<ProgressBar, &ProgressBar::fillColor>("fillColor", "Colour of the fill."),
                    assetField<ProgressBar, &ProgressBar::fillTextureRef, "progressBar.fillTextureRef", ".vfImage">(
                        "Optional .vfImage of the fill."),
                    env::readOnly(env::floatField<ProgressBar, &ProgressBar::displayValue>(
                                      "displayValue", -maxValueMagnitude, maxValueMagnitude,
                                      "The value the fill shows (lags 'value' with smoothInterpolation)."),
                                  runtimeHint)
                };
                t.crossCheck = &checkProgressBar;
                return t;
            }();
            return table;
        }

        const env::SectionTable<Dropdown>& dropdownTable()
        {
            static const env::SectionTable<Dropdown> table = []
            {
                env::SectionTable<Dropdown> t;
                t.name = "dropdown";
                t.fields = {
                    dropdownOptionsField(),
                    env::intField<Dropdown, &Dropdown::selectedIndex>(
                        "selectedIndex", -1.0, 63.0, "Index of the selected option (-1 = none: the placeholder shows)."),
                    env::stringField<Dropdown, &Dropdown::placeholderText>(
                        "placeholderText", "", "Shown while nothing is selected."),
                    env::intField<Dropdown, &Dropdown::maxVisibleItems>(
                        "maxVisibleItems", 1.0, 50.0, "Rows of the open list before it scrolls."),
                    env::boolField<Dropdown, &Dropdown::interactable>("interactable", "false = disabled."),
                    env::colorField<Dropdown, &Dropdown::normalColor>(
                        "normalColor", "Header colour at rest (pushed into the header image every frame)."),
                    env::colorField<Dropdown, &Dropdown::hoveredColor>("hoveredColor", "Header under the pointer."),
                    env::colorField<Dropdown, &Dropdown::openColor>("openColor", "Header while the list is open."),
                    env::colorField<Dropdown, &Dropdown::disabledColor>(
                        "disabledColor", "Header while not interactable."),
                    env::colorField<Dropdown, &Dropdown::listBackgroundColor>(
                        "listBackgroundColor", "Background of the open list."),
                    env::colorField<Dropdown, &Dropdown::itemNormalColor>("itemNormalColor", "List row at rest."),
                    env::colorField<Dropdown, &Dropdown::itemHoveredColor>("itemHoveredColor", "List row under the pointer."),
                    assetField<Dropdown, &Dropdown::fontRef, "dropdown.fontRef", ".vfFont">(
                        "Font of the option list, \"\" = the header label's font or the default."),
                    env::floatField<Dropdown, &Dropdown::fontSize>("fontSize", 1.0, 200.0, "Option font size."),
                    env::floatField<Dropdown, &Dropdown::colorTransitionDuration>(
                        "colorTransitionDuration", 0.0, 2.0, "Seconds to blend between header colours."),
                    env::readOnly(codedEnumField<Dropdown, &Dropdown::currentState, DropdownStateNames>(
                                      "currentState", "The pointer state."),
                                  runtimeHint),
                    env::readOnly(env::boolField<Dropdown, &Dropdown::isOpen>("isOpen", "The list is open."), runtimeHint),
                    env::readOnly(env::intField<Dropdown, &Dropdown::hoveredOptionIndex>(
                                      "hoveredOptionIndex", -1.0, 1.0e6, "The option under the pointer (-1 = none)."),
                                  runtimeHint)
                };
                t.crossCheck = &checkDropdown;
                return t;
            }();
            return table;
        }

        const env::SectionTable<TextInput>& textInputTable()
        {
            static const env::SectionTable<TextInput> table = []
            {
                env::SectionTable<TextInput> t;
                t.name = "textInput";
                t.fields = {
                    env::stringField<TextInput, &TextInput::text>("text", "", "Current text."),
                    env::stringField<TextInput, &TextInput::placeholderText>(
                        "placeholderText", "", "Shown while the text is empty."),
                    assetField<TextInput, &TextInput::fontRef, "textInput.fontRef", ".vfFont">(
                        "A .vfFont (project-relative), \"\" for the default font."),
                    env::floatField<TextInput, &TextInput::fontSize>("fontSize", 1.0, 200.0, "Font size."),
                    env::colorField<TextInput, &TextInput::textColor>("textColor", "Text colour."),
                    env::colorField<TextInput, &TextInput::placeholderColor>("placeholderColor", "Placeholder colour."),
                    env::colorField<TextInput, &TextInput::normalColor>(
                        "normalColor", "Background at rest (pushed into the field's image every frame)."),
                    env::colorField<TextInput, &TextInput::hoveredColor>("hoveredColor", "Background under the pointer."),
                    env::colorField<TextInput, &TextInput::focusedColor>("focusedColor", "Background while typing."),
                    env::colorField<TextInput, &TextInput::disabledColor>(
                        "disabledColor", "Background while not interactable."),
                    env::floatField<TextInput, &TextInput::colorTransitionDuration>(
                        "colorTransitionDuration", 0.0, 2.0, "Seconds to blend between background colours."),
                    env::boolField<TextInput, &TextInput::interactable>("interactable", "false = disabled."),
                    env::intField<TextInput, &TextInput::maxLength>("maxLength", 0.0, 10000.0, "Character limit (0 = none)."),
                    env::colorField<TextInput, &TextInput::selectionColor>("selectionColor", "Selection highlight."),
                    env::colorField<TextInput, &TextInput::caretColor>("caretColor", "Caret colour."),
                    env::floatField<TextInput, &TextInput::caretWidth>("caretWidth", 0.5, 10.0, "Caret width in pixels."),
                    env::floatField<TextInput, &TextInput::caretBlinkRate>(
                        "caretBlinkRate", 0.1, 2.0, "Seconds per caret blink."),
                    env::readOnly(codedEnumField<TextInput, &TextInput::currentState, TextInputStateNames>(
                                      "currentState", "The pointer / focus state."),
                                  runtimeHint)
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<LayoutGroup>& layoutGroupTable()
        {
            static const env::SectionTable<LayoutGroup> table = []
            {
                env::SectionTable<LayoutGroup> t;
                t.name = "layoutGroup";
                t.fields = {
                    codedEnumField<LayoutGroup, &LayoutGroup::direction, DirectionNames>(
                        "direction",
                        "vertical, horizontal or grid: the order DIRECT children are stacked in (the layout rewrites "
                        "their position every frame; their size stays theirs)."),
                    env::floatField<LayoutGroup, &LayoutGroup::spacing>(
                        "spacing", 0.0, 500.0, "Gap between children in screen pixels (not scaled with the canvas)."),
                    env::vec4Field<LayoutGroup, &LayoutGroup::padding>(
                        "padding", 0.0, 500.0, "Inner padding [left, right, top, bottom] in screen pixels."),
                    codedEnumField<LayoutGroup, &LayoutGroup::childAlignment, ChildAlignmentNames>(
                        "childAlignment", "start, center or end across the stacking direction."),
                    env::intField<LayoutGroup, &LayoutGroup::constraintCount>(
                        "constraintCount", 1.0, 20.0, "Columns of a grid.")
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<ListView>& listViewTable()
        {
            static const env::SectionTable<ListView> table = []
            {
                env::SectionTable<ListView> t;
                t.name = "listView";
                t.fields = {
                    assetField<ListView, &ListView::itemTemplateRef, "listView.itemTemplateRef", ".vfPrefab">(
                        "The .vfPrefab every row is instantiated from (make one with ui_save_prefab); \"\" = no rows."),
                    env::intField<ListView, &ListView::itemCount>(
                        "itemCount", 0.0, 256.0, "Number of rows (engine-managed instances, never saved)."),
                    env::boolField<ListView, &ListView::selectable>("selectable", "Rows can be clicked to select them."),
                    env::colorField<ListView, &ListView::selectedTint>("selectedTint", "Overlay on the selected row."),
                    env::readOnly(env::intField<ListView, &ListView::selectedIndex>(
                                      "selectedIndex", -1.0, 1.0e6, "The selected row (-1 = none)."),
                                  runtimeHint)
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Mask>& maskTable()
        {
            static const env::SectionTable<Mask> table = []
            {
                env::SectionTable<Mask> t;
                t.name = "mask";
                t.fields = {
                    assetField<Mask, &Mask::maskTextureRef, "mask.maskTextureRef", ".vfImage">(
                        "Alpha .vfImage whose shape clips the children, \"\" = the rect."),
                    env::floatField<Mask, &Mask::alphaThreshold>(
                        "alphaThreshold", 0.0, 1.0, "Mask alpha below this clips."),
                    env::boolField<Mask, &Mask::showMaskGraphic>("showMaskGraphic", "Also draw the mask shape.")
                };
                return t;
            }();
            return table;
        }

        const env::SectionTable<Scroll>& scrollTable()
        {
            static const env::SectionTable<Scroll> table = []
            {
                env::SectionTable<Scroll> t;
                t.name = "scroll";
                t.fields = {
                    env::boolField<Scroll, &Scroll::horizontalScrollEnabled>(
                        "horizontalScrollEnabled", "Content can scroll sideways."),
                    env::boolField<Scroll, &Scroll::verticalScrollEnabled>(
                        "verticalScrollEnabled", "Content can scroll up and down."),
                    codedEnumField<Scroll, &Scroll::horizontalScrollbarVisibility, ScrollbarVisibilityNames>(
                        "horizontalScrollbarVisibility", "auto (only when the content overflows), alwaysVisible or hidden."),
                    codedEnumField<Scroll, &Scroll::verticalScrollbarVisibility, ScrollbarVisibilityNames>(
                        "verticalScrollbarVisibility", "auto, alwaysVisible or hidden."),
                    env::floatField<Scroll, &Scroll::scrollSensitivity>(
                        "scrollSensitivity", 0.1, 10.0, "Mouse-wheel speed multiplier.")
                };
                return t;
            }();
            return table;
        }

        // The table of a section's value (overloads by alternative type, for std::visit).
        const env::SectionTable<CanvasValues>& tableFor(const CanvasValues&) { return canvasTable(); }
        const env::SectionTable<Rect>& tableFor(const Rect&) { return rectTable(); }
        const env::SectionTable<Image>& tableFor(const Image&) { return imageTable(); }
        const env::SectionTable<Label>& tableFor(const Label&) { return labelTable(); }
        const env::SectionTable<Button>& tableFor(const Button&) { return buttonTable(); }
        const env::SectionTable<Checkbox>& tableFor(const Checkbox&) { return checkboxTable(); }
        const env::SectionTable<Slider>& tableFor(const Slider&) { return sliderTable(); }
        const env::SectionTable<ProgressBar>& tableFor(const ProgressBar&) { return progressBarTable(); }
        const env::SectionTable<Dropdown>& tableFor(const Dropdown&) { return dropdownTable(); }
        const env::SectionTable<TextInput>& tableFor(const TextInput&) { return textInputTable(); }
        const env::SectionTable<LayoutGroup>& tableFor(const LayoutGroup&) { return layoutGroupTable(); }
        const env::SectionTable<ListView>& tableFor(const ListView&) { return listViewTable(); }
        const env::SectionTable<Mask>& tableFor(const Mask&) { return maskTable(); }
        const env::SectionTable<Scroll>& tableFor(const Scroll&) { return scrollTable(); }

        // ------------------------------------------------------------------
        // Runtime members a write must leave as the engine has them
        // ------------------------------------------------------------------

        template <typename T>
        void keepRuntime(T&, const T&)
        {
        }

        void keepRuntime(CanvasValues& out, const CanvasValues& live)
        {
            out.theme = live.theme;
        }

        void keepRuntime(Image& out, const Image& live)
        {
            out.renderTextureSource = live.renderTextureSource;
            out.externalTextureKey = live.externalTextureKey;
        }

        void keepRuntime(Button& out, const Button& live)
        {
            out.currentState = live.currentState;
        }

        void keepRuntime(Checkbox& out, const Checkbox& live)
        {
            out.currentState = live.currentState;
        }

        void keepRuntime(Slider& out, const Slider& live)
        {
            out.currentState = live.currentState;
            out.isDragging = live.isDragging;
        }

        void keepRuntime(ProgressBar& out, const ProgressBar& live)
        {
            out.displayValue = live.displayValue;
        }

        void keepRuntime(Dropdown& out, const Dropdown& live)
        {
            out.currentState = live.currentState;
            out.isOpen = live.isOpen;
            out.hoveredOptionIndex = live.hoveredOptionIndex;
        }

        void keepRuntime(TextInput& out, const TextInput& live)
        {
            out.currentState = live.currentState;
        }

        void keepRuntime(ListView& out, const ListView& live)
        {
            out.selectedIndex = live.selectedIndex;
        }

        // ------------------------------------------------------------------
        // Engine events
        // ------------------------------------------------------------------

        template <typename Query>
        bool queryHas(const services::EntityHandle& entity)
        {
            Query query;
            query.entity = entity;
            return dispatcher().query(query);
        }

        template <typename Command>
        bool executeOn(const services::EntityHandle& entity)
        {
            Command command;
            command.entity = entity;
            return dispatcher().execute(command);
        }

        template <typename Query>
        std::optional<SectionData> queryData(const services::EntityHandle& entity)
        {
            Query query;
            query.entity = entity;
            auto result = dispatcher().query(query);
            if (!result.has_value())
            {
                return std::nullopt;
            }
            return SectionData(std::move(*result));
        }

        // GetCanvasThemeQuery answers the resolved path; reported project-relative.
        std::string canvasThemePath(const services::EntityHandle& entity)
        {
            events::ui::GetCanvasThemeQuery query;
            query.entity = entity;
            std::optional<std::string> path;
            try
            {
                path = dispatcher().query(query);
            }
            catch (const std::runtime_error&)
            {
                return {};
            }
            if (!path.has_value() || path->empty())
            {
                return {};
            }
            return projectRelativeUtf8(tryTerrainProjectRoot(), fs::path(*path));
        }

        bool setData(const services::EntityHandle& entity, const CanvasValues& value)
        {
            events::ui::SetUICanvasDataCommand command;
            command.entity = entity;
            command.canvasData = value.data;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Rect& value)
        {
            events::ui::SetUIRectDataCommand command;
            command.entity = entity;
            command.rectData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Image& value)
        {
            events::ui::SetUIImageDataCommand command;
            command.entity = entity;
            command.imageData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Label& value)
        {
            events::ui::SetUILabelDataCommand command;
            command.entity = entity;
            command.labelData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Button& value)
        {
            events::ui::SetUIButtonDataCommand command;
            command.entity = entity;
            command.buttonData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Checkbox& value)
        {
            events::ui::SetUICheckboxDataCommand command;
            command.entity = entity;
            command.checkboxData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Slider& value)
        {
            events::ui::SetUISliderDataCommand command;
            command.entity = entity;
            command.sliderData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const ProgressBar& value)
        {
            events::ui::SetUIProgressBarDataCommand command;
            command.entity = entity;
            command.progressBarData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Dropdown& value)
        {
            events::ui::SetUIDropdownDataCommand command;
            command.entity = entity;
            command.dropdownData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const TextInput& value)
        {
            events::ui::SetUITextInputDataCommand command;
            command.entity = entity;
            command.textInputData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const LayoutGroup& value)
        {
            events::ui::SetUILayoutGroupDataCommand command;
            command.entity = entity;
            command.layoutGroupData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const ListView& value)
        {
            events::ui::SetUIListViewDataCommand command;
            command.entity = entity;
            command.listViewData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Mask& value)
        {
            events::ui::SetUIMaskDataCommand command;
            command.entity = entity;
            command.maskData = value;
            return dispatcher().execute(command);
        }

        bool setData(const services::EntityHandle& entity, const Scroll& value)
        {
            events::ui::SetUIScrollDataCommand command;
            command.entity = entity;
            command.scrollData = value;
            return dispatcher().execute(command);
        }

        std::string componentName(Section section)
        {
            return std::string(sectionComponents[static_cast<std::size_t>(section)]);
        }

        std::string entityText(const services::EntityHandle& entity)
        {
            return "entity " + std::to_string(toId(entity));
        }

        // ------------------------------------------------------------------
        // UI component names
        // ------------------------------------------------------------------

        struct UIComponentId
        {
            ComponentTypeId id;
            std::string_view name;
        };

        // In Section order (UIMask has no ComponentTypeId and is queried in between), then the UI components no
        // section covers.
        constexpr std::array<UIComponentId, 12> sectionComponentIds{{
            {ComponentTypeId::UICanvas, "UICanvas"},
            {ComponentTypeId::UIRect, "UIRect"},
            {ComponentTypeId::UIImage, "UIImage"},
            {ComponentTypeId::UILabel, "UILabel"},
            {ComponentTypeId::UIButton, "UIButton"},
            {ComponentTypeId::UICheckbox, "UICheckbox"},
            {ComponentTypeId::UISlider, "UISlider"},
            {ComponentTypeId::UIProgressBar, "UIProgressBar"},
            {ComponentTypeId::UIDropdown, "UIDropdown"},
            {ComponentTypeId::UITextInput, "UITextInput"},
            {ComponentTypeId::UILayoutGroup, "UILayoutGroup"},
            {ComponentTypeId::UIListView, "UIListView"},
        }};
        constexpr std::array<UIComponentId, 5> otherComponentIds{{
            {ComponentTypeId::UIScroll, "UIScroll"},
            {ComponentTypeId::UITabs, "UITabs"},
            {ComponentTypeId::UIWindow, "UIWindow"},
            {ComponentTypeId::UITooltip, "UITooltip"},
            {ComponentTypeId::UIStyle, "UIStyle"},
        }};

        bool hasMask(const services::EntityHandle& entity)
        {
            try
            {
                return queryHas<events::ui::HasUIMaskComponentQuery>(entity);
            }
            catch (const std::runtime_error&)
            {
                return false;
            }
        }

        std::optional<services::EntityData> parentOf(const services::EntityData& entity)
        {
            if (!entity.parent.has_value() || !entity.parent->isValid())
            {
                return std::nullopt;
            }
            return findEntity(*entity.parent);
        }
    }

    // ------------------------------------------------------------------
    // Sections
    // ------------------------------------------------------------------

    std::optional<Section> sectionFromKey(std::string_view key)
    {
        for (std::size_t i = 0; i < sectionCount; ++i)
        {
            if (sectionKeys[i] == key)
            {
                return static_cast<Section>(i);
            }
        }
        return std::nullopt;
    }

    bool isAttachable(Section section)
    {
        switch (section)
        {
        case Section::Image:
        case Section::Label:
        case Section::LayoutGroup:
        case Section::Mask:
        case Section::Scroll:
            return true;
        default:
            return false;
        }
    }

    SectionData defaultSection(Section section)
    {
        switch (section)
        {
        case Section::Canvas: return CanvasValues{};
        case Section::Rect: return Rect{};
        case Section::Image: return Image{};
        case Section::Label: return Label{};
        case Section::Button: return Button{};
        case Section::Checkbox: return Checkbox{};
        case Section::Slider: return Slider{};
        case Section::ProgressBar: return ProgressBar{};
        case Section::Dropdown: return Dropdown{};
        case Section::TextInput: return TextInput{};
        case Section::LayoutGroup: return LayoutGroup{};
        case Section::ListView: return ListView{};
        case Section::Mask: return Mask{};
        case Section::Scroll: return Scroll{};
        }
        throw std::logic_error("defaultSection: unknown section");
    }

    // ------------------------------------------------------------------
    // Engine access
    // ------------------------------------------------------------------

    bool hasSection(const services::EntityHandle& entity, Section section)
    {
        switch (section)
        {
        case Section::Canvas: return queryHas<events::ui::HasUICanvasComponentQuery>(entity);
        case Section::Rect: return queryHas<events::ui::HasUIRectComponentQuery>(entity);
        case Section::Image: return queryHas<events::ui::HasUIImageComponentQuery>(entity);
        case Section::Label: return queryHas<events::ui::HasUILabelComponentQuery>(entity);
        case Section::Button: return queryHas<events::ui::HasUIButtonComponentQuery>(entity);
        case Section::Checkbox: return queryHas<events::ui::HasUICheckboxComponentQuery>(entity);
        case Section::Slider: return queryHas<events::ui::HasUISliderComponentQuery>(entity);
        case Section::ProgressBar: return queryHas<events::ui::HasUIProgressBarComponentQuery>(entity);
        case Section::Dropdown: return queryHas<events::ui::HasUIDropdownComponentQuery>(entity);
        case Section::TextInput: return queryHas<events::ui::HasUITextInputComponentQuery>(entity);
        case Section::LayoutGroup: return queryHas<events::ui::HasUILayoutGroupComponentQuery>(entity);
        case Section::ListView: return queryHas<events::ui::HasUIListViewComponentQuery>(entity);
        case Section::Mask: return queryHas<events::ui::HasUIMaskComponentQuery>(entity);
        case Section::Scroll: return queryHas<events::ui::HasUIScrollComponentQuery>(entity);
        }
        throw std::logic_error("hasSection: unknown section");
    }

    std::vector<Section> presentSections(const services::EntityHandle& entity)
    {
        std::vector<Section> out;
        for (std::size_t i = 0; i < sectionCount; ++i)
        {
            const Section section = static_cast<Section>(i);
            if (hasSection(entity, section))
            {
                out.push_back(section);
            }
        }
        return out;
    }

    std::optional<SectionData> readSection(const services::EntityHandle& entity, Section section)
    {
        switch (section)
        {
        case Section::Canvas:
        {
            events::ui::GetUICanvasDataQuery query;
            query.entity = entity;
            std::optional<services::UICanvasData> data = dispatcher().query(query);
            if (!data.has_value())
            {
                return std::nullopt;
            }
            CanvasValues values;
            values.data = *data;
            values.theme = canvasThemePath(entity);
            return SectionData(std::in_place_type<CanvasValues>, std::move(values));
        }
        case Section::Rect: return queryData<events::ui::GetUIRectDataQuery>(entity);
        case Section::Image: return queryData<events::ui::GetUIImageDataQuery>(entity);
        case Section::Label: return queryData<events::ui::GetUILabelDataQuery>(entity);
        case Section::Button: return queryData<events::ui::GetUIButtonDataQuery>(entity);
        case Section::Checkbox: return queryData<events::ui::GetUICheckboxDataQuery>(entity);
        case Section::Slider: return queryData<events::ui::GetUISliderDataQuery>(entity);
        case Section::ProgressBar: return queryData<events::ui::GetUIProgressBarDataQuery>(entity);
        case Section::Dropdown: return queryData<events::ui::GetUIDropdownDataQuery>(entity);
        case Section::TextInput: return queryData<events::ui::GetUITextInputDataQuery>(entity);
        case Section::LayoutGroup: return queryData<events::ui::GetUILayoutGroupDataQuery>(entity);
        case Section::ListView: return queryData<events::ui::GetUIListViewDataQuery>(entity);
        case Section::Mask: return queryData<events::ui::GetUIMaskDataQuery>(entity);
        case Section::Scroll: return queryData<events::ui::GetUIScrollDataQuery>(entity);
        }
        throw std::logic_error("readSection: unknown section");
    }

    void writeSection(const services::EntityHandle& entity, const SectionData& data)
    {
        const bool written = std::visit([&](const auto& value) { return setData(entity, value); }, data);
        if (!written)
        {
            throw std::runtime_error("the engine refused to write the " + componentName(sectionOf(data)) + " of " +
                                     entityText(entity) + " (it has none, or the entity is gone)");
        }
    }

    void addSection(const services::EntityHandle& entity, Section section)
    {
        bool added = false;
        switch (section)
        {
        case Section::Canvas: added = executeOn<events::ui::AddUICanvasComponentCommand>(entity); break;
        case Section::Rect: added = executeOn<events::ui::AddUIRectComponentCommand>(entity); break;
        case Section::Image: added = executeOn<events::ui::AddUIImageComponentCommand>(entity); break;
        case Section::Label: added = executeOn<events::ui::AddUILabelComponentCommand>(entity); break;
        case Section::Button: added = executeOn<events::ui::AddUIButtonComponentCommand>(entity); break;
        case Section::Checkbox: added = executeOn<events::ui::AddUICheckboxComponentCommand>(entity); break;
        case Section::Slider: added = executeOn<events::ui::AddUISliderComponentCommand>(entity); break;
        case Section::ProgressBar: added = executeOn<events::ui::AddUIProgressBarComponentCommand>(entity); break;
        case Section::Dropdown: added = executeOn<events::ui::AddUIDropdownComponentCommand>(entity); break;
        case Section::TextInput: added = executeOn<events::ui::AddUITextInputComponentCommand>(entity); break;
        case Section::LayoutGroup: added = executeOn<events::ui::AddUILayoutGroupComponentCommand>(entity); break;
        case Section::ListView: added = executeOn<events::ui::AddUIListViewComponentCommand>(entity); break;
        case Section::Mask: added = executeOn<events::ui::AddUIMaskComponentCommand>(entity); break;
        case Section::Scroll: added = executeOn<events::ui::AddUIScrollComponentCommand>(entity); break;
        }
        if (!added)
        {
            throw std::runtime_error("could not add a " + componentName(section) + " to " + entityText(entity) +
                                     " (it already has one, or the entity is gone)");
        }
    }

    void removeSection(const services::EntityHandle& entity, Section section)
    {
        bool removed = false;
        switch (section)
        {
        case Section::Canvas: removed = executeOn<events::ui::RemoveUICanvasComponentCommand>(entity); break;
        case Section::Rect: removed = executeOn<events::ui::RemoveUIRectComponentCommand>(entity); break;
        case Section::Image: removed = executeOn<events::ui::RemoveUIImageComponentCommand>(entity); break;
        case Section::Label: removed = executeOn<events::ui::RemoveUILabelComponentCommand>(entity); break;
        case Section::Button: removed = executeOn<events::ui::RemoveUIButtonComponentCommand>(entity); break;
        case Section::Checkbox: removed = executeOn<events::ui::RemoveUICheckboxComponentCommand>(entity); break;
        case Section::Slider: removed = executeOn<events::ui::RemoveUISliderComponentCommand>(entity); break;
        case Section::ProgressBar: removed = executeOn<events::ui::RemoveUIProgressBarComponentCommand>(entity); break;
        case Section::Dropdown: removed = executeOn<events::ui::RemoveUIDropdownComponentCommand>(entity); break;
        case Section::TextInput: removed = executeOn<events::ui::RemoveUITextInputComponentCommand>(entity); break;
        case Section::LayoutGroup: removed = executeOn<events::ui::RemoveUILayoutGroupComponentCommand>(entity); break;
        case Section::ListView: removed = executeOn<events::ui::RemoveUIListViewComponentCommand>(entity); break;
        case Section::Mask: removed = executeOn<events::ui::RemoveUIMaskComponentCommand>(entity); break;
        case Section::Scroll: removed = executeOn<events::ui::RemoveUIScrollComponentCommand>(entity); break;
        }
        if (!removed)
        {
            throw std::runtime_error("could not remove the " + componentName(section) + " of " + entityText(entity) +
                                     " (it has none, or the entity is gone)");
        }
    }

    // ------------------------------------------------------------------
    // Values
    // ------------------------------------------------------------------

    nlohmann::json sectionJson(const SectionData& data)
    {
        return std::visit([](const auto& value) { return env::toJson(tableFor(value), value); }, data);
    }

    nlohmann::json authoredJson(const SectionData& data)
    {
        return std::visit(
            [](const auto& value)
            {
                json out = json::object();
                for (const auto& field : tableFor(value).fields)
                {
                    if (field.persisted && field.setHint.empty())
                    {
                        out[std::string(field.key)] = field.get(value);
                    }
                }
                return out;
            },
            data);
    }

    SectionData patchSection(const SectionData& base, const nlohmann::json& patch, std::string_view prefix)
    {
        return std::visit(
            [&](const auto& value) -> SectionData
            {
                const auto& table = tableFor(value);
                if (!patch.is_object())
                {
                    return env::strictPatch(table, value, patch, prefix); // throws: not an object
                }
                const json remaining = env::dropUnchanged(table, value, patch, prefix);
                if (remaining.empty())
                {
                    // A pure echo: nothing changes, and a stored cross-field state (from a scene or a script) is
                    // left as it is rather than re-validated.
                    return value;
                }
                return env::strictPatch(table, value, remaining, prefix);
            },
            base);
    }

    SectionData copyAuthored(const SectionData& live, const SectionData& authored)
    {
        if (live.index() != authored.index())
        {
            throw std::logic_error("copyAuthored: the live value and the authored value are different sections");
        }
        return std::visit(
            [&](const auto& liveValue) -> SectionData
            {
                using T = std::decay_t<decltype(liveValue)>;
                T out = std::get<T>(authored);
                keepRuntime(out, liveValue);
                return out;
            },
            live);
    }

    bool sameAuthored(const SectionData& a, const SectionData& b)
    {
        return a.index() == b.index() && authoredJson(a) == authoredJson(b);
    }

    nlohmann::json sectionRanges(Section section)
    {
        return std::visit([](const auto& value) { return env::rangesJson(tableFor(value)); }, defaultSection(section));
    }

    nlohmann::json sectionSchema(Section section, std::string description)
    {
        return std::visit(
            [&](const auto& value)
            {
                const auto& table = tableFor(value);
                json out = env::schemaJson(table, std::move(description));
                for (const auto& field : table.fields)
                {
                    if (!field.persisted || field.setHint.empty())
                    {
                        continue;
                    }
                    out["properties"][std::string(field.key)] = {
                        {"description", "Read-only (" + std::string(field.setHint) +
                                            "). ui_get reports it; it is accepted back only unchanged."}
                    };
                }
                return out;
            },
            defaultSection(section));
    }

    // ------------------------------------------------------------------
    // Widget types
    // ------------------------------------------------------------------

    std::optional<WidgetType> widgetTypeFromName(std::string_view name)
    {
        for (std::size_t i = 0; i < widgetTypeNames.size(); ++i)
        {
            if (env::detail::equalsIgnoreCase(name, widgetTypeNames[i]))
            {
                return static_cast<WidgetType>(i);
            }
        }
        return std::nullopt;
    }

    std::string detectType(const services::EntityData& entity)
    {
        static constexpr std::array<UIComponentId, 14> byPriority{{
            {ComponentTypeId::UICanvas, "Canvas"},
            {ComponentTypeId::UIButton, "Button"},
            {ComponentTypeId::UICheckbox, "Checkbox"},
            {ComponentTypeId::UISlider, "Slider"},
            {ComponentTypeId::UIProgressBar, "ProgressBar"},
            {ComponentTypeId::UIDropdown, "Dropdown"},
            {ComponentTypeId::UITextInput, "TextInput"},
            {ComponentTypeId::UIListView, "ListView"},
            {ComponentTypeId::UITabs, "Tabs"},
            {ComponentTypeId::UIWindow, "Window"},
            {ComponentTypeId::UILayoutGroup, "LayoutGroup"},
            {ComponentTypeId::UILabel, "Label"},
            {ComponentTypeId::UIImage, "Image"},
            {ComponentTypeId::UIRect, "Rect"},
        }};
        for (const UIComponentId& entry : byPriority)
        {
            if (entity.hasComponent(entry.id))
            {
                return std::string(entry.name);
            }
        }
        return {};
    }

    // ------------------------------------------------------------------
    // Hierarchy
    // ------------------------------------------------------------------

    std::vector<std::string> uiComponentNames(const services::EntityData& entity)
    {
        std::vector<std::string> out;
        for (const UIComponentId& entry : sectionComponentIds)
        {
            if (entity.hasComponent(entry.id))
            {
                out.emplace_back(entry.name);
            }
        }
        if (hasMask(entity.handle))
        {
            out.emplace_back("UIMask");
        }
        for (const UIComponentId& entry : otherComponentIds)
        {
            if (entity.hasComponent(entry.id))
            {
                out.emplace_back(entry.name);
            }
        }
        return out;
    }

    bool isUIEntity(const services::EntityData& entity)
    {
        for (const UIComponentId& entry : sectionComponentIds)
        {
            if (entity.hasComponent(entry.id))
            {
                return true;
            }
        }
        for (const UIComponentId& entry : otherComponentIds)
        {
            if (entity.hasComponent(entry.id))
            {
                return true;
            }
        }
        return hasMask(entity.handle);
    }

    std::optional<services::EntityData> findCanvas(const services::EntityData& entity)
    {
        if (entity.hasComponent(ComponentTypeId::UICanvas))
        {
            return entity;
        }
        std::optional<services::EntityData> current = parentOf(entity);
        for (int depth = 0; current.has_value() && depth < maxHierarchyDepth; ++depth)
        {
            if (current->hasComponent(ComponentTypeId::UICanvas))
            {
                return current;
            }
            current = parentOf(*current);
        }
        return std::nullopt;
    }

    bool hasUIAncestor(const services::EntityData& entity)
    {
        std::optional<services::EntityData> current = parentOf(entity);
        for (int depth = 0; current.has_value() && depth < maxHierarchyDepth; ++depth)
        {
            if (isUIEntity(*current))
            {
                return true;
            }
            current = parentOf(*current);
        }
        return false;
    }

    bool isListItem(const services::EntityHandle& entity)
    {
        try
        {
            events::ui::IsUIListItemQuery query;
            query.entity = entity;
            return dispatcher().query(query);
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    bool inPreviewSandbox(const services::EntityHandle& entity)
    {
        try
        {
            events::ui::IsInUIPreviewSandboxQuery query;
            query.entity = entity;
            return dispatcher().query(query);
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    std::optional<services::EntityHandle> checkboxLabelPart(const services::EntityData& checkbox)
    {
        if (!checkbox.hasComponent(ComponentTypeId::UICheckbox))
        {
            return std::nullopt;
        }
        for (const services::EntityHandle& child : checkbox.children)
        {
            const std::optional<services::EntityData> data = findEntity(child);
            if (data.has_value() && data->hasComponent(ComponentTypeId::UILabel))
            {
                return child;
            }
        }
        return std::nullopt;
    }

    std::size_t countEntitiesNamed(const std::string& name)
    {
        events::scene::FindEntitiesByNameQuery query;
        query.name = name;
        try
        {
            return dispatcher().query(query).size();
        }
        catch (const std::runtime_error&)
        {
            return 0;
        }
    }

    // ------------------------------------------------------------------
    // Placement
    // ------------------------------------------------------------------

    namespace
    {
        glm::vec4 readMargin(const json& value, const std::string& path)
        {
            if (value.is_number())
            {
                return glm::vec4(env::requireFloat(value, 0.0, maxReferencePixels, path));
            }
            if (value.is_array())
            {
                const glm::vec2 margin = env::requireVec2(value, 0.0, maxReferencePixels, path);
                return glm::vec4(margin.x, margin.x, margin.y, margin.y);
            }
            if (value.is_object())
            {
                env::rejectUnknownKeys(value, {"left", "right", "top", "bottom"}, path);
                static constexpr std::array<std::string_view, 4> sides{"left", "right", "top", "bottom"};
                glm::vec4 out(0.0f);
                for (std::size_t i = 0; i < sides.size(); ++i)
                {
                    const auto it = value.find(std::string(sides[i]));
                    if (it != value.end() && !it->is_null())
                    {
                        out[static_cast<glm::length_t>(i)] =
                            env::requireFloat(*it, 0.0, maxReferencePixels, path + "." + std::string(sides[i]));
                    }
                }
                return out;
            }
            throw ArgError("'" + path + "' must be a number, [x, y] or {left, right, top, bottom} (got " +
                           env::detail::valueText(value) + ")");
        }
    }

    PlacementArgs readPlacement(const nlohmann::json& value, glm::vec2 defaultSize, std::string_view path)
    {
        const std::string base(path);
        env::rejectUnknownKeys(value, {"preset", "size", "margin", "offset", "relativeTo"}, base);

        PlacementArgs args;
        args.placement.size = defaultSize;

        const auto preset = value.find("preset");
        if (preset != value.end() && !preset->is_null())
        {
            bool found = false;
            if (preset->is_string())
            {
                const std::string name = preset->get<std::string>();
                for (const utilities::ui::AnchorPreset& entry : utilities::ui::anchorPresets)
                {
                    if (env::detail::equalsIgnoreCase(name, entry.name))
                    {
                        args.placement.h = entry.h;
                        args.placement.v = entry.v;
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
            {
                std::vector<std::string_view> names;
                for (const utilities::ui::AnchorPreset& entry : utilities::ui::anchorPresets)
                {
                    names.push_back(entry.name);
                }
                throw ArgError("'" + base + ".preset' must be one of " + env::detail::joined(names) + " (got " +
                               env::detail::valueText(*preset) + ")");
            }
        }

        const auto size = value.find("size");
        if (size != value.end() && !size->is_null())
        {
            args.placement.size = env::requireVec2(*size, 0.0, maxReferencePixels, base + ".size");
        }

        const auto margin = value.find("margin");
        if (margin != value.end() && !margin->is_null())
        {
            args.placement.margin = readMargin(*margin, base + ".margin");
        }

        const auto offset = value.find("offset");
        if (offset != value.end() && !offset->is_null())
        {
            args.placement.offset = env::requireVec2(*offset, -maxReferencePixels, maxReferencePixels, base + ".offset");
        }

        const auto relativeTo = value.find("relativeTo");
        if (relativeTo != value.end() && !relativeTo->is_null())
        {
            static constexpr std::array<std::string_view, 2> frames{"parent", "canvas"};
            args.relativeToCanvas = env::detail::checkedEnum(*relativeTo, frames, base + ".relativeTo") == "canvas";
        }
        return args;
    }

    nlohmann::json placementSchema(std::string description)
    {
        std::vector<std::string> presets;
        for (const utilities::ui::AnchorPreset& entry : utilities::ui::anchorPresets)
        {
            presets.emplace_back(entry.name);
        }

        const auto pair = [](std::string text, double min, double max) -> json
        {
            return {
                {"type", "array"},
                {"items", {{"type", "number"}, {"minimum", min}, {"maximum", max}}},
                {"minItems", 2},
                {"maxItems", 2},
                {"description", std::move(text)}
            };
        };

        json margin = json::object();
        margin["description"] =
            "Inward distance from the frame's edges in reference px: n (all sides), [x, y] (left/right, top/bottom) or "
            "{left, right, top, bottom}. A side the preset does not anchor to ignores its margin. Default 0.";
        margin["anyOf"] = json::array({
            {{"type", "number"}, {"minimum", 0.0}, {"maximum", maxReferencePixels}},
            pair("[x, y]", 0.0, maxReferencePixels),
            {
                {"type", "object"},
                {"properties", {
                    {"left", {{"type", "number"}, {"minimum", 0.0}}},
                    {"right", {{"type", "number"}, {"minimum", 0.0}}},
                    {"top", {{"type", "number"}, {"minimum", 0.0}}},
                    {"bottom", {{"type", "number"}, {"minimum", 0.0}}}
                }},
                {"additionalProperties", false}
            }
        });

        json properties = json::object();
        properties["preset"] = schema::enumString(
            "Where the widget sits in its frame: topLeft, top, topRight, left, center, right, bottomLeft, bottom, "
            "bottomRight; stretchTop / stretchMiddle / stretchBottom span the frame's width, stretchLeft / "
            "stretchCenter / stretchRight its height, stretch both. Default center.",
            presets);
        properties["size"] = pair(
            "[width, height] in canvas reference pixels (ignored along a stretched axis). Default: the widget type's "
            "size.",
            0.0, maxReferencePixels);
        properties["margin"] = std::move(margin);
        properties["offset"] = pair("[dx, dy] nudge in reference pixels after placing, +y DOWN. Default [0, 0].",
                                    -maxReferencePixels, maxReferencePixels);
        properties["relativeTo"] = schema::enumString(
            "The frame: 'parent' (default) = the parent widget's rect, so the widget stays inside it at every "
            "resolution; 'canvas' = the whole canvas.",
            {"parent", "canvas"});

        json out = schema::object(std::move(properties));
        out["additionalProperties"] = false;
        out["description"] = std::move(description);
        return out;
    }

    utilities::ui::AffineRect placementFrame(const services::EntityData& parent, bool relativeToCanvas)
    {
        if (relativeToCanvas || parent.hasComponent(ComponentTypeId::UICanvas) ||
            !parent.hasComponent(ComponentTypeId::UIRect))
        {
            return utilities::ui::fullViewportRect();
        }
        const std::optional<SectionData> rect = readSection(parent.handle, Section::Rect);
        if (!rect.has_value())
        {
            return utilities::ui::fullViewportRect();
        }
        return utilities::ui::affineRectOf(utilities::ui::toRectLayout(std::get<Rect>(*rect)));
    }

    services::UIRectData placeRect(const services::UIRectData& current, const PlacementArgs& args,
                                   const utilities::ui::AffineRect& frame)
    {
        services::UIRectData out = current;
        utilities::ui::applyRectLayout(out, utilities::ui::placeInside(frame, args.placement));
        return out;
    }

    // ------------------------------------------------------------------
    // Reports
    // ------------------------------------------------------------------

    nlohmann::json resolvedJson(const services::UIRectData& rect, const services::UICanvasData& canvas,
                                std::optional<glm::vec2> viewport)
    {
        const glm::vec2 size = viewport.value_or(glm::vec2(canvas.referenceWidth, canvas.referenceHeight));
        float scale = 1.0f;
        if (canvas.scaleMode == scaleWithScreenSize && canvas.referenceWidth > 0.0f && canvas.referenceHeight > 0.0f)
        {
            scale = std::min(size.x / canvas.referenceWidth, size.y / canvas.referenceHeight);
        }
        const utilities::ui::PixelRect resolved =
            utilities::ui::resolveRect(utilities::ui::toRectLayout(rect), size.x, size.y, scale);
        return {
            {"x", env::floatJson(resolved.x)},
            {"y", env::floatJson(resolved.y)},
            {"w", env::floatJson(resolved.w)},
            {"h", env::floatJson(resolved.h)},
            {"viewport", env::vec2Json(size)},
            {"scale", env::floatJson(scale)}
        };
    }

    nlohmann::json widgetReport(const services::EntityData& entity, const ReportOptions& options)
    {
        json out = json::object();
        out["entity"] = entityId(entity.handle);
        out["name"] = entity.name;
        out["type"] = detectType(entity);
        out["active"] = entity.isActive;
        out["effectivelyActive"] = entity.isEffectivelyActive;
        out["parent"] = entityId(entity.parent);

        const std::optional<services::EntityData> canvas = findCanvas(entity);
        out["canvasId"] = canvas.has_value() ? entityId(canvas->handle) : json(nullptr);

        json children = json::array();
        for (const services::EntityHandle& child : entity.children)
        {
            children.push_back(entityId(child));
        }
        out["children"] = std::move(children);

        json parts = json::object();
        if (const std::optional<services::EntityHandle> label = checkboxLabelPart(entity))
        {
            parts["label"] = entityId(*label);
        }
        out["parts"] = std::move(parts);

        const std::optional<services::EntityData> parent = parentOf(entity);
        out["layoutControlled"] = parent.has_value() && parent->hasComponent(ComponentTypeId::UILayoutGroup);
        out["listItem"] = isListItem(entity.handle);

        json components = json::array();
        for (const std::string& name : uiComponentNames(entity))
        {
            components.push_back(name);
        }
        out["components"] = std::move(components);

        const std::vector<Section> present = presentSections(entity.handle);
        std::optional<services::UIRectData> rect;
        for (const Section section : present)
        {
            const std::optional<SectionData> data = readSection(entity.handle, section);
            if (!data.has_value())
            {
                continue;
            }
            if (section == Section::Rect)
            {
                rect = std::get<Rect>(*data);
            }
            out[std::string(sectionKey(section))] = sectionJson(*data);
        }

        if (entity.hasComponent(ComponentTypeId::UIStyle))
        {
            events::ui::GetUIStyleKeyQuery query;
            query.entity = entity.handle;
            try
            {
                if (const std::optional<std::string> key = dispatcher().query(query))
                {
                    out["styleKey"] = *key;
                }
            }
            catch (const std::runtime_error&)
            {
            }
        }

        if (rect.has_value() && canvas.has_value())
        {
            if (const std::optional<SectionData> canvasData = readSection(canvas->handle, Section::Canvas))
            {
                out["resolved"] = resolvedJson(*rect, std::get<CanvasValues>(*canvasData).data, options.viewport);
            }
        }

        try
        {
            events::ui::GetUICompoundStatusQuery query;
            query.entity = entity.handle;
            const services::UICompoundStatusData status = dispatcher().query(query);
            if (!status.ok)
            {
                out["validation"] = {{"ok", false}, {"errors", status.errors}, {"warnings", status.warnings}};
            }
        }
        catch (const std::runtime_error&)
        {
        }

        if (options.ranges)
        {
            json ranges = json::object();
            for (const Section section : present)
            {
                ranges[std::string(sectionKey(section))] = sectionRanges(section);
            }
            out["ranges"] = std::move(ranges);
        }
        return out;
    }
}
