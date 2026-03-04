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

            // Parse transform if present
            if (entityJson.contains("transform"))
            {
                const auto& t = entityJson["transform"];
                if (t.contains("position"))
                {
                    const auto& pos = t["position"];
                    if (pos.is_array() && pos.size() >= 3)
                    {
                        node.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
                    }
                    else if (pos.is_object())
                    {
                        node.position = glm::vec3(pos.value("x", 0.0f), pos.value("y", 0.0f), pos.value("z", 0.0f));
                    }
                }
                if (t.contains("rotation"))
                {
                    const auto& rot = t["rotation"];
                    if (rot.is_array() && rot.size() >= 3)
                    {
                        node.rotation = glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
                    }
                    else if (rot.is_object())
                    {
                        node.rotation = glm::vec3(rot.value("x", 0.0f), rot.value("y", 0.0f), rot.value("z", 0.0f));
                    }
                }
                if (t.contains("scale"))
                {
                    const auto& scl = t["scale"];
                    if (scl.is_array() && scl.size() >= 3)
                    {
                        node.scale = glm::vec3(scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>());
                    }
                    else if (scl.is_object())
                    {
                        node.scale = glm::vec3(scl.value("x", 1.0f), scl.value("y", 1.0f), scl.value("z", 1.0f));
                    }
                }
            }

            // Collect component types and extract asset paths
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
                    {"rigidBody", "RigidBody"}
                };

                for (const auto& [key, displayName] : componentMap)
                {
                    if (componentsJson.contains(key))
                    {
                        node.componentTypes.push_back(displayName);
                        stats.counts[displayName]++;
                    }
                }

                // Extract asset paths
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

            // Recursively parse children
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

        ImGui::SetNextWindowSize(ImVec2(550, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(windowTitle.c_str(), &isOpen, ImGuiWindowFlags_NoCollapse))
        {
            if (isOpen)
            {
                float panelWidth = 150.0f;
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Info panel on the left
                ImGui::BeginChild("InfoPanel", ImVec2(panelWidth, contentSize.y), true);
                drawInfoPanel();
                ImGui::EndChild();

                ImGui::SameLine();

                // Entity tree panel on the right
                float treeWidth = contentSize.x - panelWidth - ImGui::GetStyle().ItemSpacing.x;
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

            // Validate structure
            if (!prefabJson.contains("prefab") || !prefabJson["prefab"].contains("entity"))
            {
                result.errorMessage = "Invalid prefab format";
                return result;
            }

            // Extract metadata
            result.version = prefabJson.value("version", "unknown");
            result.prefabName = prefabJson["prefab"].value("name", "Unnamed");

            // Parse entity tree and count components
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

        // Prefab metadata
        ImGui::Text("Name:");
        ImGui::TextWrapped("  %s", prefabName.c_str());
        ImGui::Spacing();
        ImGui::Text("Version: %s", prefabVersion.c_str());

        ImGui::Separator();
        ImGui::Spacing();

        // Entity count
        ImGui::Text("Entities: %u", componentStats.totalEntities);

        ImGui::Separator();
        ImGui::Spacing();

        // Component statistics
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

        // Show selected entity details if any
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

                    // Show asset paths
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

        // Highlight if selected
        if (selectedEntityPath.has_value() && *selectedEntityPath == path)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        // Make leaf nodes not expandable
        if (node.children.empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        // Build label with component hints
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

        // Handle selection
        if (ImGui::IsItemClicked())
        {
            selectedEntityPath = path;
        }

        // Tooltip with transform info
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", node.position.x, node.position.y, node.position.z);
            ImGui::Text("Rotation: (%.2f, %.2f, %.2f)", node.rotation.x, node.rotation.y, node.rotation.z);
            ImGui::Text("Scale: (%.2f, %.2f, %.2f)", node.scale.x, node.scale.y, node.scale.z);
            ImGui::EndTooltip();
        }

        // Recursively draw children if node is open
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

        // Semi-transparent dark overlay
        drawList->AddRectFilled(
            windowPos,
            ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y),
            IM_COL32(30, 30, 30, 255)
        );

        // Center content
        float contentWidth = 200.0f;
        float contentHeight = 80.0f;
        float centerX = windowPos.x + (availSize.x - contentWidth) * 0.5f;
        float centerY = windowPos.y + (availSize.y - contentHeight) * 0.5f;

        // Spinner animation
        float time = static_cast<float>(ImGui::GetTime());
        float spinnerRadius = 16.0f;
        float spinnerThickness = 3.0f;
        ImVec2 spinnerCenter(centerX + contentWidth * 0.5f, centerY + 20.0f);

        // Draw spinner arc
        int numSegments = 24;
        float startAngle = time * 4.0f;
        float arcLength = 3.14159f * 1.3f;

        for (int i = 0; i < numSegments; ++i)
        {
            float t1 = static_cast<float>(i) / static_cast<float>(numSegments);
            float t2 = static_cast<float>(i + 1) / static_cast<float>(numSegments);
            float angle1 = startAngle + t1 * arcLength;
            float angle2 = startAngle + t2 * arcLength;

            int alpha = static_cast<int>(255 * (1.0f - t1 * 0.7f));
            ImU32 segColor = IM_COL32(100, 180, 255, alpha);

            ImVec2 p1(spinnerCenter.x + cosf(angle1) * spinnerRadius,
                      spinnerCenter.y + sinf(angle1) * spinnerRadius);
            ImVec2 p2(spinnerCenter.x + cosf(angle2) * spinnerRadius,
                      spinnerCenter.y + sinf(angle2) * spinnerRadius);

            drawList->AddLine(p1, p2, segColor, spinnerThickness);
        }

        // Status message
        const char* statusText = loadingStatus.c_str();
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        ImVec2 textPos(centerX + (contentWidth - textSize.x) * 0.5f, centerY + 50.0f);
        drawList->AddText(textPos, IM_COL32(200, 200, 200, 255), statusText);

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
