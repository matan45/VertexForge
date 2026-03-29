#pragma once
#ifdef APIENTRY
#undef APIENTRY
#endif

#include "spdlog/spdlog.h"
#include "spdlog/fmt/bundled/core.h"
#include "LogEntry.hpp"

#include <chrono>
#include <format>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>
#include <atomic>


#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define vfLogTrace(...) util::logTrace(__VA_ARGS__)
#define vfLogDebug(...) util::logDebug(__VA_ARGS__)
#define vfLogInfo(...) util::logInfo(__VA_ARGS__)
#define vfLogWarning(...) util::logWarning(__VA_ARGS__)
#define vfLogError(...) util::logError(__VA_ARGS__)
#define vfLogAssert(condition, ...) util::logAssert(__FILENAME__, __LINE__, condition, __VA_ARGS__)


namespace util {

	// Minimum log level threshold. Default Info shows Info/Warning/Error.
	// Set to LogLevel::Debug or LogLevel::Trace for verbose output.
	// Set to LogLevel::Error for shipped/exported games (errors always log).
	inline LogLevel minLogLevel = LogLevel::Info;

	inline std::string getCurrentTime() {
		auto now = std::chrono::system_clock::now();
		std::time_t now_time = std::chrono::system_clock::to_time_t(now);

		std::tm local_time;
		localtime_s(&local_time, &now_time);

		std::ostringstream oss;
		oss << std::put_time(&local_time, "[%H:%M:%S] ");
		return oss.str();
	}

	inline std::mutex imguiConsoleBufferMutex;
	inline std::vector<LogEntry> imguiConsoleBuffer;
	inline std::atomic<uint64_t> logSequenceCounter{0};

	inline void appendToConsoleBuffer(const std::string& message, LogLevel level) {
		std::lock_guard<std::mutex> lock(imguiConsoleBufferMutex);
		imguiConsoleBuffer.emplace_back(message, level, logSequenceCounter++);
	}

	inline void setConsoleColor(WORD color) {
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
	}

	inline void resetConsoleColor() {
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);
	}

	template<typename... Args>
	using format_string_t = fmt::format_string<Args...>;

	template<typename... Args>
	inline void logTrace(format_string_t<Args...> fmt, Args&&... args) {
		if (minLogLevel > LogLevel::Trace) return;
		std::string currentTime = getCurrentTime();
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}TRACE: {}", currentTime, formattedMessage);

		setConsoleColor(FOREGROUND_INTENSITY);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColor();

		spdlog::trace(formattedMessage);
		appendToConsoleBuffer(fullMessage, LogLevel::Trace);
	}

	template<typename... Args>
	inline void logDebug(format_string_t<Args...> fmt, Args&&... args) {
		if (minLogLevel > LogLevel::Debug) return;
		std::string currentTime = getCurrentTime();
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}DEBUG: {}", currentTime, formattedMessage);

		setConsoleColor(FOREGROUND_GREEN | FOREGROUND_BLUE);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColor();

		spdlog::debug(formattedMessage);
		appendToConsoleBuffer(fullMessage, LogLevel::Debug);
	}

	template<typename... Args>
	inline void logInfo(format_string_t<Args...> fmt, Args&&... args) {
		if (minLogLevel > LogLevel::Info) return;
		std::string currentTime = getCurrentTime();
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}INFO: {}", currentTime, formattedMessage);

		setConsoleColor(FOREGROUND_GREEN);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColor();

		spdlog::info(formattedMessage);
		appendToConsoleBuffer(fullMessage, LogLevel::Info);
	}

	template<typename... Args>
	inline void logWarning(format_string_t<Args...> fmt, Args&&... args) {
		if (minLogLevel > LogLevel::Warning) return;
		std::string currentTime = getCurrentTime();
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}WARNING: {}", currentTime, formattedMessage);

		setConsoleColor(FOREGROUND_GREEN | FOREGROUND_RED);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColor();

		spdlog::warn(formattedMessage);
		appendToConsoleBuffer(fullMessage, LogLevel::Warning);
	}

	template<typename... Args>
	inline void logError(format_string_t<Args...> fmt, Args&&... args) {
		std::string currentTime = getCurrentTime();
		std::string formattedMessage = fmt::format(fmt, std::forward<Args>(args)...);
		std::string fullMessage = fmt::format("{}ERROR: {}", currentTime, formattedMessage);

		setConsoleColor(FOREGROUND_RED);
		printf("%s\n", fullMessage.c_str());
		resetConsoleColor();

		spdlog::error(formattedMessage);
		appendToConsoleBuffer(fullMessage, LogLevel::Error);
	}

	template<typename... Args>
	inline void logAssert(const char* filename, int line, bool condition, format_string_t<Args...> fmt, Args&&... args)
	{
		if (condition) {
			std::string msg = fmt::format(fmt, std::forward<Args>(args)...);
			std::string fullErrorMessage = std::format(
				"Critical Assertion Failure\r\n\r\n{} (line {}) Assertion Failure: \r\n{}",
				filename, line, msg);

			logError("{}", fullErrorMessage);

			MessageBoxA(nullptr, fullErrorMessage.c_str(), "Critical Assertion Failure", MB_ICONEXCLAMATION | MB_OK);
			exit(-1);
		}
	}

}  // namespace util
