#include "print/Log.hpp"
#include "PrefabPreviewWindow.hpp"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>

using json = nlohmann::json;

namespace windows
{
    namespace
    {
        PrefabEntityNode parseEntityFromJson(const json& entityJson, ComponentStats& stats)
        {
            PrefabEntityNode node;
            stats.totalEntities++;

            node.name = entityJson.value("name", "Entity");

            auto parseVec3 = [](const json& j, float dx, float dy, float dz) -> glm::vec3 {
                if (j.is_array() && j.size() >= 3) return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
                if (j.is_object()) return {j.value("x", dx), j.value("y", dy), j.value("z", dz)};
                return {dx, dy, dz};
            };

            if (entityJson.contains("transform")) {
                const auto& t = entityJson["transform"];
                if (t.contains("position")) node.position = parseVec3(t["position"], 0, 0, 0);
                if (t.contains("rotation")) node.rotation = parseVec3(t["rotation"], 0, 0, 0);
                if (t.contains("scale"))    node.scale = parseVec3(t["scale"], 1, 1, 1);
            }

            if (entityJson.contains("components"))
            {
                const auto& componentsJson = entityJson["components"];

                static const std::vector<std::pair<std::string, std::string>> componentMap = {
                    {"camera", "Camera"},
                    {"ibl", "IBL"},
                    {"mesh", "Mesh"},
                    {"material", "Material"},
                    {"billboard", "Billboard"},
                    {"audioSource2D", "AudioSource2D"},
                    {"audioSource3D", "AudioSource3D"},
                    {"collider", "Collider"},
                    {"rigidBody", "RigidBody"},
                    {"script", "Script"},
                    {"navmeshAgent", "NavmeshAgent"},
                    {"navmeshObstacle", "NavmeshObstacle"},
                    {"vfx", "VFX"},
                    {"socketAttachment", "SocketAttachment"},
                    {"socketOverride", "SocketOverride"},
                    {"directionalLight", "DirectionalLight"},
                    {"pointLight", "PointLight"},
                    {"spotLight", "SpotLight"},
                    {"controller", "Controller"},
                    {"behaviorTree", "BehaviorTree"},
                    {"prefabInstance", "PrefabInstance"}
                };

                for (const auto& [key, displayName] : componentMap)
                {
                    if (componentsJson.contains(key))
                    {
                        node.componentTypes.push_back(displayName);
                        stats.counts[displayName]++;
                    }
                }

                if (componentsJson.contains("mesh"))
                {
                    node.meshPath = componentsJson["mesh"].value("meshPath", "");
                }
                if (componentsJson.contains("material"))
                {
                    node.materialPath = componentsJson["material"].value("defaultMaterial", "");
                }
                if (componentsJson.contains("audioSource2D"))
                {
                    node.audioPath = componentsJson["audioSource2D"].value("filePath", "");
                }
                else if (componentsJson.contains("audioSource3D"))
                {
                    node.audioPath = componentsJson["audioSource3D"].value("filePath", "");
                }
            }

            if (entityJson.contains("children") && entityJson["children"].is_array())
            {
                for (const auto& childJson : entityJson["children"])
                {
                    if (childJson.is_object())
                    {
                        node.children.push_back(parseEntityFromJson(childJson, stats));
                    }
                }
            }

            return node;
        }
    } // anonymous namespace

    PrefabPreviewWindow::PrefabPreviewWindow(const std::string& filePath)
        : prefabPath(filePath)
    {
        std::filesystem::path path(filePath);
        windowTitle = "Prefab Preview: " + path.filename().string();
    }

    PrefabPreviewWindow::~PrefabPreviewWindow()
    {
        loadingCancelled.store(true);
        if (loadFuture.valid())
        {
            loadFuture.wait();
        }
    }

    void PrefabPreviewWindow::draw()
    {
        if (!isOpen)
        {
            return;
        }

        if (needsInit)
        {
            startAsyncLoad();
            needsInit = false;
        }

        updateAsyncLoading();

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("PrefabPreview", ImVec2(800, 550));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                maximizer.drawButton();

                static float panelWidth = 150.0f;
                const float splitterThickness = 5.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();
                panelWidth = std::clamp(panelWidth, 120.0f,
                                        std::max(120.0f, contentSize.x - 250.0f - splitterThickness));
                float treeWidth = contentSize.x - panelWidth - splitterThickness;

                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 0.0f);
                editor::preview::splitterV("##prefabSplit", splitterThickness, &panelWidth,
                                           &treeWidth, 120.0f, 250.0f, contentSize.y);
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild("EntityTreePanel", ImVec2(treeWidth, contentSize.y), true);

                if (loadingInProgress.load())
                {
                    drawLoadingIndicator();
                }
                else
                {
                    drawEntityTreePanel();
                }

                ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("PrefabPreview", maximizer.effectiveSize());
            sizeSaved = true;
        }
    }

    void PrefabPreviewWindow::startAsyncLoad()
    {
        loadingInProgress.store(true);
        loadingCancelled.store(false);
        loadingStatus = "Loading prefab...";

        loadFuture = std::async(std::launch::async, [this]()
        {
            return loadPrefabBackground(prefabPath);
        });
    }

    void PrefabPreviewWindow::updateAsyncLoading()
    {
        if (!loadingInProgress.load() || !loadFuture.valid())
        {
            return;
        }

        if (loadFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            PrefabLoadResult result = loadFuture.get();

            if (result.success)
            {
                prefabName = std::move(result.prefabName);
                prefabVersion = std::move(result.version);
                rootEntity = std::move(result.rootEntity);
                componentStats = std::move(result.stats);
                prefabLoaded = true;
            }
            else
            {
                errorMessage = std::move(result.errorMessage);
                loadFailed = true;
            }

            loadingInProgress.store(false);
        }
    }

    PrefabLoadResult PrefabPreviewWindow::loadPrefabBackground(const std::string& path)
    {
        PrefabLoadResult result;

        try
        {
            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            std::ifstream file(path);
            if (!file.is_open())
            {
                result.errorMessage = "Failed to open file";
                return result;
            }

            json prefabJson = json::parse(file);
            file.close();

            if (loadingCancelled.load())
            {
                result.errorMessage = "Cancelled";
                return result;
            }

            if (!prefabJson.contains("prefab") || !prefabJson["prefab"].contains("entity"))
            {
                result.errorMessage = "Invalid prefab format";
                return result;
            }

            result.version = prefabJson.value("version", "unknown");
            result.prefabName = prefabJson["prefab"].value("name", "Unnamed");

            result.rootEntity = parseEntityFromJson(prefabJson["prefab"]["entity"], result.stats);

            result.success = true;
        }
        catch (const json::parse_error& e)
        {
            result.errorMessage = std::string("JSON parse error: ") + e.what();
            vfLogError("Prefab preview JSON parse error: {}", e.what());
        }
        catch (const std::exception& e)
        {
            result.errorMessage = e.what();
            vfLogError("Prefab preview error: {}", e.what());
        }

        return result;
    }

    void PrefabPreviewWindow::drawInfoPanel()
    {
        ImGui::Text("Info");
        ImGui::Separator();

        if (loadFailed)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load");
            if (!errorMessage.empty())
            {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", errorMessage.c_str());
            }
            return;
        }

        if (!prefabLoaded)
        {
            ImGui::TextDisabled("Loading...");
            return;
        }

        ImGui::Text("Name:"); ImGui::TextWrapped("  %s", prefabName.c_str()); ImGui::Spacing();
        ImGui::Text("Version: %s", prefabVersion.c_str());
        ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Entities: %u", componentStats.totalEntities);
        ImGui::Separator(); ImGui::Spacing();

        if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (componentStats.counts.empty())
            {
                ImGui::TextDisabled("No components");
            }
            else
            {
                for (const auto& [typeName, count] : componentStats.counts)
                {
                    ImGui::Text("  %s: %u", typeName.c_str(), count);
                }
            }
        }

        if (selectedEntityPath.has_value() && prefabLoaded)
        {
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const PrefabEntityNode* selectedNode = findNodeByPath(*selectedEntityPath);
                if (selectedNode)
                {
                    ImGui::Text("Name: %s", selectedNode->name.c_str());
                    ImGui::Spacing();

                    ImGui::Text("Position:");
                    ImGui::Text("  %.2f, %.2f, %.2f",
                                selectedNode->position.x,
                                selectedNode->position.y,
                                selectedNode->position.z);

                    ImGui::Text("Rotation:");
                    ImGui::Text("  %.2f, %.2f, %.2f",
                                selectedNode->rotation.x,
                                selectedNode->rotation.y,
                                selectedNode->rotation.z);

                    ImGui::Text("Scale:");
                    ImGui::Text("  %.2f, %.2f, %.2f",
                                selectedNode->scale.x,
                                selectedNode->scale.y,
                                selectedNode->scale.z);

                    if (!selectedNode->componentTypes.empty())
                    {
                        ImGui::Spacing();
                        ImGui::Text("Components:");
                        for (const auto& comp : selectedNode->componentTypes)
                        {
                            ImGui::Text("  - %s", comp.c_str());
                        }
                    }

                    if (!selectedNode->meshPath.empty())
                    {
                        ImGui::Spacing();
                        ImGui::Text("Mesh:");
                        ImGui::TextWrapped("  %s", selectedNode->meshPath.c_str());
                    }
                    if (!selectedNode->materialPath.empty())
                    {
                        ImGui::Spacing();
                        ImGui::Text("Material:");
                        ImGui::TextWrapped("  %s", selectedNode->materialPath.c_str());
                    }
                    if (!selectedNode->audioPath.empty())
                    {
                        ImGui::Spacing();
                        ImGui::Text("Audio:");
                        ImGui::TextWrapped("  %s", selectedNode->audioPath.c_str());
                    }
                }
            }
        }
    }

    void PrefabPreviewWindow::drawEntityTreePanel()
    {
        ImGui::Text("Entity Hierarchy");
        ImGui::Separator();

        if (loadFailed || !prefabLoaded)
        {
            ImGui::TextDisabled("No data");
            return;
        }

        drawEntityNode(rootEntity, rootEntity.name);
    }

    void PrefabPreviewWindow::drawEntityNode(const PrefabEntityNode& node, const std::string& path)
    {
        ImGui::PushID(path.c_str());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;

        if (selectedEntityPath.has_value() && *selectedEntityPath == path)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (node.children.empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        std::string label = node.name;
        if (!node.componentTypes.empty())
        {
            label += " [";
            for (size_t i = 0; i < node.componentTypes.size(); ++i)
            {
                if (i > 0) label += ", ";
                label += node.componentTypes[i];
            }
            label += "]";
        }

        bool nodeOpen = ImGui::TreeNodeEx(path.c_str(), flags, "%s", label.c_str());

        if (ImGui::IsItemClicked())
        {
            selectedEntityPath = path;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", node.position.x, node.position.y, node.position.z);
            ImGui::Text("Rotation: (%.2f, %.2f, %.2f)", node.rotation.x, node.rotation.y, node.rotation.z);
            ImGui::Text("Scale: (%.2f, %.2f, %.2f)", node.scale.x, node.scale.y, node.scale.z);
            ImGui::EndTooltip();
        }

        if (nodeOpen && !node.children.empty())
        {
            for (const auto& child : node.children)
            {
                std::string childPath = path + "/" + child.name;
                drawEntityNode(child, childPath);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void PrefabPreviewWindow::drawLoadingIndicator()
    {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        ImVec2 windowPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y), IM_COL32(30, 30, 30, 255));

        float centerX = windowPos.x + (availSize.x - 200.0f) * 0.5f;
        float centerY = windowPos.y + (availSize.y - 80.0f) * 0.5f;
        ImVec2 center(centerX + 100.0f, centerY + 20.0f);
        float startAngle = static_cast<float>(ImGui::GetTime()) * 4.0f, arcLength = 3.14159f * 1.3f;

        for (int i = 0; i < 24; ++i) {
            float t1 = static_cast<float>(i) / 24.0f, t2 = static_cast<float>(i + 1) / 24.0f;
            float a1 = startAngle + t1 * arcLength, a2 = startAngle + t2 * arcLength;
            ImU32 col = IM_COL32(100, 180, 255, static_cast<int>(255 * (1.0f - t1 * 0.7f)));
            drawList->AddLine(ImVec2(center.x + cosf(a1) * 16.0f, center.y + sinf(a1) * 16.0f),
                              ImVec2(center.x + cosf(a2) * 16.0f, center.y + sinf(a2) * 16.0f), col, 3.0f);
        }

        const char* statusText = loadingStatus.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        drawList->AddText(ImVec2(centerX + (200.0f - textSize.x) * 0.5f, centerY + 50.0f), IM_COL32(200, 200, 200, 255), statusText);
        ImGui::Dummy(availSize);
    }

    const PrefabEntityNode* PrefabPreviewWindow::findNodeByPath(const std::string& path) const
    {
        return findNodeByPathRecursive(rootEntity, rootEntity.name, path);
    }

    const PrefabEntityNode* PrefabPreviewWindow::findNodeByPathRecursive(
        const PrefabEntityNode& node,
        const std::string& currentPath,
        const std::string& targetPath) const
    {
        if (currentPath == targetPath)
        {
            return &node;
        }

        for (const auto& child : node.children)
        {
            std::string childPath = currentPath + "/" + child.name;
            const PrefabEntityNode* found = findNodeByPathRecursive(child, childPath, targetPath);
            if (found)
            {
                return found;
            }
        }

        return nullptr;
    }
}
