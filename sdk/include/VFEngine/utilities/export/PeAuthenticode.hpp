#pragma once
#include "GameExportExport.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gameExport
{
	// VK-1696: the Authenticode certificate table of a PE image (data directory 4,
	// IMAGE_DIRECTORY_ENTRY_SECURITY). Unlike every other data directory its address is a FILE
	// offset, and the table lies outside every section: signtool pads the file to 8 bytes and
	// appends the table at the end.
	struct PeSecurityDirectory
	{
		uint32_t offset = 0;
		uint32_t size = 0;

		bool present() const { return offset != 0 || size != 0; }
	};

	// A package built with `premake5 package-release --sign-*` ships a signed Runtime.exe.
	// GameExporter copies it under the game's name and then edits its resources (version info,
	// icon). Editing the resources of a signed exe breaks the signature, and EndUpdateResource
	// leaves a certificate table that points past the end of the file, which signtool then
	// refuses to sign over ("not a valid Win32 application", 0x800700C1). So the game's copy loses
	// the engine's signature first; the game's developer signs the game with their own certificate.
	class VF_GAMEEXPORT_API PeAuthenticode
	{
	public:
		// The security directory of a PE image held in memory. False, with the reason, when the
		// bytes are not a PE image the parser understands: no "MZ", e_lfanew or the headers outside
		// the buffer, no "PE\0\0", an optional-header magic other than PE32 (0x10B) or PE32+ (0x20B).
		// An image with fewer than 5 data directories has no security entry ({0, 0}).
		static bool readSecurityDirectory(std::span<const uint8_t> image,
										  PeSecurityDirectory& directoryOut,
										  std::string& errorOut);

		// Removes the certificate table: zeroes the directory entry and cuts the image at the table's
		// offset (signtool's alignment padding before it stays: zero bytes after the last section).
		// removedOut = false and the image unchanged when there is none. Refuses, leaving the image
		// unchanged, a table that is not 8-byte aligned, starts inside the headers or a section's raw
		// data, or does not end exactly at the end of the image. An entry with a size of 0 is zeroed.
		// The PE checksum is left alone: Windows only checks it for drivers and boot files.
		static bool stripSignature(std::vector<uint8_t>& image, bool& removedOut, std::string& errorOut);

		// The same on a file. stripSignatureFile rewrites the file only when it removed a table.
		static bool readSecurityDirectoryFile(const std::filesystem::path& file,
											  PeSecurityDirectory& directoryOut,
											  std::string& errorOut);
		static bool stripSignatureFile(const std::filesystem::path& file, bool& removedOut, std::string& errorOut);
	};
}
