#include "ExeVersionInfo.hpp"
#include "PeAuthenticode.hpp"

#include <chrono>
#include <climits>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace gameExport
{
	namespace
	{
		// One node of a version resource: a header (wLength, wValueLength, wType), the key (UTF-16,
		// NUL-terminated), padding to 4 bytes, the value, then the children, each starting on a
		// 4-byte boundary. wLength covers the node and its children but not the padding after the
		// last of them. wValueLength counts WCHARs (NUL included) for text values (wType 1) and
		// bytes for binary ones (wType 0).
		struct Node
		{
			std::u16string key;
			uint16_t type = 1;
			uint16_t valueLength = 0;
			std::vector<uint8_t> value;
			std::vector<Node> children;
		};

		void putU16(std::vector<uint8_t>& out, uint16_t value)
		{
			out.push_back(static_cast<uint8_t>(value & 0xFF));
			out.push_back(static_cast<uint8_t>(value >> 8));
		}

		void putU32(std::vector<uint8_t>& out, uint32_t value)
		{
			putU16(out, static_cast<uint16_t>(value & 0xFFFF));
			putU16(out, static_cast<uint16_t>(value >> 16));
		}

		void putText(std::vector<uint8_t>& out, const std::u16string& text)
		{
			for (const char16_t c : text)
			{
				putU16(out, static_cast<uint16_t>(c));
			}
			putU16(out, 0);
		}

		void align4(std::vector<uint8_t>& out)
		{
			while (out.size() % 4 != 0)
			{
				out.push_back(0);
			}
		}

		void emit(const Node& node, std::vector<uint8_t>& out)
		{
			align4(out);
			const size_t start = out.size();
			putU16(out, 0); // wLength, set below
			putU16(out, node.valueLength);
			putU16(out, node.type);
			putText(out, node.key);
			align4(out);
			out.insert(out.end(), node.value.begin(), node.value.end());
			for (const Node& child : node.children)
			{
				emit(child, out);
			}
			// buildResource refuses a resource over 65535 bytes, so no node's length is cut here.
			const size_t length = out.size() - start;
			out[start] = static_cast<uint8_t>(length & 0xFF);
			out[start + 1] = static_cast<uint8_t>((length >> 8) & 0xFF);
		}

		bool toUtf16(const std::string& text, std::u16string& out)
		{
			out.clear();
			if (text.empty())
			{
				return true;
			}
			if (text.find('\0') != std::string::npos || text.size() > static_cast<size_t>(INT_MAX))
			{
				return false;
			}
#ifdef _WIN32
			const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
												   static_cast<int>(text.size()), nullptr, 0);
			if (length <= 0)
			{
				return false;
			}
			std::wstring wide(static_cast<size_t>(length), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
									wide.data(), length) != length)
			{
				return false;
			}
			out.assign(wide.begin(), wide.end());
			return true;
#else
			return false;
#endif
		}

		std::vector<uint8_t> fixedFileInfo(const ExeVersionFields& fields)
		{
			const auto& file = fields.fileVersion;
			const auto& product = fields.productVersion;
			std::vector<uint8_t> info;
			putU32(info, 0xFEEF04BD); // dwSignature
			putU32(info, 0x00010000); // dwStrucVersion
			putU32(info, (uint32_t{file[0]} << 16) | file[1]);
			putU32(info, (uint32_t{file[2]} << 16) | file[3]);
			putU32(info, (uint32_t{product[0]} << 16) | product[1]);
			putU32(info, (uint32_t{product[2]} << 16) | product[3]);
			putU32(info, 0x0000003F); // dwFileFlagsMask = VS_FFI_FILEFLAGSMASK
			putU32(info, 0);          // dwFileFlags
			putU32(info, 0x00040004); // dwFileOS = VOS_NT_WINDOWS32
			putU32(info, 0x00000001); // dwFileType = VFT_APP
			putU32(info, 0);          // dwFileSubtype = VFT2_UNKNOWN
			putU32(info, 0);          // dwFileDateMS
			putU32(info, 0);          // dwFileDateLS
			return info;
		}

		std::u16string ascii(const char* text)
		{
			std::u16string out;
			for (; *text != '\0'; ++text)
			{
				out.push_back(static_cast<char16_t>(*text));
			}
			return out;
		}
	}

	std::optional<std::array<uint16_t, 4>> ExeVersionInfo::parseFixedVersion(std::string_view text)
	{
		std::array<uint16_t, 4> parts{};
		size_t pos = 0;
		for (size_t part = 0; part < parts.size(); ++part)
		{
			if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
			{
				if (part == 0)
				{
					return std::nullopt;
				}
				break; // "1." or "1.x": the parts read so far
			}
			uint32_t value = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			{
				value = value * 10 + static_cast<uint32_t>(text[pos] - '0');
				if (value > 0xFFFF)
				{
					return std::nullopt;
				}
				++pos;
			}
			parts[part] = static_cast<uint16_t>(value);
			if (pos >= text.size() || text[pos] != '.')
			{
				break;
			}
			++pos;
		}
		return parts;
	}

	ExeVersionFields ExeVersionInfo::gameFields(const ExportConfig& config, std::vector<std::string>& warningsOut)
	{
		ExeVersionFields fields;
		if (const auto parsed = parseFixedVersion(config.gameVersion))
		{
			fields.fileVersion = *parsed;
			fields.productVersion = *parsed;
		}
		else
		{
			warningsOut.push_back("The project version '" + config.gameVersion +
								  "' does not start with MAJOR[.MINOR[.PATCH[.BUILD]]] numbers of at most 65535, "
								  "so the game exe's numeric version is 0.0.0.0 (its version text is kept)");
		}
		fields.strings = {
			{"CompanyName", config.publisher},
			{"FileDescription", config.gameName},
			{"FileVersion", config.gameVersion},
			{"InternalName", config.gameName},
			{"OriginalFilename", config.gameName + ".exe"},
			{"ProductName", config.gameName},
			{"ProductVersion", config.gameVersion},
		};
		return fields;
	}

	bool ExeVersionInfo::buildResource(const ExeVersionFields& fields,
									   std::vector<uint8_t>& resourceOut,
									   std::string& errorOut)
	{
		resourceOut.clear();

		char tableKey[9];
		std::snprintf(tableKey, sizeof(tableKey), "%04X%04X", unsigned{language}, unsigned{codePage});
		Node table{ascii(tableKey)};
		for (const auto& [key, value] : fields.strings)
		{
			if (value.empty())
			{
				continue;
			}
			Node entry;
			std::u16string text;
			if (!toUtf16(key, entry.key) || entry.key.empty())
			{
				errorOut = "a version-info key is empty or not valid UTF-8";
				return false;
			}
			if (!toUtf16(value, text))
			{
				errorOut = "the version-info " + key + " is not valid UTF-8";
				return false;
			}
			if (text.size() + 1 > 0xFFFF)
			{
				errorOut = "the version-info " + key + " is too long";
				return false;
			}
			entry.type = 1;
			entry.valueLength = static_cast<uint16_t>(text.size() + 1);
			putText(entry.value, text);
			table.children.push_back(std::move(entry));
		}

		Node stringFileInfo{u"StringFileInfo"};
		stringFileInfo.children.push_back(std::move(table));

		Node translation{u"Translation"};
		translation.type = 0;
		translation.valueLength = 4;
		putU16(translation.value, language);
		putU16(translation.value, codePage);
		Node varFileInfo{u"VarFileInfo"};
		varFileInfo.children.push_back(std::move(translation));

		Node root{u"VS_VERSION_INFO"};
		root.type = 0;
		root.value = fixedFileInfo(fields);
		root.valueLength = static_cast<uint16_t>(root.value.size()); // 52 bytes
		root.children.push_back(std::move(stringFileInfo));
		root.children.push_back(std::move(varFileInfo));

		std::vector<uint8_t> bytes;
		emit(root, bytes);
		if (bytes.size() > 0xFFFF)
		{
			errorOut = "the version resource would exceed 65535 bytes";
			return false;
		}
		resourceOut = std::move(bytes);
		return true;
	}

#ifdef _WIN32
	namespace
	{
		const LPCWSTR kVersionType = MAKEINTRESOURCEW(16); // RT_VERSION

		// An RT_VERSION resource of the exe: an integer id (id != 0) or a string name, and a language.
		struct VersionEntry
		{
			WORD id = 0;
			std::wstring name;
			WORD language = 0;
		};

		BOOL CALLBACK collectLanguage(HMODULE, LPCWSTR, LPCWSTR name, WORD language, LONG_PTR param)
		{
			auto* entries = reinterpret_cast<std::vector<VersionEntry>*>(param);
			VersionEntry entry;
			if (IS_INTRESOURCE(name))
			{
				entry.id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
			}
			else
			{
				entry.name = name;
			}
			entry.language = language;
			entries->push_back(std::move(entry));
			return TRUE;
		}

		BOOL CALLBACK collectName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
		{
			EnumResourceLanguagesExW(module, type, name, collectLanguage, param, RESOURCE_ENUM_LN, 0);
			return TRUE;
		}

		bool noResourceOfType(DWORD error)
		{
			return error == ERROR_RESOURCE_DATA_NOT_FOUND || error == ERROR_RESOURCE_TYPE_NOT_FOUND ||
				   error == ERROR_RESOURCE_NAME_NOT_FOUND;
		}
	}
#endif

	bool ExeVersionInfo::writeResource(const std::filesystem::path& exePath,
									   const std::vector<uint8_t>& resource,
									   std::string& errorOut)
	{
#ifdef _WIN32
		const std::string exeName = exePath.filename().string();
		if (resource.empty() || resource.size() > 0xFFFF)
		{
			errorOut = "invalid version resource for " + exeName;
			return false;
		}

		PeSecurityDirectory directory;
		if (!PeAuthenticode::readSecurityDirectoryFile(exePath, directory, errorOut))
		{
			return false;
		}
		if (directory.present())
		{
			errorOut = exeName + " carries an Authenticode signature: strip it first (editing the resources of a "
								 "signed exe leaves a certificate table that signtool rejects)";
			return false;
		}

		// Every RT_VERSION it holds now, so that exactly one remains: an exe with two languages would
		// show whichever one Explorer prefers.
		std::vector<VersionEntry> existing;
		{
			HMODULE module = LoadLibraryExW(exePath.c_str(), nullptr,
											LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			if (module == nullptr)
			{
				errorOut = "cannot open " + exeName + " to read its resources (Windows error " +
						   std::to_string(GetLastError()) + ")";
				return false;
			}
			const BOOL listed = EnumResourceNamesExW(module, kVersionType, collectName,
													 reinterpret_cast<LONG_PTR>(&existing), RESOURCE_ENUM_LN, 0);
			const DWORD listError = listed ? ERROR_SUCCESS : GetLastError();
			// The mapped view blocks BeginUpdateResource's write, so it goes before the update.
			FreeLibrary(module);
			if (!listed && !noResourceOfType(listError))
			{
				errorOut = "cannot list the version resources of " + exeName + " (Windows error " +
						   std::to_string(listError) + ")";
				return false;
			}
		}

		// A freshly copied exe may still be open in an antivirus scanner, which makes
		// BeginUpdateResource or EndUpdateResource fail: the whole update is tried again, each time
		// with a new update handle.
		DWORD lastError = ERROR_SUCCESS;
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			if (attempt > 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			HANDLE update = BeginUpdateResourceW(exePath.c_str(), FALSE);
			if (update == nullptr)
			{
				lastError = GetLastError();
				continue;
			}

			bool staged = true;
			for (const VersionEntry& entry : existing)
			{
				if (entry.id == resourceId && entry.language == language)
				{
					continue; // replaced below
				}
				const LPCWSTR name = entry.id != 0 ? MAKEINTRESOURCEW(entry.id) : entry.name.c_str();
				if (!UpdateResourceW(update, kVersionType, name, entry.language, nullptr, 0))
				{
					staged = false;
					break;
				}
			}
			if (staged && !UpdateResourceW(update, kVersionType, MAKEINTRESOURCEW(resourceId), language,
										   const_cast<uint8_t*>(resource.data()), static_cast<DWORD>(resource.size())))
			{
				staged = false;
			}
			if (!staged)
			{
				const DWORD stageError = GetLastError();
				EndUpdateResourceW(update, TRUE);
				errorOut = "cannot replace the version resource of " + exeName + " (Windows error " +
						   std::to_string(stageError) + ")";
				return false;
			}

			if (EndUpdateResourceW(update, FALSE))
			{
				return true;
			}
			lastError = GetLastError();
		}
		errorOut = "cannot write the version resource of " + exeName + " (Windows error " +
				   std::to_string(lastError) + ")";
		return false;
#else
		(void)exePath;
		(void)resource;
		errorOut = "Version resources are only supported on Windows";
		return false;
#endif
	}
}
