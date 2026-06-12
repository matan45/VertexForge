#pragma once
#include "../EventTypes.hpp"
#include <string>
#include <vector>

namespace events::gameExport
{
	// ============================================
	// COMMANDS
	// ============================================

	struct ExportGameCommand : ICommand<bool> {
		std::string outputDirectory;
		bool cleanBuild = false;
		bool verifyIntegrity = true;
		bool buildScripts = true;

		std::string_view getName() const override { return "ExportGame"; }
	};

	// ============================================
	// QUERIES
	// ============================================

	struct CanExportQuery : IQuery<bool> {
		std::string_view getName() const override { return "CanExport"; }
	};

	// ============================================
	// NOTIFICATIONS
	// ============================================

	struct ExportStartedNotification : INotification {
		std::string outputDirectory;

		std::string_view getName() const override { return "ExportStarted"; }
	};

	struct ExportProgressNotification : INotification {
		float progress = 0.0f;
		std::string currentStep;

		std::string_view getName() const override { return "ExportProgress"; }
	};

	struct ExportCompletedNotification : INotification {
		bool success = false;
		std::string errorMessage;
		std::vector<std::string> warnings;
		std::string outputPath;

		std::string_view getName() const override { return "ExportCompleted"; }
	};
}
