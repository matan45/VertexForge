#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Per-field inspector metadata for plugin-registered native components.
//
// Header-only and engine-link-free: it ships verbatim into the SDK so plugin
// authors can build against it. The attribute struct is a POD with FIXED char
// arrays (never std::string / const char*) on purpose: the engine stores a
// deep value copy of every FieldAttributes in a side-map that outlives the
// plugin DLL's string storage. A raw pointer would dangle the moment the plugin
// unloads (the same lesson as the MetaComponentBridge `name` deep-copy in
// PluginContext.hpp). Copying the whole struct by value keeps all string data
// owned engine-side.

namespace plugin::inspector {

    // How a field should be drawn in the auto-generated inspector. `Auto` lets
    // the drawer pick a sensible default based on the field's C++ type.
    enum class Widget : uint8_t {
        Auto,
        Slider,
        Drag,
        Color,
        MultilineText,
        ReadOnly,
        Hidden
    };

    // Trivially-copyable POD. Stored by value engine-side; nothing dangles on
    // plugin unload because every string is an inline fixed array.
    struct FieldAttributes {
        char  label[64] = {};       // inspector display name (empty => raw field name)
        char  tooltip[256] = {};    // hover help text
        char  group[64] = {};       // collapsing-header group (empty => ungrouped)
        char  units[16] = {};       // suffix appended to the label (e.g. "m", "deg")
        char  assetFilter[32] = {}; // extension filter for AssetRef pickers (e.g. ".vfimage")
        float vmin = 0.f;
        float vmax = 0.f;
        float step = 0.f;
        Widget widget = Widget::Auto;
        bool  hasRange = false;
    };

    // Fluent builder for FieldAttributes. Chain the setters and call build().
    //
    //   ctx->setFieldAttributes("Health", "maxHP",
    //       inspector::Field{}.name("Max HP").range(0.f, 1000.f).slider()
    //                         .units("hp").help("Maximum hit points").build());
    struct Field {
        FieldAttributes a{};

        // Bounded copy into a fixed char array, always null-terminated.
        template <std::size_t N>
        static void copyInto(char (&dst)[N], const char* src) {
            if (!src) { dst[0] = '\0'; return; }
            std::snprintf(dst, N, "%s", src);
        }

        Field& name(const char* v)  { copyInto(a.label, v);   return *this; }
        Field& help(const char* v)  { copyInto(a.tooltip, v); return *this; }
        Field& group(const char* v) { copyInto(a.group, v);   return *this; }
        Field& units(const char* v) { copyInto(a.units, v);   return *this; }

        // Sets vmin/vmax/step and flags the field as ranged.
        Field& range(float lo, float hi, float step = 0.f) {
            a.vmin = lo;
            a.vmax = hi;
            a.step = step;
            a.hasRange = true;
            return *this;
        }

        Field& slider()    { a.widget = Widget::Slider;        return *this; }
        Field& color()     { a.widget = Widget::Color;         return *this; }
        Field& multiline() { a.widget = Widget::MultilineText; return *this; }
        Field& readOnly()  { a.widget = Widget::ReadOnly;      return *this; }
        Field& hidden()    { a.widget = Widget::Hidden;        return *this; }

        // Restrict an AssetRef field's drag-drop picker to a single extension.
        Field& asset(const char* ext) { copyInto(a.assetFilter, ext); return *this; }

        FieldAttributes build() const { return a; }
    };

}
