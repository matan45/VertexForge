#pragma once
#include <string>
#include <shobjidl.h>

class StringUtil
{
public:
    static std::string wstringToUtf8(std::wstring_view wstr);
    static std::string WideStringToString(PWSTR wideStr);
    static std::string toLower(const std::string& str);
};
