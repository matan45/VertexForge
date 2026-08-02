#include "TerrainMaterialEditorWindow.hpp"
#include "../../graph/ShaderGraphCompiler.hpp"
#include <terrain/TerrainMaterialAsset.hpp>
#include <resource/ResourceManager.hpp>
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <format>
#include <cstring>

namespace windows
{
    TerrainMaterialEditorWindow::TerrainMaterialEditorWindow(const std::string& materialPath)
        : materialPath(materialPath)
    {
        std::filesystem::path path(materialPath);
        windowTitle = "Terrain Material: " + path.filename().string();
    }

    void TerrainMaterialEditorWindow::loadMaterial()
    {
        materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));

        if (!materialData)
        {
            vfLogInfo("Creating new terrain material: {}", materialPath);
            std::filesystem::path path(materialPath);
            auto newMaterial = terrain::TerrainMaterialAsset::createDefault(path.stem().string());
            materialData = std::make_shared<terrain::TerrainMaterialData>(std::move(newMaterial));
        }
    }

    void TerrainMaterialEditorWindow::saveMaterial()
    {
        if (!materialData) return;

        compileMaterial();

        if (terrain::TerrainMaterialAsset::save(materialPath, *materialData))
        {
            isDirty = false;
            vfLogInfo("Terrain material saved: {}", materialPath);
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = materialPath;
            events::EventDispatcher::instance().publish(assetNotif);
        }
        else
        {
            vfLogError("Failed to save terrain material: {}", materialPath);
        }
    }

    void TerrainMaterialEditorWindow::removeLayer(int removeIndex, int currentCount)
    {
        for (int i = removeIndex; i < currentCount - 1; ++i)
        {
            materialData->layers[i] = materialData->layers[i + 1];
        }
        materialData->layers[currentCount - 1] = terrain::TerrainMaterialLayer{};
        materialData->activeLayerCount = static_cast<uint8_t>(currentCount - 1);
    }

    void TerrainMaterialEditorWindow::compileMaterial()
    {
        if (!materialData) return;

        auto result = editor::graph::ShaderGraphCompiler::compileTerrainMaterial(*materialData);

        if (result.success)
        {
            materialData->cachedMaterialSnippet = result.materialSnippet;
            materialData->needsRecompile = false;
            showCompileError = false;
            vfLogInfo("Terrain material compiled successfully: {}", materialData->name);

            std::filesystem::path snippetPath = "../../resources/shaders/material/terrain_material_generated.glsl";
            std::ofstream file(snippetPath);
            if (file.is_open())
            {
                file << "// Generated terrain material shader code\n";
                file << result.materialSnippet;
                file.close();
                vfLogInfo("Terrain material shader written to: {}", snippetPath.string());
            }
            else
            {
                vfLogError("Failed to write terrain material shader file");
            }

            events::EventDispatcher::instance().publish(
                events::terrain::TerrainMaterialCompiledNotification{});
        }
        else
        {
            showCompileError = true;
            compileErrorMessage = result.errorMessage;
            vfLogError("Terrain material compilation failed: {}", result.errorMessage);
        }
    }

    void TerrainMaterialEditorWindow::onChanged()
    {
        isDirty = true;
        materialData->needsRecompile = true;
    }

    void TerrainMaterialEditorWindow::draw()
    {
        if (!isOpen) return;

        if (!materialData)
        {
            loadMaterial();
        }

        ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ");

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();
                drawAntiTilingProperties();
                drawParallaxProperties();
                drawLayerProperties();
            }
        }
        ImGui::End();
    }

    void TerrainMaterialEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveMaterial();
                }
                if (ImGui::MenuItem("Compile", "F5"))
                {
                    compileMaterial();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Save"))
        {
            saveMaterial();
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile"))
        {
            compileMaterial();
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(120);
        if (showCompileError)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Compile Error!");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(compileErrorMessage.c_str());
                ImGui::EndTooltip();
            }
        }
        else if (materialData && !materialData->needsRecompile)
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Compiled      ");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Needs Compile");
        }
        ImGui::PopItemWidth();

        ImGui::Separator();
    }

    // VK-1611. Material-global, not per-layer: macro variation modulates the COMPOSITED albedo
    // once per fragment, and the distance-rescale knee lives in footprint space (log2 texture
    // repeats per output texel), which already accounts for each layer's own tiling scale — so one
    // knee is correct across layers that tile at wildly different rates.
    void TerrainMaterialEditorWindow::drawAntiTilingProperties()
    {
        if (!materialData) return;

        if (!ImGui::CollapsingHeader("Anti-Tiling"))
            return;

        auto& at = materialData->antiTiling;
        ImGui::Indent(8.0f);

        ImGui::SeparatorText("Macro Variation");
        ImGui::TextDisabled("Low-frequency world-anchored tint that breaks up large flat areas.");
        if (ImGui::DragFloat("Strength##Macro", &at.macroVariationStrength, 0.01f,
                             0.0f, terrain::MACRO_VARIATION_MAX_STRENGTH, "%.2f"))
        {
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("0 disables it exactly: the shader's multiplier becomes 1.0 and the\n"
                              "composite is bit-identical to a terrain without macro variation.\n"
                              "Costs no texture fetch (the noise is procedural) and is baked into\n"
                              "RVT pages, so it is paid once per page rather than every frame.");
        }

        ImGui::BeginDisabled(at.macroVariationStrength <= 0.0f);
        if (ImGui::DragFloat("Size A (m)##Macro", &at.macroVariationSize0, 1.0f,
                             terrain::MACRO_VARIATION_MIN_SIZE, terrain::MACRO_VARIATION_MAX_SIZE, "%.0f"))
        {
            onChanged();
        }
        if (ImGui::DragFloat("Size B (m)##Macro", &at.macroVariationSize1, 1.0f,
                             terrain::MACRO_VARIATION_MIN_SIZE, terrain::MACRO_VARIATION_MAX_SIZE, "%.0f"))
        {
            onChanged();
        }
        ImGui::TextDisabled("Two octaves multiplied together, in world metres.");
        int seed = static_cast<int>(at.macroVariationSeed);
        if (ImGui::DragInt("Seed##Macro", &seed, 1.0f, 0, 65535))
        {
            at.macroVariationSeed = static_cast<uint32_t>(std::max(seed, 0));
            onChanged();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::SeparatorText("Distance Tiling Rescale");
        ImGui::TextDisabled("Blends toward a larger-scale copy of each layer as it recedes.");
        if (ImGui::DragFloat("Strength##Rescale", &at.distanceRescaleStrength, 0.01f,
                             0.0f, terrain::DISTANCE_RESCALE_MAX_STRENGTH, "%.2f"))
        {
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(!)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Unlike macro variation this costs a SECOND texture fetch per layer\n"
                              "(albedo only), so it is compiled into the shader only while the\n"
                              "strength is above 0 and the scale is below 1. At 0 the extra fetch\n"
                              "does not exist in the shader at all.\n\n"
                              "With RVT on, distant terrain is already served from baked pages whose\n"
                              "mip chain mitigates repetition, so this mainly helps the RVT-off and\n"
                              "page-streaming paths.");
        }

        ImGui::BeginDisabled(at.distanceRescaleStrength <= 0.0f);
        if (ImGui::DragFloat("Far Scale##Rescale", &at.distanceRescaleScale, 0.01f,
                             terrain::DISTANCE_RESCALE_MIN_SCALE, terrain::DISTANCE_RESCALE_MAX_SCALE, "%.2f"))
        {
            onChanged();
        }
        ImGui::TextDisabled("UV multiplier for the far tap; 0.25 reads 4x larger. 1.0 disables it.");
        if (ImGui::DragFloat("Knee (log2)##Rescale", &at.distanceRescaleKnee, 0.1f,
                             terrain::DISTANCE_RESCALE_MIN_KNEE, terrain::DISTANCE_RESCALE_MAX_KNEE, "%.1f"))
        {
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Where the fade starts, in log2(texture repeats per output pixel).\n"
                              "-7 is one repeat per 128 pixels — about where a repeat stops reading\n"
                              "as detail and starts reading as a pattern. Lower = starts nearer.");
        }
        if (ImGui::DragFloat("Fade Width (log2)##Rescale", &at.distanceRescaleWidth, 0.1f,
                             terrain::DISTANCE_RESCALE_MIN_WIDTH, terrain::DISTANCE_RESCALE_MAX_WIDTH, "%.1f"))
        {
            onChanged();
        }
        ImGui::EndDisabled();

        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::Separator();
    }

    // VK-1625. Material-global for the same reason anti-tiling is, and more strictly so: the parallax
    // offset is applied ONCE to the shared base UV that every layer derives from, so a per-layer
    // amplitude is not expressible without running the composite more than once.
    void TerrainMaterialEditorWindow::drawParallaxProperties()
    {
        if (!materialData) return;

        if (!ImGui::CollapsingHeader("Parallax (POM-lite)"))
            return;

        auto& px = materialData->parallax;
        ImGui::Indent(8.0f);

        ImGui::TextDisabled("Shifts the layer textures along the view ray using the height packed\n"
                            "in ORM alpha, so gravel and rock read as relief instead of as a decal.");
        if (ImGui::DragFloat("Depth (m)##Parallax", &px.depthMetres, 0.001f,
                             0.0f, terrain::PARALLAX_MAX_DEPTH, "%.3f"))
        {
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(!)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("0 disables it exactly: the whole march, its splat gather and its\n"
                              "parameter buffer are not compiled into the shader at all, and the\n"
                              "SPIR-V is byte-identical to a build without this feature.\n\n"
                              "This is the most expensive terrain shading option. Each step costs\n"
                              "one ORM fetch per layer painted on the fragment, on top of what the\n"
                              "composite already samples. Measure before shipping it on.\n\n"
                              "Geometry does not move: silhouettes, depth and shadows are unchanged.");
        }

        ImGui::BeginDisabled(px.depthMetres <= 0.0f);

        int steps = static_cast<int>(px.steps);
        if (ImGui::DragInt("Steps##Parallax", &steps, 1.0f,
                           static_cast<int>(terrain::PARALLAX_MIN_STEPS),
                           static_cast<int>(terrain::PARALLAX_MAX_STEPS)))
        {
            px.steps = static_cast<uint32_t>(std::clamp(steps,
                                                        static_cast<int>(terrain::PARALLAX_MIN_STEPS),
                                                        static_cast<int>(terrain::PARALLAX_MAX_STEPS)));
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Search steps along the view ray. The count is uniform across the draw,\n"
                              "so every fragment runs all of them — there is no early exit to make\n"
                              "shallow ground cheaper. Cost is (steps + 1) fetches per painted layer.\n\n"
                              "4 is usually enough for gravel; raise it only if you can see the\n"
                              "stepping on a strong height map at a grazing angle.");
        }

        if (ImGui::DragFloat("Fade Start (m)##Parallax", &px.fadeStart, 0.5f,
                             0.0f, terrain::PARALLAX_MAX_FADE_DISTANCE, "%.1f"))
        {
            if (px.fadeEnd < px.fadeStart + terrain::PARALLAX_MIN_FADE_SPAN)
                px.fadeEnd = px.fadeStart + terrain::PARALLAX_MIN_FADE_SPAN;
            onChanged();
        }
        if (ImGui::DragFloat("Fade End (m)##Parallax", &px.fadeEnd, 0.5f,
                             px.fadeStart + terrain::PARALLAX_MIN_FADE_SPAN,
                             terrain::PARALLAX_MAX_FADE_DISTANCE, "%.1f"))
        {
            onChanged();
        }
        ImGui::TextDisabled("Camera distance, in metres. Past the end nothing is marched at all.");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Distance, not the footprint the anti-tiling fades use. Those have to\n"
                              "agree with the camera-less RVT bake; parallax never runs in the bake,\n"
                              "so it can use the unit that means something to you.\n\n"
                              "A top-down camera keeps the whole ground at roughly one distance, so\n"
                              "the fade saves nothing there — the entire visible surface either pays\n"
                              "or it does not.");
        }

        if (ImGui::DragFloat("Reference Height##Parallax", &px.referenceHeight, 0.01f,
                             terrain::PARALLAX_MIN_REFERENCE_HEIGHT,
                             terrain::PARALLAX_MAX_REFERENCE_HEIGHT, "%.2f"))
        {
            onChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(!)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Which ORM alpha value counts as the surface. 1.0 (the default) means\n"
                              "white, which is what a height map should peak at.\n\n"
                              "READ THIS IF THE GROUND SEEMS TO SWIM AS THE CAMERA TURNS. Packing an\n"
                              "ORM without a height input fills alpha with a flat mid-grey (128), and\n"
                              "a height field that never reaches the top sits uniformly below the\n"
                              "surface — which parallax renders as a texture that slides with the\n"
                              "view. Set this to 0.5 for such a material, or repack the ORM with a\n"
                              "real height map. Height blending cannot see this, so a material can\n"
                              "look correct until parallax is switched on.");
        }

        ImGui::EndDisabled();

        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::Separator();
    }

    void TerrainMaterialEditorWindow::drawLayerProperties()
    {
        if (!materialData)
        {
            ImGui::TextDisabled("No material loaded");
            return;
        }

        int layerCount = materialData->activeLayerCount;

        ImGui::Text("Layers: %d / %d", layerCount, terrain::MAX_TERRAIN_LAYERS);
        ImGui::SameLine();
        if (layerCount < terrain::MAX_TERRAIN_LAYERS)
        {
            if (ImGui::SmallButton("+ Add Layer"))
            {
                auto& newLayer = materialData->layers[layerCount];
                newLayer = terrain::TerrainMaterialLayer{};
                newLayer.name = "Layer " + std::to_string(layerCount);
                layerCount++;
                materialData->activeLayerCount = static_cast<uint8_t>(layerCount);
                onChanged();
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::SmallButton("+ Add Layer");
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::Separator();

        for (int i = 0; i < layerCount; ++i)
        {
            auto& layer = materialData->layers[i];

            ImGui::PushID(i);

            // VK-1613: this used to be a lie — it round-tripped through the .vfTerrainMat and hid the
            // layer from the paint picker, but the renderer never consulted it, so a hidden layer
            // kept drawing wherever it had been painted. It is now honoured at weight-upload time.
            //
            // Hiding the LAST visible layer is refused rather than allowed: with no weight left, the
            // composite's 1.0/max(totalW, 0.001) clamp renders painted terrain black, which reads as
            // a bug rather than as a choice. Same spirit as the activeLayerCount >= 1 invariant.
            {
                int visibleCount = 0;
                for (int j = 0; j < layerCount; ++j)
                {
                    if (materialData->layers[j].enabled) ++visibleCount;
                }
                const bool isLastVisible = layer.enabled && visibleCount <= 1;

                ImGui::BeginDisabled(isLastVisible);
                if (ImGui::Checkbox("##enabled", &layer.enabled))
                {
                    onChanged();
                }
                ImGui::EndDisabled();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(isLastVisible
                        ? "Visible. This is the last visible layer — at least one must stay visible."
                        : "Layer visibility.\n\n"
                          "Hidden layers are not rendered and cannot be painted with. The remaining\n"
                          "layers redistribute to fill in, and painted weights are kept untouched, so\n"
                          "unhiding restores exactly what was there.\n\n"
                          "Areas painted ONLY with hidden layers have no weight left and go black.");
                }
            }
            ImGui::SameLine();

            std::string headerLabel = std::format("{} ({})", layer.name, i);
            if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(8.0f);

                {
                    char nameBuffer[64];
                    std::strncpy(nameBuffer, layer.name.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                    {
                        layer.name = std::string(nameBuffer);
                        onChanged();
                    }
                }

                // VK-1486: terrain layers source all PBR from a referenced .vfMat / .vfMatInstance
                // (albedo/normal/ORM textures + roughness/metallic/ao/emission). tilingScale stays
                // terrain-layer-local.
                {
                    ImGui::Text("Material Source:");
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Terrain layers source PBR from the linked .vfMat or .vfMatInstance.\n"
                                          "Supported maps: albedo, packed ORM (Occlusion=R, Roughness=G, "
                                          "Metallic=B, Height=A), normal, and emission.\n"
                                          "Separate AO, roughness, metallic, and height textures are not\n"
                                          "supported - pack height into the ORM alpha channel instead.");
                    }
                    ImGui::SameLine();
                    std::string matDisplay = !layer.materialRef.isValid() ? "(None)" :
                        std::filesystem::path(layer.materialRef.resolve()).filename().string();
                    ImGui::TextDisabled("%s", matDisplay.c_str());

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Browse##material"))
                    {
                        nfd::FileDialog fileDialog;
                        std::vector<std::pair<std::wstring, std::wstring>> filters = {
                            {L"VF Material", L"*.vfMat;*.vfMatInstance"}
                        };
                        std::string selectedPath = fileDialog.openFileDialog(filters);
                        if (!selectedPath.empty())
                        {
                            selectedPath.erase(
                                std::remove(selectedPath.begin(), selectedPath.end(), '\0'),
                                selectedPath.end());
                            layer.materialRef = asset::AssetRef::fromPath(selectedPath);
                            onChanged();
                        }
                    }
                    if (layer.materialRef.isValid())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##material"))
                        {
                            layer.materialRef = asset::AssetRef::invalid();
                            onChanged();
                        }
                    }
                }

                if (!layer.materialRef.isValid())
                {
                    ImGui::TextDisabled("No material assigned (layer renders with defaults)");
                }

                // VK-1609: shown for EVERY layer, including layer 0. The composite is a symmetric
                // 8-channel blend with no privileged base layer, and rock - the layer most likely
                // to want height blending - is frequently layer 0.
                {
                    const char* blendModes[] = {"Linear", "Height Blend"};
                    int currentBlend = (layer.blendMode == terrain::TerrainLayerBlendMode::HeightBlend) ? 1 : 0;

                    if (ImGui::Combo("Blend Mode", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes)))
                    {
                        layer.blendMode = (currentBlend == 1)
                            ? terrain::TerrainLayerBlendMode::HeightBlend
                            : terrain::TerrainLayerBlendMode::Linear;
                        onChanged();
                    }
                }

                if (layer.blendMode == terrain::TerrainLayerBlendMode::HeightBlend)
                {
                    if (ImGui::DragFloat("Height Contrast", &layer.heightContrast, 0.05f,
                                         0.0f, terrain::MAX_HEIGHT_BLEND_CONTRAST, "%.2f"))
                    {
                        onChanged();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Sharpness of the height transition: higher values make this layer\n"
                                          "poke through neighbours along its own height detail instead of\n"
                                          "fading by splat weight. 0 = plain linear blend.\n\n"
                                          "Height comes from the ALPHA channel of this layer material's packed\n"
                                          "ORM texture. A layer with no ORM texture blends linearly.");
                    }
                    if (!layer.materialRef.isValid())
                    {
                        ImGui::TextDisabled("Height blend needs a material with a packed ORM texture");
                    }
                }

                if (ImGui::DragFloat("Tiling", &layer.tilingScale, 0.01f, 0.01f, 100.0f))
                {
                    onChanged();
                }

                // VK-1612 hex-tile stochastic sampling, per layer because the cost is per layer.
                if (ImGui::Checkbox("Hex Anti-Tiling", &layer.hexTiling))
                {
                    onChanged();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(!)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Samples this layer three times on a randomly offset hexagonal\n"
                                      "lattice and blends the taps, so the exemplar stops repeating on a\n"
                                      "visible grid. Costs 3x the albedo and normal fetches FOR THIS LAYER;\n"
                                      "layers with it off are unaffected.\n\n"
                                      "With RVT on, most fragments read a baked page instead of running the\n"
                                      "composite, so the steady-state frame cost is close to zero and the\n"
                                      "work is paid once per page at bake time.\n\n"
                                      "Needs an albedo texture: a layer without one composites a constant\n"
                                      "colour, and re-tiling a constant does nothing.");
                }
                if (layer.hexTiling)
                {
                    ImGui::Indent(8.0f);
                    if (ImGui::DragFloat("Cell Scale##Hex", &layer.hexCellScale, 0.05f,
                                         terrain::MIN_HEX_TILING_CELL_SCALE,
                                         terrain::MAX_HEX_TILING_CELL_SCALE, "%.2f"))
                    {
                        onChanged();
                    }
                    ImGui::TextDisabled("Hex cells per texture repeat.");
                    if (ImGui::DragFloat("Blend Contrast##Hex", &layer.hexContrast, 0.1f,
                                         terrain::MIN_HEX_TILING_CONTRAST,
                                         terrain::MAX_HEX_TILING_CONTRAST, "%.1f"))
                    {
                        onChanged();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How hard the three taps are sharpened toward a single winner.\n"
                                          "Too low and the taps average into visible ghosting; too high and\n"
                                          "the hexagon edges themselves become visible. Around 4 is the\n"
                                          "published sweet spot (ghosting below 2, hex structure above 8).");
                    }
                    if (ImGui::DragFloat("Rotation##Hex", &layer.hexRotation, 0.01f,
                                         0.0f, terrain::MAX_HEX_TILING_ROTATION, "%.2f"))
                    {
                        onChanged();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Randomly rotates each hex tile. Offsetting alone leaves every tap\n"
                                          "in the same orientation, and blending a periodic texture with\n"
                                          "itself at a shifted phase can cancel detail rather than vary it;\n"
                                          "rotation breaks that up. Defaults to 0, which is what the\n"
                                          "reference implementation ships, because rotation also swirls any\n"
                                          "deliberate directional grain in the texture.");
                    }
                    if (!layer.materialRef.isValid())
                    {
                        ImGui::TextDisabled("Hex anti-tiling needs a material with an albedo texture");
                    }
                    ImGui::Unindent(8.0f);
                }

                // VK-1614 per-layer weather response. Per layer because how a surface reacts to rain
                // and snow is a material property, not a terrain-wide one.
                if (ImGui::Checkbox("Weather Response", &layer.weatherResponse))
                {
                    onChanged();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(!)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Overrides how this layer reacts to rain and snow.\n\n"
                                      "Off, the layer uses the engine default: absorption derived from\n"
                                      "its roughness, and full snow retention — exactly the behaviour\n"
                                      "before this option existed.\n\n"
                                      "Turning it on for ANY layer compiles a per-fragment blend of these\n"
                                      "values into the terrain shader, so leave it off on layers that do\n"
                                      "not need it.");
                }
                if (layer.weatherResponse)
                {
                    ImGui::Indent(8.0f);
                    if (ImGui::DragFloat("Porosity##Weather", &layer.porosity, 0.01f, 0.0f, 1.0f, "%.2f"))
                    {
                        onChanged();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How much water this surface drinks.\n"
                                          "High (sand, soil): darkens a lot when wet, and never puddles.\n"
                                          "Low (rock, stone, tile): darkens little, and pools water in\n"
                                          "its hollows.");
                    }
                    if (ImGui::DragFloat("Snow Retention##Weather", &layer.snowRetention, 0.01f,
                                         0.0f, 1.0f, "%.2f"))
                    {
                        onChanged();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("How much of the falling snow this surface holds.\n"
                                          "1 = holds all of it, 0 = sheds it entirely.\n\n"
                                          "This scales the amount, so it composes with the shared slope\n"
                                          "rule: it can make a layer shed snow on ground that would\n"
                                          "otherwise keep it, but it cannot hold snow on a steeper slope\n"
                                          "than the engine allows.");
                    }
                    ImGui::Unindent(8.0f);
                }

                ImGui::Spacing();
                ImGui::BeginDisabled(layerCount <= 1);
                bool removeClicked = ImGui::SmallButton("Remove Layer");
                ImGui::EndDisabled();

                if (removeClicked)
                {
                    removeLayer(i, layerCount);
                    layerCount--;
                    onChanged();
                    ImGui::Unindent(8.0f);
                    ImGui::PopID();
                    break;
                }

                ImGui::Unindent(8.0f);
            }

            ImGui::PopID();
        }
    }
}
