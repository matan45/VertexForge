#include "MainImguiWindow.hpp"
#include "../camera/EditorCamera.hpp"
#include "files/FileUtils.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include "events/ResourceEvents.hpp"
#include "events/InputEvents.hpp"
#include "string/StringUtil.hpp"
#include <imgui.h>

namespace windows
{
	MainImguiWindow::MainImguiWindow()
	{
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoBackground;
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		windowFlags = window_flags;
	}

	void MainImguiWindow::draw()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (ImGui::Begin("Vulkan Engine", nullptr, windowFlags))
		{
			ImGui::PopStyleVar(1);

			ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0.0f, 0.0f),
				ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_None);

			menuBar();
			if (showIBLWindow)
			{
				iblWindow();
			}
			if (showEditorCameraWindow)
			{
				editorCameraWindow();
			}
		}
		ImGui::End();
	}

	void MainImguiWindow::menuBar()
	{
		if (openModal)
		{
			ImGui::OpenPopup("Import Files");
		}

		importModel();

		if (ImGui::BeginMainMenuBar())
		{
			handleFileMenu();
			handleSettingsMenu();
			handleAddMenu();
			ImGui::EndMainMenuBar();
		}
	}

	void MainImguiWindow::importModel()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		// Check if the popup modal is open
		if (ImGui::BeginPopupModal("Import Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			std::vector<services::ImportFileRequest> requests;
			requests.reserve(files.size());
			isFlip.resize(files.size(), false);

			ImGui::Text("Files Dropped:");
			for (size_t i = 0; i < files.size(); i++)
			{
				ImGui::PushID(files[i].c_str());
				ImGui::BulletText("%s", files[i].c_str());

				if (files::FileUtils::isHDRFile(files[i]))
				{
					bool flip = isFlip[i];
					ImGui::Checkbox("Flip Vertically", &flip);
					isFlip[i] = flip;
				}

				services::ImportFileRequest request;
				request.path = files[i];
				request.flipVertically = isFlip[i];
				requests.push_back(request);

				ImGui::PopID();
			}

			if (ImGui::Button("Continue"))
			{
				events::resource::ImportFilesCommand cmd;
				cmd.files = requests;
				dispatcher.execute(cmd);
				ImGui::CloseCurrentPopup();
				openModal = false;
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2);

			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup();
				openModal = false;
			}

			ImGui::EndPopup();
		}
	}

	void MainImguiWindow::handleFileMenu()
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Level"))
			{
			}
			else if (ImGui::MenuItem("Open Level"))
			{
			}
			else if (ImGui::MenuItem("Save Level"))
			{
			}
			else if (ImGui::MenuItem("Exit"))
			{
				events::input::CloseApplicationCommand cmd;
				events::EventDispatcher::instance().execute(cmd);
			}
			ImGui::EndMenu();
		}
	}

	void MainImguiWindow::handleSettingsMenu()
	{
		if (ImGui::BeginMenu("Settings"))
		{
			if (ImGui::MenuItem("Import"))
			{
				std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
					{L"Model Files (*.obj;*.fbx;*.dae;*.gltf)", L"*.obj;*.fbx;*.dae;*.gltf"},
					{L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp"},
					{L"Hdr Files (*.exr;*.hdr)", L"*.exr;*.hdr"},
					{L"Audio Files (*.wav;*.ogg:*.mp3)", L"*.wav;*.ogg;*.mp3"}
				};

				files = fileDialog.multiSelectFileDialog(fileTypes);
				if (!files.empty())
				{
					openModal = true;
				}
			}
			else if (ImGui::MenuItem("Editor Camera"))
			{
				showEditorCameraWindow = true;
			}
			else if (ImGui::MenuItem("Layout Style"))
			{
			}
			ImGui::EndMenu();
		}
	}

	void MainImguiWindow::handleAddMenu()
	{
		if (ImGui::BeginMenu("Add"))
		{
			if (ImGui::MenuItem("IBL"))
			{
				showIBLWindow = true;
			}
			else if (ImGui::MenuItem("Terrain"))
			{
			}
			ImGui::EndMenu();
		}
	}

	void MainImguiWindow::iblWindow()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("IBL", &showIBLWindow))
		{
			ImGui::Text("IBL Window");

			if (ImGui::Button("Select"))
			{
				std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
					{L"Hdr Files (*.vfHdr)", L"*.vfHdr"}
				};

				selectedIBLFile = fileDialog.openFileDialog(fileTypes);
				std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());

				// Update IBL component on root entity via event system
				events::scene::GetRootEntityQuery rootQuery;
				auto rootHandle = dispatcher.query(rootQuery);

				events::scene::SetIBLDataCommand iblCmd;
				iblCmd.entity = rootHandle;
				iblCmd.iblData.fileName = filePath;
				dispatcher.execute(iblCmd);
			}

			ImGui::SameLine();
			std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
			ImGui::Text("%s", filePath.c_str());

			if (ImGui::Button("Preview", ImVec2(120, 0)))
			{
				// Release old preview if exists
				if (iblPreviewHandle.isValid()) {
					events::render::ReleaseEditorTextureCommand releaseCmd;
					releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
					dispatcher.execute(releaseCmd);
					iblPreviewHandle = services::EditorTextureHandle{};
				}

				if (!filePath.empty()) {
					events::render::LoadEditorTextureCommand loadCmd;
					loadCmd.path = filePath;
					loadCmd.isHDR = true;
					iblPreviewHandle = dispatcher.execute(loadCmd);
				}
			}

			if (ImGui::Button("Apply", ImVec2(120, 0)))
			{
				events::render::SetIBLCommand cmd;
				cmd.hdrPath = filePath;
				dispatcher.execute(cmd);
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2);

			if (ImGui::Button("Remove", ImVec2(120, 0)))
			{
				selectedIBLFile = "";

				// Release editor preview texture first
				if (iblPreviewHandle.isValid()) {
					events::render::ReleaseEditorTextureCommand releaseCmd;
					releaseCmd.handle = iblPreviewHandle.imguiDescriptorSet;
					dispatcher.execute(releaseCmd);
					iblPreviewHandle = services::EditorTextureHandle{};
				}

				// Then remove IBL from renderer
				events::render::RemoveIBLCommand removeIblCmd;
				dispatcher.execute(removeIblCmd);

				// Remove IBL component from root via event system
				events::scene::GetRootEntityQuery rootQuery;
				auto rootHandle = dispatcher.query(rootQuery);

				events::scene::RemoveIBLComponentCommand removeCmd;
				removeCmd.entity = rootHandle;
				dispatcher.execute(removeCmd);
			}

			// Display preview image AFTER all buttons are processed
			// This ensures the descriptor set is valid if we display it
			if (iblPreviewHandle.isValid()) {
				ImGui::Image(iblPreviewHandle.imguiDescriptorSet, ImVec2(200, 200));
			}
		}
		ImGui::End();
	}

	void MainImguiWindow::editorCameraWindow()
	{
		ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Editor Camera Settings", &showEditorCameraWindow))
		{
			if (!editorCameraRef)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Editor Camera not available");
				ImGui::End();
				return;
			}

			ImGui::Text("Transform");
			ImGui::Separator();

			// Position
			ImGui::Text("Position");
			ImGui::PushItemWidth(-1);
			if (ImGui::DragFloat3("##Position", &editorCameraRef->position.x, 0.1f))
			{
				editorCameraRef->updateViewMatrix();
			}
			ImGui::PopItemWidth();

			// Rotation
			ImGui::Text("Rotation (degrees)");
			ImGui::PushItemWidth(-1);
			if (ImGui::DragFloat3("##Rotation", &editorCameraRef->rotation.x, 0.5f))
			{
				// Clamp pitch
				editorCameraRef->rotation.x = std::clamp(editorCameraRef->rotation.x, -89.0f, 89.0f);
				editorCameraRef->updateViewMatrix();
			}
			ImGui::PopItemWidth();

			ImGui::Spacing();
			ImGui::Text("Projection");
			ImGui::Separator();

			// Field of View
			ImGui::Text("Field of View");
			ImGui::PushItemWidth(-1);
			if (ImGui::SliderFloat("##FOV", &editorCameraRef->fieldOfView, 30.0f, 120.0f, "%.1f"))
			{
				editorCameraRef->updateProjectionMatrix();
			}
			ImGui::PopItemWidth();

			// Near Plane
			ImGui::Text("Near Plane");
			ImGui::PushItemWidth(-1);
			if (ImGui::DragFloat("##NearPlane", &editorCameraRef->nearPlane, 0.01f, 0.001f, 10.0f, "%.3f"))
			{
				editorCameraRef->updateProjectionMatrix();
			}
			ImGui::PopItemWidth();

			// Far Plane
			ImGui::Text("Far Plane");
			ImGui::PushItemWidth(-1);
			if (ImGui::DragFloat("##FarPlane", &editorCameraRef->farPlane, 1.0f, 10.0f, 100000.0f, "%.1f"))
			{
				editorCameraRef->updateProjectionMatrix();
			}
			ImGui::PopItemWidth();

			// Aspect Ratio (read-only, set by viewport)
			ImGui::Text("Aspect Ratio");
			ImGui::PushItemWidth(-1);
			ImGui::BeginDisabled();
			ImGui::InputFloat("##AspectRatio", &editorCameraRef->aspectRatio, 0, 0, "%.3f");
			ImGui::EndDisabled();
			ImGui::PopItemWidth();

			ImGui::Spacing();
			ImGui::Text("Navigation");
			ImGui::Separator();

			// Move Speed
			ImGui::Text("Move Speed");
			ImGui::PushItemWidth(-1);
			ImGui::SliderFloat("##MoveSpeed", &editorCameraRef->moveSpeed, 0.5f, 50.0f, "%.1f");
			ImGui::PopItemWidth();

			// Mouse Sensitivity
			ImGui::Text("Mouse Sensitivity");
			ImGui::PushItemWidth(-1);
			ImGui::SliderFloat("##MouseSensitivity", &editorCameraRef->mouseSensitivity, 0.01f, 0.5f, "%.3f");
			ImGui::PopItemWidth();

			ImGui::Spacing();
			if (ImGui::Button("Reset to Default", ImVec2(-1, 0)))
			{
				editorCameraRef->position = glm::vec3(0.0f, 2.0f, 5.0f);
				editorCameraRef->rotation = glm::vec3(0.0f);
				editorCameraRef->fieldOfView = 60.0f;
				editorCameraRef->nearPlane = 0.1f;
				editorCameraRef->farPlane = 1000.0f;
				editorCameraRef->moveSpeed = 5.0f;
				editorCameraRef->mouseSensitivity = 0.1f;
				editorCameraRef->updateViewMatrix();
				editorCameraRef->updateProjectionMatrix();
			}
		}
		ImGui::End();
	}

}
