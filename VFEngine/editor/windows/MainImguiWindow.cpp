#include "MainImguiWindow.hpp"
#include "files/FileUtils.hpp"
#include "ServiceLocator.hpp"
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
		auto resourceService = TRY_RESOLVE_SERVICE(services::IResourceService);
		if (!resourceService) {
			return;
		}

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
				resourceService->importFiles(requests);
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
				auto inputService = TRY_RESOLVE_SERVICE(services::IInputService);
				if (inputService) {
					inputService->requestClose();
				}
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
		auto renderService = TRY_RESOLVE_SERVICE(services::IRenderService);
		auto sceneService = TRY_RESOLVE_SERVICE(services::ISceneService);

		if (!renderService || !sceneService) {
			return;
		}

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

				// Update IBL component on root entity via service
				auto rootHandle = sceneService->getRootEntity();
				services::IBLData iblData;
				iblData.fileName = filePath;
				sceneService->setIBLData(rootHandle, iblData);
			}

			ImGui::SameLine();
			std::string filePath = StringUtil::wstringToUtf8(selectedIBLFile.wstring());
			ImGui::Text("%s", filePath.c_str());

			if (ImGui::Button("Preview", ImVec2(120, 0)))
			{
				// Release old preview if exists
				if (iblPreviewHandle.isValid()) {
					renderService->releaseEditorTexture(iblPreviewHandle);
					iblPreviewHandle = services::EditorTextureHandle{};
				}

				if (!filePath.empty()) {
					iblPreviewHandle = renderService->loadEditorHDRTexture(filePath);
				}
			}

			if (iblPreviewHandle.isValid()) {
				ImGui::Image(iblPreviewHandle.imguiDescriptorSet, ImVec2(200, 200));
			}

			if (ImGui::Button("Apply", ImVec2(120, 0)))
			{
				renderService->setIBL(filePath);
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(
				ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2);

			if (ImGui::Button("Remove", ImVec2(120, 0)))
			{
				selectedIBLFile = "";
				renderService->removeIBL();

				if (iblPreviewHandle.isValid()) {
					renderService->releaseEditorTexture(iblPreviewHandle);
					iblPreviewHandle = services::EditorTextureHandle{};
				}

				// Remove IBL component from root via service
				auto rootHandle = sceneService->getRootEntity();
				sceneService->removeIBLComponent(rootHandle);
			}
		}
		ImGui::End();
	}

}
