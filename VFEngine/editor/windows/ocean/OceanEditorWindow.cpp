#include "OceanEditorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "water/SeaState.hpp"
#include <imgui.h>

namespace windows
{
    void OceanEditorWindow::show()
    {
        visible = true;
        refreshOceanState();
    }

    void OceanEditorWindow::refreshOceanState()
    {
        hasOcean = false;
        oceanEntity = {};

        auto& dispatcher = events::EventDispatcher::instance();

        events::ocean::GetOceanEntityQuery entityQuery;
        oceanEntity = dispatcher.query(entityQuery);

        if (!oceanEntity.isValid())
            return;

        hasOcean = true;

        events::ocean::GetOceanDataQuery dataQuery;
        dataQuery.entity = oceanEntity;
        auto dataOpt = dispatcher.query(dataQuery);

        if (dataOpt.has_value())
        {
            visualSettings.shallowColor = dataOpt->shallowColor;
            visualSettings.deepColor = dataOpt->deepColor;
            visualSettings.maxVisibleDepth = dataOpt->maxVisibleDepth;
            visualSettings.fresnelPower = dataOpt->fresnelPower;
            visualSettings.refractionStrength = dataOpt->refractionStrength;
            visualSettings.refractionChromatic = dataOpt->refractionChromatic;
            visualSettings.refractionDepthScale = dataOpt->refractionDepthScale;
            visualSettings.causticStrength = dataOpt->causticStrength;
            visualSettings.causticDepthFalloff = dataOpt->causticDepthFalloff;
            visualSettings.shoreFoamRange = dataOpt->shoreFoamRange;
            visualSettings.shoreFoamIntensity = dataOpt->shoreFoamIntensity;
            visualSettings.shoreBreakingStrength = dataOpt->shoreBreakingStrength;
            visualSettings.shoreWetRange = dataOpt->shoreWetRange;
            visualSettings.shoreWetDarkening = dataOpt->shoreWetDarkening;
            visualSettings.shoreWetRoughness = dataOpt->shoreWetRoughness;
            // VK-1604
            visualSettings.ssrEnabled = dataOpt->ssrEnabled;
            visualSettings.ssrIntensity = dataOpt->ssrIntensity;
            visualSettings.ssrMaxDistance = dataOpt->ssrMaxDistance;
            visualSettings.ssrThickness = dataOpt->ssrThickness;
            visualSettings.ssrMaxSteps = dataOpt->ssrMaxSteps;
            visualSettings.ssrDebugView = dataOpt->ssrDebugView;
            visualSettings.beerLambertEnabled = dataOpt->beerLambertEnabled;
            visualSettings.absorptionCoeff = dataOpt->absorptionCoeff;
            visualSettings.scatteringColor = dataOpt->scatteringColor;
            visualSettings.scatterCoeff = dataOpt->scatterCoeff;
            visualSettings.absorptionMaxDistance = dataOpt->absorptionMaxDistance;
            visualSettings.hexTilingEnabled = dataOpt->hexTilingEnabled;
            visualSettings.hexBandMask = dataOpt->hexBandMask;
            visualSettings.hexCellScale = dataOpt->hexCellScale;
            visualSettings.hexBlendContrast = dataOpt->hexBlendContrast;
            // VK-1605
            visualSettings.shoalingEnabled = dataOpt->shoalingEnabled;
            visualSettings.shoalingStrength = dataOpt->shoalingStrength;
            visualSettings.shoalingMinDepth = dataOpt->shoalingMinDepth;
            visualSettings.shoalingWavelengthScale = dataOpt->shoalingWavelengthScale;
            visualSettings.shoalingGamma = dataOpt->shoalingGamma;
            visualSettings.shoreEdgeFadeStart = dataOpt->shoreEdgeFadeStart;
            visualSettings.shoreWavesEnabled = dataOpt->shoreWavesEnabled;
            visualSettings.shoreWaveAmplitude = dataOpt->shoreWaveAmplitude;
            visualSettings.shoreWaveLength = dataOpt->shoreWaveLength;
            visualSettings.shoreWaveSpeed = dataOpt->shoreWaveSpeed;
            visualSettings.shoreWaveBreakDepth = dataOpt->shoreWaveBreakDepth;
            visualSettings.shoreWaveBreakRange = dataOpt->shoreWaveBreakRange;
            visualSettings.shoreWaveCrestFoam = dataOpt->shoreWaveCrestFoam;
            visualSettings.shoreWaveCrestFoamThreshold = dataOpt->shoreWaveCrestFoamThreshold;
            visualSettings.shoreWaveLean = dataOpt->shoreWaveLean;
            visualSettingsDirty = false;

            physicsSettings.physicsEnabled = dataOpt->physicsEnabled;
            physicsSettingsDirty = false;

            weatherDriven = dataOpt->weatherDriven;
            weatherResponse = dataOpt->weatherResponse;
        }

        oceanConfig = dispatcher.query(events::ocean::GetOceanFFTConfigQuery{});
        oceanConfigDirty = false;
    }

    void OceanEditorWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Ocean Editor", &visible))
        {
            if (!hasOcean)
            {
                drawCreationSection();
            }
            else
            {
                drawSettingsSection();
                ImGui::Spacing();
                drawOceanFFTSection();
                ImGui::Spacing();
                drawInfoSection();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button("Delete Ocean", ImVec2(-1, 0)))
                {
                    deleteOcean();
                }
                ImGui::PopStyleColor(3);
            }
        }
        ImGui::End();
    }

    void OceanEditorWindow::drawCreationSection()
    {
        ImGui::Text("Create Ocean");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Water Height");
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##WaterHeight", &waterHeight, 0.1f, -1000.0f, 1000.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::Spacing();

        ImGui::Text("Shallow Color");
        ImGui::ColorEdit4("##ShallowColor", shallowColor);

        ImGui::Text("Deep Color");
        ImGui::ColorEdit4("##DeepColor", deepColor);

        ImGui::Spacing();

        ImGui::Checkbox("Physics Enabled", &physicsEnabled);
        ImGui::TextDisabled("Creates sensor bodies for water detection");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.2f, 0.5f, 1.0f));
        if (ImGui::Button("Create Ocean", ImVec2(-1, 30)))
        {
            createOcean();
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Or Load Existing");
        if (ImGui::Button("Load Ocean", ImVec2(-1, 30)))
        {
            loadOcean();
        }
    }

    static bool labeledDragFloat(const char* label, const char* id, float* v, float speed, float mn, float mx, const char* fmt)
    {
        ImGui::Text("%s", label);
        ImGui::PushItemWidth(-1);
        bool changed = ImGui::DragFloat(id, v, speed, mn, mx, fmt);
        ImGui::PopItemWidth();
        return changed;
    }

    void OceanEditorWindow::drawSettingsSection()
    {
        bool anyDirty = false;

        if (ImGui::CollapsingHeader("Visual Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Text("Shallow Color");
            visualSettingsDirty |= ImGui::ColorEdit4("##SettingsShallowColor", &visualSettings.shallowColor.x);
            ImGui::Text("Deep Color");
            visualSettingsDirty |= ImGui::ColorEdit4("##SettingsDeepColor", &visualSettings.deepColor.x);
            visualSettingsDirty |= labeledDragFloat("Max Visible Depth", "##MaxVisibleDepth", &visualSettings.maxVisibleDepth, 0.1f, 0.1f, 100.0f, "%.1f");
            visualSettingsDirty |= labeledDragFloat("Fresnel Power", "##FresnelPower", &visualSettings.fresnelPower, 0.1f, 0.1f, 20.0f, "%.1f");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Physics Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            physicsSettingsDirty |= ImGui::Checkbox("Enabled", &physicsSettings.physicsEnabled);
            physicsSettingsDirty |= labeledDragFloat("Density (kg/m3)", "##Density", &physicsSettings.density, 1.0f, 1.0f, 10000.0f, "%.0f");
            physicsSettingsDirty |= labeledDragFloat("Drag", "##Drag", &physicsSettings.drag, 0.01f, 0.0f, 10.0f, "%.2f");
            physicsSettingsDirty |= labeledDragFloat("Buoyancy Strength", "##BuoyancyStrength", &physicsSettings.buoyancyStrength, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            visualSettingsDirty |= labeledDragFloat("Strength", "##RefrStrength",
                &visualSettings.refractionStrength, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("0 = disabled, 0.5 = subtle, 1.0+ = strong");

            visualSettingsDirty |= labeledDragFloat("Chromatic Aberration", "##RefrChromatic",
                &visualSettings.refractionChromatic, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("0 = off, higher = more color fringing");

            visualSettingsDirty |= labeledDragFloat("Depth Scale", "##RefrDepthScale",
                &visualSettings.refractionDepthScale, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("How much depth increases distortion");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Caustics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            visualSettingsDirty |= labeledDragFloat("Strength", "##CausticStrength",
                &visualSettings.causticStrength, 0.01f, 0.0f, 3.0f, "%.2f");
            ImGui::TextDisabled("0 = disabled, 1.0 = default, higher = brighter");

            visualSettingsDirty |= labeledDragFloat("Depth Falloff", "##CausticDepthFalloff",
                &visualSettings.causticDepthFalloff, 0.01f, 0.1f, 2.0f, "%.2f");
            ImGui::TextDisabled("How quickly caustics fade with depth");
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Shoreline", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
            ImGui::Text("Shore Foam");
            visualSettingsDirty |= labeledDragFloat("Foam Range", "##ShoreFoamRange",
                &visualSettings.shoreFoamRange, 0.1f, 0.0f, 20.0f, "%.1f");
            ImGui::TextDisabled("Width of foam band at shore (world units)");

            visualSettingsDirty |= labeledDragFloat("Foam Intensity", "##ShoreFoamIntensity",
                &visualSettings.shoreFoamIntensity, 0.01f, 0.0f, 1.0f, "%.2f");

            visualSettingsDirty |= labeledDragFloat("Breaking Strength", "##ShoreBreakingStrength",
                &visualSettings.shoreBreakingStrength, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::TextDisabled("Extra foam on steep waves near shore");

            ImGui::Spacing();
            ImGui::Text("Wet Sand");
            visualSettingsDirty |= labeledDragFloat("Wet Range", "##ShoreWetRange",
                &visualSettings.shoreWetRange, 0.1f, 0.0f, 20.0f, "%.1f");
            ImGui::TextDisabled("How far above waterline terrain gets wet");

            visualSettingsDirty |= labeledDragFloat("Darkening", "##ShoreWetDarkening",
                &visualSettings.shoreWetDarkening, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("0 = black, 1 = no darkening");

            visualSettingsDirty |= labeledDragFloat("Roughness", "##ShoreWetRoughness",
                &visualSettings.shoreWetRoughness, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("Surface roughness at waterline");

            // VK-1605 — depth-driven shoreline. Everything below needs the shore-depth field.
            ImGui::Spacing();
            ImGui::Separator();
            drawShoreDepthFieldStatus();

            ImGui::Spacing();
            ImGui::Text("Shoaling");
            visualSettingsDirty |= ImGui::Checkbox("Enabled##ShoalingEnabled",
                                                   &visualSettings.shoalingEnabled);
            ImGui::TextDisabled("Waves feel the bottom: they rise, then the depth limit breaks them");

            if (visualSettings.shoalingEnabled)
            {
                visualSettingsDirty |= labeledDragFloat("Strength", "##ShoalingStrength",
                    &visualSettings.shoalingStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                ImGui::TextDisabled("0 = off, 1 = full effect");

                visualSettingsDirty |= labeledDragFloat("Wavelength Scale", "##ShoalingWavelength",
                    &visualSettings.shoalingWavelengthScale, 0.01f, 0.1f, 4.0f, "%.2f");
                ImGui::TextDisabled("Scales each band's wavelength; higher = shoals further out");

                visualSettingsDirty |= labeledDragFloat("Break Ratio", "##ShoalingGamma",
                    &visualSettings.shoalingGamma, 0.01f, 0.2f, 1.5f, "%.2f");
                ImGui::TextDisabled("Wave height / depth at which a wave breaks (0.78 = McCowan)");

                visualSettingsDirty |= labeledDragFloat("Min Depth", "##ShoalingMinDepth",
                    &visualSettings.shoalingMinDepth, 0.05f, 0.0f, 10.0f, "%.2f m");
                ImGui::TextDisabled("Depth at which waves are already fully flattened");

                visualSettingsDirty |= labeledDragFloat("Edge Fade", "##ShoreEdgeFade",
                    &visualSettings.shoreEdgeFadeStart, 0.01f, 0.0f, 0.99f, "%.2f");
                ImGui::TextDisabled("Where the depth-field window fades out; lower = smoother re-centre");
            }

            ImGui::Spacing();
            ImGui::Text("Breaking Waves");
            visualSettingsDirty |= ImGui::Checkbox("Enabled##ShoreWavesEnabled",
                                                   &visualSettings.shoreWavesEnabled);
            ImGui::TextDisabled("Travelling surf that follows the depth contours toward the beach");

            if (visualSettings.shoreWavesEnabled)
            {
                visualSettingsDirty |= labeledDragFloat("Amplitude", "##ShoreWaveAmplitude",
                    &visualSettings.shoreWaveAmplitude, 0.01f, 0.0f, 5.0f, "%.2f m");

                visualSettingsDirty |= labeledDragFloat("Spacing", "##ShoreWaveLength",
                    &visualSettings.shoreWaveLength, 0.1f, 0.5f, 60.0f, "%.1f m");
                ImGui::TextDisabled("Depth between successive crests");

                visualSettingsDirty |= labeledDragFloat("Speed", "##ShoreWaveSpeed",
                    &visualSettings.shoreWaveSpeed, 0.01f, 0.0f, 3.0f, "%.2f");
                ImGui::TextDisabled("Crests per second, travelling shoreward");

                visualSettingsDirty |= labeledDragFloat("Break Depth", "##ShoreWaveBreakDepth",
                    &visualSettings.shoreWaveBreakDepth, 0.05f, 0.1f, 20.0f, "%.2f m");
                ImGui::TextDisabled("Offshore edge of the surf zone");

                visualSettingsDirty |= labeledDragFloat("Break Range", "##ShoreWaveBreakRange",
                    &visualSettings.shoreWaveBreakRange, 0.05f, 0.05f, 20.0f, "%.2f m");
                ImGui::TextDisabled("Fade width at the waterline and at the deep edge");

                visualSettingsDirty |= labeledDragFloat("Crest Foam", "##ShoreWaveCrestFoam",
                    &visualSettings.shoreWaveCrestFoam, 0.01f, 0.0f, 2.0f, "%.2f");

                visualSettingsDirty |= labeledDragFloat("Foam Threshold", "##ShoreWaveCrestFoamThr",
                    &visualSettings.shoreWaveCrestFoamThreshold, 0.01f, 0.0f, 0.99f, "%.2f");

                visualSettingsDirty |= labeledDragFloat("Forward Lean", "##ShoreWaveLean",
                    &visualSettings.shoreWaveLean, 0.01f, 0.0f, 3.0f, "%.2f");
                ImGui::TextDisabled("Pushes crests along the depth gradient as they break");
            }
            ImGui::Unindent();
        }

        // VK-1604 — screen-space reflections
        if (ImGui::CollapsingHeader("Reflections (SSR)"))
        {
            ImGui::Indent();
            visualSettingsDirty |= ImGui::Checkbox("Enabled##SSREnabled", &visualSettings.ssrEnabled);
            ImGui::TextDisabled("Reflects scene geometry; the IBL cubemap fills misses and edges");

            if (visualSettings.ssrEnabled)
            {
                visualSettingsDirty |= labeledDragFloat("Intensity", "##SSRIntensity",
                    &visualSettings.ssrIntensity, 0.01f, 0.0f, 1.0f, "%.2f");

                visualSettingsDirty |= labeledDragFloat("Max Distance", "##SSRMaxDistance",
                    &visualSettings.ssrMaxDistance, 1.0f, 1.0f, 500.0f, "%.0f m");
                ImGui::TextDisabled("How far a reflection ray travels before giving up");

                visualSettingsDirty |= labeledDragFloat("Thickness", "##SSRThickness",
                    &visualSettings.ssrThickness, 0.01f, 0.01f, 5.0f, "%.2f m");
                ImGui::TextDisabled("Assumed depth of scene geometry; scaled up with distance");

                int steps = static_cast<int>(visualSettings.ssrMaxSteps);
                ImGui::Text("Max Steps");
                ImGui::PushItemWidth(-1);
                if (ImGui::SliderInt("##SSRMaxSteps", &steps, 4, 128))
                {
                    visualSettings.ssrMaxSteps = static_cast<uint32_t>(steps);
                    visualSettingsDirty = true;
                }
                ImGui::PopItemWidth();
                ImGui::TextDisabled("Higher = fewer missed reflections, more cost");

                ImGui::Spacing();
                visualSettingsDirty |= ImGui::Checkbox("Debug: show confidence", &visualSettings.ssrDebugView);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Renders the raw SSR confidence as greyscale instead of shading the water.\n"
                        "White = a confident hit, black = miss (IBL fallback). Use this to confirm\n"
                        "reflections land on the correct side of an off-centre object before tuning\n"
                        "anything else - a mirrored reflection still looks plausible on water.");
                }

                ImGui::TextDisabled("Not applied to render-texture or reflection-probe views");
            }
            ImGui::Unindent();
        }

        // VK-1604 — Beer-Lambert absorption
        if (ImGui::CollapsingHeader("Absorption (Beer-Lambert)"))
        {
            ImGui::Indent();
            visualSettingsDirty |= ImGui::Checkbox("Enabled##BeerLambertEnabled",
                                                   &visualSettings.beerLambertEnabled);
            ImGui::TextDisabled("Off = legacy height-based deep/shallow tint (unchanged)");

            if (visualSettings.beerLambertEnabled)
            {
                ImGui::Text("Absorption (1/m)");
                visualSettingsDirty |= ImGui::ColorEdit3("##AbsorptionCoeff",
                    &visualSettings.absorptionCoeff.x, ImGuiColorEditFlags_Float);
                ImGui::TextDisabled("Per-channel extinction; red extinguishes first in clear water");

                ImGui::Text("Scattering Color");
                visualSettingsDirty |= ImGui::ColorEdit3("##ScatteringColor",
                    &visualSettings.scatteringColor.x, ImGuiColorEditFlags_Float);

                visualSettingsDirty |= labeledDragFloat("Scatter Coefficient", "##ScatterCoeff",
                    &visualSettings.scatterCoeff, 0.005f, 0.0f, 1.0f, "%.3f");

                visualSettingsDirty |= labeledDragFloat("Max Path Length", "##AbsorptionMaxDistance",
                    &visualSettings.absorptionMaxDistance, 0.5f, 1.0f, 200.0f, "%.0f m");
                ImGui::TextDisabled("Clamp; keeps deep water from going fully black");
            }
            ImGui::Unindent();
        }

        // VK-1604 — hex tile-and-blend anti-tiling
        if (ImGui::CollapsingHeader("Anti-Tiling (Hex Blend)"))
        {
            ImGui::Indent();
            visualSettingsDirty |= ImGui::Checkbox("Enabled##HexEnabled", &visualSettings.hexTilingEnabled);
            ImGui::TextDisabled("Breaks up the far-field repeat of the FFT patches");

            if (visualSettings.hexTilingEnabled)
            {
                ImGui::Spacing();
                ImGui::Text("Bands");
                for (uint32_t band = 0; band < 3; ++band)
                {
                    const char* labels[] = {"Swell (band 0)##Hex0", "Agitation (band 1)##Hex1",
                                            "Ripples (band 2)##Hex2"};
                    bool bandOn = (visualSettings.hexBandMask & (1u << band)) != 0u;
                    if (ImGui::Checkbox(labels[band], &bandOn))
                    {
                        if (bandOn)
                            visualSettings.hexBandMask |= (1u << band);
                        else
                            visualSettings.hexBandMask &= ~(1u << band);
                        visualSettingsDirty = true;
                    }
                    if (band == 0 && ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip(
                            "The swell band carries most of the wave height, so tiling it costs the\n"
                            "most (3x samples per vertex AND per buoyancy query). CPU buoyancy\n"
                            "applies the same blend, so physics still matches the rendered surface.");
                    }
                }

                ImGui::Spacing();
                visualSettingsDirty |= labeledDragFloat("Cell Scale", "##HexCellScale",
                    &visualSettings.hexCellScale, 0.05f, 0.1f, 8.0f, "%.2f");
                ImGui::TextDisabled("Hex cells per band patch; higher = finer randomization");

                visualSettingsDirty |= labeledDragFloat("Blend Contrast", "##HexBlendContrast",
                    &visualSettings.hexBlendContrast, 0.1f, 1.0f, 16.0f, "%.1f");
                ImGui::TextDisabled("Weight sharpening; higher = harder cell transitions");
            }
            ImGui::Unindent();
        }

        anyDirty = visualSettingsDirty || physicsSettingsDirty;

        ImGui::Spacing();
        if (ImGui::Button("Apply", ImVec2(100, 0)))
        {
            applyVisualSettings();
            applyPhysicsSettings();
        }
        if (anyDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(80, 0)))
            refreshOceanState();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Save Ocean", ImVec2(-1, 0)))
            saveOcean();
    }

    // VK-1605: without this line "the shoreline isn't doing anything" is ambiguous between
    // no terrain in the scene, a bake still in flight, and a mis-tuned setting.
    void OceanEditorWindow::drawShoreDepthFieldStatus()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const auto status = dispatcher.query(events::ocean::GetShoreDepthFieldStatusQuery{});

        ImGui::Text("Shore Depth Field");
        if (!status.hasTerrain && !status.baked)
        {
            ImGui::TextDisabled("No terrain — water reads as bottomless, shoreline effects are inert");
            return;
        }

        if (status.baking)
        {
            ImGui::TextDisabled("Baking %.0f%%  (v%u)", status.progress * 100.0f, status.version);
        }
        else if (status.baked)
        {
            ImGui::TextDisabled("Baked v%u  |  %u^2 over %.0f m  |  centre %.0f, %.0f",
                                status.version, status.resolution, status.windowSize,
                                status.center.x, status.center.y);
        }
        else
        {
            ImGui::TextDisabled("Waiting for the first bake");
        }

        if (!status.hasTerrain)
            ImGui::TextDisabled("Terrain went away — the field will read bottomless after the next bake");
    }

    void OceanEditorWindow::drawOceanFFTSection()
    {
        if (!ImGui::CollapsingHeader("Ocean FFT", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Indent();

        // --- Sea state ---
        ImGui::Text("Sea State");
        ImGui::Indent();
        {
            auto& dispatcher = events::EventDispatcher::instance();

            if (ImGui::Checkbox("Weather Driven", &weatherDriven))
            {
                events::ocean::SetOceanWeatherDrivenCommand cmd;
                cmd.oceanEntity = oceanEntity;
                cmd.enabled = weatherDriven;
                cmd.response = weatherResponse;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Map the live weather wind/gusts onto the ocean bands every frame");

            if (weatherDriven)
            {
                if (labeledDragFloat("Response", "##WeatherResponse", &weatherResponse, 0.01f, 0.0f, 2.0f, "%.2f"))
                {
                    events::ocean::SetOceanWeatherDrivenCommand cmd;
                    cmd.oceanEntity = oceanEntity;
                    cmd.enabled = weatherDriven;
                    cmd.response = weatherResponse;
                    dispatcher.execute(cmd);
                }
            }

            events::ocean::GetOceanSeaStateQuery beaufortQuery;
            float currentBeaufort = dispatcher.query(beaufortQuery);
            ImGui::Text("Current: Beaufort %.1f (%.1f m/s wind)", currentBeaufort,
                        water::windSpeedFromBeaufort(currentBeaufort));

            static const char* presetNames = "Calm\0Slight\0Moderate\0Rough\0Very Rough\0Storm\0";
            ImGui::PushItemWidth(150.0f);
            ImGui::Combo("##SeaStatePreset", &seaStatePresetIndex, presetNames);
            ImGui::SameLine();
            ImGui::DragFloat("##SeaStateSeconds", &seaStateTransitionSeconds, 0.5f, 0.0f, 300.0f, "%.0f s");
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Set Sea State"))
            {
                events::ocean::SetOceanSeaStateCommand cmd;
                cmd.beaufort = water::beaufortForPreset(
                    static_cast<water::SeaStatePreset>(seaStatePresetIndex));
                cmd.transitionSeconds = seaStateTransitionSeconds;
                dispatcher.execute(cmd);
            }
        }
        ImGui::Unindent();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static const char* bandNames[] = {"Swell", "Agitation", "Ripples"};

        if (weatherDriven)
            ImGui::TextDisabled("Band wind/amplitude are driven by weather while Weather Driven is on");

        if (ImGui::BeginTabBar("OceanBands"))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ImGui::BeginTabItem(bandNames[i]))
                {
                    auto& band = oceanConfig.bands[i];

                    oceanConfigDirty |= ImGui::Checkbox("Enabled", &band.enabled);

                    if (band.enabled)
                    {
                        ImGui::Text("Resolution");
                        ImGui::PushItemWidth(-1);
                        static const uint32_t resolutions[] = {64, 128, 256, 512};
                        int resIndex = 2;
                        for (int r = 0; r < 4; ++r)
                        {
                            if (resolutions[r] == band.resolution)
                            {
                                resIndex = r;
                                break;
                            }
                        }
                        char resId[32];
                        snprintf(resId, sizeof(resId), "##Res%d", i);
                        if (ImGui::Combo(resId, &resIndex, "64\0" "128\0" "256\0" "512\0"))
                        {
                            band.resolution = resolutions[resIndex];
                            oceanConfigDirty = true;
                        }
                        ImGui::PopItemWidth();

                        char id[32];
                        snprintf(id, sizeof(id), "##PatchSize%d", i);
                        oceanConfigDirty |= labeledDragFloat("Patch Size", id, &band.patchSize, 1.0f, 10.0f, 2000.0f, "%.0f");

                        snprintf(id, sizeof(id), "##Amplitude%d", i);
                        oceanConfigDirty |= labeledDragFloat("Amplitude", id, &band.amplitude, 0.000001f, 0.00001f, 0.001f, "%.6f");

                        ImGui::Spacing();

                        snprintf(id, sizeof(id), "##WindSpeed%d", i);
                        oceanConfigDirty |= labeledDragFloat("Wind Speed (m/s)", id, &band.windSpeed, 0.01f, 0.1f, 100.0f, "%.2f");

                        snprintf(id, sizeof(id), "##WindDir%d", i);
                        ImGui::Text("Wind Direction");
                        ImGui::PushItemWidth(-1);
                        oceanConfigDirty |= ImGui::SliderFloat(id, &band.windDirection, 0.0f, 360.0f, "%.0f deg");
                        ImGui::PopItemWidth();

                        ImGui::Spacing();

                        snprintf(id, sizeof(id), "##Choppiness%d", i);
                        oceanConfigDirty |= labeledDragFloat("Choppiness", id, &band.choppiness, 0.01f, 0.0f, 5.0f, "%.2f");

                        snprintf(id, sizeof(id), "##FoamThreshold%d", i);
                        oceanConfigDirty |= labeledDragFloat("Foam Threshold", id, &band.foamThreshold, 0.01f, -1.0f, 2.0f, "%.2f");

                        snprintf(id, sizeof(id), "##DisplacementScale%d", i);
                        oceanConfigDirty |= labeledDragFloat("Wave Height Scale", id, &band.displacementScale, 0.1f, 0.1f, 50.0f, "%.1f");

                        ImGui::Spacing();

                        snprintf(id, sizeof(id), "##FoamPersistence%d", i);
                        oceanConfigDirty |= labeledDragFloat("Foam Persistence", id, &band.foamPersistence, 0.01f, 0.0f, 0.99f, "%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("How much advected previous-frame foam survives (0 = instantaneous foam only)");

                        snprintf(id, sizeof(id), "##FoamDecay%d", i);
                        oceanConfigDirty |= labeledDragFloat("Foam Decay (1/s)", id, &band.foamDecay, 0.01f, 0.01f, 5.0f, "%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Exponential fade rate of persistent foam trails");
                    }

                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        if (ImGui::Button("Apply Ocean", ImVec2(100, 0)))
            applyOceanConfig();
        if (oceanConfigDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::Unindent();
    }

    void OceanEditorWindow::drawInfoSection()
    {
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            events::ocean::GetOceanDataQuery query;
            query.entity = oceanEntity;
            auto data = dispatcher.query(query);

            if (data.has_value())
            {
                ImGui::Text("Water Height: %.2f", data->waterHeight);
                ImGui::Text("Physics: %s", data->physicsEnabled ? "Enabled" : "Disabled");
            }
            else
            {
                ImGui::TextDisabled("No ocean data available");
            }

            ImGui::Unindent();
        }
    }

    void OceanEditorWindow::createOcean()
    {
        services::OceanCreationData config;
        config.waterHeight = waterHeight;
        config.physicsEnabled = physicsEnabled;
        config.shallowColor = glm::vec4(shallowColor[0], shallowColor[1], shallowColor[2], shallowColor[3]);
        config.deepColor = glm::vec4(deepColor[0], deepColor[1], deepColor[2], deepColor[3]);
        config.oceanConfig = services::OceanFFTConfigData{};
        config.oceanConfig.enabled = true;

        events::ocean::CreateOceanCommand cmd;
        cmd.config = config;
        events::EventDispatcher::instance().execute(cmd);

        refreshOceanState();
    }

    void OceanEditorWindow::deleteOcean()
    {
        if (!hasOcean)
            return;

        events::ocean::DeleteOceanCommand cmd;
        cmd.oceanEntity = oceanEntity;
        events::EventDispatcher::instance().execute(cmd);

        hasOcean = false;
        oceanEntity = {};
    }

    void OceanEditorWindow::applyOceanConfig()
    {
        events::ocean::SetOceanFFTConfigCommand cmd;
        cmd.config = oceanConfig;
        events::EventDispatcher::instance().execute(cmd);
        oceanConfigDirty = false;
    }

    void OceanEditorWindow::applyVisualSettings()
    {
        if (!hasOcean || !visualSettingsDirty)
            return;

        events::ocean::SetOceanVisualSettingsCommand cmd;
        cmd.oceanEntity = oceanEntity;
        cmd.settings = visualSettings;
        events::EventDispatcher::instance().execute(cmd);
        visualSettingsDirty = false;
    }

    void OceanEditorWindow::applyPhysicsSettings()
    {
        if (!hasOcean || !physicsSettingsDirty)
            return;

        events::ocean::SetOceanPhysicsSettingsCommand cmd;
        cmd.oceanEntity = oceanEntity;
        cmd.settings = physicsSettings;
        events::EventDispatcher::instance().execute(cmd);
        physicsSettingsDirty = false;
    }

    void OceanEditorWindow::saveOcean()
    {
        if (!hasOcean)
            return;

        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Ocean (*.vfOcean)", L"*.vfOcean"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfOcean");
        if (!path.empty())
        {
            events::ocean::SaveOceanCommand cmd;
            cmd.oceanEntity = oceanEntity;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    void OceanEditorWindow::loadOcean()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Ocean (*.vfOcean)", L"*.vfOcean"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            events::ocean::LoadOceanCommand cmd;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);

            refreshOceanState();
        }
    }
}
