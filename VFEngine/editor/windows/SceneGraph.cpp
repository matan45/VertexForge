#include "SceneGraph.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/MaterialEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include "resource/MeshResource.hpp"
#include <imgui.h>
#include <fstream>

namespace windows
{
    // Static member definition
    std::unordered_map<std::string, std::vector<std::string>> SceneGraph::submeshNameCache;

    SceneGraph::SceneGraph()
    {
        subscribeToEvents();
    }

    SceneGraph::~SceneGraph()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void SceneGraph::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&) {
                onSceneCleared();
            });
    }

    void SceneGraph::onSceneCleared()
    {
        selectedHandle = services::EntityHandle::invalid();
        submeshNameCache.clear();  // Clear cached submesh names
    }

    void SceneGraph::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Sync local selection with global selection (e.g., from viewport picking)
        events::scene::GetSelectedEntityQuery selectedQuery;
        auto globalSelected = dispatcher.query(selectedQuery);
        selectedHandle = globalSelected.value_or(services::EntityHandle::invalid());

        // Detect selection change and auto-expand to show selected entity
        if (selectedHandle.id != lastSelectedHandle.id && selectedHandle.isValid())
        {
            expandToSelection(selectedHandle);
        }
        lastSelectedHandle = selectedHandle;

        if (ImGui::Begin("SceneGraph"))
        {
            // Query root entity through event system
            events::scene::GetSceneHierarchyQuery hierarchyQuery;
            auto hierarchy = dispatcher.query(hierarchyQuery);

            if (!hierarchy.entities.empty())
            {
                // Root is first entity
                auto rootHandle = hierarchy.entities[0].handle;
                drawEntityNode(rootHandle);
            }

            // Right-click context menu for adding/removing entities
            if (ImGui::BeginPopupContextWindow())
            {
                if (ImGui::MenuItem("Add New Entity"))
                {
                    // Get root for default parent
                    events::scene::GetSceneHierarchyQuery rootQuery;
                    auto rootHierarchy = dispatcher.query(rootQuery);
                    services::EntityHandle parentHandle = selectedHandle.isValid()
                                                              ? selectedHandle
                                                              : (rootHierarchy.entities.empty()
                                                                     ? services::EntityHandle::invalid()
                                                                     : rootHierarchy.entities[0].handle);

                    events::scene::CreateEntityCommand cmd;
                    cmd.name = "New Entity";
                    cmd.parent = parentHandle.isValid() ? std::optional{parentHandle} : std::nullopt;
                    dispatcher.execute(cmd);
                }

                // Prevent deleting the root entity
                if (selectedHandle.isValid())
                {
                    events::scene::GetSceneHierarchyQuery rootCheckQuery;
                    auto rootCheckHierarchy = dispatcher.query(rootCheckQuery);
                    bool isRoot = !rootCheckHierarchy.entities.empty() &&
                        rootCheckHierarchy.entities[0].handle.id == selectedHandle.id;

                    if (!isRoot && ImGui::MenuItem("Remove Selected Entity"))
                    {
                        events::scene::DeleteEntityCommand cmd;
                        cmd.entity = selectedHandle;
                        dispatcher.execute(cmd);
                        selectedHandle = services::EntityHandle::invalid();
                    }
                }

                ImGui::EndPopup();
            }
        }
        ImGui::End();

        // Show the selected entity's components in the "Details" window
        if (ImGui::Begin("Details"))
        {
            if (selectedHandle.isValid())
            {
                drawDetails(selectedHandle);
            }
        }
        ImGui::End();
    }

    void SceneGraph::drawEntityNode(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::PushID(static_cast<int>(handle.id));

        // Query entity data
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        std::string entityName = entityDataOpt.has_value() ? entityDataOpt->name : "Unknown";
        
        if (expandedHandles.count(handle.id) > 0)
        {
            ImGui::SetNextItemOpen(true);
            expandedHandles.erase(handle.id);  // Only expand once
        }

        ImGuiTreeNodeFlags flags = (selectedHandle.id == handle.id) ? ImGuiTreeNodeFlags_Selected : 0;
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;

        bool nodeOpen = ImGui::TreeNodeEx((void*)handle.id, flags, "%s", entityName.c_str());

        // Select the entity when clicked
        if (ImGui::IsItemClicked())
        {
            selectedHandle = handle;

            // Publish selection through command
            events::scene::SelectEntityCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("DND_ENTITY_HANDLE", &handle, sizeof(services::EntityHandle));
            ImGui::Text("Move %s", entityName.c_str());
            ImGui::EndDragDropSource();
        }

        dragDropEntity(handle);

        // If the entity has children, recursively draw them
        if (nodeOpen)
        {
            if (entityDataOpt.has_value())
            {
                for (const auto& child : entityDataOpt->children)
                {
                    drawEntityNode(child);
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneGraph::drawDetails(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Query entity data
        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        if (!entityDataOpt.has_value())
        {
            return;
        }

        // Display name
        char buffer[256];
        std::strncpy(buffer, entityDataOpt->name.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        {
            events::scene::SetEntityNameCommand cmd;
            cmd.entity = handle;
            cmd.newName = buffer;
            dispatcher.execute(cmd);
        }

        ImGui::Separator();

        // Transform component
        events::scene::GetTransformQuery transformQuery;
        transformQuery.entity = handle;
        auto transformOpt = dispatcher.query(transformQuery);

        if (transformOpt.has_value())
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                services::TransformData transform = *transformOpt;
                bool changed = false;

                changed |= ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                changed |= ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
                changed |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);

                if (changed)
                {
                    events::scene::SetTransformCommand cmd;
                    cmd.entity = handle;
                    cmd.transform = transform;
                    dispatcher.execute(cmd);
                }
            }
        }

        // Camera component
        events::scene::HasCameraComponentQuery hasCameraQuery;
        hasCameraQuery.entity = handle;
        bool hasCamera = dispatcher.query(hasCameraQuery);

        if (hasCamera)
        {
            events::scene::GetCameraDataQuery cameraQuery;
            cameraQuery.entity = handle;
            auto cameraOpt = dispatcher.query(cameraQuery);

            if (cameraOpt.has_value())
            {
                ImGui::PushID("CameraComponent");

                bool removeCamera = false;

                pushComponentHeaderStyle();
                bool isOpen = ImGui::CollapsingHeader("##CameraHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImGui::SameLine();
                ImGui::Text("Camera");

                pushRemoveButtonStyle();
                if (ImGui::Button("x##RemoveCamera", ImVec2(18, 18)))
                {
                    removeCamera = true;
                }
                popRemoveButtonStyle();
                popComponentHeaderStyle();

                if (isOpen)
                {
                    ImGui::Indent(10.0f);

                    services::CameraData camera = *cameraOpt;
                    bool changed = false;

                    changed |= ImGui::DragFloat("Field of View", &camera.fieldOfView, 1.0f, 1.0, 180.0f);
                    changed |= ImGui::DragFloat("Near Plane", &camera.nearPlane,  0.01f, 0.01f, camera.farPlane - 0.1f);
                    changed |= ImGui::DragFloat("Far Plane", &camera.farPlane, 0.1f, camera.nearPlane + 0.1f, 10000.0f);
                    changed |= ImGui::DragFloat("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.1f, 10.0f);
                    changed |= ImGui::Checkbox("Perspective", &camera.isPerspective);
                    changed |= ImGui::Checkbox("Primary Camera", &camera.isPrimary);
                    changed |= ImGui::Checkbox("Show Frustum", &camera.showFrustum);

                    if (!camera.isPerspective)
                    {
                        changed |= ImGui::DragFloat("Orthographic Size", &camera.orthoSize, 0.1f, 0.1f, 1000.0f);
                    }

                    if (changed)
                    {
                        events::scene::SetCameraDataCommand cmd;
                        cmd.entity = handle;
                        cmd.cameraData = camera;
                        dispatcher.execute(cmd);
                    }

                    ImGui::Unindent(10.0f);
                }

                ImGui::PopID();

                if (removeCamera)
                {
                    events::scene::RemoveCameraComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }
        }

        // IBL component
        events::scene::HasIBLComponentQuery hasIBLQuery;
        hasIBLQuery.entity = handle;
        bool hasIBL = dispatcher.query(hasIBLQuery);

        if (hasIBL)
        {
            events::scene::GetIBLDataQuery iblQuery;
            iblQuery.entity = handle;
            auto iblOpt = dispatcher.query(iblQuery);

            if (iblOpt.has_value())
            {
                ImGui::PushID("IBLComponent");

                bool removeIBL = false;

                pushComponentHeaderStyle();
                bool isOpen = ImGui::CollapsingHeader("##IBLHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImGui::SameLine();
                ImGui::Text("IBL");

                pushRemoveButtonStyle();
                if (ImGui::Button("x##RemoveIBL", ImVec2(18, 18)))
                {
                    removeIBL = true;
                }
                popRemoveButtonStyle();
                popComponentHeaderStyle();

                if (isOpen)
                {
                    ImGui::Indent(10.0f);

                    // Display IBL file path (read-only)
                    ImGui::Text("File: %s", iblOpt->fileName.c_str());

                    ImGui::Unindent(10.0f);
                }

                ImGui::PopID();

                if (removeIBL)
                {
                    events::scene::RemoveIBLComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);

                    // Also remove from renderer
                    events::render::RemoveIBLCommand removeRenderCmd;
                    dispatcher.execute(removeRenderCmd);
                }
            }
        }
        
        events::scene::HasMeshComponentQuery hasMeshQuery;
        hasMeshQuery.entity = handle;
        bool hasMesh = dispatcher.query(hasMeshQuery);

        if (hasMesh)
        {
            events::scene::GetMeshDataQuery meshQuery;
            meshQuery.entity = handle;
            auto meshOpt = dispatcher.query(meshQuery);

            if (meshOpt.has_value())
            {
                ImGui::PushID("MeshComponent");

                bool removeMesh = false;

                pushComponentHeaderStyle();
                bool isOpen = ImGui::CollapsingHeader("##MeshHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImGui::SameLine();
                ImGui::Text("Mesh");

                pushRemoveButtonStyle();
                if (ImGui::Button("x##RemoveMesh", ImVec2(18, 18)))
                {
                    removeMesh = true;
                }
                popRemoveButtonStyle();
                popComponentHeaderStyle();

                if (isOpen)
                {
                    ImGui::Indent(10.0f);

                    // Display current mesh path
                    if (!meshOpt->meshPath.empty())
                    {
                        // Extract filename for display
                        std::string filename = meshOpt->meshPath;
                        auto lastSlash = filename.find_last_of("/\\");
                        if (lastSlash != std::string::npos)
                        {
                            filename = filename.substr(lastSlash + 1);
                        }
                        ImGui::Text("Mesh: %s", filename.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("No mesh selected");
                    }

                    // Select Mesh button
                    if (ImGui::Button("Select Mesh"))
                    {
                        nfd::FileDialog fileDialog;
                        std::string path = fileDialog.openFileDialog(
                            {{L"VF Mesh Files (*.vfmesh)", L"*.vfmesh"}});
                        if (!path.empty())
                        {
                            // Verify file exists before setting
                            std::ifstream file(path);
                            if (file.good())
                            {
                                file.close();
                                events::scene::SetMeshDataCommand cmd;
                                cmd.entity = handle;
                                cmd.meshData.meshPath = path;
                                cmd.meshData.showBoundingBox = meshOpt->showBoundingBox;
                                dispatcher.execute(cmd);
                            }
                            else
                            {
                                vfLogError("Selected mesh file does not exist or cannot be read: {}", path);
                            }
                        }
                    }

                    // Show Bounding Box checkbox
                    bool showBoundingBox = meshOpt->showBoundingBox;
                    if (ImGui::Checkbox("Show Bounding Box", &showBoundingBox))
                    {
                        events::scene::SetMeshDataCommand cmd;
                        cmd.entity = handle;
                        cmd.meshData.meshPath = meshOpt->meshPath;
                        cmd.meshData.showBoundingBox = showBoundingBox;
                        dispatcher.execute(cmd);
                    }

                    // Static flag for BVH optimization
                    events::scene::IsEntityStaticQuery staticQuery;
                    staticQuery.entity = handle;
                    bool isStatic = dispatcher.query(staticQuery);

                    if (ImGui::Checkbox("Is Static", &isStatic))
                    {
                        events::scene::SetEntityStaticCommand cmd;
                        cmd.entity = handle;
                        cmd.isStatic = isStatic;
                        dispatcher.execute(cmd);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Static meshes are placed in a BVH tree that rebuilds less frequently.\nUncheck for meshes that move often.");
                    }

                    ImGui::Unindent(10.0f);
                }

                ImGui::PopID();

                if (removeMesh)
                {
                    events::scene::RemoveMeshComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }
        }

        // ========== MATERIAL COMPONENT ==========
        // Only show if entity has a mesh
        if (hasMesh)
        {
            // Query mesh data for the material section
            events::scene::GetMeshDataQuery meshDataQuery;
            meshDataQuery.entity = handle;
            auto meshDataOpt = dispatcher.query(meshDataQuery);

            if (!meshDataOpt.has_value() || meshDataOpt->meshPath.empty())
            {
                // No mesh data or no path - skip material section
            }
            else
            {
            events::material::HasMaterialComponentQuery hasMaterialQuery;
            hasMaterialQuery.entity = handle;
            bool hasMaterial = dispatcher.query(hasMaterialQuery);

            // Auto-add MaterialComponent if entity has mesh but no material
            if (!hasMaterial)
            {
                events::material::AddMaterialComponentCommand addMatCmd;
                addMatCmd.entity = handle;
                dispatcher.execute(addMatCmd);
                hasMaterial = true;
            }

            if (hasMaterial)
            {
                events::material::GetMaterialDataQuery matQuery;
                matQuery.entity = handle;
                auto matOpt = dispatcher.query(matQuery);

                if (matOpt.has_value())
                {
                    ImGui::PushID("MaterialComponent");

                    pushComponentHeaderStyle();
                    bool isMatOpen = ImGui::CollapsingHeader("##MaterialHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                    ImGui::SameLine();
                    ImGui::Text("Materials");
                    popComponentHeaderStyle();

                    if (isMatOpen)
                    {
                        ImGui::Indent(10.0f);

                        // Default Material
                        ImGui::Text("Default Material:");
                        std::string defaultMatDisplay = matOpt->defaultMaterial.empty() ? "(None)" : matOpt->defaultMaterial;
                        if (defaultMatDisplay.length() > 35)
                        {
                            defaultMatDisplay = "..." + defaultMatDisplay.substr(defaultMatDisplay.length() - 32);
                        }
                        ImGui::TextDisabled("%s", defaultMatDisplay.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button("Browse##DefaultMat"))
                        {
                            nfd::FileDialog fileDialog;
                            std::string path = fileDialog.openFileDialog({{L"VF Material (*.vfMat)", L"*.vfMat"}});
                            if (!path.empty())
                            {
                                events::material::SetDefaultMaterialCommand cmd;
                                cmd.entity = handle;
                                cmd.materialPath = path;
                                dispatcher.execute(cmd);
                            }
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear##DefaultMat"))
                        {
                            events::material::SetDefaultMaterialCommand cmd;
                            cmd.entity = handle;
                            cmd.materialPath = "";
                            dispatcher.execute(cmd);
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        // Collapsible submesh materials section
                        if (ImGui::CollapsingHeader("Submesh Materials", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::Indent(10.0f);

                            // Check cache first
                            const std::string& meshPath = meshDataOpt->meshPath;
                            auto cacheIt = submeshNameCache.find(meshPath);
                            if (cacheIt == submeshNameCache.end())
                            {
                                // Load and cache submesh names
                                try
                                {
                                    resource::MeshesData meshData = resource::MeshResource::loadMesh(meshPath);
                                    std::vector<std::string> names;
                                    for (size_t i = 0; i < meshData.meshes.size(); ++i)
                                    {
                                        const auto& submesh = meshData.meshes[i];
                                        names.push_back(submesh.name.empty() ? "Submesh_" + std::to_string(i) : submesh.name);
                                    }
                                    cacheIt = submeshNameCache.emplace(meshPath, std::move(names)).first;
                                }
                                catch (const std::exception& e)
                                {
                                    ImGui::TextDisabled("Failed to load mesh: %s", e.what());
                                    cacheIt = submeshNameCache.emplace(meshPath, std::vector<std::string>{}).first;
                                }
                            }

                            const auto& submeshNames = cacheIt->second;
                            if (submeshNames.empty())
                            {
                                ImGui::TextDisabled("No submeshes found");
                            }
                            else
                            {
                                for (size_t i = 0; i < submeshNames.size(); ++i)
                            {
                                const std::string& submeshName = submeshNames[i];

                                ImGui::PushID(static_cast<int>(i));

                                // Get current material for this submesh
                                auto it = matOpt->subMeshMaterials.find(submeshName);
                                std::string currentMat = (it != matOpt->subMeshMaterials.end()) ? it->second : "";

                                // Display submesh name
                                ImGui::BulletText("%s", submeshName.c_str());
                                ImGui::Indent(20.0f);

                                // Show current material
                                std::string matDisplay = currentMat.empty() ? "(Default)" : currentMat;
                                if (matDisplay.length() > 30)
                                {
                                    matDisplay = "..." + matDisplay.substr(matDisplay.length() - 27);
                                }
                                ImGui::TextDisabled("%s", matDisplay.c_str());

                                ImGui::SameLine();
                                std::string browseId = "Browse##submesh" + std::to_string(i);
                                if (ImGui::Button(browseId.c_str()))
                                {
                                    nfd::FileDialog fileDialog;
                                    std::string path = fileDialog.openFileDialog({{L"VF Material (*.vfMat)", L"*.vfMat"}});
                                    if (!path.empty())
                                    {
                                        events::material::SetSubMeshMaterialCommand cmd;
                                        cmd.entity = handle;
                                        cmd.submeshName = submeshName;
                                        cmd.materialPath = path;
                                        dispatcher.execute(cmd);
                                    }
                                }

                                ImGui::SameLine();
                                std::string clearId = "Clear##submesh" + std::to_string(i);
                                if (ImGui::Button(clearId.c_str()))
                                {
                                    events::material::SetSubMeshMaterialCommand cmd;
                                    cmd.entity = handle;
                                    cmd.submeshName = submeshName;
                                    cmd.materialPath = "";  // Clear = use default
                                    dispatcher.execute(cmd);
                                }

                                ImGui::Unindent(20.0f);
                                ImGui::PopID();
                            }
                            }

                            ImGui::Unindent(10.0f);
                        } // end CollapsingHeader

                        ImGui::Unindent(10.0f);
                    }

                    ImGui::PopID();
                }
            }
            } // end else (meshDataOpt has value)
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Center the button and style it
        float buttonWidth = ImGui::GetContentRegionAvail().x * 0.6f;
        float buttonOffset = (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonOffset);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 28)))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Popup menu for adding components
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            ImGui::TextDisabled("Components");
            ImGui::Separator();

            if (!hasCamera)
            {
                if (ImGui::Selectable("  Camera"))
                {
                    events::scene::AddCameraComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }

            if (!hasMesh)
            {
                if (ImGui::Selectable("  Mesh"))
                {
                    events::scene::AddMeshComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
            }

            if (hasCamera && hasMesh)
            {
                ImGui::TextDisabled("All components added");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }

    void SceneGraph::dragDropEntity(services::EntityHandle handle)
    {
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ENTITY_HANDLE"))
            {
                services::EntityHandle draggedHandle = *(services::EntityHandle*)payload->Data;

                if (draggedHandle.id != handle.id)
                {
                    auto& dispatcher = events::EventDispatcher::instance();

                    events::scene::ReparentEntityCommand cmd;
                    cmd.entity = draggedHandle;
                    cmd.newParent = handle;
                    dispatcher.execute(cmd);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneGraph::pushComponentHeaderStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    }

    void SceneGraph::popComponentHeaderStyle()
    {
        ImGui::PopStyleColor(3);
    }

    void SceneGraph::pushRemoveButtonStyle()
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    }

    void SceneGraph::popRemoveButtonStyle()
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    void SceneGraph::expandToSelection(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Clear previous expansion state
        expandedHandles.clear();

        // Walk up the parent chain and collect all ancestors
        services::EntityHandle current = handle;
        while (current.isValid())
        {
            events::scene::GetEntityQuery entityQuery;
            entityQuery.entity = current;
            auto entityDataOpt = dispatcher.query(entityQuery);

            if (!entityDataOpt.has_value() || !entityDataOpt->parent.has_value())
            {
                break;  // Reached root or invalid entity
            }

            // Add parent to expand set
            services::EntityHandle parentHandle = entityDataOpt->parent.value();
            expandedHandles.insert(parentHandle.id);

            // Move up to parent
            current = parentHandle;
        }
    }
}
