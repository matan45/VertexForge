#include "WaterToolSupport.hpp"

#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "water/SeaState.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mcp::tools::watertool
{
    namespace
    {
        // ------------------------------------------------------------------
        // Look fields that are not one plain member
        // ------------------------------------------------------------------

        // shallowColor / deepColor: the rgb of a vec4. The shallow alpha is 'opacity'; the deep alpha is never
        // read by the shader.
        template <glm::vec4 Visual::*Member>
        env::FieldDesc<Visual> rgbOfVec4Field(std::string_view key, std::string_view help)
        {
            env::FieldDesc<Visual> field;
            field.key = key;
            field.kind = env::FieldKind::Vec3;
            field.min = 0.0;
            field.max = 1.0;
            field.help = help;
            field.get = [](const Visual& value) -> nlohmann::json
            {
                const glm::vec4& color = value.*Member;
                return env::vec3Json(glm::vec3(color.x, color.y, color.z));
            };
            field.set = [](Visual& value, const nlohmann::json& v)
            {
                glm::vec4& color = value.*Member;
                color.x = static_cast<float>(v.at(0).get<double>());
                color.y = static_cast<float>(v.at(1).get<double>());
                color.z = static_cast<float>(v.at(2).get<double>());
            };
            return field;
        }

        env::FieldDesc<Visual> opacityField()
        {
            env::FieldDesc<Visual> field;
            field.key = "opacity";
            field.kind = env::FieldKind::Float;
            field.min = 0.0;
            field.max = 1.0;
            field.help = "How opaque the water is looking straight down (0-1, the shallow colour's alpha); grazing "
                         "views get more opaque with the fresnel term.";
            field.get = [](const Visual& value) -> nlohmann::json { return env::floatJson(value.shallowColor.w); };
            field.set = [](Visual& value, const nlohmann::json& v)
            {
                value.shallowColor.w = static_cast<float>(v.get<double>());
            };
            return field;
        }

        std::string checkBandResolution(const Band& band)
        {
            if (std::find(bandResolutions.begin(), bandResolutions.end(), band.resolution) != bandResolutions.end())
            {
                return {};
            }
            return "'resolution' must be 64, 128, 256 or 512 (got " + std::to_string(band.resolution) + ")";
        }

        float wrapDegrees(float degrees)
        {
            float wrapped = std::fmod(degrees, 360.0f);
            if (wrapped < 0.0f)
            {
                wrapped += 360.0f;
            }
            // A value just below 0 can round back up to exactly 360, and -0 must not be reported as "-0".
            return wrapped >= 360.0f || wrapped == 0.0f ? 0.0f : wrapped;
        }

        // The members of lookSections()[index], copied as values (an undo must restore the exact floats, which a
        // trip through the tables' JSON form would only guarantee up to the last bit). Keep each case in step
        // with its table; the unexposed members (deepColor alpha, maxVisibleDepth, ssrDebugView) stay.
        void copyLookSection(std::size_t index, Visual& target, const Visual& source)
        {
            switch (index)
            {
            case 0: // color
                target.shallowColor = source.shallowColor;
                target.deepColor = glm::vec4(source.deepColor.x, source.deepColor.y, source.deepColor.z, target.deepColor.w);
                target.fresnelPower = source.fresnelPower;
                return;
            case 1: // refraction
                target.refractionStrength = source.refractionStrength;
                target.refractionChromatic = source.refractionChromatic;
                target.refractionDepthScale = source.refractionDepthScale;
                return;
            case 2: // caustics
                target.causticStrength = source.causticStrength;
                target.causticDepthFalloff = source.causticDepthFalloff;
                return;
            case 3: // shoreFoam
                target.shoreFoamRange = source.shoreFoamRange;
                target.shoreFoamIntensity = source.shoreFoamIntensity;
                target.shoreBreakingStrength = source.shoreBreakingStrength;
                return;
            case 4: // shoreline
                target.shoreWetRange = source.shoreWetRange;
                target.shoalingEnabled = source.shoalingEnabled;
                target.shoalingStrength = source.shoalingStrength;
                target.shoalingWavelengthScale = source.shoalingWavelengthScale;
                target.shoalingGamma = source.shoalingGamma;
                target.shoalingMinDepth = source.shoalingMinDepth;
                target.shoreEdgeFadeStart = source.shoreEdgeFadeStart;
                target.shoreWavesEnabled = source.shoreWavesEnabled;
                target.shoreWaveAmplitude = source.shoreWaveAmplitude;
                target.shoreWaveLength = source.shoreWaveLength;
                target.shoreWaveSpeed = source.shoreWaveSpeed;
                target.shoreWaveBreakDepth = source.shoreWaveBreakDepth;
                target.shoreWaveBreakRange = source.shoreWaveBreakRange;
                target.shoreWaveCrestFoam = source.shoreWaveCrestFoam;
                target.shoreWaveCrestFoamThreshold = source.shoreWaveCrestFoamThreshold;
                target.shoreWaveLean = source.shoreWaveLean;
                return;
            case 5: // reflections
                target.ssrEnabled = source.ssrEnabled;
                target.ssrIntensity = source.ssrIntensity;
                target.ssrMaxDistance = source.ssrMaxDistance;
                target.ssrThickness = source.ssrThickness;
                target.ssrMaxSteps = source.ssrMaxSteps;
                return;
            case 6: // absorption
                target.beerLambertEnabled = source.beerLambertEnabled;
                target.absorptionCoeff = source.absorptionCoeff;
                target.scatteringColor = source.scatteringColor;
                target.scatterCoeff = source.scatterCoeff;
                target.absorptionMaxDistance = source.absorptionMaxDistance;
                return;
            case 7: // antiTiling
                target.hexTilingEnabled = source.hexTilingEnabled;
                target.hexBandMask = source.hexBandMask;
                target.hexCellScale = source.hexCellScale;
                target.hexBlendContrast = source.hexBlendContrast;
                return;
            case 8: // interactiveRipples
                target.rippleSimEnabled = source.rippleSimEnabled;
                target.ripplePatchSize = source.ripplePatchSize;
                target.rippleWaveSpeed = source.rippleWaveSpeed;
                target.rippleDamping = source.rippleDamping;
                target.rippleHeightScale = source.rippleHeightScale;
                target.rippleNormalScale = source.rippleNormalScale;
                target.rippleFoamGain = source.rippleFoamGain;
                target.rippleFoamScale = source.rippleFoamScale;
                target.rippleFoamDecay = source.rippleFoamDecay;
                target.rippleEdgeFadeStart = source.rippleEdgeFadeStart;
                return;
            default:
                throw std::logic_error("copyLookSection: no look section " + std::to_string(index));
            }
        }

        // Compared in the reported form: floatJson is exact for floats (distinct floats never share a value).
        template <typename T>
        bool sameFields(const env::SectionTable<T>& table, const T& a, const T& b)
        {
            for (const env::FieldDesc<T>& field : table.fields)
            {
                if (field.get(a) != field.get(b))
                {
                    return false;
                }
            }
            return true;
        }

        nlohmann::json numberRange(double min, double max)
        {
            return {{"type", "number"}, {"min", min}, {"max", max}};
        }

        nlohmann::json namesJson(std::span<const std::string_view> names)
        {
            nlohmann::json out = nlohmann::json::array();
            for (std::string_view name : names)
            {
                out.push_back(std::string(name));
            }
            return out;
        }
    }

    // ------------------------------------------------------------------
    // Tables
    // ------------------------------------------------------------------

    const std::vector<env::SectionTable<Visual>>& lookSections()
    {
        static const std::vector<env::SectionTable<Visual>> sections = [] {
            std::vector<env::SectionTable<Visual>> out;

            env::SectionTable<Visual> color;
            color.name = "color";
            color.fields = {
                rgbOfVec4Field<&Visual::shallowColor>(
                    "shallowColor", "Colour of the wave crests (rgb 0-1). Flat water (a lake without waves) shows the "
                                    "even mix of shallowColor and deepColor."),
                rgbOfVec4Field<&Visual::deepColor>("deepColor", "Colour of the wave troughs (rgb 0-1)."),
                opacityField(),
                env::floatField<Visual, &Visual::fresnelPower>(
                    "fresnelPower", 0.1, 20.0, "Fresnel exponent: higher keeps the sky reflection to grazing views.")
            };
            out.push_back(std::move(color));

            env::SectionTable<Visual> refraction;
            refraction.name = "refraction";
            refraction.fields = {
                env::floatField<Visual, &Visual::refractionStrength>(
                    "refractionStrength", 0.0, 2.0, "Distortion of what is seen through the water (0 = off)."),
                env::floatField<Visual, &Visual::refractionChromatic>(
                    "refractionChromatic", 0.0, 1.0, "Colour fringing of the refraction (0 = off)."),
                env::floatField<Visual, &Visual::refractionDepthScale>(
                    "refractionDepthScale", 0.0, 1.0, "How much deeper water distorts more.")
            };
            out.push_back(std::move(refraction));

            env::SectionTable<Visual> caustics;
            caustics.name = "caustics";
            caustics.fields = {
                env::floatField<Visual, &Visual::causticStrength>(
                    "causticStrength", 0.0, 3.0, "Brightness of the light caustics on the ground under the visible "
                                                 "ocean (0 = off)."),
                env::floatField<Visual, &Visual::causticDepthFalloff>(
                    "causticDepthFalloff", 0.1, 2.0, "How quickly caustics fade with depth.")
            };
            out.push_back(std::move(caustics));

            env::SectionTable<Visual> shoreFoam;
            shoreFoam.name = "shoreFoam";
            shoreFoam.fields = {
                env::floatField<Visual, &Visual::shoreFoamRange>(
                    "shoreFoamRange", 0.0, 20.0, "Width in metres of the foam band where water meets terrain or other "
                                                 "geometry (0 = off)."),
                env::floatField<Visual, &Visual::shoreFoamIntensity>(
                    "shoreFoamIntensity", 0.0, 1.0, "Strength of that foam."),
                env::floatField<Visual, &Visual::shoreBreakingStrength>(
                    "shoreBreakingStrength", 0.0, 2.0, "Extra foam on steep waves near the shore.")
            };
            out.push_back(std::move(shoreFoam));

            env::SectionTable<Visual> shoreline;
            shoreline.name = "shoreline";
            shoreline.fields = {
                env::floatField<Visual, &Visual::shoreWetRange>(
                    "shoreWetRange", 0.0, 20.0, "Metres above the waterline that terrain looks wet (visible ocean only)."),
                env::boolField<Visual, &Visual::shoalingEnabled>(
                    "shoalingEnabled", "Waves feel the bottom: they rise, then break at the depth limit."),
                env::floatField<Visual, &Visual::shoalingStrength>("shoalingStrength", 0.0, 1.0,
                                                                     "0 = off, 1 = the full effect."),
                env::floatField<Visual, &Visual::shoalingWavelengthScale>(
                    "shoalingWavelengthScale", 0.1, 4.0, "Scales each band's wavelength; higher = shoals further out."),
                env::floatField<Visual, &Visual::shoalingGamma>(
                    "shoalingGamma", 0.2, 1.5, "Wave height / depth at which a wave breaks (0.78 = McCowan)."),
                env::floatField<Visual, &Visual::shoalingMinDepth>(
                    "shoalingMinDepth", 0.0, 10.0, "Depth in metres at which waves are already fully flattened."),
                env::floatField<Visual, &Visual::shoreEdgeFadeStart>(
                    "shoreEdgeFadeStart", 0.0, 0.99, "Where the camera-following shore-depth window fades out."),
                env::boolField<Visual, &Visual::shoreWavesEnabled>(
                    "shoreWavesEnabled", "Breaking waves that travel toward the beach along the depth contours."),
                env::floatField<Visual, &Visual::shoreWaveAmplitude>("shoreWaveAmplitude", 0.0, 5.0,
                                                                       "Breaker height in metres (0 = off)."),
                env::floatField<Visual, &Visual::shoreWaveLength>("shoreWaveLength", 0.5, 60.0,
                                                                    "Metres of depth between successive crests."),
                env::floatField<Visual, &Visual::shoreWaveSpeed>("shoreWaveSpeed", 0.0, 3.0,
                                                                   "Crests per second travelling shoreward."),
                env::floatField<Visual, &Visual::shoreWaveBreakDepth>("shoreWaveBreakDepth", 0.1, 20.0,
                                                                        "Offshore edge of the surf zone, metres."),
                env::floatField<Visual, &Visual::shoreWaveBreakRange>(
                    "shoreWaveBreakRange", 0.05, 20.0, "Fade width at the waterline and at the deep edge, metres."),
                env::floatField<Visual, &Visual::shoreWaveCrestFoam>("shoreWaveCrestFoam", 0.0, 2.0,
                                                                       "Foam on the breaking crests."),
                env::floatField<Visual, &Visual::shoreWaveCrestFoamThreshold>(
                    "shoreWaveCrestFoamThreshold", 0.0, 0.99, "Crest height fraction where that foam starts."),
                env::floatField<Visual, &Visual::shoreWaveLean>("shoreWaveLean", 0.0, 3.0,
                                                                  "Forward lean of the crests as they break.")
            };
            out.push_back(std::move(shoreline));

            env::SectionTable<Visual> reflections;
            reflections.name = "reflections";
            reflections.fields = {
                env::boolField<Visual, &Visual::ssrEnabled>(
                    "ssrEnabled", "Screen-space reflections of scene geometry (the sky cubemap fills misses)."),
                env::floatField<Visual, &Visual::ssrIntensity>("ssrIntensity", 0.0, 1.0, "Reflection strength."),
                env::floatField<Visual, &Visual::ssrMaxDistance>("ssrMaxDistance", 1.0, 500.0,
                                                                   "How far a reflection ray travels, metres."),
                env::floatField<Visual, &Visual::ssrThickness>("ssrThickness", 0.01, 5.0,
                                                                 "Assumed thickness of scene geometry, metres."),
                env::uintField<Visual, &Visual::ssrMaxSteps>("ssrMaxSteps", 4, 128,
                                                               "Ray steps; more = fewer missed reflections, more cost.")
            };
            out.push_back(std::move(reflections));

            env::SectionTable<Visual> absorption;
            absorption.name = "absorption";
            absorption.fields = {
                env::boolField<Visual, &Visual::beerLambertEnabled>(
                    "beerLambertEnabled", "Physically based absorption and in-scattering through the water depth (needs "
                                          "refractionStrength > 0). Off = the crest/trough colour tint."),
                env::vec3Field<Visual, &Visual::absorptionCoeff>("absorptionCoeff", 0.0, 1.0,
                                                                   "Per-channel extinction per metre (rgb)."),
                env::vec3Field<Visual, &Visual::scatteringColor>("scatteringColor", 0.0, 1.0,
                                                                   "Colour scattered back by the water (rgb)."),
                env::floatField<Visual, &Visual::scatterCoeff>("scatterCoeff", 0.0, 1.0, "In-scattering per metre."),
                env::floatField<Visual, &Visual::absorptionMaxDistance>(
                    "absorptionMaxDistance", 1.0, 200.0, "Path length clamp in metres (keeps deep water from going black).")
            };
            out.push_back(std::move(absorption));

            env::SectionTable<Visual> antiTiling;
            antiTiling.name = "antiTiling";
            antiTiling.fields = {
                env::boolField<Visual, &Visual::hexTilingEnabled>(
                    "hexTilingEnabled", "Hex tile-and-blend: breaks up the far-field repeat of the FFT wave patches."),
                env::uintField<Visual, &Visual::hexBandMask>("hexBandMask", 0, 7,
                                                               "Bands that use it: bit 0 swell, 1 agitation, 2 ripples."),
                env::floatField<Visual, &Visual::hexCellScale>("hexCellScale", 0.1, 8.0, "Hex cells per band patch."),
                env::floatField<Visual, &Visual::hexBlendContrast>("hexBlendContrast", 1.0, 16.0,
                                                                     "Sharpness of the cell transitions.")
            };
            out.push_back(std::move(antiTiling));

            env::SectionTable<Visual> ripples;
            ripples.name = "interactiveRipples";
            ripples.fields = {
                env::boolField<Visual, &Visual::rippleSimEnabled>(
                    "rippleSimEnabled", "Camera-following wave simulation for wakes and splashes (works on flat water too)."),
                env::floatField<Visual, &Visual::ripplePatchSize>("ripplePatchSize", 20.0, 400.0,
                                                                    "Metres the simulated patch covers."),
                env::floatField<Visual, &Visual::rippleWaveSpeed>("rippleWaveSpeed", 0.1, 20.0, "Ripple speed, m/s."),
                env::floatField<Visual, &Visual::rippleDamping>("rippleDamping", 0.0, 10.0,
                                                                  "How fast ripples die out, per second."),
                env::floatField<Visual, &Visual::rippleHeightScale>("rippleHeightScale", 0.0, 10.0, "Ripple height."),
                env::floatField<Visual, &Visual::rippleNormalScale>("rippleNormalScale", 0.0, 10.0,
                                                                      "Ripple shading strength."),
                env::floatField<Visual, &Visual::rippleFoamGain>("rippleFoamGain", 0.0, 2.0,
                                                                   "Foam generated per unit of ripple curvature."),
                env::floatField<Visual, &Visual::rippleFoamScale>("rippleFoamScale", 0.0, 5.0, "Ripple foam brightness."),
                env::floatField<Visual, &Visual::rippleFoamDecay>("rippleFoamDecay", 0.0, 10.0,
                                                                    "Ripple foam decay per second."),
                env::floatField<Visual, &Visual::rippleEdgeFadeStart>("rippleEdgeFadeStart", 0.0, 0.99,
                                                                        "Where the patch border starts fading out.")
            };
            out.push_back(std::move(ripples));

            return out;
        }();
        return sections;
    }

    const env::SectionTable<Visual>* findLookSection(std::string_view key)
    {
        for (const env::SectionTable<Visual>& section : lookSections())
        {
            if (section.name == key)
            {
                return &section;
            }
        }
        return nullptr;
    }

    const env::SectionTable<Band>& bandTable()
    {
        static const env::SectionTable<Band> table = [] {
            env::SectionTable<Band> out;
            out.name = "band";
            out.fields = {
                env::uintField<Band, &Band::resolution>("resolution", 64, 512,
                                                          "FFT grid size: 64, 128, 256 or 512 (higher = finer, costs more)."),
                env::floatField<Band, &Band::patchSize>("patchSize", 10.0, 2000.0,
                                                          "Metres one wave patch spans before it repeats."),
                env::floatField<Band, &Band::windSpeed>("windSpeed", 0.1, 100.0, "Wind speed in m/s driving this band."),
                env::floatField<Band, &Band::windDirection>("windDirection", -maxBandWindDirection, maxBandWindDirection,
                                                              "Wind direction in degrees (any value; 360 apart is the "
                                                              "same direction)."),
                env::floatField<Band, &Band::amplitude>("amplitude", 0.0, 0.001,
                                                          "Spectrum amplitude (wave energy; about 1e-5 to 2e-4)."),
                env::floatField<Band, &Band::choppiness>("choppiness", 0.0, maxChoppiness,
                                                           "Horizontal displacement: sharper crests."),
                env::floatField<Band, &Band::foamThreshold>("foamThreshold", -1.0, 2.0,
                                                              "Foam onset; higher = more foam."),
                env::floatField<Band, &Band::displacementScale>("displacementScale", 0.1, 50.0, "Wave height scale."),
                env::boolField<Band, &Band::enabled>("enabled", "Run this band at all."),
                env::floatField<Band, &Band::foamPersistence>("foamPersistence", 0.0, 0.99,
                                                                "How much foam survives from frame to frame."),
                env::floatField<Band, &Band::foamDecay>("foamDecay", 0.01, 5.0, "Fade rate of persistent foam, per second.")
            };
            out.crossCheck = checkBandResolution;
            return out;
        }();
        return table;
    }

    const env::SectionTable<Physics>& physicsTable()
    {
        static const env::SectionTable<Physics> table = [] {
            env::SectionTable<Physics> out;
            out.name = "physics";
            out.fields = {
                env::boolField<Physics, &Physics::physicsEnabled>(
                    "physicsEnabled", "The ocean floats dynamic rigid bodies (lakes and pools have their own switch)."),
                env::floatField<Physics, &Physics::drag>(
                    "drag", 0.0, 10.0, "Water drag on submerged bodies - in EVERY water, lakes and pools included."),
                env::floatField<Physics, &Physics::buoyancyStrength>(
                    "buoyancyStrength", 0.0, 10.0, "Upward force multiplier - in EVERY water, lakes and pools included.")
            };
            return out;
        }();
        return table;
    }

    // ------------------------------------------------------------------
    // Names
    // ------------------------------------------------------------------

    int bandIndex(std::string_view name)
    {
        for (std::size_t i = 0; i < bandNames.size(); ++i)
        {
            if (env::detail::equalsIgnoreCase(name, bandNames[i]))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    uint32_t bandMaskFromNames(const nlohmann::json& names, std::string_view path)
    {
        if (!names.is_array())
        {
            throw ArgError("'" + std::string(path) + "' must be an array of band names (swell, agitation, ripples); got " +
                           env::detail::valueText(names));
        }
        uint32_t mask = 0;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            const nlohmann::json& name = names[i];
            const int index = name.is_string() ? bandIndex(name.get<std::string>()) : -1;
            if (index < 0)
            {
                throw ArgError("'" + std::string(path) + "[" + std::to_string(i) + "]' must be one of swell, agitation, "
                               "ripples (got " + env::detail::valueText(name) + ")");
            }
            mask |= 1u << static_cast<uint32_t>(index);
        }
        return mask;
    }

    nlohmann::json bandMaskNames(uint32_t mask)
    {
        nlohmann::json out = nlohmann::json::array();
        for (std::size_t i = 0; i < bandNames.size(); ++i)
        {
            if ((mask & (1u << i)) != 0u)
            {
                out.push_back(std::string(bandNames[i]));
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Ranges
    // ------------------------------------------------------------------

    double maxSeaStateWindSpeed()
    {
        return static_cast<double>(water::windSpeedFromBeaufort(static_cast<float>(maxBeaufort)));
    }

    nlohmann::json rangesJson()
    {
        nlohmann::json look = nlohmann::json::object();
        for (const env::SectionTable<Visual>& section : lookSections())
        {
            look[std::string(section.name)] = env::rangesJson(section);
        }

        nlohmann::json band = env::rangesJson(bandTable());
        band["resolution"] = {{"type", "integer"}, {"values", bandResolutions}};

        nlohmann::json bandNamesJson = namesJson(bandNames);
        return {
            {"look", std::move(look)},
            {"bands", std::move(band)},
            {"physics", env::rangesJson(physicsTable())},
            {"ocean", {
                {"level", numberRange(-maxLevel, maxLevel)},
                {"levelAboveTerrain", numberRange(-maxLevelAboveTerrain, maxLevelAboveTerrain)},
                {"weatherResponse", numberRange(0.0, maxWeatherResponse)},
                {"seaState", numberRange(0.0, maxBeaufort)},
                {"windSpeed", numberRange(0.0, maxSeaStateWindSpeed())},
                {"windDirection", numberRange(-maxWindDirection, maxWindDirection)},
                {"choppiness", numberRange(0.0, maxChoppiness)},
                {"bandNames", bandNamesJson}
            }},
            {"body", {
                {"center", numberRange(-ArgReader::maxGroundCoordinate, ArgReader::maxGroundCoordinate)},
                {"halfExtents", numberRange(0.0, maxHalfExtent)},
                {"level", numberRange(-maxLevel, maxLevel)},
                {"levelAboveTerrain", numberRange(-maxLevelAboveTerrain, maxLevelAboveTerrain)},
                {"depth", numberRange(0.0, maxBodyDepth)},
                {"type", {{"type", "enum"}, {"values", namesJson(bodyTypeNames)}}},
                {"waves", {{"type", "array"}, {"values", bandNamesJson}}},
                {"terrainReference", {{"type", "enum"}, {"values", namesJson(terrainReferenceNames)}}}
            }}
        };
    }

    // ------------------------------------------------------------------
    // Ocean state edits
    // ------------------------------------------------------------------

    void applySeaState(services::OceanStateData& state, float beaufort)
    {
        const float clamped = std::clamp(beaufort, 0.0f, static_cast<float>(maxBeaufort));
        const water::SeaState sea = water::seaStateFromBeaufort(clamped, state.bands[0].windDirection);
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS && i < water::SEA_STATE_BANDS; ++i)
        {
            Band& band = state.bands[i];
            band.windSpeed = sea.bands[i].windSpeed;
            band.windDirection = sea.bands[i].windDirection;
            band.amplitude = sea.bands[i].amplitude;
            band.choppiness = sea.bands[i].choppiness;
            band.displacementScale = sea.bands[i].displacementScale;
            band.foamThreshold = sea.bands[i].foamThreshold;
        }
        state.currentBeaufort = clamped;
    }

    void rotateWind(services::OceanStateData& state, float directionDeg)
    {
        const float swell = state.bands[0].windDirection;
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            const float offset = state.bands[i].windDirection - swell;
            state.bands[i].windDirection = wrapDegrees(directionDeg + offset);
        }
    }

    void setChoppiness(services::OceanStateData& state, float choppiness)
    {
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            state.bands[i].choppiness = choppiness;
        }
    }

    bool weatherOwnedChanged(const services::OceanStateData& a, const services::OceanStateData& b)
    {
        if (a.currentBeaufort != b.currentBeaufort)
        {
            return true;
        }
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            const Band& x = a.bands[i];
            const Band& y = b.bands[i];
            if (x.windSpeed != y.windSpeed || x.windDirection != y.windDirection || x.amplitude != y.amplitude ||
                x.choppiness != y.choppiness || x.displacementScale != y.displacementScale ||
                x.foamThreshold != y.foamThreshold)
            {
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // Parts
    // ------------------------------------------------------------------

    OceanParts changedParts(const services::OceanStateData& a, const services::OceanStateData& b)
    {
        OceanParts parts = 0;
        if (a.waterHeight != b.waterHeight)
        {
            parts |= partLevel;
        }
        if (a.isActive != b.isActive)
        {
            parts |= partVisible;
        }
        if (!sameFields(physicsTable(), a.physics, b.physics))
        {
            parts |= partPhysics;
        }
        if (a.weatherDriven != b.weatherDriven || a.weatherResponse != b.weatherResponse)
        {
            parts |= partWeather;
        }
        if (a.currentBeaufort != b.currentBeaufort)
        {
            parts |= partSeaState;
        }
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            if (!(a.bands[i] == b.bands[i]))
            {
                parts |= partBand0 << i;
            }
        }
        const std::vector<env::SectionTable<Visual>>& sections = lookSections();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            if (!sameFields(sections[i], a.visual, b.visual))
            {
                parts |= partLook0 << i;
            }
        }
        return parts;
    }

    void mergeParts(services::OceanStateData& target, const services::OceanStateData& source, OceanParts parts)
    {
        if ((parts & partLevel) != 0u)
        {
            target.waterHeight = source.waterHeight;
        }
        if ((parts & partVisible) != 0u)
        {
            target.isActive = source.isActive;
        }
        if ((parts & partPhysics) != 0u)
        {
            // The physicsTable fields; density (never exposed) stays.
            target.physics.physicsEnabled = source.physics.physicsEnabled;
            target.physics.drag = source.physics.drag;
            target.physics.buoyancyStrength = source.physics.buoyancyStrength;
        }
        if ((parts & partWeather) != 0u)
        {
            target.weatherDriven = source.weatherDriven;
            target.weatherResponse = source.weatherResponse;
        }
        if ((parts & partSeaState) != 0u)
        {
            target.currentBeaufort = source.currentBeaufort;
        }
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            if ((parts & (partBand0 << i)) != 0u)
            {
                target.bands[i] = source.bands[i];
            }
        }
        const std::size_t sectionCount = lookSections().size();
        for (std::size_t i = 0; i < sectionCount; ++i)
        {
            if ((parts & (partLook0 << i)) != 0u)
            {
                copyLookSection(i, target.visual, source.visual);
            }
        }
    }

    std::string partsText(OceanParts parts)
    {
        std::vector<std::string> names;
        if ((parts & partLevel) != 0u) names.emplace_back("level");
        if ((parts & partVisible) != 0u) names.emplace_back("visibility");
        if ((parts & partPhysics) != 0u) names.emplace_back("physics");
        if ((parts & partWeather) != 0u) names.emplace_back("weather");
        if ((parts & partSeaState) != 0u) names.emplace_back("sea state");
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            if ((parts & (partBand0 << i)) != 0u)
            {
                names.push_back(std::string(bandNames[i]) + " band");
            }
        }
        const std::vector<env::SectionTable<Visual>>& sections = lookSections();
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            if ((parts & (partLook0 << i)) != 0u)
            {
                names.emplace_back(sections[i].name);
            }
        }
        return env::detail::joined(names);
    }

    // ------------------------------------------------------------------
    // Terrain footprints
    // ------------------------------------------------------------------

    Footprint footprintGrid(const glm::vec2& center, const glm::vec2& halfExtents, float spacing)
    {
        constexpr uint32_t maxSamples = 32;
        const float step = spacing > 0.0f ? spacing : 1.0f;
        const auto samplesAlong = [&](float halfExtent) -> uint32_t
        {
            if (!(halfExtent > 0.0f))
            {
                return 1;
            }
            const double wanted = std::ceil(2.0 * static_cast<double>(halfExtent) / static_cast<double>(step)) + 1.0;
            return static_cast<uint32_t>(std::clamp(wanted, 2.0, static_cast<double>(maxSamples)));
        };

        Footprint footprint;
        footprint.samplesX = samplesAlong(halfExtents.x);
        footprint.samplesZ = samplesAlong(halfExtents.y);
        footprint.points.reserve(static_cast<std::size_t>(footprint.samplesX) * footprint.samplesZ + 1);

        const glm::vec2 lower = center - glm::max(halfExtents, glm::vec2(0.0f));
        const glm::vec2 size = 2.0f * glm::max(halfExtents, glm::vec2(0.0f));
        for (uint32_t iz = 0; iz < footprint.samplesZ; ++iz)
        {
            const float tz = footprint.samplesZ > 1 ? static_cast<float>(iz) / static_cast<float>(footprint.samplesZ - 1)
                                                     : 0.5f;
            for (uint32_t ix = 0; ix < footprint.samplesX; ++ix)
            {
                const float tx = footprint.samplesX > 1
                    ? static_cast<float>(ix) / static_cast<float>(footprint.samplesX - 1)
                    : 0.5f;
                footprint.points.emplace_back(lower.x + size.x * tx, lower.y + size.y * tz);
            }
        }
        footprint.points.push_back(center);
        return footprint;
    }

    FootprintStats footprintStats(const Footprint& footprint, std::span<const services::TerrainHeightSample> heights)
    {
        FootprintStats stats;
        const std::size_t gridCount = static_cast<std::size_t>(footprint.samplesX) * footprint.samplesZ;
        if (heights.size() != footprint.points.size() || footprint.points.size() != gridCount + 1)
        {
            throw std::logic_error("footprintStats: the heights do not answer the footprint's points");
        }

        stats.total = gridCount;
        bool any = false;
        const auto include = [&stats, &any](float height)
        {
            stats.min = any ? std::min(stats.min, height) : height;
            stats.max = any ? std::max(stats.max, height) : height;
            any = true;
        };
        for (std::size_t i = 0; i < gridCount; ++i)
        {
            const services::TerrainHeightSample& sample = heights[i];
            if (!sample.valid)
            {
                continue;
            }
            ++stats.valid;
            include(sample.height);

            const std::size_t ix = i % footprint.samplesX;
            const std::size_t iz = i / footprint.samplesX;
            const bool onRim = ix == 0 || iz == 0 || ix + 1 == footprint.samplesX || iz + 1 == footprint.samplesZ;
            if (onRim)
            {
                stats.rimMin = stats.rimMin.has_value() ? std::min(*stats.rimMin, sample.height) : sample.height;
            }
        }
        // The exact centre is a grid point only when both sample counts are odd; a basin sculpted with one
        // dab is deepest right there, so "lowest" must see it either way.
        const services::TerrainHeightSample& center = heights[gridCount];
        if (center.valid)
        {
            stats.center = center.height;
            include(center.height);
        }
        return stats;
    }

    std::optional<float> referenceHeight(const FootprintStats& stats, TerrainReference reference)
    {
        switch (reference)
        {
        case TerrainReference::Lowest:
            return stats.anyHeight() ? std::optional<float>(stats.min) : std::nullopt;
        case TerrainReference::Center:
            return stats.center;
        case TerrainReference::Rim:
            return stats.rimMin;
        }
        return std::nullopt;
    }

    nlohmann::json footprintJson(const FootprintStats& stats)
    {
        const auto height = [](const std::optional<float>& value)
        {
            return value.has_value() ? env::floatJson(*value) : nlohmann::json(nullptr);
        };
        const bool any = stats.anyHeight();
        return {
            {"samples", stats.total},
            {"valid", stats.valid},
            {"min", any ? env::floatJson(stats.min) : nlohmann::json(nullptr)},
            {"max", any ? env::floatJson(stats.max) : nlohmann::json(nullptr)},
            {"center", height(stats.center)},
            {"rimMin", height(stats.rimMin)}
        };
    }

    // ------------------------------------------------------------------
    // Creation
    // ------------------------------------------------------------------

    services::OceanCreationData creationDefaults(float level)
    {
        services::OceanCreationData config;
        config.waterHeight = level;
        config.oceanConfig = services::OceanFFTConfigData{};
        config.oceanConfig.enabled = true;
        return config;
    }

    services::OceanCreationData creationFromState(const services::OceanStateData& state)
    {
        services::OceanCreationData config;
        config.waterHeight = state.waterHeight;
        config.physicsEnabled = state.physics.physicsEnabled;
        config.shallowColor = state.visual.shallowColor;
        config.deepColor = state.visual.deepColor;
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            config.oceanConfig.bands[i] = state.bands[i];
        }
        config.oceanConfig.gravity = state.gravity;
        config.oceanConfig.enabled = true;
        return config;
    }

    // ------------------------------------------------------------------
    // Water bodies
    // ------------------------------------------------------------------

    bool sameBodyData(const services::WaterBodyComponentData& a, const services::WaterBodyComponentData& b)
    {
        return a.type == b.type && a.waterHeight == b.waterHeight && a.halfExtents == b.halfExtents &&
            a.depth == b.depth && a.bandMask == b.bandMask && a.physicsEnabled == b.physicsEnabled &&
            a.isActive == b.isActive;
    }

    float bodyLevel(const services::TransformData& transform, const services::WaterBodyComponentData& data)
    {
        // The same float sum OceanService::collectWaterBodies computes for the surface.
        return transform.position.y + data.waterHeight;
    }

    // ------------------------------------------------------------------
    // Reports
    // ------------------------------------------------------------------

    nlohmann::json lookJson(const Visual& visual)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const env::SectionTable<Visual>& section : lookSections())
        {
            out[std::string(section.name)] = env::toJson(section, visual);
        }
        return out;
    }

    nlohmann::json oceanParamsJson(const services::OceanStateData& state)
    {
        nlohmann::json bands = nlohmann::json::object();
        for (uint32_t i = 0; i < services::MAX_OCEAN_BANDS; ++i)
        {
            bands[std::string(bandNames[i])] = env::toJson(bandTable(), state.bands[i]);
        }
        return {
            {"level", env::floatJson(state.waterHeight)},
            {"weatherDriven", state.weatherDriven},
            {"weatherResponse", env::floatJson(state.weatherResponse)},
            {"bands", std::move(bands)},
            {"physics", env::toJson(physicsTable(), state.physics)}
        };
    }

    nlohmann::json oceanJson(const services::EntityHandle& ocean, const std::string& name,
                             const services::OceanStateData& state)
    {
        return {
            {"entity", static_cast<uint32_t>(ocean.id)},
            {"name", name},
            {"visible", state.isActive},
            {"seaState", env::floatJson(state.currentBeaufort)},
            {"params", oceanParamsJson(state)}
        };
    }

    nlohmann::json bodyJson(const services::EntityHandle& entity, const std::string& name,
                            const services::TransformData& transform, const services::WaterBodyComponentData& data)
    {
        const std::size_t type = data.type < bodyTypeNames.size() ? data.type : 0;
        return {
            {"entity", static_cast<uint32_t>(entity.id)},
            {"name", name},
            {"type", std::string(bodyTypeNames[type])},
            {"center", nlohmann::json::array({env::floatJson(transform.position.x), env::floatJson(transform.position.z)})},
            {"halfExtents", nlohmann::json::array({env::floatJson(data.halfExtents.x), env::floatJson(data.halfExtents.y)})},
            {"level", env::floatJson(bodyLevel(transform, data))},
            {"depth", env::floatJson(data.depth)},
            {"waves", bandMaskNames(data.bandMask)},
            {"physicsEnabled", data.physicsEnabled},
            {"active", data.isActive}
        };
    }

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    services::EntityHandle trackedOcean()
    {
        try
        {
            return events::EventDispatcher::instance().query(events::ocean::GetOceanEntityQuery{});
        }
        catch (const std::runtime_error&)
        {
            return {};  // no ocean service
        }
    }

    std::optional<services::OceanStateData> readOceanState(const services::EntityHandle& ocean)
    {
        events::ocean::GetOceanStateQuery query;
        query.entity = ocean;
        return events::EventDispatcher::instance().query(query);
    }

    void writeOceanState(const services::EntityHandle& ocean, const services::OceanStateData& state)
    {
        events::ocean::SetOceanStateCommand command;
        command.entity = ocean;
        command.state = state;
        if (!events::EventDispatcher::instance().execute(command))
        {
            throw std::runtime_error("the engine refused the ocean update: entity " + std::to_string(ocean.id) +
                                     " is not the scene's ocean");
        }
    }

    OceanExtras oceanExtras(const services::EntityHandle& ocean)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        OceanExtras extras;

        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = ocean;
        const std::optional<services::EntityData> data = dispatcher.query(entityQuery);
        if (!data.has_value())
        {
            return extras;   // gone: nothing left to lose
        }
        extras.children = data->children.size();

        // The prefab snapshot's "components" holds every optional component it can serialize - and never the
        // ocean itself (PrefabSerialization writes no ocean key), so for a plain Ocean entity it is empty.
        events::scene::CopyEntityToJsonQuery copy;
        copy.entity = ocean;
        const nlohmann::json snapshot = nlohmann::json::parse(dispatcher.query(copy), nullptr, false);
        if (snapshot.is_object())
        {
            const auto components = snapshot.find("components");
            if (components != snapshot.end() && components->is_object())
            {
                for (auto it = components->begin(); it != components->end(); ++it)
                {
                    extras.components.push_back(it.key());
                }
            }
        }
        return extras;
    }

    std::vector<services::TerrainHeightSample> sampleTerrainHeights(const services::EntityHandle& terrain,
                                                                    const std::vector<glm::vec2>& points)
    {
        events::terrainAuthoring::GetTerrainHeightsQuery query;
        query.terrainEntity = terrain;
        query.positions = points;
        query.pageIn = true;
        std::vector<services::TerrainHeightSample> heights = events::EventDispatcher::instance().query(query);
        if (heights.size() != points.size())
        {
            throw std::runtime_error("The terrain height query returned " + std::to_string(heights.size()) +
                                     " samples for " + std::to_string(points.size()) + " positions");
        }
        return heights;
    }
}
