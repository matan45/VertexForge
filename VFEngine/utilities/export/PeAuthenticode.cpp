#include "PeAuthenticode.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

namespace gameExport
{
	namespace
	{
		constexpr size_t kDosHeaderSize = 0x40;
		constexpr size_t kLfanewOffset = 0x3C;
		constexpr size_t kFileHeaderSize = 20;
		constexpr size_t kSectionHeaderSize = 40;
		constexpr size_t kDataDirectorySize = 8;
		constexpr uint32_t kSecurityDirectoryIndex = 4;
		constexpr uint16_t kMagicPe32 = 0x10B;
		constexpr uint16_t kMagicPe32Plus = 0x20B;

		uint16_t readU16(std::span<const uint8_t> bytes, size_t at)
		{
			return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
		}

		uint32_t readU32(std::span<const uint8_t> bytes, size_t at)
		{
			return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
				   (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
		}

		// Where the parts of a PE image the parser needs lie. securityEntry is empty when the
		// optional header has fewer than 5 data directories.
		struct PeLayout
		{
			std::optional<size_t> securityEntry;
			size_t checksum = 0; // the optional header's CheckSum (offset 64 in PE32 and PE32+)
			size_t sectionTable = 0;
			uint16_t numberOfSections = 0;
		};

		bool locate(std::span<const uint8_t> image, PeLayout& layout, std::string& errorOut)
		{
			if (image.size() < kDosHeaderSize || image[0] != 'M' || image[1] != 'Z')
			{
				errorOut = "not a PE image (no MZ header)";
				return false;
			}
			const size_t pe = readU32(image, kLfanewOffset);
			if (pe > image.size() || image.size() - pe < 4 + kFileHeaderSize)
			{
				errorOut = "not a PE image (the PE header lies outside the file)";
				return false;
			}
			if (image[pe] != 'P' || image[pe + 1] != 'E' || image[pe + 2] != 0 || image[pe + 3] != 0)
			{
				errorOut = "not a PE image (no PE signature)";
				return false;
			}

			const size_t fileHeader = pe + 4;
			layout.numberOfSections = readU16(image, fileHeader + 2);
			const size_t optionalSize = readU16(image, fileHeader + 16);
			const size_t optional = fileHeader + kFileHeaderSize;
			if (image.size() - optional < optionalSize || optionalSize < 2)
			{
				errorOut = "malformed PE image (the optional header lies outside the file)";
				return false;
			}

			size_t countOffset = 0;
			const uint16_t magic = readU16(image, optional);
			if (magic == kMagicPe32)
			{
				countOffset = 92;
			}
			else if (magic == kMagicPe32Plus)
			{
				countOffset = 108;
			}
			else
			{
				errorOut = "unsupported PE image (optional header magic is neither PE32 nor PE32+)";
				return false;
			}
			if (optionalSize < countOffset + 4)
			{
				errorOut = "malformed PE image (the optional header is too short)";
				return false;
			}

			layout.checksum = optional + 64;
			const uint32_t directoryCount = readU32(image, optional + countOffset);
			const size_t directories = optional + countOffset + 4;
			if (directoryCount > kSecurityDirectoryIndex)
			{
				const size_t entry = directories + kSecurityDirectoryIndex * kDataDirectorySize;
				if (entry + kDataDirectorySize > optional + optionalSize)
				{
					errorOut = "malformed PE image (the data directories exceed the optional header)";
					return false;
				}
				layout.securityEntry = entry;
			}

			layout.sectionTable = optional + optionalSize;
			if ((image.size() - layout.sectionTable) / kSectionHeaderSize < layout.numberOfSections)
			{
				errorOut = "malformed PE image (the section table lies outside the file)";
				return false;
			}
			return true;
		}

		bool readWholeFile(const std::filesystem::path& file, std::vector<uint8_t>& bytes, std::string& errorOut)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in)
			{
				errorOut = "cannot open " + file.string();
				return false;
			}
			bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			if (in.bad())
			{
				errorOut = "cannot read " + file.string();
				return false;
			}
			return true;
		}
	}

	bool PeAuthenticode::readSecurityDirectory(std::span<const uint8_t> image,
											   PeSecurityDirectory& directoryOut,
											   std::string& errorOut)
	{
		PeLayout layout;
		if (!locate(image, layout, errorOut))
		{
			return false;
		}
		directoryOut = {};
		if (layout.securityEntry)
		{
			directoryOut.offset = readU32(image, *layout.securityEntry);
			directoryOut.size = readU32(image, *layout.securityEntry + 4);
		}
		return true;
	}

	bool PeAuthenticode::stripSignature(std::vector<uint8_t>& image, bool& removedOut, std::string& errorOut)
	{
		removedOut = false;
		PeLayout layout;
		if (!locate(image, layout, errorOut))
		{
			return false;
		}
		if (!layout.securityEntry)
		{
			return true;
		}

		const size_t entry = *layout.securityEntry;
		const uint32_t offset = readU32(image, entry);
		const uint32_t size = readU32(image, entry + 4);
		if (offset == 0 && size == 0)
		{
			return true;
		}

		if (size != 0)
		{
			if (offset % 8 != 0)
			{
				errorOut = "the certificate table is not 8-byte aligned";
				return false;
			}

			// The table must come after the headers and after every section's raw data, so cutting
			// the file at its offset removes nothing else.
			uint64_t dataEnd = layout.sectionTable + uint64_t{layout.numberOfSections} * kSectionHeaderSize;
			for (uint16_t i = 0; i < layout.numberOfSections; ++i)
			{
				const size_t header = layout.sectionTable + size_t{i} * kSectionHeaderSize;
				const uint32_t rawSize = readU32(image, header + 16);
				const uint32_t rawPointer = readU32(image, header + 20);
				if (rawSize != 0)
				{
					dataEnd = std::max(dataEnd, uint64_t{rawPointer} + rawSize);
				}
			}
			if (offset < dataEnd)
			{
				errorOut = "the certificate table overlaps the headers or a section";
				return false;
			}
			if (uint64_t{offset} + size != image.size())
			{
				errorOut = "the certificate table does not end at the end of the file";
				return false;
			}
		}

		std::fill_n(image.begin() + static_cast<std::ptrdiff_t>(entry), kDataDirectorySize, uint8_t{0});
		if (size != 0)
		{
			// signtool wrote a checksum over the signed file; 0 is the linker's "none".
			std::fill_n(image.begin() + static_cast<std::ptrdiff_t>(layout.checksum), 4, uint8_t{0});
			image.resize(offset);
		}
		removedOut = true;
		return true;
	}

	bool PeAuthenticode::readSecurityDirectoryFile(const std::filesystem::path& file,
												   PeSecurityDirectory& directoryOut,
												   std::string& errorOut)
	{
		std::vector<uint8_t> image;
		if (!readWholeFile(file, image, errorOut))
		{
			return false;
		}
		if (!readSecurityDirectory(image, directoryOut, errorOut))
		{
			errorOut = file.filename().string() + ": " + errorOut;
			return false;
		}
		return true;
	}

	bool PeAuthenticode::stripSignatureFile(const std::filesystem::path& file, bool& removedOut, std::string& errorOut)
	{
		removedOut = false;
		std::vector<uint8_t> image;
		if (!readWholeFile(file, image, errorOut))
		{
			return false;
		}
		bool removed = false;
		if (!stripSignature(image, removed, errorOut))
		{
			errorOut = file.filename().string() + ": " + errorOut;
			return false;
		}
		if (!removed)
		{
			return true;
		}

		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (out)
		{
			out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
		}
		if (!out || !out.flush())
		{
			errorOut = "cannot write " + file.string();
			return false;
		}
		removedOut = true;
		return true;
	}
}
