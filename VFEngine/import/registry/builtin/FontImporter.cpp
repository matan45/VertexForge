#include "FontImporter.hpp"
#include <algorithm>
#include <stdexcept>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char otfSig[] = {0x4F, 0x54, 0x54, 0x4F};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }

        // Option keys are the FontImportConfig field names, so a .vfmeta sidecar
        // reads as documentation of the bake. Referenced by both options() and
        // configFromOptions() — never inline one of them.
        constexpr const char* kBaseFontSize = "baseFontSize";
        constexpr const char* kFieldMode = "fieldMode";
        constexpr const char* kIncludeKerning = "includeKerning";
        constexpr const char* kSdfSpread = "sdfSpread";
        constexpr const char* kSdfPadding = "sdfPadding";
        constexpr const char* kSdfOnEdgeValue = "sdfOnEdgeValue";
        constexpr const char* kMtsdfPxRange = "mtsdfPxRange";
        constexpr const char* kBasicLatin = "includeBasicLatin";
        constexpr const char* kLatin1 = "includeLatin1Supplement";
        constexpr const char* kLatinExtA = "includeLatinExtendedA";
        constexpr const char* kLatinExtB = "includeLatinExtendedB";
        constexpr const char* kGreek = "includeGreek";
        constexpr const char* kCyrillic = "includeCyrillic";
        constexpr const char* kEmoji = "includeEmoji";
        constexpr const char* kMiscSymbols = "includeMiscSymbols";
        constexpr const char* kDingbats = "includeDingbats";
        constexpr const char* kAtlasSize = "atlasSize";
        constexpr const char* kAtlasPadding = "atlasPadding";

        // Shared by the descriptor ranges and the defensive clamps in
        // configFromOptions: the dialog's sliders cannot produce an out-of-range
        // value, but a hand-edited .vfmeta or a plugin-supplied option can.
        constexpr int32_t minFontSize = 8;
        constexpr int32_t maxFontSize = 128;
        constexpr int32_t minSdfPadding = 0;
        constexpr int32_t maxSdfPadding = 16;
        constexpr float minSdfSpread = 1.0f;
        constexpr float maxSdfSpread = 16.0f;
        constexpr float minMtsdfPxRange = 1.0f;
        constexpr float maxMtsdfPxRange = 16.0f;
        constexpr int32_t minOnEdge = 0;
        constexpr int32_t maxOnEdge = 255;
        constexpr int32_t minAtlasPadding = 0;
        constexpr int32_t maxAtlasPadding = 8;
        constexpr int32_t atlasSizeCount = 4; // 512, 1024, 2048, 4096

        // 512 << index. Kept in lockstep with the atlasSize enumNames below.
        uint32_t atlasSizeForIndex(int32_t index)
        {
            const int32_t clamped = std::clamp(index, 0, atlasSizeCount - 1);
            return 512u << static_cast<uint32_t>(clamped);
        }

        int32_t atlasIndexForSize(uint32_t size)
        {
            for (int32_t i = 0; i < atlasSizeCount; ++i)
                if (atlasSizeForIndex(i) == size) return i;
            return 1; // 1024, the FontImportConfig default
        }

        // Descriptor factories. They take strongly typed defaults on purpose:
        // ImportOptionValue is variant<bool,int32_t,float,string>, so assigning an
        // untyped literal is a trap (`32u` is ambiguous between int32_t and float,
        // and a double like `4.0` is ill-formed — every alternative is narrowing).
        ImportOptionDesc boolOption(const char* key, const char* label, const char* tooltip, bool defaultValue)
        {
            ImportOptionDesc desc;
            desc.key = key;
            desc.label = label;
            desc.tooltip = tooltip;
            desc.type = ImportOptionDesc::Type::Bool;
            desc.defaultValue = defaultValue;
            return desc;
        }

        ImportOptionDesc intOption(const char* key, const char* label, const char* tooltip,
                                   int32_t defaultValue, int32_t minValue, int32_t maxValue)
        {
            ImportOptionDesc desc;
            desc.key = key;
            desc.label = label;
            desc.tooltip = tooltip;
            desc.type = ImportOptionDesc::Type::Int;
            desc.defaultValue = defaultValue;
            desc.minValue = static_cast<float>(minValue);
            desc.maxValue = static_cast<float>(maxValue);
            return desc;
        }

        ImportOptionDesc floatOption(const char* key, const char* label, const char* tooltip,
                                     float defaultValue, float minValue, float maxValue)
        {
            ImportOptionDesc desc;
            desc.key = key;
            desc.label = label;
            desc.tooltip = tooltip;
            desc.type = ImportOptionDesc::Type::Float;
            desc.defaultValue = defaultValue;
            desc.minValue = minValue;
            desc.maxValue = maxValue;
            return desc;
        }

        ImportOptionDesc enumOption(const char* key, const char* label, const char* tooltip,
                                    int32_t defaultIndex, std::vector<std::string> names)
        {
            ImportOptionDesc desc;
            desc.key = key;
            desc.label = label;
            desc.tooltip = tooltip;
            desc.type = ImportOptionDesc::Type::Enum;
            desc.defaultValue = defaultIndex;
            desc.enumNames = std::move(names);
            return desc;
        }

        // Every fallback is read off a default-constructed FontImportConfig rather
        // than hard-coded, so this function and the descriptor defaults above can
        // never drift from the struct — the empty-customOptions path (used to bake
        // the shipped default font) must stay byte-identical to the old behaviour.
        types::FontImportConfig configFromOptions(const importConfig::ImportConfig& config)
        {
            types::FontImportConfig cfg;

            cfg.baseFontSize = static_cast<uint32_t>(std::clamp(
                optionInt(config, kBaseFontSize, static_cast<int32_t>(cfg.baseFontSize)),
                minFontSize, maxFontSize));
            constexpr int32_t maxFieldMode =
                static_cast<int32_t>(types::FontFieldMode::MTSDF);
            cfg.fieldMode = static_cast<types::FontFieldMode>(std::clamp(
                optionInt(config, kFieldMode, static_cast<int32_t>(cfg.fieldMode)),
                0, maxFieldMode));
            cfg.includeKerning = optionBool(config, kIncludeKerning, cfg.includeKerning);

            cfg.sdfSpread = std::clamp(optionFloat(config, kSdfSpread, cfg.sdfSpread),
                                       minSdfSpread, maxSdfSpread);
            cfg.sdfPadding = static_cast<uint32_t>(std::clamp(
                optionInt(config, kSdfPadding, static_cast<int32_t>(cfg.sdfPadding)),
                minSdfPadding, maxSdfPadding));
            cfg.sdfOnEdgeValue = static_cast<uint8_t>(std::clamp(
                optionInt(config, kSdfOnEdgeValue, static_cast<int32_t>(cfg.sdfOnEdgeValue)),
                minOnEdge, maxOnEdge));
            cfg.mtsdfPxRange = std::clamp(
                optionFloat(config, kMtsdfPxRange, cfg.mtsdfPxRange),
                minMtsdfPxRange, maxMtsdfPxRange);

            cfg.includeBasicLatin = optionBool(config, kBasicLatin, cfg.includeBasicLatin);
            cfg.includeLatin1Supplement = optionBool(config, kLatin1, cfg.includeLatin1Supplement);
            cfg.includeLatinExtendedA = optionBool(config, kLatinExtA, cfg.includeLatinExtendedA);
            cfg.includeLatinExtendedB = optionBool(config, kLatinExtB, cfg.includeLatinExtendedB);
            cfg.includeGreek = optionBool(config, kGreek, cfg.includeGreek);
            cfg.includeCyrillic = optionBool(config, kCyrillic, cfg.includeCyrillic);
            cfg.includeEmoji = optionBool(config, kEmoji, cfg.includeEmoji);
            cfg.includeMiscSymbols = optionBool(config, kMiscSymbols, cfg.includeMiscSymbols);
            cfg.includeDingbats = optionBool(config, kDingbats, cfg.includeDingbats);

            // One square "Atlas Size" drives both extents; every consumer treats
            // them as a single budget (FontAtlasGenerator raises colour-font
            // atlases to >= 2048 on its own regardless).
            const uint32_t atlasSize = atlasSizeForIndex(
                optionInt(config, kAtlasSize, atlasIndexForSize(cfg.atlasWidth)));
            cfg.atlasWidth = atlasSize;
            cfg.atlasHeight = atlasSize;
            cfg.atlasPadding = static_cast<uint32_t>(std::clamp(
                optionInt(config, kAtlasPadding, static_cast<int32_t>(cfg.atlasPadding)),
                minAtlasPadding, maxAtlasPadding));

            return cfg;
        }

        bool isTTF(std::span<const unsigned char> header)
        {
            if (header.size() < 4) return false;

            // Standard TrueType: 00 01 00 00
            if (header[0] == 0x00 && header[1] == 0x01 &&
                header[2] == 0x00 && header[3] == 0x00)
                return true;

            // Apple TrueType: 'true' (74 72 75 65)
            if (header[0] == 0x74 && header[1] == 0x72 &&
                header[2] == 0x75 && header[3] == 0x65)
                return true;

            // TrueType Collection: 'ttcf' (74 74 63 66)
            if (header[0] == 0x74 && header[1] == 0x74 &&
                header[2] == 0x63 && header[3] == 0x66)
                return true;

            return false;
        }
    }

    std::vector<FormatInfo> FontImporter::formats() const
    {
        return {
            {"OTF", "Font Files", {"otf"}, FileExtension::font, resource::AssetType::Font, 100},
            {"TTF", "Font Files", {"ttf"}, FileExtension::font, resource::AssetType::Font, 15},
        };
    }

    bool FontImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "OTF") return matchesSignature(input.header, otfSig);
        if (fileType == "TTF") return isTTF(input.header);
        return false;
    }

    void FontImporter::process(pipeline::ImportContext& context)
    {
        const types::FontImportConfig config = configFromOptions(context.file.config);
        if (!fontProcessor.loadFromFile(context.file, context.fileName, context.location, config,
                                        wrapFileProgress<types::FontProgressCallback>(context)))
        {
            throw std::runtime_error("Failed to import font: " + std::string(context.fileName));
        }
    }

    std::vector<ImportOptionDesc> FontImporter::options() const
    {
        const types::FontImportConfig defaults;

        return {
            intOption(kBaseFontSize, "Base Font Size",
                      "Pixel size the glyphs are rasterised at. SDF text scales up from this, so a "
                      "larger size costs atlas space but stays sharper. Colour-emoji faces snap to "
                      "their nearest built-in strike instead.",
                      static_cast<int32_t>(defaults.baseFontSize), minFontSize, maxFontSize),
            enumOption(kFieldMode, "Field Mode",
                       "Choose native grayscale coverage, a single-channel signed distance field, "
                       "or a multi-channel signed distance field for sharper corners.",
                       static_cast<int32_t>(defaults.fieldMode),
                       {"Grayscale (raster)", "SDF (legacy)", "MTSDF"}),
            boolOption(kIncludeKerning, "Include Kerning",
                       "Extract kerning pairs from the face's legacy 'kern' table. Modern OpenType "
                       "fonts that only ship GPOS kerning yield an empty table.",
                       defaults.includeKerning),
            floatOption(kSdfSpread, "SDF Spread",
                        "SDF-only: distance in pixels the field ramps across the glyph edge. "
                        "Larger values give more effect range at the cost of precision.",
                        defaults.sdfSpread, minSdfSpread, maxSdfSpread),
            intOption(kSdfPadding, "SDF Padding",
                      "SDF-only: extra pixels rasterised around each glyph so the distance field "
                      "is not clipped. Should be at least the spread.",
                      static_cast<int32_t>(defaults.sdfPadding), minSdfPadding, maxSdfPadding),
            intOption(kSdfOnEdgeValue, "SDF Edge Value",
                      "SDF-only: the 0-255 value representing the glyph outline; the shader treats "
                      "this as the cut-off. 128 centres the field.",
                      static_cast<int32_t>(defaults.sdfOnEdgeValue), minOnEdge, maxOnEdge),
            floatOption(kMtsdfPxRange, "MTSDF Pixel Range",
                        "MTSDF-only: distance in atlas pixels represented by the field. Larger "
                        "values provide more effect range at the cost of edge precision.",
                        defaults.mtsdfPxRange, minMtsdfPxRange, maxMtsdfPxRange),

            boolOption(kBasicLatin, "Range: Basic Latin",
                       "U+0020-007E. ASCII: letters, digits, punctuation. Required for English text.",
                       defaults.includeBasicLatin),
            boolOption(kLatin1, "Range: Latin-1 Supplement",
                       "U+00A0-00FF. Accented Western European letters and common symbols.",
                       defaults.includeLatin1Supplement),
            boolOption(kLatinExtA, "Range: Latin Extended-A",
                       "U+0100-017F. Central/Eastern European Latin letters.",
                       defaults.includeLatinExtendedA),
            boolOption(kLatinExtB, "Range: Latin Extended-B",
                       "U+0180-024F. Further Latin letters, incl. African and phonetic forms.",
                       defaults.includeLatinExtendedB),
            boolOption(kGreek, "Range: Greek",
                       "U+0370-03FF. Greek and Coptic.", defaults.includeGreek),
            boolOption(kCyrillic, "Range: Cyrillic",
                       "U+0400-04FF. Russian and other Cyrillic scripts.", defaults.includeCyrillic),
            boolOption(kEmoji, "Range: Emoji",
                       "Emoticons, pictographs and supplemental symbols. Only useful on a "
                       "colour-emoji face; these ranges are enabled automatically for one.",
                       defaults.includeEmoji),
            boolOption(kMiscSymbols, "Range: Misc Symbols",
                       "U+2600-26FF. Weather, chess, zodiac and other misc symbols.",
                       defaults.includeMiscSymbols),
            boolOption(kDingbats, "Range: Dingbats",
                       "U+2700-27BF. Arrows, checkmarks, stars and ornaments.",
                       defaults.includeDingbats),

            enumOption(kAtlasSize, "Atlas Size",
                       "Square atlas budget in pixels. Raise it when the log reports glyphs that "
                       "did not fit; wide character ranges at a large base size need 2048 or more.",
                       atlasIndexForSize(defaults.atlasWidth), {"512", "1024", "2048", "4096"}),
            intOption(kAtlasPadding, "Atlas Padding",
                      "Pixels of gutter between packed glyphs. Prevents neighbouring glyphs from "
                      "bleeding into each other under bilinear filtering.",
                      static_cast<int32_t>(defaults.atlasPadding), minAtlasPadding, maxAtlasPadding),
        };
    }
}
