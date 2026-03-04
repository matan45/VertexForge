#include "PathResolver.hpp"
#include "../print/Log.hpp"

namespace resource
{
	void PathResolver::initialize()
	{
		if (initialized) return;
		initialized = true;

		// If "resources/shaders/" exists next to the executable, we're in an exported build
		exportedBuild = std::filesystem::exists("resources/shaders");

		if (exportedBuild)
		{
			vfLogInfo("PathResolver: Detected exported build mode");
		}
	}

	std::string PathResolver::resolveEnginePath(const std::string& path)
	{
		if (!exportedBuild) return path;

		const std::string devPrefix = "../../resources/";
		if (path.size() > devPrefix.size() &&
			path.compare(0, devPrefix.size(), devPrefix) == 0)
		{
			return "resources/" + path.substr(devPrefix.size());
		}

		return path;
	}
}
