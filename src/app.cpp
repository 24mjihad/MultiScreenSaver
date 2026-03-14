#include "app.h"

#include <CommCtrl.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kIdleTimerId = 100;
constexpr UINT kIdlePollIntervalMs = 1000;
constexpr UINT kTrayIconId = 42;
constexpr UINT kTrayCommandOpenSettings = 40001;
constexpr UINT kTrayCommandPreview = 40002;
constexpr UINT kTrayCommandExit = 40003;
constexpr wchar_t kBuiltInSaverId[] = L"builtin:bounce";

mms::App* g_app = nullptr;

DWORD IdleMilliseconds() {
    LASTINPUTINFO lastInput{ sizeof(lastInput) };
    if (!GetLastInputInfo(&lastInput)) {
        return 0;
    }
    return GetTickCount() - lastInput.dwTime;
}

std::wstring MakeDisplayLabel(const std::wstring& fileNameStem) {
    std::wstring label;
    label.reserve(fileNameStem.size());
    bool capitalize = true;
    for (wchar_t ch : fileNameStem) {
        if (ch == L'_' || ch == L'-') {
            label.push_back(L' ');
            capitalize = true;
            continue;
        }
        label.push_back(capitalize ? static_cast<wchar_t>(towupper(ch)) : ch);
        capitalize = false;
    }
    return label;
}

bool IsBlockedScreenSaver(const std::filesystem::path& path) {
    const std::wstring fileName = path.filename().wstring();
    return _wcsicmp(fileName.c_str(), L"scrnsave.scr") == 0 || _wcsicmp(fileName.c_str(), L"bubbles.scr") == 0;
}

}  // namespace

namespace mms {

constexpr wchar_t App::kMainClassName[];

App::App(HINSTANCE instance)
    : instance_(instance) {
    g_app = this;
}

App::~App() {
    StopAllOverlays();
    RemoveInputHooks();
    RemoveTrayIcon();
    if (g_app == this) {
        g_app = nullptr;
    }
}

int App::Run() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls{ sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&commonControls);

    LoadSettings();
    availableScreenSavers_ = DiscoverScreenSavers();
    RefreshMonitors();

    if (!CreateMainWindow()) {
        return 1;
    }

    InstallInputHooks();

    settingsWindow_ = std::make_unique<SettingsWindow>(*this);
    settingsWindow_->Create(instance_);
    settingsWindow_->Show();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

HINSTANCE App::Instance() const {
    return instance_;
}

HWND App::MainWindow() const {
    return mainWindow_;
}

const std::vector<MonitorInfo>& App::Monitors() const {
    return monitors_;
}

const std::vector<ScreenSaverOption>& App::AvailableScreenSavers() const {
    return availableScreenSavers_;
}

bool App::IsMonitorEnabled(const std::wstring& deviceName) const {
    const auto it = settings_.enabledByMonitor.find(SettingsStore::SanitizeMonitorName(deviceName));
    if (it == settings_.enabledByMonitor.end()) {
        return true;
    }
    return it->second;
}

unsigned int App::IdleMinutes() const {
    return settings_.idleMinutes;
}

const ScreenSaverOption& App::SelectedScreenSaver(const std::wstring& deviceName) const {
    const std::wstring sanitized = SettingsStore::SanitizeMonitorName(deviceName);
    const auto it = settings_.saverByMonitor.find(sanitized);
    if (it != settings_.saverByMonitor.end()) {
        if (const ScreenSaverOption* option = FindScreenSaverOption(it->second)) {
            return *option;
        }
    }
    if (const ScreenSaverOption* option = FindScreenSaverOption(kBuiltInSaverId)) {
        return *option;
    }
    return availableScreenSavers_.front();
}

void App::SetMonitorEnabled(const std::wstring& deviceName, bool enabled) {
    settings_.enabledByMonitor[SettingsStore::SanitizeMonitorName(deviceName)] = enabled;
    SaveSettings();
    if (!enabled) {
        DismissMonitorOverlay(deviceName);
    }
}

void App::SetIdleMinutes(unsigned int idleMinutes) {
    settings_.idleMinutes = std::max(1u, idleMinutes);
    SaveSettings();
}

void App::SetMonitorScreenSaver(const std::wstring& deviceName, const std::wstring& saverId) {
    settings_.saverByMonitor[SettingsStore::SanitizeMonitorName(deviceName)] = saverId;
    SaveSettings();
    DismissMonitorOverlay(deviceName);
}

void App::RefreshMonitors() {
    monitors_ = EnumerateMonitors();
    if (settingsWindow_ != nullptr) {
        settingsWindow_->Refresh();
    }
}

void App::PreviewEnabledMonitors() {
    for (const MonitorInfo& monitor : monitors_) {
        if (!IsMonitorEnabled(monitor.deviceName)) {
            continue;
        }

        const auto existing = std::find_if(overlays_.begin(), overlays_.end(), [&](const auto& overlay) {
            return overlay->DeviceName() == monitor.deviceName && overlay->IsOpen();
        });
        if (existing != overlays_.end()) {
            continue;
        }

        auto overlay = std::make_unique<OverlayWindow>(*this, monitor);
        if (overlay->Create(instance_)) {
            overlays_.push_back(std::move(overlay));
        }
    }
}

void App::DismissMonitorOverlay(const std::wstring& deviceName) {
    for (auto& overlay : overlays_) {
        if (overlay->DeviceName() == deviceName) {
            overlay->Close();
            break;
        }
    }

    overlays_.erase(
        std::remove_if(overlays_.begin(), overlays_.end(), [](const auto& overlay) {
            return !overlay->IsOpen();
        }),
        overlays_.end());
}

void App::ShowSettings() {
    if (settingsWindow_ != nullptr) {
        settingsWindow_->Show();
    }
}

void App::StopAllOverlays() {
    for (auto& overlay : overlays_) {
        overlay->Close();
    }
    overlays_.clear();
}

LRESULT CALLBACK App::MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app != nullptr) {
        switch (wParam) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            const auto* mouseInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            g_app->HandlePointerInput(mouseInfo->pt);
            break;
        }
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK App::KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app != nullptr) {
        switch (wParam) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            g_app->HandleKeyboardInput();
            break;
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK App::MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->mainWindow_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMainMessage(hwnd, message, wParam, lParam);
}

BOOL CALLBACK App::MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(context);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    MonitorInfo monitorInfo;
    monitorInfo.index = static_cast<int>(monitors->size()) + 1;
    monitorInfo.handle = monitor;
    monitorInfo.bounds = info.rcMonitor;
    monitorInfo.deviceName = info.szDevice;
    monitorInfo.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    monitorInfo.label = L"Display " + std::to_wstring(monitorInfo.index);
    if (monitorInfo.primary) {
        monitorInfo.label += L" (Primary)";
    }

    monitors->push_back(std::move(monitorInfo));
    return TRUE;
}

LRESULT App::HandleMainMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        InstallTrayIcon();
        SetTimer(hwnd, kIdleTimerId, kIdlePollIntervalMs, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == kIdleTimerId) {
            HandleIdleTick();
            return 0;
        }
        break;
    case WM_DISPLAYCHANGE:
        RefreshMonitors();
        return 0;
    case kTrayCallbackMessage:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowSettings();
            return 0;
        }
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowTrayMenu();
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kTrayCommandOpenSettings:
            ShowSettings();
            return 0;
        case kTrayCommandPreview:
            PreviewEnabledMonitors();
            return 0;
        case kTrayCommandExit:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, kIdleTimerId);
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool App::CreateMainWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::MainWindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kMainClassName;

    if (RegisterClassExW(&wc) == 0) {
        return false;
    }

    mainWindow_ = CreateWindowExW(
        0,
        kMainClassName,
        L"MultiScreenSaver Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        200,
        nullptr,
        nullptr,
        instance_,
        this);

    if (mainWindow_ == nullptr) {
        return false;
    }

    ShowWindow(mainWindow_, SW_HIDE);
    return true;
}

void App::LoadSettings() {
    settings_ = settingsStore_.Load();
}

void App::SaveSettings() const {
    settingsStore_.Save(settings_);
}

void App::InstallTrayIcon() {
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = mainWindow_;
    trayIcon_.uID = kTrayIconId;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayCallbackMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, std::size(trayIcon_.szTip), L"MultiScreenSaver");
    Shell_NotifyIconW(NIM_ADD, &trayIcon_);
}

void App::RemoveTrayIcon() {
    if (trayIcon_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayIcon_.hWnd = nullptr;
    }
}

void App::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kTrayCommandOpenSettings, L"Settings");
    AppendMenuW(menu, MF_STRING, kTrayCommandPreview, L"Preview now");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayCommandExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(mainWindow_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, mainWindow_, nullptr);
    DestroyMenu(menu);
}

void App::HandleIdleTick() {
    const unsigned int idleMsThreshold = settings_.idleMinutes * 60u * 1000u;
    if (IdleMilliseconds() >= idleMsThreshold) {
        PreviewEnabledMonitors();
    }
}

void App::InstallInputHooks() {
    if (mouseHook_ == nullptr) {
        mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, &App::MouseHookProc, nullptr, 0);
    }
    if (keyboardHook_ == nullptr) {
        keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &App::KeyboardHookProc, nullptr, 0);
    }
}

void App::RemoveInputHooks() {
    if (mouseHook_ != nullptr) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
}

void App::HandlePointerInput(POINT point) {
    for (const auto& overlay : overlays_) {
        if (overlay->IsOpen() && overlay->ContainsScreenPoint(point)) {
            DismissMonitorOverlay(overlay->DeviceName());
            return;
        }
    }
}

void App::HandleKeyboardInput() {
    POINT cursor{};
    GetCursorPos(&cursor);
    HandlePointerInput(cursor);
}

const ScreenSaverOption* App::FindScreenSaverOption(const std::wstring& saverId) const {
    const auto it = std::find_if(availableScreenSavers_.begin(), availableScreenSavers_.end(), [&](const auto& option) {
        return option.id == saverId;
    });
    if (it == availableScreenSavers_.end()) {
        return nullptr;
    }
    return &(*it);
}

std::vector<ScreenSaverOption> App::DiscoverScreenSavers() const {
    std::vector<ScreenSaverOption> screenSavers;
    screenSavers.push_back(ScreenSaverOption{ kBuiltInSaverId, L"Built-in Bounce", L"", true });

    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const std::filesystem::path systemDirectory = std::filesystem::path(windowsDirectory) / L"System32";

    std::error_code errorCode;
    for (const auto& entry : std::filesystem::directory_iterator(systemDirectory, errorCode)) {
        if (errorCode) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != L".scr") {
            continue;
        }
        if (IsBlockedScreenSaver(entry.path())) {
            continue;
        }

        const std::wstring stem = entry.path().stem().wstring();
        screenSavers.push_back(ScreenSaverOption{
            entry.path().wstring(),
            MakeDisplayLabel(stem),
            entry.path().wstring(),
            false,
        });
    }

    std::sort(screenSavers.begin() + 1, screenSavers.end(), [](const auto& left, const auto& right) {
        return left.label < right.label;
    });

    return screenSavers;
}

std::vector<MonitorInfo> App::EnumerateMonitors() const {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &App::MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

}  // namespace mms