#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace gameExport
{
	// VK-1732 / VK-1733: the text of a .vfplugin descriptor with its "enabled" member set, changing as
	// little as possible. Used by the Plugin Manager (a toggle in the dev tree, or on a user's own plugin)
	// and by the game exporter (a shipped game's descriptors say "enabled": true).
	//
	// - The value already matches (or "enabled" is absent and `enabled` is true, the loader's default):
	//   the original bytes, unchanged.
	// - Otherwise the member is set where it stands (an absent one is appended), and the object is written
	//   back in its own member order with a 4-space indent, as the scaffolder and every shipped descriptor
	//   are. A UTF-8 BOM, CRLF line endings and a final newline are kept as the original had them.
	// - nullopt: the text is not a JSON object (or holds invalid UTF-8).
	//
	// Header-only: the Plugin module and GameExport.dll both include VFEngine/utilities.
	inline std::optional<std::string> descriptorTextWithEnabled(std::string_view original, bool enabled)
	{
		try
		{
			constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
			const bool hasBom = original.substr(0, utf8Bom.size()) == utf8Bom;
			const std::string_view body = hasBom ? original.substr(utf8Bom.size()) : original;

			nlohmann::ordered_json document = nlohmann::ordered_json::parse(body, nullptr, false);
			if (document.is_discarded() || !document.is_object())
			{
				return std::nullopt;
			}

			const auto member = document.find("enabled");
			if (member == document.end())
			{
				if (enabled)
				{
					return std::string(original);
				}
				document["enabled"] = false;  // ordered_json appends a new member
			}
			else
			{
				if (member->is_boolean() && member->get<bool>() == enabled)
				{
					return std::string(original);
				}
				*member = enabled;  // replaced where it stands
			}

			// dump() escapes CR and LF inside strings, so every '\n' it emits is a line break.
			std::string text = document.dump(4);
			if (!original.empty() && original.back() == '\n')
			{
				text += '\n';
			}
			if (original.find("\r\n") != std::string_view::npos)
			{
				std::string crlf;
				crlf.reserve(text.size() + text.size() / 8);
				for (const char c : text)
				{
					if (c == '\n')
					{
						crlf += '\r';
					}
					crlf += c;
				}
				text = std::move(crlf);
			}
			if (hasBom)
			{
				text.insert(0, utf8Bom);
			}
			return text;
		}
		catch (const std::exception&)
		{
			// A guard: dump() throws for a string that is not valid UTF-8, which the parser already
			// rejects; std::bad_alloc.
			return std::nullopt;
		}
	}
}
