#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace editor
{
    /**
     * Native Win32 splash screen displayed during editor initialization.
     *
     * Runs on a separate thread to allow status updates while the main
     * thread performs heavy initialization (Vulkan, GLFW, etc.).
     *
     * Uses GDI+ for PNG image loading and rendering.
     */
    class SplashScreen
    {
    public:
        /**
         * Gets the singleton instance.
         */
        static SplashScreen& instance();

        /**
         * Shows the splash screen window.
         * Creates and starts the window thread.
         */
        void show();

        /**
         * Closes the splash screen and waits for thread to finish.
         */
        void close();

        /**
         * Updates the status text displayed on the splash.
         * Thread-safe, can be called from any thread.
         *
         * @param status The status message to display
         */
        void setStatus(const std::string& status);

    private:
        SplashScreen() = default;
        ~SplashScreen();

        // Non-copyable
        SplashScreen(const SplashScreen&) = delete;
        SplashScreen& operator=(const SplashScreen&) = delete;

#ifdef _WIN32
        void windowThread();
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void render(HDC hdc);
        void loadSplashImage();

        HWND hwnd = nullptr;
        ULONG_PTR gdiplusToken = 0;
        void* splashImage = nullptr;  // Gdiplus::Image*
#endif

        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<bool> shouldClose{false};

        std::mutex statusMutex;
        std::string currentStatus = "Starting...";

        static constexpr int WINDOW_WIDTH = 600;
        static constexpr int WINDOW_HEIGHT = 450;
    };

} // namespace editor
