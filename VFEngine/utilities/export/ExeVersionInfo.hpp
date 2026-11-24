#pragma once
#include "GameExportExport.hpp"
#include "ExportConfig.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gameExport
{
	// VK-1696: what an exe's version resource (VS_VERSIONINFO) says.
	struct ExeVersionFields
	{
		// MAJOR, MINOR, PATCH, BUILD: the numeric file and product versions Windows compares.
		std::array<uint16_t, 4> fileVersion{};
		std::array<uint16_t, 4> productVersion{};
		// The StringFileInfo values, UTF-8, written in this order; pairs with an empty value are
		// left out.
		std::vector<std::pair<std::string, std::string>> strings;
	};

	// Writes the version resource of an exported game's exe. The engine's own executables get
	// theirs from VFEngine/utilities/config/VersionInfo.rc2 at build time; an exported game is a copy
	// of Runtime.exe, which says "VertexForge Runtime", so GameExporter::prepareGameExecutable
	// replaces it with the game's name, version and publisher (Task Manager and Explorer show them).
	// One string table, en-US / Unicode ("040904B0"), as VersionInfo.rc2 writes.
	class VF_GAMEEXPORT_API ExeVersionInfo
	{
	public:
		static constexpr uint16_t language = 0x0409;  // MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)
		static constexpr uint16_t codePage = 1200;    // UTF-16LE
		static constexpr uint16_t resourceId = 1;     // VS_VERSION_INFO

		// The leading MAJOR[.MINOR[.PATCH[.BUILD]]] numbers of a free-form version text, each at most
		// 65535, missing parts 0, anything after them ignored: "1.2.3" -> 1.2.3.0, "2" -> 2.0.0.0,
		// "1.2-beta" -> 1.2.0.0, "1.2.3.4.5" -> 1.2.3.4. nullopt when the text does not start with a
		// digit ("v1", "") or a part exceeds 65535 ("70000").
		static std::optional<std::array<uint16_t, 4>> parseFixedVersion(std::string_view text);

		// The fields of an exported game's exe: CompanyName = publisher (left out when empty),
		// FileDescription = InternalName = ProductName = gameName, OriginalFilename = "<gameName>.exe",
		// FileVersion = ProductVersion = gameVersion as authored; the numeric versions come from
		// parseFixedVersion(gameVersion), or 0.0.0.0 with a warning appended to warningsOut.
		static ExeVersionFields gameFields(const ExportConfig& config, std::vector<std::string>& warningsOut);

		// The VS_VERSIONINFO resource bytes. False for a value that is not valid UTF-8 or a resource
		// larger than 65535 bytes.
		static bool buildResource(const ExeVersionFields& fields,
								  std::vector<uint8_t>& resourceOut,
								  std::string& errorOut);

		// Replaces every RT_VERSION resource of an exe with this one (VS_VERSION_INFO, en-US); every
		// other resource is kept. Refuses an exe that carries an Authenticode signature (strip it
		// first, PeAuthenticode::stripSignatureFile).
		static bool writeResource(const std::filesystem::path& exePath,
								  const std::vector<uint8_t>& resource,
								  std::string& errorOut);
	};
}
