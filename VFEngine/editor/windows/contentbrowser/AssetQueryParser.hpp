#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace windows
{
    // Parsed form of the content browser search box. Plain words are substring
    // terms (AND-ed); key:value tokens add structured constraints. Values may
    // be double-quoted to contain spaces (type:"material instance"); unknown
    // keys degrade to plain terms; the last occurrence of a key wins.
    struct ParsedAssetQuery
    {
        std::vector<std::string> terms; // lowercased substring terms
        std::string typeToken;          // lowercased type label
        std::string extToken;           // lowercased, leading dot guaranteed
        std::string guidToken;          // lowercased hex (validated by the resolver)
        std::string refToken;           // path or guid the results must reference... see resolver

        bool hasStructuredTokens() const
        {
            return !typeToken.empty() || !extToken.empty() ||
                   !guidToken.empty() || !refToken.empty();
        }
    };

    namespace querydetail
    {
        inline std::string toLowerCopy(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        inline std::string stripQuotes(std::string s)
        {
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        }
    }

    // Lowercase + forward slashes, for comparing editor file paths against
    // asset-database paths regardless of separator/case style.
    inline std::string normalizePathForCompare(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return querydetail::toLowerCopy(std::move(path));
    }

    inline ParsedAssetQuery parseAssetQuery(std::string_view query)
    {
        ParsedAssetQuery result;

        // Whitespace tokenization with double-quote support, so quoted values
        // and quoted plain terms survive as one token.
        std::vector<std::string> tokens;
        std::string current;
        bool inQuotes = false;
        for (char c : query)
        {
            if (c == '"')
            {
                inQuotes = !inQuotes;
                current += c;
                continue;
            }
            if (!inQuotes && std::isspace(static_cast<unsigned char>(c)))
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
                continue;
            }
            current += c;
        }
        if (!current.empty())
            tokens.push_back(current);

        for (const auto& token : tokens)
        {
            auto colon = token.find(':');
            if (colon != std::string::npos && colon > 0)
            {
                std::string key = querydetail::toLowerCopy(token.substr(0, colon));
                std::string value = querydetail::stripQuotes(token.substr(colon + 1));
                if (!value.empty())
                {
                    if (key == "type")
                    {
                        result.typeToken = querydetail::toLowerCopy(value);
                        continue;
                    }
                    if (key == "ext")
                    {
                        std::string ext = querydetail::toLowerCopy(value);
                        if (ext.front() != '.')
                            ext.insert(ext.begin(), '.');
                        result.extToken = ext;
                        continue;
                    }
                    if (key == "guid")
                    {
                        result.guidToken = querydetail::toLowerCopy(value);
                        continue;
                    }
                    if (key == "ref")
                    {
                        result.refToken = value;
                        continue;
                    }
                }
            }

            std::string term = querydetail::stripQuotes(token);
            if (!term.empty())
                result.terms.push_back(querydetail::toLowerCopy(term));
        }

        return result;
    }
}
