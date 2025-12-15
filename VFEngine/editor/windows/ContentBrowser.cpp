#include "ContentBrowser.hpp"
#include "MeshPreviewWindow.hpp"
#include "ImagePreviewWindow.hpp"
#include "AudioPreviewWindow.hpp"
#include "MaterialEditorWindow.hpp"
#include <material/MaterialAsset.hpp>
#include "resource/ResourceManager.hpp"
#include "string/StringUtil.hpp"
#include "print/EditorLogger.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/ResourceEvents.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include <IconsFontAwesome6.h>
#include <algorithm>

namespace windows
{
	ContentBrowser::ContentBrowser()
	{
		// Defer navigation until draw() when services are ready
		// Just load directory without setting import location
		if (fs::exists(currentPath) && fs::is_directory(currentPath)) {
			loadDirectory(currentPath);
		}
	}

	void ContentBrowser::loadIcons()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		auto loadIcon = [&dispatcher](const std::string& path) {
			events::render::LoadEditorTextureCommand cmd;
			cmd.path = path;
			cmd.isHDR = false;
			return dispatcher.execute(cmd);
		};

		fileIcon = loadIcon("../../resources/editor/contentBrowser/file.vfImage");
		folderIcon = loadIcon("../../resources/editor/contentBrowser/folder.vfImage");
		textureIcon = loadIcon("../../resources/editor/contentBrowser/texture-file.vfImage");
		audioIcon = loadIcon("../../resources/editor/contentBrowser/audio-file.vfImage");
		meshIcon = loadIcon("../../resources/editor/contentBrowser/mesh-file.vfImage");
		glslIcon = loadIcon("../../resources/editor/contentBrowser/glsl-file.vfImage");
		animationIcon = loadIcon("../../resources/editor/contentBrowser/animation-file.vfImage");
		hdrIcon = loadIcon("../../resources/editor/contentBrowser/hdr-file.vfImage");
		sceneIcon = loadIcon("../../resources/editor/contentBrowser/scene.vfImage");
		materialIcon = loadIcon("../../resources/editor/contentBrowser/material-icon.vfImage");

		iconsLoaded = true;
	}

	void ContentBrowser::draw()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		// Lazy load icons on first draw (after services are initialized)
		if (!iconsLoaded) {
			loadIcons();
		}
		
		if (!importLocationSet) {
			events::resource::SetImportLocationCommand cmd;
			cmd.path = currentPath.string();
			dispatcher.execute(cmd);
			importLocationSet = true;
		}
		
		if (showCreateFolderModal)
		{
			ImGui::OpenPopup("Create New Folder");
		}
		createNewFolderModel();

		if (showCreateMaterialModal)
		{
			ImGui::OpenPopup("Create New Material");
		}
		createNewMaterialModal();

		if (showRenameFileModal)
		{
			ImGui::OpenPopup("Rename File");
		}
		renameFileModal();

		if (showDeleteConfirmModal)
		{
			ImGui::OpenPopup("Delete File?");
		}
		deleteFileConfirmModal();

		if (ImGui::Begin("Folder Structure", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
		{
			drawFolderTree(currentPath);
		}
		ImGui::End();


		if (ImGui::Begin("Content Folder"))
		{
			if (ImGui::Button(ICON_FA_ARROW_LEFT))
			{
				auto parentPath = currentPath.parent_path();
				navigateTo(parentPath);
			}

			ImGui::SameLine();
			
			ImGui::Text("Current Path: %s", StringUtil::wstringToUtf8(currentPath.wstring()).c_str());
			
			ImGui::Text("Search:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.0f);
			char searchBuffer[256];
			std::strncpy(searchBuffer, searchQuery.c_str(), sizeof(searchBuffer));
			if (ImGui::InputText("##Search", searchBuffer, sizeof(searchBuffer)))
			{
				searchQuery = std::string(searchBuffer);
			}

			ImGui::Separator();

			float panelWidth = ImGui::GetContentRegionAvail().x;
			float cellSize = PADDING + THUMBNAIL_SIZE;
			int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));
			ImGui::Columns(columnCount, "", false);

			handleCreateFiles();

			if (showFileWindow)
			{
				drawFileWindow();
			}

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

			for (const auto& asset : assets)
			{
				if (matchesSearchQuery(asset))
				{
					bool isSelected = (selectedFile == fs::path(asset.path));
					printFilesNames(asset, isSelected);

					// Single click to select
					if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						selectedFile = asset.path;
						selectedType = asset.type;
						
						// Double click to open preview (for non-folders, excluding scenes)
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
							&& selectedType != AssetType::Scene
							&& (selectedType != AssetType::Other || !fs::is_directory(asset.path)))
						{
							showFileWindow = true;
						}
					}
				}
			}
			ImGui::PopStyleColor();
		}

		ImGui::Columns(1);
		ImGui::End();
	}

	void ContentBrowser::loadDirectory(const fs::path& path)
	{
		assets.clear();

		std::error_code ec;
		for (auto& entry : fs::directory_iterator(path, ec))
		{
			using enum windows::AssetType;
			Asset asset;
			asset.path = StringUtil::wstringToUtf8(entry.path().wstring());
			asset.name = StringUtil::wstringToUtf8(entry.path().filename().wstring());
			if (entry.is_directory())
			{
				asset.type = Other; // Indicate it's a folder
			}
			else
			{
				// Check for .vfMat files first (JSON format, not binary header)
				std::string extension = entry.path().extension().string();
				if (extension == ".vfMat")
				{
					asset.type = Material;
				}
				else
				{
					// Determine the asset type by its file extension.
					resource::FileType ext = resource::ResourceManager::readHeaderFile(entry);

					if (ext == resource::FileType::TEXTURE)
					{
						asset.type = Texture;
					}
					else if (ext == resource::FileType::SCENE)
					{
						asset.type = Scene;
					}
					else if (ext == resource::FileType::HDR)
					{
						asset.type = HDR;
					}
					else if (ext == resource::FileType::MESH)
					{
						asset.type = Model;
					}
					else if (ext == resource::FileType::SHADER)
					{
						asset.type = Shader;
					}
					else if (ext == resource::FileType::AUDIO)
					{
						asset.type = Audio;
					}
					else if (ext == resource::FileType::ANIMATION)
					{
						asset.type = Animation;
					}
					else
					{
						asset.type = Other;
					}
				}
			}
			assets.push_back(asset);
		}
	}

	void ContentBrowser::printFilesNames(const Asset& asset, bool isSelected)
	{
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		float itemWidth = THUMBNAIL_SIZE + PADDING;
		float itemHeight = THUMBNAIL_SIZE + ImGui::GetTextLineHeightWithSpacing() + 4.0f;

		// Draw selection highlight background
		if (isSelected)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImU32 highlightColor = IM_COL32(70, 130, 180, 100);  // Steel blue with transparency
			drawList->AddRectFilled(
				cursorPos,
				ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
				highlightColor,
				4.0f  // Rounded corners
			);
		}

		switch (asset.type)
		{
			using enum windows::AssetType;
		case Texture:
			ImGui::BeginGroup();
			if (textureIcon.isValid()) {
				ImGui::Image(textureIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case HDR:
			ImGui::BeginGroup();
			if (hdrIcon.isValid()) {
				ImGui::Image(hdrIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Scene:
			ImGui::BeginGroup();
			if (sceneIcon.isValid()) {
				ImGui::Image(sceneIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Model:
			ImGui::BeginGroup();
			if (meshIcon.isValid()) {
				ImGui::Image(meshIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Audio:
			ImGui::BeginGroup();
			if (audioIcon.isValid()) {
				ImGui::Image(audioIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Animation:
			ImGui::BeginGroup();
			if (animationIcon.isValid()) {
				ImGui::Image(animationIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Shader:
			ImGui::BeginGroup();
			if (glslIcon.isValid()) {
				ImGui::Image(glslIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Material:
			ImGui::BeginGroup();
			if (materialIcon.isValid()) {
				ImGui::Image(materialIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
			}
			ImGui::TextWrapped("%s", asset.name.c_str());
			ImGui::EndGroup();
			break;
		case Other:
			if (fs::is_directory(asset.path))
			{
				ImGui::BeginGroup();
				std::string folderName = asset.name;
				if (folderIcon.isValid()) {
					if (ImGui::ImageButton(folderName.c_str(), folderIcon.imguiDescriptorSet,
						ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE)))
					{
						navigateFolder = true;
					}
				}

				ImGui::TextWrapped("%s", folderName.c_str());
				ImGui::EndGroup();

				if (navigateFolder)
				{
					navigateFolder = false;
					navigateTo(asset.path);
				}
			}
			else
			{
				ImGui::BeginGroup();
				if (fileIcon.isValid()) {
					ImGui::Image(fileIcon.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
				}
				ImGui::Text("%s", asset.name.c_str());
				ImGui::EndGroup();
			}
			break;
		}

		ImGui::NextColumn();
	}

	void ContentBrowser::drawFileWindow()
	{
		ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

		if (std::string windowTitle = "File: " + StringUtil::wstringToUtf8(selectedFile.filename().wstring());
			ImGui::Begin(windowTitle.c_str(), &showFileWindow))
		{
			ImGui::Text("File Name: %s", StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
			ImGui::Text("File Path: %s", StringUtil::wstringToUtf8(selectedFile.wstring()).c_str());
			ImGui::Separator();

			if (selectedType == AssetType::Texture || selectedType == AssetType::HDR)
			{
				std::string path = StringUtil::wstringToUtf8(selectedFile.wstring());
				bool isHDR = (selectedType == AssetType::HDR);

				// Check if preview window already exists and is still open
				auto it = openImagePreviews.find(path);
				if (it == openImagePreviews.end() || it->second.expired())
				{
					// Create new preview window
					auto previewWindow = std::make_shared<ImagePreviewWindow>(path, isHDR);
					controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
					openImagePreviews[path] = previewWindow;
				}
				
				showFileWindow = false;
			}
			else if (selectedType == AssetType::Shader)
			{
				//TODO open in vscode
			}
			else if (selectedType == AssetType::Model)
			{
				std::string path = StringUtil::wstringToUtf8(selectedFile.wstring());

				// Check if preview window already exists and is still open
				auto it = openMeshPreviews.find(path);
				if (it == openMeshPreviews.end() || it->second.expired())
				{
					// Create new preview window
					auto previewWindow = std::make_shared<MeshPreviewWindow>(path);
					controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
					openMeshPreviews[path] = previewWindow;
				}
				
				showFileWindow = false;
			}
			else if (selectedType == AssetType::Audio)
			{
				std::string path = StringUtil::wstringToUtf8(selectedFile.wstring());

				// Check if preview window already exists and is still open
				auto it = openAudioPreviews.find(path);
				if (it == openAudioPreviews.end() || it->second.expired())
				{
					// Create new preview window
					auto previewWindow = std::make_shared<AudioPreviewWindow>(path);
					controllers::imguiHandler::ImguiWindowHandler::add(previewWindow);
					openAudioPreviews[path] = previewWindow;
				}

				showFileWindow = false;
			}
			else if (selectedType == AssetType::Material)
			{
				std::string path = StringUtil::wstringToUtf8(selectedFile.wstring());

				// Check if editor window already exists and is still open
				auto it = openMaterialEditors.find(path);
				if (it == openMaterialEditors.end() || it->second.expired())
				{
					// Create new material editor window
					auto editorWindow = std::make_shared<MaterialEditorWindow>(path);
					controllers::imguiHandler::ImguiWindowHandler::add(editorWindow);
					openMaterialEditors[path] = editorWindow;
				}

				showFileWindow = false;
			}
		}

		ImGui::End();
	}

	void ContentBrowser::createNewFolder(const std::string& folderName)
	{
		fs::path newFolderPath = currentPath / folderName;
		try
		{
			if (!fs::exists(newFolderPath))
			{
				fs::create_directory(newFolderPath);
				loadDirectory(currentPath); // Refresh the directory to include the new folder.
			}
			else
			{
				vfLogWarning("Folder already exists.");
			}
		}
		catch (const fs::filesystem_error& e)
		{
			ImGui::Text("Failed to create folder: %s", e.what());
		}
	}

	void ContentBrowser::createNewFolderModel()
	{
		if (showCreateFolderModal &&
			ImGui::BeginPopupModal("Create New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Use a buffer initialized with the current folder name.
			char buffer[256];
			std::strncpy(buffer, newFolderName.c_str(), sizeof(buffer));
			if (ImGui::InputText("Folder Name", buffer, IM_ARRAYSIZE(buffer)))
			{
				newFolderName = std::string(buffer);
			}

			if (ImGui::Button("Create", ImVec2(120, 0)))
			{
				createNewFolder(newFolderName);
				ImGui::CloseCurrentPopup();
				showCreateFolderModal = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				showCreateFolderModal = false;
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowser::createNewMaterialModal()
	{
		if (showCreateMaterialModal &&
			ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			char buffer[256];
			std::strncpy(buffer, newMaterialName.c_str(), sizeof(buffer));
			if (ImGui::InputText("Material Name", buffer, IM_ARRAYSIZE(buffer)))
			{
				newMaterialName = std::string(buffer);
			}

			if (ImGui::Button("Create", ImVec2(120, 0)))
			{
				if (!newMaterialName.empty())
				{
					std::string extension = ".vfMat";
					fs::path newMaterialPath = currentPath / (newMaterialName + extension);

					// Check if file already exists
					int counter = 1;
					while (fs::exists(newMaterialPath)) {
						newMaterialPath = currentPath / (newMaterialName + "_" + std::to_string(counter) + extension);
						counter++;
					}

					// Create default material and save it
					std::string pathStr = StringUtil::wstringToUtf8(newMaterialPath.wstring());
					auto defaultMat = material::MaterialAsset::createDefault(newMaterialName);
					if (material::MaterialAsset::save(pathStr, defaultMat)) {
						loadDirectory(currentPath);  // Refresh
					}
				}
				ImGui::CloseCurrentPopup();
				showCreateMaterialModal = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				showCreateMaterialModal = false;
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowser::renameFileModal()
	{
		if (showRenameFileModal &&
			ImGui::BeginPopupModal("Rename File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Renaming: %s", StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
			ImGui::Separator();

			char buffer[256];
			std::strncpy(buffer, renameFileName.c_str(), sizeof(buffer));
			if (ImGui::InputText("New Name", buffer, IM_ARRAYSIZE(buffer)))
			{
				renameFileName = std::string(buffer);
			}

			if (ImGui::Button("Rename", ImVec2(120, 0)))
			{
				if (!renameFileName.empty() && !selectedFile.empty())
				{
					fs::path newPath = selectedFile.parent_path() / (renameFileName + selectedFile.extension().string());

					std::error_code ec;
					if (!fs::exists(newPath))
					{
						fs::rename(selectedFile, newPath, ec);
						if (!ec)
						{
							selectedFile = newPath;
							loadDirectory(currentPath);  // Refresh
						}
						else
						{
							vfLogError("Failed to rename file: {}", ec.message());
						}
					}
					else
					{
						vfLogError("A file with that name already exists");
					}
				}
				ImGui::CloseCurrentPopup();
				showRenameFileModal = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				showRenameFileModal = false;
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowser::deleteFileConfirmModal()
	{
		if (showDeleteConfirmModal &&
			ImGui::BeginPopupModal("Delete File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Are you sure you want to delete:");
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s",
				StringUtil::wstringToUtf8(selectedFile.filename().wstring()).c_str());
			ImGui::Separator();
			ImGui::Text("This action cannot be undone!");

			ImGui::Spacing();

			if (ImGui::Button("Delete", ImVec2(120, 0)))
			{
				if (!selectedFile.empty())
				{
					std::error_code ec;
					if (fs::is_directory(selectedFile))
					{
						fs::remove_all(selectedFile, ec);
					}
					else
					{
						fs::remove(selectedFile, ec);
					}

					if (!ec)
					{
						selectedFile.clear();
						loadDirectory(currentPath);  // Refresh
					}
					else
					{
						vfLogError("Failed to delete file: {}", ec.message());
					}
				}
				ImGui::CloseCurrentPopup();
				showDeleteConfirmModal = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				showDeleteConfirmModal = false;
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowser::handleCreateFiles()
	{
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::MenuItem("Create New Folder"))
			{
				showCreateFolderModal = true;
				newFolderName.clear(); // Clear the previous input.
			}
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Material"))
				{
					showCreateMaterialModal = true;
					newMaterialName.clear();
				}
				ImGui::EndMenu();
			}
			bool hasSelection = !selectedFile.empty();
			if (ImGui::MenuItem("Delete", nullptr, false, hasSelection))
			{
				showDeleteConfirmModal = true;
			}
			if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
			{
				// Get filename without extension for the input field
				renameFileName = StringUtil::wstringToUtf8(selectedFile.stem().wstring());
				showRenameFileModal = true;
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowser::drawFolderTree(const fs::path& path)
	{
		std::error_code ec;
		for (auto& entry : fs::directory_iterator(path, ec))
		{
			if (entry.is_directory())
			{
				ImGui::Text(ICON_FA_FOLDER ""); // Add folder icon before the name
				ImGui::SameLine(); // Place the folder name next to the icon
				// Use a tree node for directories
				if (ImGui::TreeNode(StringUtil::wstringToUtf8(entry.path().filename().wstring()).c_str()))
				{
					// If the directory is selected, navigate to it in the content browser.
					if (ImGui::IsItemClicked())
					{
						navigateTo(entry.path());
					}

					// Recursively draw child directories
					drawFolderTree(entry.path());

					ImGui::TreePop(); // Close the tree node.
				}
			}
		}
	}

	bool ContentBrowser::matchesSearchQuery(const Asset& asset) const
	{
		if (searchQuery.empty())
		{
			return true; // If no search term is entered, show all assets.
		}

		// Convert both the asset name and the search query to lowercase for case-insensitive comparison.
		std::string assetNameLower = StringUtil::toLower(asset.name);
		std::string searchQueryLower = StringUtil::toLower(searchQuery);

		return assetNameLower.find(searchQueryLower) != std::string::npos;
	}

	void ContentBrowser::navigateTo(const fs::path& path)
	{
		if (fs::exists(path) && fs::is_directory(path))
		{
			currentPath = path;

			events::resource::SetImportLocationCommand cmd;
			cmd.path = currentPath.string();
			events::EventDispatcher::instance().execute(cmd);

			loadDirectory(currentPath);
		}
	}
}
