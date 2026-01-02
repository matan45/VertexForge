#include "EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/MaterialEvents.hpp"
#include "events/AudioEvents.hpp"
#include "events/ScriptingEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "print/EditorLogger.hpp"
#include "resource/MeshResource.hpp"
#include "data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <imgui.h>
#include <fstream>

namespace windows
{
    EntityDetailsPanel::EntityDetailsPanel()
    {
        subscribeToEvents();
    }

    EntityDetailsPanel::~EntityDetailsPanel()
    {
        events::EventDispatcher::instance().unsubscribe(sceneClearedToken);
    }

    void EntityDetailsPanel::subscribeToEvents()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                onSceneCleared();
            });
    }

    void EntityDetailsPanel::onSceneCleared()
    {
        submeshNameCache.clear();
        audioPreviewHandles.clear();
    }

    void EntityDetailsPanel::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Query current selection from global state
        events::scene::GetSelectedEntityQuery selectedQuery;
        auto selectedHandle = dispatcher.query(selectedQuery).value_or(services::EntityHandle::invalid());

        if (ImGui::Begin("Details"))
        {
            if (selectedHandle.isValid())
            {
                drawDetails(selectedHandle);
            }
        }
        ImGui::End();
    }

    void EntityDetailsPanel::drawDetails(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetEntityQuery entityQuery;
        entityQuery.entity = handle;
        auto entityDataOpt = dispatcher.query(entityQuery);

        if (!entityDataOpt.has_value())
            return;

        drawEntityName(handle, entityDataOpt->name);
        drawEntityActiveCheckbox(handle, entityDataOpt->isActive);
        ImGui::Separator();

        drawTransformComponent(handle);
        bool hasCamera = drawCameraComponent(handle);
        drawIBLComponent(handle);
        bool hasMesh = drawMeshComponent(handle);

        if (hasMesh)
            drawMaterialComponent(handle);

        bool hasAudio2D = drawAudioSource2DComponent(handle);
        bool hasAudio3D = drawAudioSource3DComponent(handle);
        bool hasScript = drawScriptComponent(handle);

        drawAddComponentButton(handle, hasCamera, hasMesh, hasAudio2D, hasAudio3D, hasScript);
    }

    void EntityDetailsPanel::drawEntityName(services::EntityHandle handle, const std::string& currentName)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        char buffer[256];
        std::strncpy(buffer, currentName.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
        {
            events::scene::SetEntityNameCommand cmd;
            cmd.entity = handle;
            cmd.newName = buffer;
            dispatcher.execute(cmd);
        }
    }

    void EntityDetailsPanel::drawEntityActiveCheckbox(services::EntityHandle handle, bool isActive)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (ImGui::Checkbox("Active", &isActive))
        {
            events::scene::SetEntityActiveCommand cmd;
            cmd.entity = handle;
            cmd.isActive = isActive;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("When disabled, the entity and all its components are inactive");
        }
    }

    void EntityDetailsPanel::drawTransformComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

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

                // Static flag - affects BVH placement, physics, and lights
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
                    ImGui::SetTooltip(
                        "Static entities are placed in a BVH that rebuilds less frequently.\n"
                        "Uncheck for entities that move often (affects rendering, physics, and lights).");
                }
            }
        }
    }

    bool EntityDetailsPanel::drawCameraComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasCameraComponentQuery hasCameraQuery;
        hasCameraQuery.entity = handle;
        bool hasCamera = dispatcher.query(hasCameraQuery);

        if (!hasCamera)
            return false;

        events::scene::GetCameraDataQuery cameraQuery;
        cameraQuery.entity = handle;
        auto cameraOpt = dispatcher.query(cameraQuery);

        if (!cameraOpt.has_value())
            return true;

        ImGui::PushID("CameraComponent");

        bool removeCamera = false;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##CameraHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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
            changed |= ImGui::DragFloat("Near Plane", &camera.nearPlane, 0.01f, 0.01f, camera.farPlane - 0.1f);
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

        return true;
    }

    void EntityDetailsPanel::drawIBLComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasIBLComponentQuery hasIBLQuery;
        hasIBLQuery.entity = handle;
        bool hasIBL = dispatcher.query(hasIBLQuery);

        if (!hasIBL)
            return;

        events::scene::GetIBLDataQuery iblQuery;
        iblQuery.entity = handle;
        auto iblOpt = dispatcher.query(iblQuery);

        if (!iblOpt.has_value())
            return;

        ImGui::PushID("IBLComponent");

        bool removeIBL = false;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##IBLHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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
            ImGui::Text("File: %s", iblOpt->fileName.c_str());
            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeIBL)
        {
            events::scene::RemoveIBLComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            events::render::RemoveIBLCommand removeRenderCmd;
            dispatcher.execute(removeRenderCmd);
        }
    }

    bool EntityDetailsPanel::drawMeshComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasMeshComponentQuery hasMeshQuery;
        hasMeshQuery.entity = handle;
        bool hasMesh = dispatcher.query(hasMeshQuery);

        if (!hasMesh)
            return false;

        events::scene::GetMeshDataQuery meshQuery;
        meshQuery.entity = handle;
        auto meshOpt = dispatcher.query(meshQuery);

        if (!meshOpt.has_value())
            return true;

        ImGui::PushID("MeshComponent");

        bool removeMesh = false;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##MeshHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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

            if (!meshOpt->meshPath.empty())
            {
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

            if (ImGui::Button("Select Mesh"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Mesh Files (*.vfmesh)", L"*.vfmesh"}});
                if (!path.empty())
                {
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

            bool showBoundingBox = meshOpt->showBoundingBox;
            if (ImGui::Checkbox("Show Bounding Box", &showBoundingBox))
            {
                events::scene::SetMeshDataCommand cmd;
                cmd.entity = handle;
                cmd.meshData.meshPath = meshOpt->meshPath;
                cmd.meshData.showBoundingBox = showBoundingBox;
                dispatcher.execute(cmd);
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

        return true;
    }

    void EntityDetailsPanel::drawMaterialComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::GetMeshDataQuery meshDataQuery;
        meshDataQuery.entity = handle;
        auto meshDataOpt = dispatcher.query(meshDataQuery);

        if (!meshDataOpt.has_value() || meshDataOpt->meshPath.empty())
            return;

        events::material::HasMaterialComponentQuery hasMaterialQuery;
        hasMaterialQuery.entity = handle;
        bool hasMaterial = dispatcher.query(hasMaterialQuery);

        if (!hasMaterial)
        {
            events::material::AddMaterialComponentCommand addMatCmd;
            addMatCmd.entity = handle;
            dispatcher.execute(addMatCmd);
            hasMaterial = true;
        }

        if (!hasMaterial)
            return;

        events::material::GetMaterialDataQuery matQuery;
        matQuery.entity = handle;
        auto matOpt = dispatcher.query(matQuery);

        if (!matOpt.has_value())
            return;

        ImGui::PushID("MaterialComponent");

        pushComponentHeaderStyle();
        bool isMatOpen = ImGui::CollapsingHeader("##MaterialHeader",
                                                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

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
                std::string path = fileDialog.openFileDialog({
                    {L"VF Material (*.vfMat, *.vfMatInstance)", L"*.vfMat;*.vfMatInstance"}
                });
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

                const std::string& meshPath = meshDataOpt->meshPath;
                auto cacheIt = submeshNameCache.find(meshPath);
                if (cacheIt == submeshNameCache.end())
                {
                    try
                    {
                        resource::MeshesData meshData = resource::MeshResource::loadMesh(meshPath);
                        std::vector<std::string> names;
                        for (size_t i = 0; i < meshData.meshes.size(); ++i)
                        {
                            const auto& submesh = meshData.meshes[i];
                            names.push_back(submesh.name.empty() ? "SubMesh_" + std::to_string(i) : submesh.name);
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

                        auto it = matOpt->subMeshMaterials.find(submeshName);
                        std::string currentMat = (it != matOpt->subMeshMaterials.end()) ? it->second : "";

                        ImGui::BulletText("%s", submeshName.c_str());
                        ImGui::Indent(20.0f);

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
                            std::string path = fileDialog.openFileDialog({
                                {L"VF Material (*.vfMat, *.vfMatInstance)", L"*.vfMat;*.vfMatInstance"}
                            });
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
                            cmd.materialPath = "";
                            dispatcher.execute(cmd);
                        }

                        ImGui::Unindent(20.0f);
                        ImGui::PopID();
                    }
                }

                ImGui::Unindent(10.0f);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();
    }

    bool EntityDetailsPanel::drawAudioSource2DComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasAudioSource2DComponentQuery hasAudioQuery;
        hasAudioQuery.entity = handle;
        bool hasAudioSource = dispatcher.query(hasAudioQuery);

        if (!hasAudioSource)
            return false;

        events::scene::GetAudioSource2DDataQuery audioQuery;
        audioQuery.entity = handle;
        auto audioOpt = dispatcher.query(audioQuery);

        if (!audioOpt.has_value())
            return true;

        ImGui::PushID("AudioSource2DComponent");

        bool removeAudioSource = false;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##AudioSource2DHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Audio Source 2D (Streaming)");

        pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAudioSource2D", ImVec2(18, 18)))
        {
            removeAudioSource = true;
        }
        popRemoveButtonStyle();
        popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::AudioSource2DData audioData = *audioOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: background music, ambient sounds");
            ImGui::Spacing();

            if (!audioData.audioFilePath.empty())
            {
                std::string filename = audioData.audioFilePath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }
                ImGui::Text("File: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No audio file selected");
            }

            if (ImGui::Button("Select Audio File##2D"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Audio Files (*.vfAudio)", L"*.vfAudio"}});
                if (!path.empty())
                {
                    std::ifstream file(path);
                    if (file.good())
                    {
                        file.close();
                        audioData.audioFilePath = path;
                        changed = true;
                    }
                    else
                    {
                        vfLogError("Selected audio file does not exist or cannot be read: {}", path);
                    }
                }
            }

            ImGui::SameLine();
            if (audioData.audioFilePath.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##Audio2D"))
            {
                audioData.audioFilePath = "";
                changed = true;
            }
            if (audioData.audioFilePath.empty()) ImGui::EndDisabled();

            ImGui::Spacing();

            if (ImGui::SliderFloat("Volume##2D", &audioData.volume, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }

            if (ImGui::SliderFloat("Pitch##2D", &audioData.pitch, 0.5f, 2.0f, "%.2f"))
            {
                changed = true;
            }

            ImGui::Spacing();

            if (ImGui::Checkbox("Loop##2D", &audioData.loop))
            {
                changed = true;
            }

            if (changed)
            {
                events::scene::SetAudioSource2DDataCommand cmd;
                cmd.entity = handle;
                cmd.audioData = audioData;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Check if we have an active preview handle for this entity (use unique key for 2D)
            uint64_t previewKey = handle.id * 2; // 2D uses even keys
            auto previewIt = audioPreviewHandles.find(previewKey);
            bool hasPreviewHandle = previewIt != audioPreviewHandles.end();

            // Clean up invalid handles
            if (hasPreviewHandle && !previewIt->second.isValid())
            {
                audioPreviewHandles.erase(previewIt);
                hasPreviewHandle = false;
                previewIt = audioPreviewHandles.end();
            }

            // Check if currently playing
            bool isCurrentlyPlaying = false;
            if (hasPreviewHandle)
            {
                events::audio::IsSoundPlayingQuery playingQuery;
                playingQuery.handle = previewIt->second;
                isCurrentlyPlaying = dispatcher.query(playingQuery);
            }

            // Track if paused (has handle but not playing)
            bool isPaused = hasPreviewHandle && !isCurrentlyPlaying;

            // Play button - only enabled if audio file is set and not already playing
            bool canPlay = !audioData.audioFilePath.empty() && !isCurrentlyPlaying;
            if (!canPlay) ImGui::BeginDisabled();
            if (ImGui::Button("Play##2D", ImVec2(60, 0)))
            {
                if (isPaused)
                {
                    // Resume from paused position
                    events::audio::ResumeSoundCommand resumeCmd;
                    resumeCmd.handle = previewIt->second;
                    dispatcher.execute(resumeCmd);
                }
                else
                {
                    // Start new playback
                    events::audio::PlayStreamingSoundCommand playCmd;
                    playCmd.path = audioData.audioFilePath;
                    playCmd.params.volume = audioData.volume;
                    playCmd.params.pitch = audioData.pitch;
                    playCmd.params.loop = audioData.loop;

                    services::AudioHandle newHandle = dispatcher.execute(playCmd);
                    audioPreviewHandles[previewKey] = newHandle;
                }
            }
            if (!canPlay) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!isCurrentlyPlaying) ImGui::BeginDisabled();
            if (ImGui::Button("Pause##2D", ImVec2(60, 0)))
            {
                if (hasPreviewHandle)
                {
                    events::audio::PauseSoundCommand pauseCmd;
                    pauseCmd.handle = previewIt->second;
                    dispatcher.execute(pauseCmd);
                }
            }
            if (!isCurrentlyPlaying) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!hasPreviewHandle) ImGui::BeginDisabled();
            if (ImGui::Button("Stop##2D", ImVec2(60, 0)))
            {
                if (hasPreviewHandle)
                {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = previewIt->second;
                    dispatcher.execute(stopCmd);
                    audioPreviewHandles.erase(previewKey);
                }
            }
            if (!hasPreviewHandle) ImGui::EndDisabled();

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeAudioSource)
        {
            events::scene::RemoveAudioSource2DComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            uint64_t previewKey = handle.id * 2;
            audioPreviewHandles.erase(previewKey);
        }

        return true;
    }

    bool EntityDetailsPanel::drawAudioSource3DComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::HasAudioSource3DComponentQuery hasAudioQuery;
        hasAudioQuery.entity = handle;
        bool hasAudioSource = dispatcher.query(hasAudioQuery);

        if (!hasAudioSource)
            return false;

        events::scene::GetAudioSource3DDataQuery audioQuery;
        audioQuery.entity = handle;
        auto audioOpt = dispatcher.query(audioQuery);

        if (!audioOpt.has_value())
            return true;

        ImGui::PushID("AudioSource3DComponent");

        bool removeAudioSource = false;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##AudioSource3DHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Audio Source 3D (Spatial)");

        pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAudioSource3D", ImVec2(18, 18)))
        {
            removeAudioSource = true;
        }
        popRemoveButtonStyle();
        popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::AudioSource3DData audioData = *audioOpt;
            bool changed = false;

            ImGui::TextDisabled("Use for: spatial sound effects");
            ImGui::Spacing();

            if (!audioData.audioFilePath.empty())
            {
                std::string filename = audioData.audioFilePath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }
                ImGui::Text("File: %s", filename.c_str());
            }
            else
            {
                ImGui::TextDisabled("No audio file selected");
            }

            if (ImGui::Button("Select Audio File##3D"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"VF Audio Files (*.vfAudio)", L"*.vfAudio"}});
                if (!path.empty())
                {
                    std::ifstream file(path);
                    if (file.good())
                    {
                        file.close();
                        audioData.audioFilePath = path;
                        changed = true;
                    }
                    else
                    {
                        vfLogError("Selected audio file does not exist or cannot be read: {}", path);
                    }
                }
            }

            ImGui::SameLine();
            if (audioData.audioFilePath.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("Clear##Audio3D"))
            {
                audioData.audioFilePath = "";
                changed = true;
            }
            if (audioData.audioFilePath.empty()) ImGui::EndDisabled();

            ImGui::Spacing();

            if (ImGui::SliderFloat("Volume##3D", &audioData.volume, 0.0f, 1.0f, "%.2f"))
            {
                changed = true;
            }

            if (ImGui::SliderFloat("Pitch##3D", &audioData.pitch, 0.5f, 2.0f, "%.2f"))
            {
                changed = true;
            }

            ImGui::Spacing();

            if (ImGui::Checkbox("Loop##3D", &audioData.loop))
            {
                changed = true;
            }

            ImGui::Spacing();
            ImGui::Text("Spatial Settings:");
            ImGui::Indent(10.0f);

            if (ImGui::SliderFloat("Min Distance##3D", &audioData.minDistance, 0.1f, 50.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Distance at which volume starts to attenuate");
            }

            if (ImGui::SliderFloat("Max Distance##3D", &audioData.maxDistance, 1.0f, 500.0f, "%.1f"))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Distance at which volume reaches minimum");
            }

            ImGui::Spacing();
            if (ImGui::Checkbox("Show Debug Spheres##3D", &audioData.showDebugSpheres))
            {
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draw wireframe spheres for min/max distance");
            }

            ImGui::Unindent(10.0f);

            if (changed)
            {
                events::scene::SetAudioSource3DDataCommand cmd;
                cmd.entity = handle;
                cmd.audioData = audioData;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Check if we have an active preview handle for this entity (use unique key for 3D)
            uint64_t previewKey = handle.id * 2 + 1; // 3D uses odd keys
            auto previewIt = audioPreviewHandles.find(previewKey);
            bool hasPreviewHandle = previewIt != audioPreviewHandles.end();

            // Clean up invalid handles
            if (hasPreviewHandle && !previewIt->second.isValid())
            {
                audioPreviewHandles.erase(previewIt);
                hasPreviewHandle = false;
                previewIt = audioPreviewHandles.end();
            }

            // Check if currently playing
            bool isCurrentlyPlaying = false;
            if (hasPreviewHandle)
            {
                events::audio::IsSoundPlayingQuery playingQuery;
                playingQuery.handle = previewIt->second;
                isCurrentlyPlaying = dispatcher.query(playingQuery);
            }

            // Track if paused (has handle but not playing)
            bool isPaused = hasPreviewHandle && !isCurrentlyPlaying;

            // Play button - only enabled if audio file is set and not already playing
            bool canPlay = !audioData.audioFilePath.empty() && !isCurrentlyPlaying;
            if (!canPlay) ImGui::BeginDisabled();
            if (ImGui::Button("Play##3D", ImVec2(60, 0)))
            {
                if (isPaused)
                {
                    // Resume from paused position
                    events::audio::ResumeSoundCommand resumeCmd;
                    resumeCmd.handle = previewIt->second;
                    dispatcher.execute(resumeCmd);
                }
                else
                {
                    // Start new playback
                    // 3D audio: use cached resource audio at entity position
                    events::scene::GetTransformQuery transformQuery;
                    transformQuery.entity = handle;
                    auto transformOpt = dispatcher.query(transformQuery);
                    glm::vec3 position = transformOpt.has_value() ? transformOpt->position : glm::vec3(0.0f);

                    events::audio::PlaySound3DCommand playCmd;
                    playCmd.path = audioData.audioFilePath;
                    playCmd.position = position;
                    playCmd.params.volume = audioData.volume;
                    playCmd.params.pitch = audioData.pitch;
                    playCmd.params.loop = audioData.loop;
                    playCmd.params.minDistance = audioData.minDistance;
                    playCmd.params.maxDistance = audioData.maxDistance;

                    services::AudioHandle newHandle = dispatcher.execute(playCmd);
                    audioPreviewHandles[previewKey] = newHandle;
                }
            }
            if (!canPlay) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!isCurrentlyPlaying) ImGui::BeginDisabled();
            if (ImGui::Button("Pause##3D", ImVec2(60, 0)))
            {
                if (hasPreviewHandle)
                {
                    events::audio::PauseSoundCommand pauseCmd;
                    pauseCmd.handle = previewIt->second;
                    dispatcher.execute(pauseCmd);
                }
            }
            if (!isCurrentlyPlaying) ImGui::EndDisabled();

            ImGui::SameLine();

            if (!hasPreviewHandle) ImGui::BeginDisabled();
            if (ImGui::Button("Stop##3D", ImVec2(60, 0)))
            {
                if (hasPreviewHandle)
                {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = previewIt->second;
                    dispatcher.execute(stopCmd);
                    audioPreviewHandles.erase(previewKey);
                }
            }
            if (!hasPreviewHandle) ImGui::EndDisabled();

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeAudioSource)
        {
            events::scene::RemoveAudioSource3DComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);

            uint64_t previewKey = handle.id * 2 + 1;
            audioPreviewHandles.erase(previewKey);
        }

        return true;
    }

    bool EntityDetailsPanel::drawScriptComponent(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Check if entity has ScriptComponent by checking GetScriptDataQuery
        events::scripting::GetScriptDataQuery scriptQuery;
        scriptQuery.entity = handle;
        auto scriptOpt = dispatcher.query(scriptQuery);

        // If no ScriptComponent, return false
        if (!scriptOpt.has_value())
            return false;

        // Get all script paths attached to this entity
        events::scripting::GetScriptPathsQuery pathsQuery;
        pathsQuery.entity = handle;
        auto scriptPaths = dispatcher.query(pathsQuery);

        ImGui::PushID("ScriptComponent");

        bool removeAllScripts = false;
        std::string scriptToRemove;

        pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##ScriptHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Scripts (%zu)", scriptPaths.size());

        pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveAllScripts", ImVec2(18, 18)))
        {
            removeAllScripts = true;
        }
        popRemoveButtonStyle();
        popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            // Display each script
            for (size_t i = 0; i < scriptPaths.size(); ++i)
            {
                const auto& scriptPath = scriptPaths[i];
                ImGui::PushID(static_cast<int>(i));

                // Get script filename
                std::string filename = scriptPath;
                auto lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos)
                {
                    filename = filename.substr(lastSlash + 1);
                }

                // Script entry with enabled checkbox and remove button
                events::scripting::IsScriptEnabledQuery enabledQuery;
                enabledQuery.entity = handle;
                enabledQuery.scriptPath = scriptPath;
                bool isEnabled = dispatcher.query(enabledQuery);

                if (ImGui::Checkbox("##ScriptEnabled", &isEnabled))
                {
                    events::scripting::SetScriptEnabledCommand cmd;
                    cmd.entity = handle;
                    cmd.scriptPath = scriptPath;
                    cmd.enabled = isEnabled;
                    dispatcher.execute(cmd);
                }

                ImGui::SameLine();
                ImGui::Text("%s", filename.c_str());
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", scriptPath.c_str());
                }

                ImGui::SameLine();
                pushRemoveButtonStyle();
                if (ImGui::Button("x##RemoveScript", ImVec2(16, 16)))
                {
                    scriptToRemove = scriptPath;
                }
                popRemoveButtonStyle();

                ImGui::PopID();
            }

            if (scriptPaths.empty())
            {
                ImGui::TextDisabled("No scripts attached");
            }

            ImGui::Spacing();

            // Add Script button
            if (ImGui::Button("Add Script"))
            {
                nfd::FileDialog fileDialog;
                std::string path = fileDialog.openFileDialog(
                    {{L"mType Script Files (*.mt)", L"*.mt"}});
                if (!path.empty())
                {
                    events::scripting::AttachScriptCommand attachCmd;
                    attachCmd.entity = handle;
                    attachCmd.data.scriptPath = path;
                    attachCmd.data.enabled = true;
                    dispatcher.execute(attachCmd);
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        // Handle script removal (after UI rendering to avoid iterator invalidation)
        if (!scriptToRemove.empty())
        {
            events::scripting::DetachScriptCommand cmd;
            cmd.entity = handle;
            cmd.scriptPath = scriptToRemove;
            dispatcher.execute(cmd);
        }

        if (removeAllScripts)
        {
            // Detach all scripts from this entity
            for (const auto& scriptPath : scriptPaths)
            {
                events::scripting::DetachScriptCommand cmd;
                cmd.entity = handle;
                cmd.scriptPath = scriptPath;
                dispatcher.execute(cmd);
            }
            // If component was empty (no scripts), we need to remove it manually
            if (scriptPaths.empty())
            {
                auto enttEntity = services::internal::fromHandle(handle);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (registry.all_of<components::ScriptComponent>(enttEntity))
                {
                    registry.remove<components::ScriptComponent>(enttEntity);
                }
            }
        }

        return true;
    }

    void EntityDetailsPanel::drawAddComponentButton(services::EntityHandle handle, bool hasCamera, bool hasMesh,
                                                    bool hasAudio2D, bool hasAudio3D, bool hasScript)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

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

            if (!hasAudio2D)
            {
                if (ImGui::Selectable("  Audio Source 2D"))
                {
                    events::scene::AddAudioSource2DComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Streaming audio for background music and ambient sounds");
                }
            }

            if (!hasAudio3D)
            {
                if (ImGui::Selectable("  Audio Source 3D"))
                {
                    events::scene::AddAudioSource3DComponentCommand cmd;
                    cmd.entity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Cached audio for spatial sound effects");
                }
            }


            if (!hasScript)
            {
                if (ImGui::Selectable("  Script"))
                {
                    // Add empty script component - user will select script file in the inspector
                    events::scripting::AttachScriptCommand cmd;
                    cmd.entity = handle;
                    cmd.data.scriptPath = ""; // Empty path, user selects later
                    cmd.data.enabled = true;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("mType script for custom behavior");
                }
            }

            if (hasCamera && hasMesh && hasAudio2D && hasAudio3D && hasScript)
            {
                ImGui::TextDisabled("All components added");
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
    }

    void EntityDetailsPanel::pushComponentHeaderStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    }

    void EntityDetailsPanel::popComponentHeaderStyle()
    {
        ImGui::PopStyleColor(3);
    }

    void EntityDetailsPanel::pushRemoveButtonStyle()
    {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    }

    void EntityDetailsPanel::popRemoveButtonStyle()
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
}
