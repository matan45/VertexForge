#include "print/Log.hpp"
#include "VFXEditorWindow.hpp"
#include "VFXPreviewPanel.hpp"
#include "../../graph/VFXGraphEditor.hpp"
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXEventTypes.hpp>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXBoundsUtil.hpp>
#include <vfx/VFXModifierConfigLoader.hpp>
#include <vfx/VFXForceConfigLoader.hpp>
#include <vfx/VFXShapeConfigLoader.hpp>
#include <providers/vfx/IVFXPreviewProvider.hpp>
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include <filesystem>
#include <algorithm>

namespace windows
{
    VFXEditorWindow::VFXEditorWindow(const std::string& vfxPath)
        : vfxPath(vfxPath)
        , graphEditor(std::make_unique<editor::graph::VFXGraphEditor>())
        , previewPanel(std::make_unique<editor::vfxeditor::VFXPreviewPanel>(this))
    {
        std::filesystem::path path(vfxPath);
        windowTitle = "VFX Editor: " + path.filename().string();
    }

    VFXEditorWindow::~VFXEditorWindow()
    {
        if (graphEditor)
        {
            graphEditor->cleanUp();
        }
    }

    void VFXEditorWindow::initEditor()
    {
        graphEditor->init();
        loadVFX();

        if (vfxData)
        {
            graphEditor->setGraph(&vfxData->graph);
            graphEditor->setOnGraphChanged([this]() { onGraphChanged(); });
            graphEditor->navigateToContent();
            needsPreviewUpdate = true;

            propertyPanel.setOnPropertyChanged([this]() { onGraphChanged(); });
        }
    }

    void VFXEditorWindow::loadVFX()
    {
        auto loadedData = vfx::VFXAsset::load(vfxPath);

        if (loadedData.has_value())
        {
            vfxData = std::make_unique<vfx::VFXData>(std::move(loadedData.value()));
        }
        else
        {
            vfLogInfo("Creating new VFX: {}", vfxPath);
            std::filesystem::path path(vfxPath);
            vfxData = std::make_unique<vfx::VFXData>(
                vfx::VFXAsset::createDefault(path.stem().string()));
        }
    }

    void VFXEditorWindow::saveVFX()
    {
        if (!vfxData) return;

        if (vfx::VFXAsset::save(vfxPath, *vfxData))
        {
            isDirty = false;
            vfLogInfo("VFX saved: {}", vfxPath);
            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = vfxPath;
            events::EventDispatcher::instance().publish(assetNotif);
        }
        else
        {
            vfLogError("Failed to save VFX: {}", vfxPath);
        }
    }

    void VFXEditorWindow::onGraphChanged()
    {
        isDirty = true;
        updatePreviewFromGraph();
    }

    void VFXEditorWindow::updatePreviewFromGraph()
    {
        if (!vfxData) return;

        const vfx::VFXNode* emitterNode = vfxData->graph.findEmitterNode();
        if (!emitterNode) return;

        auto getFloat = [](const vfx::VFXNode& node, const std::string& propName, float defaultValue) -> float {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<float>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getVec3 = [](const vfx::VFXNode& node, const std::string& propName, const glm::vec3& defaultValue) -> glm::vec3 {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<glm::vec3>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getVec4 = [](const vfx::VFXNode& node, const std::string& propName, const glm::vec4& defaultValue) -> glm::vec4 {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<glm::vec4>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getBool = [](const vfx::VFXNode& node, const std::string& propName, bool defaultValue) -> bool {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<bool>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getInt = [](const vfx::VFXNode& node, const std::string& propName, int32_t defaultValue) -> int32_t {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<int32_t>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        auto getString = [](const vfx::VFXNode& node, const std::string& propName, const std::string& defaultValue) -> std::string {
            auto it = node.properties.find(propName);
            if (it != node.properties.end()) {
                if (auto* val = std::get_if<std::string>(&it->second.value)) {
                    return *val;
                }
            }
            return defaultValue;
        };

        services::VFXPreviewParams params;
        params.spawnRate = getFloat(*emitterNode, "spawnRate", vfx::EmitterDefaults::SPAWN_RATE);
        params.lifetime = getFloat(*emitterNode, "lifetime", vfx::EmitterDefaults::LIFETIME);
        params.startSize = getFloat(*emitterNode, "startSize", vfx::EmitterDefaults::START_SIZE);
        params.startSpeed = getFloat(*emitterNode, "startSpeed", vfx::EmitterDefaults::START_SPEED);
        params.emitDirection = getVec3(*emitterNode, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));
        params.startColor = getVec4(*emitterNode, "startColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        params.looping = getBool(*emitterNode, "looping", vfx::EmitterDefaults::LOOPING);
        params.texturePath = getString(*emitterNode, "texture", "");
        params.sizeVariance = std::clamp(
            getFloat(*emitterNode, "sizeVariance", vfx::EmitterDefaults::SIZE_VARIANCE), 0.0f, 1.0f);
        params.lifetimeVariance = std::clamp(
            getFloat(*emitterNode, "lifetimeVariance", vfx::EmitterDefaults::LIFETIME_VARIANCE), 0.0f, 1.0f);
        params.speedVariance = std::clamp(
            getFloat(*emitterNode, "speedVariance", vfx::EmitterDefaults::SPEED_VARIANCE), 0.0f, 1.0f);
        params.rotationVariance = glm::radians(std::max(0.0f,
            getFloat(*emitterNode, "rotationVariance", vfx::EmitterDefaults::ROTATION_VARIANCE_DEGREES)));
        params.angularVelocityVariance = glm::radians(std::max(0.0f,
            getFloat(*emitterNode, "angularVelocityVariance", vfx::EmitterDefaults::ANGULAR_VELOCITY_VARIANCE_DEGREES)));
        params.colorValueVariance = std::clamp(
            getFloat(*emitterNode, "colorValueVariance", vfx::EmitterDefaults::COLOR_VALUE_VARIANCE), 0.0f, 1.0f);
        params.alphaVariance = std::clamp(
            getFloat(*emitterNode, "alphaVariance", vfx::EmitterDefaults::ALPHA_VARIANCE), 0.0f, 1.0f);

        params.modifiers = vfx::VFXModifierConfigLoader::fromGraph(vfxData->graph);
        params.forces = vfx::VFXForceConfigLoader::fromGraph(vfxData->graph);
        params.shape = vfx::VFXShapeConfigLoader::fromGraph(vfxData->graph);
        params.bursts = vfx::loadBurstsFromNode(*emitterNode);

        params.flipbookRows = std::clamp(getInt(*emitterNode, "flipbookRows", vfx::EmitterDefaults::FLIPBOOK_ROWS), 1, 16);
        params.flipbookColumns = std::clamp(getInt(*emitterNode, "flipbookColumns", vfx::EmitterDefaults::FLIPBOOK_COLUMNS), 1, 16);
        params.flipbookFrameRate = getFloat(*emitterNode, "flipbookFrameRate", vfx::EmitterDefaults::FLIPBOOK_FRAME_RATE);
        params.flipbookRandomStart = getBool(*emitterNode, "flipbookRandomStart", vfx::EmitterDefaults::FLIPBOOK_RANDOM_START);
        params.flipbookFrameBlend = getBool(*emitterNode, "flipbookFrameBlend", vfx::EmitterDefaults::FLIPBOOK_FRAME_BLEND);

        // Rendering
        params.alphaClipThreshold = getFloat(*emitterNode, "alphaClipThreshold", vfx::EmitterDefaults::ALPHA_CLIP_THRESHOLD);
        // VK-1472: blendMode string wins; legacy assets fall back to the additiveBlend bool.
        {
            const std::string blendModeStr = getString(*emitterNode, "blendMode", "");
            params.blendMode = blendModeStr.empty()
                ? vfx::blendModeFromLegacy(getBool(*emitterNode, "additiveBlend", vfx::EmitterDefaults::ADDITIVE_BLEND))
                : vfx::stringToBlendMode(blendModeStr);
        }

        params.renderMode = getInt(*emitterNode, "renderMode", vfx::EmitterDefaults::RENDER_MODE);
        params.softParticleDistance = getFloat(*emitterNode, "softParticleDistance", vfx::EmitterDefaults::SOFT_PARTICLE_DISTANCE);
        params.stretchMultiplier = getFloat(*emitterNode, "stretchMultiplier", vfx::EmitterDefaults::STRETCH_MULTIPLIER);

        params.meshPath = getString(*emitterNode, "meshPath", "");
        params.materialPath = getString(*emitterNode, "materialRef", ""); // VK-1526

        // VK-1476: mesh orientation (only used when renderMode == MeshParticle).
        params.meshOrientationMode = vfx::stringToOrientationMode(getString(*emitterNode, "meshOrientationMode", ""));
        params.meshOrientationAxis = getVec3(*emitterNode, "meshOrientationAxis", glm::vec3(0.0f, 1.0f, 0.0f));
        params.meshOrientationSpinRate = getFloat(*emitterNode, "meshOrientationSpinRate", vfx::EmitterDefaults::MESH_ORIENTATION_SPIN_RATE);

        params.maxTrailPoints = getInt(*emitterNode, "maxTrailPoints", vfx::EmitterDefaults::MAX_TRAIL_POINTS);
        params.ribbonWidth = getFloat(*emitterNode, "ribbonWidth", vfx::EmitterDefaults::RIBBON_WIDTH);
        params.ribbonMinDistance = getFloat(*emitterNode, "ribbonMinDistance", vfx::EmitterDefaults::RIBBON_MIN_DISTANCE);
        // VK-1474: over-trail width curve + tail gradient (present only when authored).
        if (auto wcIt = emitterNode->properties.find("ribbonWidthCurve"); wcIt != emitterNode->properties.end())
            if (auto* c = std::get_if<vfx::VFXCurve>(&wcIt->second.value)) { params.ribbonWidthCurve = *c; params.hasRibbonWidthCurve = true; }
        if (auto tgIt = emitterNode->properties.find("ribbonTailGradient"); tgIt != emitterNode->properties.end())
            if (auto* g = std::get_if<vfx::VFXGradient>(&tgIt->second.value)) { params.ribbonTailGradient = *g; params.hasRibbonTailGradient = true; }

        params.uvScrollSpeedU = getFloat(*emitterNode, "uvScrollSpeedU", vfx::EmitterDefaults::UV_SCROLL_SPEED_U);
        params.uvScrollSpeedV = getFloat(*emitterNode, "uvScrollSpeedV", vfx::EmitterDefaults::UV_SCROLL_SPEED_V);

        // Events
        params.events = vfx::loadEventConfigFromNode(*emitterNode);

        // Lighting
        params.emissiveIntensity = std::max(0.0f,
            getFloat(*emitterNode, "emissiveIntensity", vfx::EmitterDefaults::EMISSIVE_INTENSITY));
        params.lightingInfluence = std::clamp(
            getFloat(*emitterNode, "lightingInfluence", vfx::EmitterDefaults::LIGHTING_INFLUENCE), 0.0f, 1.0f);
        params.normalMode = std::clamp(
            getInt(*emitterNode, "normalMode", vfx::EmitterDefaults::NORMAL_MODE), 0, 2);
        params.ambientAmount = std::clamp(
            getFloat(*emitterNode, "ambientAmount", vfx::EmitterDefaults::AMBIENT_AMOUNT), 0.0f, 1.0f);

        // Collision
        params.collisionEnabled = getBool(*emitterNode, "collisionEnabled", vfx::EmitterDefaults::COLLISION_ENABLED);
        params.collisionBounce = std::clamp(
            getFloat(*emitterNode, "collisionBounce", vfx::EmitterDefaults::COLLISION_BOUNCE), 0.0f, 1.0f);
        params.collisionFriction = std::clamp(
            getFloat(*emitterNode, "collisionFriction", vfx::EmitterDefaults::COLLISION_FRICTION), 0.0f, 1.0f);
        params.collisionLifetimeLoss = std::clamp(
            getFloat(*emitterNode, "collisionLifetimeLoss", vfx::EmitterDefaults::COLLISION_LIFETIME_LOSS), 0.0f, 1.0f);

        previewPanel->setParams(params);
    }

    void VFXEditorWindow::draw()
    {
        if (!isOpen)
        {
            previewPanel->cleanup();
            return;
        }

        if (needsInit)
        {
            initEditor();
            needsInit = false;
        }

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("VFXEditor", ImVec2(1000, 700));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        std::string title = windowTitle + (isDirty ? " *" : "  ") +
                            "###VFXEditor:" + vfxPath;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        flags |= maximizer.windowFlags();
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (isOpen)
            {
                drawToolbar();

                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                float spacing = ImGui::GetStyle().ItemSpacing.y;
                float topHeight = contentSize.y - propertyPanelHeight - spacing;

                // Top row: Preview | Graph
                ImGui::BeginChild("TopRow", ImVec2(0, topHeight), false, ImGuiWindowFlags_NoScrollbar);
                {
                    ImVec2 topSize = ImGui::GetContentRegionAvail();

                    ImGui::BeginChild("PreviewPanel", ImVec2(previewPanelWidth, topSize.y), true);
                    if (vfxData)
                        previewPanel->setBoundsOverlay(showBounds, vfx::resolveBounds(vfxData->bounds, *vfxData));
                    previewPanel->draw();
                    ImGui::EndChild();

                    if (needsPreviewUpdate)
                    {
                        updatePreviewFromGraph();
                        previewPanel->play();
                        needsPreviewUpdate = false;
                    }

                    ImGui::SameLine();

                    float graphWidth = topSize.x - previewPanelWidth - ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("GraphPanel", ImVec2(graphWidth, topSize.y), true,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    drawGraphPanel();
                    ImGui::EndChild();
                }
                ImGui::EndChild();

                // Bottom row: Property Panel
                ImGui::BeginChild("PropertyPanel", ImVec2(0, propertyPanelHeight), true);
                drawPropertyPanel();
                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("VFXEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void VFXEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveVFX();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    isOpen = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Navigate to Content"))
                {
                    graphEditor->navigateToContent();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Save"))
        {
            saveVFX();
        }
        ImGui::SameLine();
        maximizer.drawButton();
        ImGui::SameLine();
        ImGui::Checkbox("Show Bounds", &showBounds);

        if (ImGui::CollapsingHeader("Bounds & Scalability"))
        {
            ImGui::Indent();
            ImGui::TextDisabled("Bounds (cull + visualization)");
            drawBoundsControls();
            ImGui::Spacing();
            ImGui::TextDisabled("Scalability profile");
            drawScalabilityControls();
            ImGui::Spacing();
            ImGui::TextDisabled("Significance (live-instance cap)");
            if (vfxData &&
                ImGui::DragFloat("Significance", &vfxData->significance, 0.05f, 0.0f, 1.0e6f))
                isDirty = true;
            ImGui::SetItemTooltip(
                "Per-asset importance for the runtime significance cap (scored as "
                "significance / distance^2). Higher = more likely to stay live when a "
                "scene-wide live-instance budget is set. Default 1.0 is neutral.");
            ImGui::Unindent();
        }

        ImGui::Separator();
    }

    void VFXEditorWindow::drawBoundsControls()
    {
        if (!vfxData) return;
        auto& b = vfxData->bounds;

        int mode = static_cast<int>(b.mode);
        if (ImGui::RadioButton("Auto", &mode, static_cast<int>(vfx::VFXBoundsMode::Auto)))
        {
            b.mode = vfx::VFXBoundsMode::Auto;
            isDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Fixed", &mode, static_cast<int>(vfx::VFXBoundsMode::Fixed)))
        {
            b.mode = vfx::VFXBoundsMode::Fixed;
            isDirty = true;
        }

        if (b.mode == vfx::VFXBoundsMode::Fixed)
        {
            if (ImGui::DragFloat3("Center", &b.center.x, 0.1f))
                isDirty = true;
            if (ImGui::DragFloat3("Extents", &b.extents.x, 0.1f, 0.0f, 1.0e6f))
                isDirty = true;
        }
        else
        {
            const glm::vec3 e = vfx::computeAutoBounds(*vfxData).getExtents();
            ImGui::Text("Auto extents: %.1f, %.1f, %.1f", e.x, e.y, e.z);
        }

        if (ImGui::Button("Recalculate from emitter"))
        {
            const math::AABB a = vfx::computeAutoBounds(*vfxData);
            b.mode = vfx::VFXBoundsMode::Fixed;
            b.center = a.getCenter();
            b.extents = a.getExtents();
            isDirty = true;
        }
        ImGui::SetItemTooltip("Capture the analytic Auto bounds as an editable Fixed box.");

        if (ImGui::Checkbox("Cull-eligible (allow pre-spawn cull)", &vfxData->cullEligible))
            isDirty = true;
    }

    void VFXEditorWindow::drawScalabilityControls()
    {
        if (!vfxData) return;
        auto& s = vfxData->scalability;

        if (ImGui::Checkbox("Enable scalability profile", &s.enabled))
            isDirty = true;
        if (!s.enabled)
        {
            ImGui::TextDisabled("Disabled: effect is unaffected by the quality tier.");
            return;
        }

        static const char* tierNames[vfx::kVFXQualityTierCount] = {"Low", "Medium", "High", "Ultra"};
        for (int i = 0; i < vfx::kVFXQualityTierCount; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::TreeNode(tierNames[i]))
            {
                auto& lvl = s.levels[i];
                if (ImGui::DragFloat("Spawn rate scale", &lvl.spawnRateScale, 0.01f, 0.0f, 8.0f))
                    isDirty = true;
                if (ImGui::DragInt("Max particles (-1=off)", &lvl.maxParticles, 1.0f, -1, 1000000))
                    isDirty = true;
                if (ImGui::DragFloat("Cull distance (-1=global)", &lvl.cullDistance, 0.5f, -1.0f, 1.0e5f))
                    isDirty = true;
                if (ImGui::DragInt("Update interval", &lvl.updateInterval, 1.0f, 1, 16))
                    isDirty = true;
                if (ImGui::Checkbox("Renderer enabled", &lvl.rendererEnabled))
                    isDirty = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void VFXEditorWindow::drawGraphPanel()
    {
        if (graphEditor && vfxData)
        {
            graphEditor->draw();
        }
        else
        {
            ImGui::TextDisabled("No VFX loaded");
        }
    }

    void VFXEditorWindow::drawPropertyPanel()
    {
        if (vfxData && graphEditor)
        {
            propertyPanel.draw(&vfxData->graph, graphEditor->getSelectedNodeId());
        }
        else
        {
            ImGui::TextDisabled("Select a modifier node to edit its curve or gradient");
        }
    }
}
