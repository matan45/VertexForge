#pragma once

#ifdef APIENTRY
#undef APIENTRY
#endif

#include "spdlog/spdlog.h"
#include "spdlog/fmt/bundled/core.h"  // fmt library used by spdlog
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>  // Vulkan header (for vk::ArrayWrapper1D)

#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define loggerInfo(...) util::infoLog(__FILENAME__, __LINE__, __VA_ARGS__)
#define loggerWarning(...) util::warningLog(__FILENAME__, __LINE__, __VA_ARGS__)
#define loggerError(...) util::infoError(__FILENAME__, __LINE__, __VA_ARGS__)
#define loggerAssert(condition, ...) util::infoAssert(__FILENAME__, __LINE__, condition, __VA_ARGS__)



template <>
struct fmt::formatter<vk::ArrayWrapper1D<char, 256>> {
	// Parse the format specifier (we don't use any specifiers, so just return)
	constexpr auto parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin()) {
		return ctx.end();
	}

	// Format the object
	template <typename FormatContext>
	auto format(const vk::ArrayWrapper1D<char, 256>& obj, FormatContext& ctx) const -> decltype(ctx.out()) {
		// Get a pointer to the underlying char array
		const char* charArray = obj.data();

		// Create a string from the array, avoiding trailing null characters
		std::string str(charArray, strnlen(charArray, 256));

		// Format the string and its size
		return fmt::format_to(ctx.out(), "{}", str);
	}
};


namespace util {



	// Helper function to set console text color
	inline void setConsoleColor(WORD color)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
	}

	// Helper function to reset console text color to default
	inline void resetConsoleColor()
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 0x0F);  // Reset to normal
	}

	template<typename... Args>
	using format_string_t = fmt::format_string<Args...>;

	// Info logging function
	template<typename... Args>
	inline void infoLog(const char* filename, int line, format_string_t<Args...> fmt, Args&&... args)
	{
		setConsoleColor(FOREGROUND_GREEN);  // Green text for info
		printf("%s (line %d) INFO: \n", filename, line);
		resetConsoleColor();

		spdlog::info(fmt, std::forward<Args>(args)...);
	}

	// Warning logging function
	template<typename... Args>
	inline void warningLog(const char* filename, int line, format_string_t<Args...> fmt, Args&&... args)
	{
		setConsoleColor(FOREGROUND_GREEN | FOREGROUND_RED);  // Yellow text for warning
		printf("%s (line %d) WARNING: \n", filename, line);
		resetConsoleColor();

		spdlog::warn(fmt, std::forward<Args>(args)...);
	}

	// Error logging function
	template<typename... Args>
	inline void infoError(const char* filename, int line, format_string_t<Args...> fmt, Args&&... args)
	{
		setConsoleColor(FOREGROUND_RED);  // Red text for error
		printf("%s (line %d) ERROR: \n", filename, line);
		resetConsoleColor();

		spdlog::error(fmt, std::forward<Args>(args)...);
	}

	// Assertion logging function
	template<typename... Args>
	inline void infoAssert(const char* filename, int line, bool condition, format_string_t<Args...> fmt, Args&&... args)
	{
		if (condition) {
			const std::string fullErrorMessage = std::format(
				"Critical Assertion Failure\r\n\r\n{} (line {}) Assertion Failure: \r\n",
				filename,
				line);

			setConsoleColor(FOREGROUND_RED);  // Red text for assertion failure
			printf("%s", fullErrorMessage.c_str());
			resetConsoleColor();

			spdlog::error(fmt, std::forward<Args>(args)...);

			MessageBoxA(nullptr, fullErrorMessage.c_str(), "Critical Assertion Failure", MB_ICONEXCLAMATION | MB_OK);
			exit(-1);
		}
	}

}  // namespace util
