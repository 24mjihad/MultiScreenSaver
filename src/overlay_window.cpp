#include "app.h"

#include <algorithm>
#include <string>

namespace {

constexpr UINT_PTR kAnimationTimerId = 1;
constexpr UINT kAnimationIntervalMs = 16;

RECT ClientRect(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    return rect;
}

RECT NormalizedRect(RECT rect) {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

}  // namespace

namespace mms {

constexpr wchar_t OverlayWindow::kClassName[];

OverlayWindow::OverlayWindow(App& app, MonitorInfo monitor)
    : app_(app), monitor_(std::move(monitor)), saverOption_(app.SelectedScreenSaver(monitor_.deviceName)) {
}

OverlayWindow::~OverlayWindow() {
    Close();
}

bool OverlayWindow::Create(HINSTANCE instance) {
    if (hwnd_ != nullptr) {
        return true;
    }

    if (EnsureClass(instance) == 0) {
        return false;
    }

    const RECT& bounds = monitor_.bounds;
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kClassName,
        L"MultiScreenSaver Overlay",
        WS_POPUP,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance,
        this);

    if (hwnd_ == nullptr) {
        return false;
    }

    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void OverlayWindow::Close() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

bool OverlayWindow::IsOpen() const {
    return hwnd_ != nullptr;
}

const std::wstring& OverlayWindow::DeviceName() const {
    return monitor_.deviceName;
}

bool OverlayWindow::ContainsScreenPoint(POINT point) const {
    const RECT rect = NormalizedRect(monitor_.bounds);
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

ATOM OverlayWindow::EnsureClass(HINSTANCE instance) {
    static ATOM atom = 0;
    if (atom != 0) {
        return atom;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    atom = RegisterClassExW(&wc);
    return atom;
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        if (!saverOption_.builtin) {
            StartExternalScreenSaver();
        }
        SetTimer(hwnd, kAnimationTimerId, kAnimationIntervalMs, nullptr);
        return 0;
    case WM_SIZE:
        ResizeHostedSaver();
        return 0;
    case WM_TIMER:
        if (wParam == kAnimationTimerId && saverOption_.builtin) {
            UpdateAnimation();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (saverOption_.builtin) {
            Paint(dc);
        } else {
            const RECT rect = ClientRect(hwnd_);
            FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kAnimationTimerId);
        StopExternalScreenSaver();
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool OverlayWindow::StartExternalScreenSaver() {
    if (hwnd_ == nullptr || saverOption_.builtin || saverOption_.path.empty()) {
        return false;
    }

    hostWindow_ = CreateWindowExW(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        app_.Instance(),
        nullptr);

    if (hostWindow_ == nullptr) {
        return false;
    }

    ResizeHostedSaver();

    std::wstring commandLine = L"\"" + saverOption_.path + L"\" /p " + std::to_wstring(reinterpret_cast<UINT_PTR>(hostWindow_));
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&saverProcess_, sizeof(saverProcess_));

    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &saverProcess_)) {
        DestroyWindow(hostWindow_);
        hostWindow_ = nullptr;
        saverProcess_ = {};
        return false;
    }

    return true;
}

void OverlayWindow::StopExternalScreenSaver() {
    if (saverProcess_.hProcess != nullptr) {
        DWORD exitCode = STILL_ACTIVE;
        if (GetExitCodeProcess(saverProcess_.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            TerminateProcess(saverProcess_.hProcess, 0);
        }
        CloseHandle(saverProcess_.hThread);
        CloseHandle(saverProcess_.hProcess);
        saverProcess_ = {};
    }

    if (hostWindow_ != nullptr) {
        DestroyWindow(hostWindow_);
        hostWindow_ = nullptr;
    }
}

void OverlayWindow::UpdateAnimation() {
    if (hwnd_ == nullptr) {
        return;
    }

    const RECT rect = ClientRect(hwnd_);
    constexpr int textWidth = 300;
    constexpr int textHeight = 80;

    textPosition_.x += textVelocity_.x;
    textPosition_.y += textVelocity_.y;

    if (textPosition_.x < 20 || textPosition_.x + textWidth > rect.right - 20) {
        textVelocity_.x = -textVelocity_.x;
        textPosition_.x = std::clamp<int>(textPosition_.x, 20, static_cast<int>(rect.right) - textWidth - 20);
    }
    if (textPosition_.y < 20 || textPosition_.y + textHeight > rect.bottom - 20) {
        textVelocity_.y = -textVelocity_.y;
        textPosition_.y = std::clamp<int>(textPosition_.y, 20, static_cast<int>(rect.bottom) - textHeight - 20);
    }
}

void OverlayWindow::ResizeHostedSaver() const {
    if (hostWindow_ == nullptr) {
        return;
    }

    const RECT rect = ClientRect(hwnd_);
    MoveWindow(hostWindow_, 0, 0, rect.right - rect.left, rect.bottom - rect.top, TRUE);
}

void OverlayWindow::Paint(HDC dc) const {
    const RECT rect = ClientRect(hwnd_);
    FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 245, 245));

    HFONT titleFont = CreateFontW(
        52,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");
    HFONT bodyFont = CreateFontW(
        22,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");

    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));
    RECT titleRect{ textPosition_.x, textPosition_.y, textPosition_.x + 420, textPosition_.y + 70 };
    DrawTextW(dc, saverOption_.label.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(dc, bodyFont);
    RECT bodyRect{ textPosition_.x, textPosition_.y + 68, textPosition_.x + 580, textPosition_.y + 130 };
    DrawTextW(dc, monitor_.label.c_str(), -1, &bodyRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);
}

}  // namespace mms