#pragma once
#ifdef APIENTRY
#undef APIENTRY
#endif

#include "spdlog/spdlog.h"
#include "spdlog/fmt/bundled/core.h"  // fmt library used by spdlog
#include "LogEntry.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>


#define vfLogInfo(...) util::infoLogEditor(__VA_ARGS__)
#define vfLogWarning(...) util::warningLogEditor(__VA_ARGS__)
#define vfLogError(...) util::infoErrorEditor(__VA_ARGS__)


namespace util {

	inline std::string getCurrentTimeEditor() {
		// Get the current time
		auto now = std::chrono::system_clock::now();
		std::time_t now_time = std::chrono::system_clock::to_time_t(now);

		// Convert it to a tm structure
		std::tm local_time;
		localtime_s(&local_time, &now_time);

		// Format the time as [HH:MM:SS]
		std::ostringstream oss;
		oss << std::put_time(&local_time, "[%H:%M:%S] ");
		return oss.str();
	}

	// Mutex for thread-safe access to imguiConsoleBuffer
	inline std::mutex imguiConsoleBufferMutex;

	// Buffer to store log messages for ImGui console
	inline std::vector<LogEntry> imguiConsoleBuffer;

	// Sequence counter for stable ordering
	inline uint64_t logSequenceCounter = 0;

	// Append log message to ImGui buffer (thread-safe)
	inline void appendToImGuiConsoleEditor(const std::string& message, LogLevel level) {
		std::lock_guard<std::mutex> lock(imguiConsoleBufferMutex);
		imguiConsoleBuffer.emplace_back(message, level, logSequenceCounter++);
	}

	// Helper function to set console text color (Windows-specific)
	inline void setConsoleColorEditor(WORD color) {
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
	}

	// Helper function to reset console text color to default
	inline void resetConsoleColorEditor() {
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);  // Reset to normal
	}

	template<typename... Args>
	using format_string_t = fmt::format_string<Args...>;

	// Info logging function (without file name and line)
	template<typename... Args>
	inline void infoLogEditor(format_string_t<Args...> fmt, Args&&... args) {
		std::string currentTime = getCurrentTimeEditor();

		// Format log message with just the message
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}INFO: {}", currentTime, formattedMessage);

		// Set console color to green for info
		setConsoleColorEditor(FOREGROUND_GREEN);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColorEditor();

		// Log using spdlog
		spdlog::info(formattedMessage);

		// Append the log with a timestamp to ImGui buffer
		appendToImGuiConsoleEditor(fullMessage, LogLevel::Info);
	}

	// Warning logging function (without file name and line)
	template<typename... Args>
	inline void warningLogEditor(format_string_t<Args...> fmt, Args&&... args) {
		std::string currentTime = getCurrentTimeEditor();

		// Format log message
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}WARNING: {}", currentTime, formattedMessage);

		// Set console color to yellow for warning
		setConsoleColorEditor(FOREGROUND_GREEN | FOREGROUND_RED);  // Yellow text for warning
		printf("%s\n", fullMessage.c_str());
		resetConsoleColorEditor();

		// Log using spdlog
		spdlog::warn(formattedMessage);

		// Append the log with a timestamp to ImGui buffer
		appendToImGuiConsoleEditor(fullMessage, LogLevel::Warning);
	}

	// Error logging function (without file name and line)
	template<typename... Args>
	inline void infoErrorEditor(format_string_t<Args...> fmt, Args&&... args) {
		std::string currentTime = getCurrentTimeEditor();

		// Format log message
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}ERROR: {}", currentTime, formattedMessage);

		// Set console color to red for error
		setConsoleColorEditor(FOREGROUND_RED);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColorEditor();

		// Log using spdlog
		spdlog::error(formattedMessage);

		// Append the log with a timestamp to ImGui buffer
		appendToImGuiConsoleEditor(fullMessage, LogLevel::Error);
	}

}  // namespace util
