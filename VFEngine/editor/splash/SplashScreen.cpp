#include "SplashScreen.hpp"

#ifdef _WIN32
// Windows headers must be included in correct order for GDI+
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#pragma comment(lib, "gdiplus.lib")
#endif

namespace editor
{
    SplashScreen& SplashScreen::instance()
    {
        static SplashScreen instance;
        return instance;
    }

    SplashScreen::~SplashScreen()
    {
        close();
    }

    void SplashScreen::setStatus(const std::string& status)
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        currentStatus = status;

#ifdef _WIN32
        if (hwnd && running)
        {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
#endif
    }

#ifdef _WIN32

    void SplashScreen::show()
    {
        if (running)
        {
            return;
        }

        running = true;
        shouldClose = false;

        thread = std::thread(&SplashScreen::windowThread, this);
    }

    void SplashScreen::close()
    {
        if (!running)
        {
            return;
        }

        shouldClose = true;

        if (hwnd)
        {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }

        if (thread.joinable())
        {
            thread.join();
        }

        running = false;
    }

    void SplashScreen::windowThread()
    {
        // Initialize GDI+
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

        // Load splash image
        loadSplashImage();

        // Register window class (may already be registered from previous show() call)
        const wchar_t* className = L"VertexForgeSplash";
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        wc.hbrBackground = nullptr;  // We'll paint ourselves

        ATOM classAtom = RegisterClassExW(&wc);
        if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            // Registration failed for reason other than already exists
            if (splashImage)
            {
                delete static_cast<Gdiplus::Image*>(splashImage);
                splashImage = nullptr;
            }
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return;
        }

        // Calculate center position
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - WINDOW_WIDTH) / 2;
        int y = (screenHeight - WINDOW_HEIGHT) / 2;

        // Create borderless window
        hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            className,
            L"VertexForge",
            WS_POPUP,
            x, y, WINDOW_WIDTH, WINDOW_HEIGHT,
            nullptr, nullptr,
            GetModuleHandle(nullptr),
            this
        );

        if (!hwnd)
        {
            UnregisterClassW(className, GetModuleHandle(nullptr));
            if (splashImage)
            {
                delete static_cast<Gdiplus::Image*>(splashImage);
                splashImage = nullptr;
            }
            Gdiplus::GdiplusShutdown(gdiplusToken);
            return;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        // Message loop
        MSG msg;
        while (!shouldClose && GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Cleanup
        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }

        UnregisterClassW(className, GetModuleHandle(nullptr));

        if (splashImage)
        {
            delete static_cast<Gdiplus::Image*>(splashImage);
            splashImage = nullptr;
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);
    }

    LRESULT CALLBACK SplashScreen::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        SplashScreen* self = nullptr;

        if (msg == WM_CREATE)
        {
            auto createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = static_cast<SplashScreen*>(createStruct->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<SplashScreen*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        switch (msg)
        {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (self)
                {
                    self->render(hdc);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_ERASEBKGND:
                return 1;  // Prevent flicker

            case WM_CLOSE:
                PostQuitMessage(0);
                return 0;

            default:
                return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    void SplashScreen::render(HDC hdc)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        // Dark background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 30, 35));
        graphics.FillRectangle(&bgBrush, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

        // Border
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 80, 80, 90), 1);
        graphics.DrawRectangle(&borderPen, 0, 0, WINDOW_WIDTH - 1, WINDOW_HEIGHT - 1);

        int yOffset = 30;

        // Draw splash image if loaded
        if (splashImage)
        {
            auto* image = static_cast<Gdiplus::Image*>(splashImage);
            int imgWidth = image->GetWidth();
            int imgHeight = image->GetHeight();

            // Validate image dimensions to prevent division by zero
            if (imgWidth > 0 && imgHeight > 0)
            {
                // Scale to fit while maintaining aspect ratio
                int maxWidth = WINDOW_WIDTH - 40;
                int maxHeight = 300;
                float scale = (std::min)(static_cast<float>(maxWidth) / imgWidth,
                                         static_cast<float>(maxHeight) / imgHeight);
                int drawWidth = static_cast<int>(imgWidth * scale);
                int drawHeight = static_cast<int>(imgHeight * scale);
                int imgX = (WINDOW_WIDTH - drawWidth) / 2;

                graphics.DrawImage(image, imgX, yOffset, drawWidth, drawHeight);
                yOffset += drawHeight + 15;
            }
        }
        else
        {
            // Draw title text if no image
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            Gdiplus::Font titleFont(&fontFamily, 28, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));

            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF titleRect(0, static_cast<float>(yOffset),
                                     static_cast<float>(WINDOW_WIDTH), 50);
            graphics.DrawString(L"VertexForge", -1, &titleFont, titleRect, &format, &titleBrush);
            yOffset += 80;
        }

        // Draw "Engine Editor" subtitle
        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font subtitleFont(&fontFamily, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush subtitleBrush(Gdiplus::Color(255, 150, 150, 160));

        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF subtitleRect(0, static_cast<float>(yOffset),
                                    static_cast<float>(WINDOW_WIDTH), 25);
        graphics.DrawString(L"Engine Editor", -1, &subtitleFont, subtitleRect, &format, &subtitleBrush);

        // Draw status text at bottom
        {
            std::lock_guard<std::mutex> lock(statusMutex);

            // Proper UTF-8 to wide string conversion
            std::wstring wideStatus;
            if (!currentStatus.empty())
            {
                int wideLen = MultiByteToWideChar(CP_UTF8, 0, currentStatus.c_str(),
                                                   static_cast<int>(currentStatus.size()), nullptr, 0);
                if (wideLen > 0)
                {
                    wideStatus.resize(wideLen);
                    MultiByteToWideChar(CP_UTF8, 0, currentStatus.c_str(),
                                        static_cast<int>(currentStatus.size()), &wideStatus[0], wideLen);
                }
            }

            Gdiplus::Font statusFont(&fontFamily, 14, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush statusBrush(Gdiplus::Color(255, 120, 180, 255));

            Gdiplus::RectF statusRect(0, WINDOW_HEIGHT - 50.0f,
                                      static_cast<float>(WINDOW_WIDTH), 35);
            graphics.DrawString(wideStatus.c_str(), -1, &statusFont, statusRect, &format, &statusBrush);
        }

        // Draw loading indicator dots
        int dotY = WINDOW_HEIGHT - 75;
        int dotRadius = 4;
        int dotSpacing = 15;
        int totalDotsWidth = 3 * (dotRadius * 2) + 2 * dotSpacing;
        int startX = (WINDOW_WIDTH - totalDotsWidth) / 2;

        static int animFrame = 0;
        animFrame = (animFrame + 1) % 30;

        for (int i = 0; i < 3; i++)
        {
            int alpha = 80;
            if (animFrame / 10 == i)
            {
                alpha = 255;
            }
            Gdiplus::SolidBrush dotBrush(Gdiplus::Color(static_cast<BYTE>(alpha), 120, 180, 255));
            graphics.FillEllipse(&dotBrush,
                                 startX + i * (dotRadius * 2 + dotSpacing),
                                 dotY,
                                 dotRadius * 2,
                                 dotRadius * 2);
        }
    }

    void SplashScreen::loadSplashImage()
    {
        // Try to find splash image relative to executable
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

        // Executable is at: bin/Editor/Debug/x64/Editor.exe
        // Logo is at: resources/editor/VertexForge-logo.png
        // So we need to go up 4 levels from exe dir
        std::vector<std::filesystem::path> searchPaths = {
            (exeDir / ".." / ".." / ".." / ".." / "resources" / "editor" / "VertexForge-logo.png").lexically_normal(),
            (exeDir / "resources" / "editor" / "VertexForge-logo.png").lexically_normal(),
            (exeDir / "VertexForge-logo.png").lexically_normal()
        };

        for (const auto& path : searchPaths)
        {
            if (std::filesystem::exists(path))
            {
                std::wstring wpath = path.wstring();
                splashImage = new Gdiplus::Image(wpath.c_str());
                auto* img = static_cast<Gdiplus::Image*>(splashImage);
                if (img->GetLastStatus() == Gdiplus::Ok)
                {
                    return;
                }
                delete img;
                splashImage = nullptr;
            }
        }

        // No splash image found - will show text title instead
    }

#else
    // Non-Windows platforms: stub implementation
    void SplashScreen::show() {}
    void SplashScreen::close() {}
#endif

} // namespace editor
