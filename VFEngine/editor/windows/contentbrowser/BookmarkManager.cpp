#include "BookmarkManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/save/ConfigEvents.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>

namespace windows
{
    using json = nlohmann::json;

    BookmarkManager::BookmarkManager()
    {
        load();
    }

    void BookmarkManager::addBookmark(const std::string& path)
    {
        if (isBookmarked(path)) return;

        Bookmark b;
        b.path = path;
        b.name = std::filesystem::path(path).filename().string();
        if (b.name.empty()) b.name = path;
        bookmarks.push_back(std::move(b));
        save();
    }

    void BookmarkManager::removeBookmark(size_t index)
    {
        if (index < bookmarks.size())
        {
            bookmarks.erase(bookmarks.begin() + static_cast<ptrdiff_t>(index));
            save();
        }
    }

    bool BookmarkManager::isBookmarked(const std::string& path) const
    {
        return std::any_of(bookmarks.begin(), bookmarks.end(),
            [&](const Bookmark& b) { return b.path == path; });
    }

    void BookmarkManager::save()
    {
        json arr = json::array();
        for (const auto& b : bookmarks)
            arr.push_back({{"name", b.name}, {"path", b.path}});

        events::save::SetConfigStringCommand cmd;
        cmd.key = "contentBrowser.bookmarks";
        cmd.value = arr.dump();
        events::EventDispatcher::instance().execute(cmd);
    }

    void BookmarkManager::load()
    {
        events::save::GetConfigStringQuery q;
        q.key = "contentBrowser.bookmarks";
        q.defaultValue = "[]";
        std::string data = events::EventDispatcher::instance().query(q);

        try
        {
            json arr = json::parse(data);
            bookmarks.clear();
            for (const auto& item : arr)
            {
                Bookmark b;
                b.name = item.value("name", "");
                b.path = item.value("path", "");
                if (!b.path.empty())
                    bookmarks.push_back(std::move(b));
            }
        }
        catch (...) {}
    }
}
