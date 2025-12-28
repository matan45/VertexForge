#include "StringUtil.hpp"
#include <Windows.h>
#include <stdexcept>
#include <algorithm>
#include "../print/EditorLogger.hpp"

std::string StringUtil::wstringToUtf8(std::wstring_view wstr)
{
    if (wstr.empty())
    {
        return "";
    }

    const auto size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr,
                                                 nullptr);
    if (size_needed <= 0)
    {
        vfLogError("WideCharToMultiByte() failed: {}", std::to_string(size_needed));
        return std::string();
    }

    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(), size_needed, nullptr, nullptr);
    return result;
}

std::string StringUtil::WideStringToString(PWSTR wideStr)
{
    if (!wideStr || !*wideStr) {
        return std::string();
    }
    // Note: -1 means null-terminated input, and sizeNeeded includes the null terminator
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (sizeNeeded <= 1) {
        return std::string();
    }
    // Allocate size - 1 to exclude the null terminator from std::string
    std::string strTo(sizeNeeded - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &strTo[0], sizeNeeded, NULL, NULL);
    return strTo;
}

std::wstring StringUtil::utf8ToWstring(const std::string& utf8Str)
{
    if (utf8Str.empty()) {
        return std::wstring();
    }
    // Note: -1 means null-terminated input, and wideSize includes the null terminator
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wideSize <= 1) {
        return std::wstring();
    }
    // Allocate size - 1 to exclude the null terminator from std::wstring
    std::wstring wideString(wideSize - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &wideString[0], wideSize);
    return wideString;
}

std::string StringUtil::toLower(const std::string& str)
{
    std::string lowerStr = str;
    std::ranges::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

// Function to find the first non-whitespace character in a vector
size_t StringUtil::findFirstNotOf(const std::vector<unsigned char>& data, const std::string& chars)
{
    for (size_t i = 0; i < data.size(); ++i)
    {
        // Check if the character is not in the specified set
        if (chars.find(static_cast<char>(data[i])) == std::string::npos)
        {
            return i;
        }
    }
    return std::string::npos; // Return npos if all characters are in the specified set
}
