#pragma once
#include "../clipboard/ClipboardManager.hpp"
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace windows
{
    class AsyncFileOperations
    {
    public:
        static void pasteAsync(std::vector<ClipboardItem> items, ClipboardOperation operation,
                               const std::string& targetFolder, std::function<void()> onCutComplete);

        static void dropAsync(std::vector<std::string> paths, bool isMove,
                              const std::string& targetFolder);

        static void deleteAsync(const std::string& path);
        static void deleteAsync(const std::vector<std::string>& paths);

        static bool isBusy();

    private:
        static void executeBatch(std::vector<std::string> sourcePaths, bool isMove,
                                 const std::string& targetFolder, std::function<void()> onComplete);

        static std::atomic<bool> busy;
    };
}
