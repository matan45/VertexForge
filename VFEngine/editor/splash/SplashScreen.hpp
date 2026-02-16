#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace editor
{
    class SplashScreen
    {
    private:
        HWND hwnd = nullptr;
        ULONG_PTR gdiplusToken = 0;
        void* splashImage = nullptr; // Gdiplus::Image*

        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> shouldClose{false};

        std::mutex statusMutex;
        std::string currentStatus = "Starting...";

        static constexpr int WINDOW_WIDTH = 800;
        static constexpr int WINDOW_HEIGHT = 600;

    public:
        static SplashScreen& instance();

        void show();
        void close();

        void setStatus(const std::string& status);

    private:
        SplashScreen() = default;
        ~SplashScreen();

        // Non-copyable
        SplashScreen(const SplashScreen&) = delete;
        SplashScreen& operator=(const SplashScreen&) = delete;

        void windowThread();
        bool registerWindowClass(const wchar_t* className);
        bool createSplashWindow(const wchar_t* className);
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void render(HDC hdc);
        void renderBackground(HDC hdc);
        void renderStatusText(HDC hdc);
        void renderLoadingIndicator(HDC hdc);
        void loadSplashImage();
    };
}
