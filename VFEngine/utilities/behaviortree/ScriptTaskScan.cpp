#include "ScriptTaskScan.hpp"

#include <cstddef>
#include <vector>

namespace behaviortree
{
    namespace
    {
        bool isIdentifierChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        size_t skipSpaces(std::string_view text, size_t pos)
        {
            while (pos < text.size() && isSpace(text[pos]))
                ++pos;
            return pos;
        }

        // The identifier that starts at `pos` ("" when none does).
        std::string_view identifierAt(std::string_view text, size_t pos)
        {
            size_t end = pos;
            while (end < text.size() && isIdentifierChar(text[end]))
                ++end;
            return text.substr(pos, end - pos);
        }

        // `pos` is at the first '/' of a line comment; returns the index of the '\n' that ends it.
        size_t skipLineComment(std::string_view text, size_t pos)
        {
            const size_t end = text.find('\n', pos);
            return end == std::string_view::npos ? text.size() : end;
        }

        // `pos` is at the '/' of a block comment; returns the index just past its "*/".
        size_t skipBlockComment(std::string_view text, size_t pos)
        {
            const size_t close = text.find("*/", pos + 2);
            return close == std::string_view::npos ? text.size() : close + 2;
        }

        size_t skipInterpolation(std::string_view text, size_t pos);

        // `pos` is just past a string's opening quote; returns the index just past its closing quote.
        // An interpolated string ($"...") continues past each {expression}, which may hold strings.
        size_t skipStringBody(std::string_view text, size_t pos, bool interpolated)
        {
            while (pos < text.size())
            {
                const char c = text[pos];
                if (c == '\\' && pos + 1 < text.size())
                {
                    pos += 2;
                    continue;
                }
                if (c == '"')
                    return pos + 1;
                if (interpolated && c == '{')
                {
                    pos = skipInterpolation(text, pos + 1);
                    continue;
                }
                ++pos;
            }
            return text.size();
        }

        // `pos` is just past an interpolation's '{'; returns the index just past its matching '}'.
        size_t skipInterpolation(std::string_view text, size_t pos)
        {
            int depth = 0;
            while (pos < text.size())
            {
                const char c = text[pos];
                const char next = pos + 1 < text.size() ? text[pos + 1] : '\0';
                if (c == '/' && next == '/')
                {
                    pos = skipLineComment(text, pos);
                    continue;
                }
                if (c == '/' && next == '*')
                {
                    pos = skipBlockComment(text, pos);
                    continue;
                }
                if (c == '$' && next == '"')
                {
                    pos = skipStringBody(text, pos + 2, true);
                    continue;
                }
                if (c == '"')
                {
                    pos = skipStringBody(text, pos + 1, false);
                    continue;
                }
                if (c == '{')
                {
                    ++depth;
                }
                else if (c == '}')
                {
                    if (depth == 0)
                        return pos + 1;
                    --depth;
                }
                ++pos;
            }
            return text.size();
        }

        // `source` with every comment and string literal replaced by spaces (newlines kept), so the
        // declarations below are matched only on what the compiler reads as code.
        std::string blankCommentsAndStrings(std::string_view source)
        {
            std::string code(source);
            size_t pos = 0;
            while (pos < source.size())
            {
                const char c = source[pos];
                const char next = pos + 1 < source.size() ? source[pos + 1] : '\0';
                size_t end = pos;
                if (c == '/' && next == '/')
                    end = skipLineComment(source, pos);
                else if (c == '/' && next == '*')
                    end = skipBlockComment(source, pos);
                else if (c == '$' && next == '"')
                    end = skipStringBody(source, pos + 2, true);
                else if (c == '"')
                    end = skipStringBody(source, pos + 1, false);

                if (end == pos)
                {
                    ++pos;
                    continue;
                }
                for (size_t i = pos; i < end; ++i)
                {
                    if (code[i] != '\n')
                        code[i] = ' ';
                }
                pos = end;
            }
            return code;
        }

        struct ScriptClass
        {
            std::string name;
            size_t afterName = 0;
        };

        // The first match of ScriptingAdapter::extractClassName's pattern, @Script\s+(public\s+)?class\s+(\w+).
        std::optional<ScriptClass> findScriptClass(std::string_view code)
        {
            constexpr std::string_view annotation = "@Script";
            size_t from = 0;
            while (true)
            {
                const size_t at = code.find(annotation, from);
                if (at == std::string_view::npos)
                    return std::nullopt;
                from = at + annotation.size();

                size_t pos = from;
                if (pos >= code.size() || !isSpace(code[pos]))
                    continue;
                pos = skipSpaces(code, pos);

                std::string_view word = identifierAt(code, pos);
                if (word == "public")
                {
                    pos += word.size();
                    if (pos >= code.size() || !isSpace(code[pos]))
                        continue;
                    pos = skipSpaces(code, pos);
                    word = identifierAt(code, pos);
                }
                if (word != "class")
                    continue;

                pos += word.size();
                if (pos >= code.size() || !isSpace(code[pos]))
                    continue;
                pos = skipSpaces(code, pos);

                const std::string_view name = identifierAt(code, pos);
                if (name.empty())
                    continue;
                return ScriptClass{std::string(name), pos + name.size()};
            }
        }

        // The leading block of // lines (blank lines before it skipped), each without its slashes and
        // one following space, joined by '\n'.
        std::string leadingCommentBlock(std::string_view source)
        {
            constexpr std::string_view utf8Bom = "\xEF\xBB\xBF";
            if (source.starts_with(utf8Bom))
                source.remove_prefix(utf8Bom.size());

            std::vector<std::string_view> lines;
            size_t pos = 0;
            while (pos < source.size())
            {
                size_t end = source.find('\n', pos);
                if (end == std::string_view::npos)
                    end = source.size();
                std::string_view line = source.substr(pos, end - pos);
                pos = end + 1;

                const size_t first = line.find_first_not_of(" \t\r");
                if (first == std::string_view::npos)
                {
                    if (lines.empty())
                        continue;
                    break;
                }
                line.remove_prefix(first);
                if (!line.starts_with("//"))
                    break;

                const size_t text = line.find_first_not_of('/');
                line = text == std::string_view::npos ? std::string_view{} : line.substr(text);
                if (line.starts_with(' '))
                    line.remove_prefix(1);
                const size_t last = line.find_last_not_of(" \t\r");
                line = last == std::string_view::npos ? std::string_view{} : line.substr(0, last + 1);
                lines.push_back(line);
            }

            while (!lines.empty() && lines.back().empty())
                lines.pop_back();

            std::string summary;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (i > 0)
                    summary += '\n';
                summary += lines[i];
            }
            return summary;
        }
    }

    std::optional<ScriptTaskInfo> scanScriptTask(std::string_view source)
    {
        const std::string code = blankCommentsAndStrings(source);
        const std::optional<ScriptClass> scriptClass = findScriptClass(code);
        if (!scriptClass)
            return std::nullopt;

        const size_t open = code.find('{', scriptClass->afterName);
        if (open == std::string::npos)
            return std::nullopt;

        ScriptTaskInfo info;
        info.className = scriptClass->name;
        bool hasTick = false;

        // Walk the class body; only declarations directly inside it (depth 1) are members.
        int depth = 0;
        for (size_t pos = open; pos < code.size(); ++pos)
        {
            const char c = code[pos];
            if (c == '{')
            {
                ++depth;
                continue;
            }
            if (c == '}')
            {
                if (--depth == 0)
                    break;
                continue;
            }
            if (!isIdentifierChar(c) || isIdentifierChar(code[pos - 1]))
                continue;

            const std::string_view word = identifierAt(code, pos);
            if (depth == 1 && word == "function")
            {
                const size_t namePos = skipSpaces(code, pos + word.size());
                const std::string_view name = identifierAt(code, namePos);
                const size_t paren = skipSpaces(code, namePos + name.size());
                if (!name.empty() && paren < code.size() && code[paren] == '(')
                {
                    if (name == "tick")
                    {
                        if (identifierAt(code, skipSpaces(code, paren + 1)) == "float")
                            hasTick = true;
                    }
                    else if (name == "onStart")
                    {
                        info.hasOnStart = true;
                    }
                    else if (name == "onAbort")
                    {
                        info.hasOnAbort = true;
                    }
                    else if (name == "onEnd")
                    {
                        info.hasOnEnd = true;
                    }
                }
            }
            pos += word.size() - 1;
        }

        if (!hasTick)
            return std::nullopt;
        info.summary = leadingCommentBlock(source);
        return info;
    }
}
