#pragma once
#include "nfd/FileDialog.hpp"
#include <string>

namespace windows
{
	// Export Game dialog (File > Export Game...): collects the output directory
	// and export options, then fires ExportGameCommand. Progress is shown by the
	// separate ExportProgressWindow, which listens for the export notifications.
	class ExportGameWindow
	{
	private:
		bool visible = false;
		nfd::FileDialog fileDialog;

		std::string outputDirectory;
		bool cleanBuild = false;
		bool verifyIntegrity = true;
		bool buildScripts = true;

		std::string gameName;
		std::string gameVersion;

		void loadFromPreferences();
		void saveToPreferences() const;
		void startExport();

	public:
		void draw();
		void show();
	};
}
