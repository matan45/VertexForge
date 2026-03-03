#include "SplashScreen.hpp"

// Windows headers must be included in correct order for GDI+
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>

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

        if (hwnd && running)
        {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

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
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

        loadSplashImage();

        const wchar_t* className = L"VertexForgeSplash";

        if (!registerWindowClass(className) || !createSplashWindow(className))
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

        MSG msg;
        while (!shouldClose && GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

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

    bool SplashScreen::registerWindowClass(const wchar_t* className)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        wc.hbrBackground = nullptr;

        ATOM classAtom = RegisterClassExW(&wc);
        return classAtom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool SplashScreen::createSplashWindow(const wchar_t* className)
    {
        // Use the monitor that contains the cursor for proper multi-monitor centering
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        HMONITOR monitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);

        int monitorWidth = mi.rcWork.right - mi.rcWork.left;
        int monitorHeight = mi.rcWork.bottom - mi.rcWork.top;

        // Size the splash window as 30% of monitor width, maintaining 4:3 aspect ratio
        windowWidth = monitorWidth * 30 / 100;
        windowHeight = windowWidth * 3 / 4;

        int x = mi.rcWork.left + (monitorWidth - windowWidth) / 2;
        int y = mi.rcWork.top + (monitorHeight - windowHeight) / 2;

        hwnd = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            className,
            L"VertexForge",
            WS_POPUP,
            x, y, windowWidth, windowHeight,
            nullptr, nullptr,
            GetModuleHandle(nullptr),
            this
        );

        if (!hwnd)
        {
            return false;
        }

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
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
            return 1; // Prevent flicker

        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    void SplashScreen::render(HDC hdc)
    {
        renderBackground(hdc);
        renderStatusText(hdc);
        renderLoadingIndicator(hdc);
    }

    void SplashScreen::renderBackground(HDC hdc)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        if (splashImage)
        {
            auto* image = static_cast<Gdiplus::Image*>(splashImage);
            graphics.DrawImage(image, 0, 0, windowWidth, windowHeight);
        }
        else
        {
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 30, 30, 35));
            graphics.FillRectangle(&bgBrush, 0, 0, windowWidth, windowHeight);

            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            float titleFontSize = windowHeight * 28.0f / 600.0f;
            Gdiplus::Font titleFont(&fontFamily, titleFontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));

            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);

            float titleY = windowHeight * 180.0f / 600.0f;
            Gdiplus::RectF titleRect(0, titleY, static_cast<float>(windowWidth), titleFontSize * 2.0f);
            graphics.DrawString(L"VertexForge", -1, &titleFont, titleRect, &format, &titleBrush);
        }

        Gdiplus::Pen borderPen(Gdiplus::Color(255, 80, 80, 90), 1);
        graphics.DrawRectangle(&borderPen, 0, 0, windowWidth - 1, windowHeight - 1);
    }

    void SplashScreen::renderStatusText(HDC hdc)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);

        std::lock_guard<std::mutex> lock(statusMutex);

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

        float statusFontSize = windowHeight * 20.0f / 600.0f;
        Gdiplus::Font statusFont(&fontFamily, statusFontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush statusBrush(Gdiplus::Color(255, 120, 180, 255));

        float statusY = windowHeight - windowHeight * 50.0f / 600.0f;
        Gdiplus::RectF statusRect(0, statusY,
                                  static_cast<float>(windowWidth), statusFontSize * 2.0f);
        graphics.DrawString(wideStatus.c_str(), -1, &statusFont, statusRect, &format, &statusBrush);
    }

    void SplashScreen::renderLoadingIndicator(HDC hdc)
    {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int dotY = windowHeight - windowHeight * 75 / 600;
        int dotRadius = windowHeight * 4 / 600;
        if (dotRadius < 2) dotRadius = 2;
        int dotSpacing = windowHeight * 15 / 600;
        int totalDotsWidth = 3 * (dotRadius * 2) + 2 * dotSpacing;
        int startX = (windowWidth - totalDotsWidth) / 2;

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
    }
}
