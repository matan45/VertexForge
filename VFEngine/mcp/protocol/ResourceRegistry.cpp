#include "ResourceRegistry.hpp"

namespace mcp
{
    namespace
    {
        int hexValue(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        }
    }

    void ResourceRegistry::add(ResourceDef resource)
    {
        for (ResourceDef& existing : staticResources)
        {
            if (existing.uri == resource.uri)
            {
                existing = std::move(resource);
                return;
            }
        }
        staticResources.push_back(std::move(resource));
    }

    void ResourceRegistry::addTemplate(ResourceTemplateDef resourceTemplate)
    {
        for (ResourceTemplateDef& existing : resourceTemplates)
        {
            if (existing.uriTemplate == resourceTemplate.uriTemplate)
            {
                existing = std::move(resourceTemplate);
                return;
            }
        }
        resourceTemplates.push_back(std::move(resourceTemplate));
    }

    void ResourceRegistry::addContributor(ResourceListContributor contributor)
    {
        listContributors.push_back(std::move(contributor));
    }

    const ResourceDef* ResourceRegistry::findExact(std::string_view uri) const
    {
        for (const ResourceDef& resource : staticResources)
        {
            if (resource.uri == uri)
            {
                return &resource;
            }
        }
        return nullptr;
    }

    const ResourceTemplateDef* ResourceRegistry::findTemplate(std::string_view uri) const
    {
        const ResourceTemplateDef* best = nullptr;
        for (const ResourceTemplateDef& resourceTemplate : resourceTemplates)
        {
            if (resourceTemplate.prefix.empty() || !uri.starts_with(resourceTemplate.prefix))
            {
                continue;
            }
            if (!best || resourceTemplate.prefix.size() > best->prefix.size())
            {
                best = &resourceTemplate;
            }
        }
        return best;
    }

    nlohmann::json ResourceRegistry::describe(const ResourceDef& resource)
    {
        nlohmann::json entry{
            {"uri", resource.uri},
            {"name", resource.name},
            {"description", resource.description},
            {"mimeType", resource.mimeType}
        };
        if (!resource.title.empty())
        {
            entry["title"] = resource.title;
        }
        return entry;
    }

    nlohmann::json ResourceRegistry::describe(const ResourceTemplateDef& resourceTemplate)
    {
        nlohmann::json entry{
            {"uriTemplate", resourceTemplate.uriTemplate},
            {"name", resourceTemplate.name},
            {"description", resourceTemplate.description},
            {"mimeType", resourceTemplate.mimeType}
        };
        if (!resourceTemplate.title.empty())
        {
            entry["title"] = resourceTemplate.title;
        }
        return entry;
    }

    std::optional<std::string> ResourceRegistry::percentDecode(std::string_view text)
    {
        std::string decoded;
        decoded.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c != '%')
            {
                // '+' stays literal: URIs are not form-encoded.
                decoded.push_back(c);
                continue;
            }
            if (i + 2 >= text.size())
            {
                return std::nullopt;
            }
            const int high = hexValue(text[i + 1]);
            const int low = hexValue(text[i + 2]);
            if (high < 0 || low < 0)
            {
                return std::nullopt;
            }
            const int value = high * 16 + low;
            if (value == 0)
            {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>(value));
            i += 2;
        }
        return decoded;
    }
}
