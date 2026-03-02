#pragma once
#include <string>
#include <vector>
#include <string_view>

namespace nfd {
	class FileDialog
	{
	public:
		// Constructor: Initializes COM.
		explicit FileDialog();
		~FileDialog();
		std::string openFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes) const;
		std::vector<std::string> multiSelectFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes) const;
		std::string saveFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes,
			const std::wstring& defaultExtension = L"") const;
		std::string selectFolderDialog() const;
	};

}

