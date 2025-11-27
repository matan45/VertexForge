#include "MainImguiWindow.hpp"
#include "Import.hpp"
#include "config/Config.hpp"
#include "files/FileUtils.hpp"
#include <imgui.h>

#include "scene/EntityRegistry.hpp"
#include "string/StringUtil.hpp"


namespace windows
{
	MainImguiWindow::MainImguiWindow(controllers::CoreInterface& coreInterface, controllers::OffScreen& offscreen, std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem)
		: coreInterface{ coreInterface }, offscreen{ offscreen }, sceneGraphSystem{ sceneGraphSystem }
	{
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoBackground;
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		windowFlags = window_flags;
	}

	MainImguiWindow::~MainImguiWindow()
	{
		delete iblPreview;
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
		// Check if the popup modal is open
		if (ImGui::BeginPopupModal("Import Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			std::vector<importConfig::ImportFiles> paths;
			paths.reserve(files.size());
			isFlip.resize(files.size(), false);

			ImGui::Text("Files Dropped:");
			// Display the dropped files in the modal popup
			for (int i = 0; i < files.size(); i++)
			{
				ImGui::PushID(files[i].c_str());
				importConfig::ImportConfig config;
				ImGui::BulletText("%s", files[i].c_str());
				if (files::FileUtils::isHDRFile(files[i]))
				{
					bool flip = isFlip[i];
					ImGui::Checkbox("Flip Vertically", &flip);
					isFlip[i] = flip;
					config.isImageFlipVertically = isFlip[i];
				}
				importConfig::ImportFiles importFile(files[i], config);
				paths.emplace_back(importFile);
				ImGui::PopID();
			}

			if (ImGui::Button("Continue"))
			{
				controllers::Import::importFiles(paths);
				ImGui::CloseCurrentPopup(); // Close the popup
				openModal = false; // Reset the flag
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x *
				2);

			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup(); // Close the popup
				openModal = false; // Reset the flag
			}

			ImGui::EndPopup(); // End the modal
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
				coreInterface.closeWindow();
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
				auto root = sceneGraphSystem->GetRoot();

				if (root.hasComponent<components::IBLComponent>()) {
					root.getComponent<components::IBLComponent>().fileName = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
				}
				else
				{
					root.addComponent<components::IBLComponent>(StringUtil::wstringToUtf8(selectedIBLFile.wstring()));
				}
			}
			ImGui::SameLine();
			std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
			ImGui::Text(filePath.c_str());
			if (ImGui::Button("Preview", ImVec2(120, 0)))
			{
				if (iblPreview)
				{
					deletePreview = true;
				}
				if (!filePath.empty())
				{
					iblPreview = controllers::EditorTextureController::loadHdrTexture(filePath);
				}

			}

			if (deletePreview)
			{
				delete iblPreview;
				deletePreview = false;
				iblPreview = nullptr;
			}
			else if(!deletePreview && iblPreview)
			{
				ImGui::Image(iblPreview->getDescriptorSet(), ImVec2(200, 200));
			}

			if (ImGui::Button("Apply", ImVec2(120, 0)))
			{
				if (auto* firstCamera = getFirstCameraComponent(); firstCamera != nullptr) {
					offscreen.iblAdd(filePath, firstCamera);
				}

			}
			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x *
				2);
			if (ImGui::Button("Remove", ImVec2(120, 0)))
			{
				selectedIBLFile = "";
				offscreen.iblRemove();
				if (iblPreview)
				{
					deletePreview = true;
				}
				auto root = sceneGraphSystem->GetRoot();
				if (root.hasComponent<components::IBLComponent>()) {
					root.removeComponent<components::IBLComponent>();
				}
			}
		}
		ImGui::End();
	}

	components::CameraComponent* MainImguiWindow::getFirstCameraComponent() const
	{
		auto& registry = scene::EntityRegistry::getRegistry();
		// Must have both CameraComponent and TransformComponent for camera updates to work
		auto view = registry.view<components::CameraComponent, components::TransformComponent>();
		for (auto entity : view) {
			return &view.get<components::CameraComponent>(entity);
		}
		return nullptr;
	}

}
