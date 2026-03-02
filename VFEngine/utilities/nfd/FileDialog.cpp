#include "FileDialog.hpp"

#include <bit>
#include "../string/StringUtil.hpp"
#include "../print/EditorLogger.hpp"

namespace nfd {

	FileDialog::FileDialog()
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (FAILED(hr))
		{
			vfLogError("Failed to initialize COM");
		}
	}

	FileDialog::~FileDialog()
	{
		CoUninitialize();
	}

	std::string FileDialog::openFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes) const
	{
		IFileOpenDialog* pFileOpen = nullptr;

		// Create the FileOpenDialog object
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, std::bit_cast<void**>(&pFileOpen));

		if (FAILED(hr))
		{
			vfLogError("Failed to create File Open Dialog");
			return std::string();
		}

		// Prepare COMDLG_FILTERSPEC array from the input fileTypes
		std::vector<COMDLG_FILTERSPEC> filterSpec(fileTypes.size());
		for (size_t i = 0; i < fileTypes.size(); ++i)
		{
			filterSpec[i].pszName = fileTypes[i].first.c_str(); // Description (e.g., "Text Files (*.txt)")
			filterSpec[i].pszSpec = fileTypes[i].second.c_str(); // Filter pattern (e.g., "*.txt")
		}

		// Set file type filters
		hr = pFileOpen->SetFileTypes(static_cast<UINT>(filterSpec.size()), filterSpec.data());
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to set file filters");
			return std::string();
		}

		// Set the default file type index (optional)
		hr = pFileOpen->SetFileTypeIndex(1);  // 1-based index, here it will default to the first filter
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to set default file type");
			return {};
		}

		// Show the Open dialog box
		hr = pFileOpen->Show(nullptr);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("No file was selected or dialog failed");
			return {};
		}

		// Get the file name from the dialog box
		IShellItem* pItem = nullptr;
		hr = pFileOpen->GetResult(&pItem);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to retrieve file result");
			return {};
		}

		// Extract the file path
		PWSTR pszFilePath = nullptr;
		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		if (FAILED(hr))
		{
			pItem->Release();
			pFileOpen->Release();
			vfLogError("Failed to get file path");
			return {};
		}

		// Convert the wide string (WCHAR) to a standard string (char)
		std::string filePath = StringUtil::WideStringToString(pszFilePath);

		// Free memory and release resources
		CoTaskMemFree(pszFilePath);
		pItem->Release();
		pFileOpen->Release();

		return filePath;
	}
	std::vector<std::string> FileDialog::multiSelectFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes) const
	{
		IFileOpenDialog* pFileOpen = nullptr;

		// Create the FileOpenDialog object
		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, std::bit_cast<void**>(&pFileOpen));

		if (FAILED(hr))
		{
			vfLogError("Failed to create File Open Dialog");
			return {};
		}

		// Enable multi-selection
		DWORD dwFlags;
		hr = pFileOpen->GetOptions(&dwFlags);
		if (SUCCEEDED(hr))
		{
			hr = pFileOpen->SetOptions(dwFlags | FOS_ALLOWMULTISELECT);
		}
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to enable multi-select");
			return {};
		}

		// Prepare COMDLG_FILTERSPEC array from the input fileTypes
		std::vector<COMDLG_FILTERSPEC> filterSpec(fileTypes.size());
		for (size_t i = 0; i < fileTypes.size(); ++i)
		{
			filterSpec[i].pszName = fileTypes[i].first.c_str(); // Description (e.g., "Text Files (*.txt)")
			filterSpec[i].pszSpec = fileTypes[i].second.c_str(); // Filter pattern (e.g., "*.txt")
		}

		// Set file type filters
		hr = pFileOpen->SetFileTypes(static_cast<UINT>(filterSpec.size()), filterSpec.data());
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to set file filters");
			return {};
		}

		// Set the default file type index (optional)
		hr = pFileOpen->SetFileTypeIndex(1);  // 1-based index, here it will default to the first filter
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to set default file type");
			return {};
		}

		// Show the Open dialog box
		hr = pFileOpen->Show(nullptr);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("No file was selected or dialog failed");
			return {};
		}

		// Get the file names from the dialog box
		IShellItemArray* pItemArray = nullptr;
		hr = pFileOpen->GetResults(&pItemArray);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to retrieve file results");
			return {};
		}

		// Extract the file paths
		std::vector<std::string> filePaths;
		DWORD numItems = 0;
		hr = pItemArray->GetCount(&numItems);
		if (SUCCEEDED(hr))
		{
			for (DWORD i = 0; i < numItems; ++i)
			{
				IShellItem* pItem = nullptr;
				hr = pItemArray->GetItemAt(i, &pItem);
				if (SUCCEEDED(hr))
				{
					PWSTR pszFilePath = nullptr;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
					if (SUCCEEDED(hr))
					{
						// Convert the wide string (WCHAR) to a standard string (char)
						std::string filePath = StringUtil::WideStringToString(pszFilePath);
						filePaths.push_back(filePath);
						CoTaskMemFree(pszFilePath);
					}
					pItem->Release();
				}
			}
		}

		// Release resources
		pItemArray->Release();
		pFileOpen->Release();

		return filePaths;
	}

	std::string FileDialog::selectFolderDialog() const
	{
		IFileOpenDialog* pFileOpen = nullptr;

		HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, std::bit_cast<void**>(&pFileOpen));
		if (FAILED(hr))
		{
			vfLogError("Failed to create Folder Dialog");
			return {};
		}

		DWORD dwFlags;
		hr = pFileOpen->GetOptions(&dwFlags);
		if (SUCCEEDED(hr))
		{
			hr = pFileOpen->SetOptions(dwFlags | FOS_PICKFOLDERS);
		}
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to set folder picker mode");
			return {};
		}

		hr = pFileOpen->Show(nullptr);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			return {};
		}

		IShellItem* pItem = nullptr;
		hr = pFileOpen->GetResult(&pItem);
		if (FAILED(hr))
		{
			pFileOpen->Release();
			vfLogError("Failed to retrieve folder result");
			return {};
		}

		PWSTR pszFolderPath = nullptr;
		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
		if (FAILED(hr))
		{
			pItem->Release();
			pFileOpen->Release();
			vfLogError("Failed to get folder path");
			return {};
		}

		std::string folderPath = StringUtil::WideStringToString(pszFolderPath);

		CoTaskMemFree(pszFolderPath);
		pItem->Release();
		pFileOpen->Release();

		return folderPath;
	}

	std::string FileDialog::saveFileDialog(const std::vector<std::pair<std::wstring, std::wstring>>& fileTypes,
		const std::wstring& defaultExtension) const
	{
		IFileSaveDialog* pFileSave = nullptr;

		// Create the FileSaveDialog object
		HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_IFileSaveDialog, std::bit_cast<void**>(&pFileSave));

		if (FAILED(hr))
		{
			vfLogError("Failed to create File Save Dialog");
			return std::string();
		}

		// Prepare COMDLG_FILTERSPEC array from the input fileTypes
		std::vector<COMDLG_FILTERSPEC> filterSpec(fileTypes.size());
		for (size_t i = 0; i < fileTypes.size(); ++i)
		{
			filterSpec[i].pszName = fileTypes[i].first.c_str();
			filterSpec[i].pszSpec = fileTypes[i].second.c_str();
		}

		// Set file type filters
		hr = pFileSave->SetFileTypes(static_cast<UINT>(filterSpec.size()), filterSpec.data());
		if (FAILED(hr))
		{
			pFileSave->Release();
			vfLogError("Failed to set file filters");
			return std::string();
		}

		// Set the default file type index
		hr = pFileSave->SetFileTypeIndex(1);
		if (FAILED(hr))
		{
			pFileSave->Release();
			vfLogError("Failed to set default file type");
			return {};
		}

		// Set default extension
		if (!defaultExtension.empty())
		{
			hr = pFileSave->SetDefaultExtension(defaultExtension.c_str());
			if (FAILED(hr))
			{
				pFileSave->Release();
				vfLogError("Failed to set default extension");
				return {};
			}
		}

		// Show the Save dialog box
		hr = pFileSave->Show(nullptr);
		if (FAILED(hr))
		{
			pFileSave->Release();
			// User cancelled - not an error
			return {};
		}

		// Get the file name from the dialog box
		IShellItem* pItem = nullptr;
		hr = pFileSave->GetResult(&pItem);
		if (FAILED(hr))
		{
			pFileSave->Release();
			vfLogError("Failed to retrieve file result");
			return {};
		}

		// Extract the file path
		PWSTR pszFilePath = nullptr;
		hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		if (FAILED(hr))
		{
			pItem->Release();
			pFileSave->Release();
			vfLogError("Failed to get file path");
			return {};
		}

		// Convert the wide string (WCHAR) to a standard string (char)
		std::string filePath = StringUtil::WideStringToString(pszFilePath);

		// Free memory and release resources
		CoTaskMemFree(pszFilePath);
		pItem->Release();
		pFileSave->Release();

		return filePath;
	}
}
