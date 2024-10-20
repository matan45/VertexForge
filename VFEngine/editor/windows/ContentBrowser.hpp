#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "imgui.h"
#include "Import.hpp"


#include <string>
#include <filesystem>
namespace fs = std::filesystem;

namespace windows {

	struct Asset {
		std::wstring name;
		std::wstring path;
		enum class AssetType { Texture, Model, Shader, Other } type;
	};


	class ContentBrowser : public controllers::imguiHandler::ImguiWindow
	{
	private:
		std::vector<Asset> assets;
		fs::path currentPath = "c:\\matan";
	public:
		explicit ContentBrowser() {
			navigateTo(currentPath);
		};
		~ContentBrowser() override = default;

		void draw() override;

	private:
		void navigateTo(const fs::path& path) {
			if (fs::exists(path) && fs::is_directory(path)) {
				currentPath = path;
				controllers::Import::setLocation(currentPath.string());
				loadDirectory(currentPath);
			}
		}

		void loadDirectory(const fs::path& path);
	};
}

