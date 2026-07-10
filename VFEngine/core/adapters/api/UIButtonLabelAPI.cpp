// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIButtonLabelAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "asset/AssetRef.hpp"

namespace core::api
{
    namespace
    {
        value::Value getButtonData(events::EventDispatcher& dispatcher,
                                   std::span<const value::Value> args,
                                   const char* context)
        {
            if (args.empty()) return value::Value(static_cast<int64_t>(-1));
            auto handle = intToEntity(extractInt64(args[0], context));

            events::ui::HasUIButtonComponentQuery hasQuery;
            hasQuery.entity = handle;
            if (!dispatcher.query(hasQuery))
                return value::Value(static_cast<int64_t>(-1));

            events::ui::GetUIButtonDataQuery getQuery;
            getQuery.entity = handle;
            auto data = dispatcher.query(getQuery);
            if (!data.has_value())
                return value::Value(static_cast<int64_t>(-1));

            return value::Value(static_cast<int64_t>(data->currentState));
        }
    }

    void UIButtonLabelAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_ui_isButtonHovered",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto state = getButtonData(dispatcher, args, "_native_ui_isButtonHovered");
                if (value::isInt(state))
                    return value::Value(value::asInt(state) == 1);
                return value::Value(false);
            }});

        interpreter->registerNativeFunction("_native_ui_isButtonPressed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto state = getButtonData(dispatcher, args, "_native_ui_isButtonPressed");
                if (value::isInt(state))
                    return value::Value(value::asInt(state) == 2);
                return value::Value(false);
            }});

        interpreter->registerNativeFunction("_native_ui_isPointerOverUI",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::ui::IsPointerOverUIQuery query;
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_ui_getButtonState",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return getButtonData(dispatcher, args, "_native_ui_getButtonState");
            }});

        interpreter->registerNativeFunction("_native_ui_setButtonInteractable",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setButtonInteractable"));
                bool interactable = extractBool(args[1]);

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto buttonData = data.value();
                buttonData.interactable = interactable;

                events::ui::SetUIButtonDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.buttonData = buttonData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelText"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(std::string(""));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(std::string(""));

                return value::Value(data->text);
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelText"));
                std::string text = extractString(args[1], "_native_ui_setLabelText");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.text = text;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        // Point a UIImage at a texture asset by path at runtime. Mirrors
        // _native_ui_setLabelText: query the current image data, swap the
        // textureRef, and write it back. The UI renderer resolves textureRef
        // per-frame, so the swap is live (used by the RTS selection portrait
        // + command-card icons). Path must be a registered asset (AssetRef
        // ::fromPath resolves path -> GUID via the AssetDatabase).
        interpreter->registerNativeFunction("_native_ui_setImageTexture",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setImageTexture"));
                std::string path = extractString(args[1], "_native_ui_setImageTexture");

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto imageData = data.value();
                imageData.textureRef = asset::AssetRef::fromPath(path);

                events::ui::SetUIImageDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.imageData = imageData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getImageTexture",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getImageTexture"));

                events::ui::HasUIImageComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(std::string(""));

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(std::string(""));

                return value::Value(data->textureRef.resolve());
            }});

        // VK-1488: bind a plugin/GPU texture to a UIImage by its "__plugintex_<id>__" key
        // (the key returned by PluginContext::registerUITexture). Unlike setImageTexture this
        // is NOT an asset path — it resolves against the UI bindless external-texture table
        // each frame, so the image updates live. Pass "" (or clearImageExternalTexture) to unbind.
        interpreter->registerNativeFunction("_native_ui_setImageExternalTexture",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setImageExternalTexture"));
                std::string key = extractString(args[1], "_native_ui_setImageExternalTexture");

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto imageData = data.value();
                imageData.externalTextureKey = key;

                events::ui::SetUIImageDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.imageData = imageData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_clearImageExternalTexture",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_clearImageExternalTexture"));

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto imageData = data.value();
                imageData.externalTextureKey.clear();

                events::ui::SetUIImageDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.imageData = imageData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        // ---- UILabel property setters/getters (VK-1352) ----
        // Each mirrors _native_ui_setLabelText: query the full UILabelData,
        // mutate one field, write it back via SetUILabelDataCommand. The UI
        // renderer re-reads the label data per-frame, so changes are live.

        interpreter->registerNativeFunction("_native_ui_setLabelFontSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelFontSize"));
                float size = extractFloat(args[1], "_native_ui_setLabelFontSize");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.fontSize = size;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelFontSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelFontSize"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(0.0f);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(0.0f);

                return value::Value(data->fontSize);
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 5) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelColor"));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.color.r = extractFloat(args[1], "_native_ui_setLabelColor");
                labelData.color.g = extractFloat(args[2], "_native_ui_setLabelColor");
                labelData.color.b = extractFloat(args[3], "_native_ui_setLabelColor");
                labelData.color.a = extractFloat(args[4], "_native_ui_setLabelColor");

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto makeColor = [](float r, float g, float b, float a) -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    arr->set(0, value::Value(r));
                    arr->set(1, value::Value(g));
                    arr->set(2, value::Value(b));
                    arr->set(3, value::Value(a));
                    return value::Value(arr);
                };
                if (args.empty()) return makeColor(0.0f, 0.0f, 0.0f, 0.0f);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelColor"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return makeColor(0.0f, 0.0f, 0.0f, 0.0f);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return makeColor(0.0f, 0.0f, 0.0f, 0.0f);

                return makeColor(data->color.r, data->color.g, data->color.b, data->color.a);
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelStyle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelStyle"));
                int64_t style = extractInt64(args[1], "_native_ui_setLabelStyle");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.fontStyle = static_cast<uint8_t>(style);

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelStyle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelStyle"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(static_cast<int64_t>(0));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(static_cast<int64_t>(0));

                return value::Value(static_cast<int64_t>(data->fontStyle));
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelAlignment",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelAlignment"));
                int64_t horizontal = extractInt64(args[1], "_native_ui_setLabelAlignment");
                int64_t vertical = extractInt64(args[2], "_native_ui_setLabelAlignment");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.horizontalAlignment = static_cast<uint8_t>(horizontal);
                labelData.verticalAlignment = static_cast<uint8_t>(vertical);

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelAlignment",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto makeAlign = [](float h, float v) -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(2, value::ValueType::FLOAT);
                    arr->set(0, value::Value(h));
                    arr->set(1, value::Value(v));
                    return value::Value(arr);
                };
                if (args.empty()) return makeAlign(0.0f, 0.0f);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelAlignment"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return makeAlign(0.0f, 0.0f);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return makeAlign(0.0f, 0.0f);

                return makeAlign(static_cast<float>(data->horizontalAlignment),
                                 static_cast<float>(data->verticalAlignment));
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelOverflow",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelOverflow"));
                int64_t overflow = extractInt64(args[1], "_native_ui_setLabelOverflow");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.overflow = static_cast<uint8_t>(overflow);

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelOverflow",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelOverflow"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(static_cast<int64_t>(0));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(static_cast<int64_t>(0));

                return value::Value(static_cast<int64_t>(data->overflow));
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelWordWrap",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelWordWrap"));
                bool wrap = extractBool(args[1], "_native_ui_setLabelWordWrap");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.wordWrap = wrap;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelWordWrap",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelWordWrap"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(false);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(false);

                return value::Value(data->wordWrap);
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelRichText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelRichText"));
                bool richText = extractBool(args[1], "_native_ui_setLabelRichText");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.richText = richText;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelRichText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelRichText"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(false);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(false);

                return value::Value(data->richText);
            }});

        interpreter->registerNativeFunction("_native_ui_setLabelSpacing",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 3) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelSpacing"));
                float line = extractFloat(args[1], "_native_ui_setLabelSpacing");
                float letter = extractFloat(args[2], "_native_ui_setLabelSpacing");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.lineSpacing = line;
                labelData.letterSpacing = letter;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelSpacing",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto makeSpacing = [](float line, float letter) -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(2, value::ValueType::FLOAT);
                    arr->set(0, value::Value(line));
                    arr->set(1, value::Value(letter));
                    return value::Value(arr);
                };
                if (args.empty()) return makeSpacing(0.0f, 0.0f);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelSpacing"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return makeSpacing(0.0f, 0.0f);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return makeSpacing(0.0f, 0.0f);

                return makeSpacing(data->lineSpacing, data->letterSpacing);
            }});

        // Set the label's font by asset path (e.g. an imported .vfFont). Like
        // setImageTexture, the path must resolve to a registered asset GUID.
        interpreter->registerNativeFunction("_native_ui_setLabelFont",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelFont"));
                std::string path = extractString(args[1], "_native_ui_setLabelFont");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.fontRef = asset::AssetRef::fromPath(path);

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getLabelFont",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelFont"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(std::string(""));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(std::string(""));

                return value::Value(data->fontRef.resolve());
            }});

        // ---- UIImage colorTint + UIButton state colors ----
        // Companions to _native_ui_setImageTexture / _native_ui_setButtonInteractable:
        // needed so scripts can skin textured HUD images/buttons at runtime (a dark
        // authored colorTint would otherwise multiply and darken the new texture).

        interpreter->registerNativeFunction("_native_ui_setImageColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 5) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setImageColor"));

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto imageData = data.value();
                imageData.colorTint.r = extractFloat(args[1], "_native_ui_setImageColor");
                imageData.colorTint.g = extractFloat(args[2], "_native_ui_setImageColor");
                imageData.colorTint.b = extractFloat(args[3], "_native_ui_setImageColor");
                imageData.colorTint.a = extractFloat(args[4], "_native_ui_setImageColor");

                events::ui::SetUIImageDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.imageData = imageData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        // _native_ui_setRectPixels(entityId, x, y, w, h) — position/size a UIRect in
        // viewport pixels (top-left origin, y down), same pixel space as
        // Input::getViewportMouseX/Y. Used for runtime-driven overlays (drag boxes).
        interpreter->registerNativeFunction("_native_ui_setRectPixels",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 5) return value::Value(false);

                events::ui::SetUIRectPixelsCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setRectPixels"));
                cmd.x = extractFloat(args[1], "_native_ui_setRectPixels");
                cmd.y = extractFloat(args[2], "_native_ui_setRectPixels");
                cmd.w = extractFloat(args[3], "_native_ui_setRectPixels");
                cmd.h = extractFloat(args[4], "_native_ui_setRectPixels");
                return value::Value(dispatcher.execute(cmd));
            }});

        // _native_ui_getRectPixels(entityId) — resolved on-screen rect of a UIRect in
        // viewport pixels (top-left origin, y down), same pixel space as setRectPixels
        // and Input::getViewportMouseX/Y. miss (no UIRect / no viewport) -> [0.0];
        // hit -> [1.0, x, y, w, h].
        interpreter->registerNativeFunction("_native_ui_getRectPixels",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto missArray = []() -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    arr->set(0, value::Value(0.0f));
                    return value::Value(arr);
                };
                if (args.empty()) return missArray();

                events::ui::GetUIResolvedRectQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getRectPixels"));
                auto rect = dispatcher.query(query);
                if (!rect.has_value()) return missArray();

                auto result = std::make_shared<value::NativeArray>(5, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(rect->x));
                result->set(2, value::Value(rect->y));
                result->set(3, value::Value(rect->w));
                result->set(4, value::Value(rect->h));
                return value::Value(result);
            }});

        // _native_ui_getRectData(entityId) — the AUTHORED UIRect fields in canvas
        // units (anchors normalized [0,1], pivot, sizeDelta + anchoredPosition in
        // canvas units). Extent-independent, unlike getRectPixels — two elements
        // expressed in the same basis stay aligned under any viewport. miss -> [0.0];
        // hit -> [1, anchorMinX, anchorMinY, anchorMaxX, anchorMaxY, pivotX, pivotY,
        //         sizeDeltaX, sizeDeltaY, anchoredX, anchoredY].
        interpreter->registerNativeFunction("_native_ui_getRectData",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto missArray = []() -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    arr->set(0, value::Value(0.0f));
                    return value::Value(arr);
                };
                if (args.empty()) return missArray();

                events::ui::GetUIRectDataQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getRectData"));
                auto data = dispatcher.query(query);
                if (!data.has_value()) return missArray();

                auto result = std::make_shared<value::NativeArray>(11, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(data->anchorMin.x));
                result->set(2, value::Value(data->anchorMin.y));
                result->set(3, value::Value(data->anchorMax.x));
                result->set(4, value::Value(data->anchorMax.y));
                result->set(5, value::Value(data->pivot.x));
                result->set(6, value::Value(data->pivot.y));
                result->set(7, value::Value(data->sizeDelta.x));
                result->set(8, value::Value(data->sizeDelta.y));
                result->set(9, value::Value(data->anchoredPosition.x));
                result->set(10, value::Value(data->anchoredPosition.y));
                return value::Value(result);
            }});

        // _native_ui_setRectData(entityId, anchorMinX, anchorMinY, anchorMaxX,
        //   anchorMaxY, pivotX, pivotY, sizeDeltaX, sizeDeltaY, anchoredX, anchoredY)
        // — set the AUTHORED UIRect fields in canvas units. Read-modify-write so the
        // element's existing blocksRaycast flag is preserved. Returns true on success.
        interpreter->registerNativeFunction("_native_ui_setRectData",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 11) return value::Value(false);

                services::EntityHandle entity = intToEntity(extractInt64(args[0], "_native_ui_setRectData"));

                // Preserve blocksRaycast (and any future non-geometry field) by
                // starting from the current data when present.
                events::ui::GetUIRectDataQuery getQuery;
                getQuery.entity = entity;
                auto existing = dispatcher.query(getQuery);
                services::UIRectData rectData = existing.value_or(services::UIRectData{});

                rectData.anchorMin = glm::vec2(extractFloat(args[1], "_native_ui_setRectData"),
                                               extractFloat(args[2], "_native_ui_setRectData"));
                rectData.anchorMax = glm::vec2(extractFloat(args[3], "_native_ui_setRectData"),
                                               extractFloat(args[4], "_native_ui_setRectData"));
                rectData.pivot = glm::vec2(extractFloat(args[5], "_native_ui_setRectData"),
                                           extractFloat(args[6], "_native_ui_setRectData"));
                rectData.sizeDelta = glm::vec2(extractFloat(args[7], "_native_ui_setRectData"),
                                               extractFloat(args[8], "_native_ui_setRectData"));
                rectData.anchoredPosition = glm::vec2(extractFloat(args[9], "_native_ui_setRectData"),
                                                      extractFloat(args[10], "_native_ui_setRectData"));

                events::ui::SetUIRectDataCommand cmd;
                cmd.entity = entity;
                cmd.rectData = rectData;
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_getImageColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto makeColor = [](float r, float g, float b, float a) -> value::Value {
                    auto arr = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                    arr->set(0, value::Value(r));
                    arr->set(1, value::Value(g));
                    arr->set(2, value::Value(b));
                    arr->set(3, value::Value(a));
                    return value::Value(arr);
                };
                if (args.empty()) return makeColor(1.0f, 1.0f, 1.0f, 1.0f);
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getImageColor"));

                events::ui::HasUIImageComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return makeColor(1.0f, 1.0f, 1.0f, 1.0f);

                events::ui::GetUIImageDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return makeColor(1.0f, 1.0f, 1.0f, 1.0f);

                return makeColor(data->colorTint.r, data->colorTint.g,
                                 data->colorTint.b, data->colorTint.a);
            }});

        // Set a UIButton's three interactive-state colors (normal/hovered/pressed)
        // in one call. 13 args: id, then 3 RGBA quads.
        interpreter->registerNativeFunction("_native_ui_setButtonColors",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 13) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setButtonColors"));

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto buttonData = data.value();
                buttonData.normalColor = glm::vec4(extractFloat(args[1], "_native_ui_setButtonColors"),
                                                   extractFloat(args[2], "_native_ui_setButtonColors"),
                                                   extractFloat(args[3], "_native_ui_setButtonColors"),
                                                   extractFloat(args[4], "_native_ui_setButtonColors"));
                buttonData.hoveredColor = glm::vec4(extractFloat(args[5], "_native_ui_setButtonColors"),
                                                    extractFloat(args[6], "_native_ui_setButtonColors"),
                                                    extractFloat(args[7], "_native_ui_setButtonColors"),
                                                    extractFloat(args[8], "_native_ui_setButtonColors"));
                buttonData.pressedColor = glm::vec4(extractFloat(args[9], "_native_ui_setButtonColors"),
                                                    extractFloat(args[10], "_native_ui_setButtonColors"),
                                                    extractFloat(args[11], "_native_ui_setButtonColors"),
                                                    extractFloat(args[12], "_native_ui_setButtonColors"));

                events::ui::SetUIButtonDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.buttonData = buttonData;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        vfLogInfo("[UIButtonLabelAPI] Registered Button/Label native functions");
    }
}
