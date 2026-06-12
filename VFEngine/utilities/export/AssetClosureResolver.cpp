#include "AssetClosureResolver.hpp"
#include "../asset/AssetDatabase.hpp"
#include "../asset/DependencyScanner.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <deque>

namespace gameExport
{
	namespace fs = std::filesystem;

	namespace
	{
		std::string toLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
						   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		// Glob match on already-lowercased strings. '**' spans '/', '*' and '?' do not.
		bool globMatch(const char* text, const char* pattern)
		{
			while (*pattern)
			{
				if (*pattern == '*')
				{
					bool doubleStar = pattern[1] == '*';
					const char* next = pattern + (doubleStar ? 2 : 1);
					// '**/' also matches zero directories
					if (doubleStar && *next == '/')
					{
						if (globMatch(text, next + 1)) return true;
					}
					for (const char* t = text;; ++t)
					{
						if (globMatch(t, next)) return true;
						if (*t == '\0' || (!doubleStar && *t == '/')) return false;
					}
				}
				if (*text == '\0') return false;
				if (*pattern != '?' && *pattern != *text) return false;
				++pattern;
				++text;
			}
			return *text == '\0';
		}

		// Safety net for asset references the dependency graph cannot see
		// (scripts load by raw path) plus runtime-required loose data.
		const std::vector<std::string>& builtinAlwaysIncludePatterns()
		{
			static const std::vector<std::string> patterns = {
				"scripts/**",
				"*.vfsettings",
				"*.vfmeta",
				"*.vfnavindex",
				"*.vfnavtile",
				"*.vfinputmapping",
				"*.vftheme",
				"*.ttf",
				"*.otf",
			};
			return patterns;
		}
	}

	AssetClosure AssetClosureResolver::resolve(const fs::path& projectRoot, bool refreshScan)
	{
		AssetClosure closure;
		auto& db = asset::AssetDatabase::instance();

		if (db.getAssetCount() == 0)
		{
			vfLogWarning("Asset closure: database is empty, skipping unreferenced-asset analysis");
			return closure;
		}

		if (refreshScan)
		{
			asset::DependencyScanner::scanAll(projectRoot.generic_string());
		}

		// Seed from every scene in the project
		std::deque<asset::AssetGUID> pending;
		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(projectRoot, ec);
		     it != fs::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file()) continue;
			if (toLower(it->path().extension().string()) != ".vfscene") continue;

			if (auto guid = db.getGUID(it->path().string()))
			{
				if (closure.referencedGuids.insert(*guid).second)
				{
					pending.push_back(*guid);
				}
			}
		}

		if (pending.empty())
		{
			vfLogWarning("Asset closure: no scene resolved to a database GUID, skipping unreferenced-asset analysis");
			return closure;
		}

		while (!pending.empty())
		{
			asset::AssetGUID current = pending.front();
			pending.pop_front();

			for (const auto& dep : db.getDependencies(current))
			{
				if (closure.referencedGuids.insert(dep).second)
				{
					pending.push_back(dep);
				}
			}
		}

		closure.valid = true;
		vfLogInfo("Asset closure: {} assets reachable from scenes ({} in database)",
				  closure.referencedGuids.size(), db.getAssetCount());
		return closure;
	}

	bool AssetClosureResolver::matchesPattern(const std::string& relativePath, const std::string& pattern)
	{
		if (pattern.empty()) return false;

		std::string path = toLower(relativePath);
		std::string glob = toLower(pattern);

		// Pattern without '/' matches the filename only (gitignore convention)
		if (glob.find('/') == std::string::npos)
		{
			auto lastSlash = path.find_last_of('/');
			if (lastSlash != std::string::npos)
			{
				path = path.substr(lastSlash + 1);
			}
		}

		return globMatch(path.c_str(), glob.c_str());
	}

	bool AssetClosureResolver::isAlwaysIncluded(const std::string& relativePath,
												const std::vector<std::string>& userPatterns)
	{
		for (const auto& pattern : builtinAlwaysIncludePatterns())
		{
			if (matchesPattern(relativePath, pattern)) return true;
		}
		for (const auto& pattern : userPatterns)
		{
			if (matchesPattern(relativePath, pattern)) return true;
		}
		return false;
	}
}
