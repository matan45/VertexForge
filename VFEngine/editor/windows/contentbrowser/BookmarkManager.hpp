#pragma once
#include <string>
#include <vector>

namespace windows
{
    struct Bookmark
    {
        std::string name;
        std::string path;
    };

    class BookmarkManager
    {
    private:
        std::vector<Bookmark> bookmarks;

    public:
        BookmarkManager();

        void addBookmark(const std::string& path);
        void removeBookmark(size_t index);
        const std::vector<Bookmark>& getBookmarks() const { return bookmarks; }
        bool isBookmarked(const std::string& path) const;

        void save();
        void load();
    };
}
