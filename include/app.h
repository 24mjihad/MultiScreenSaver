#pragma once

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mms {

struct ScreenSaverOption {
    std::wstring id;
    std::wstring label;
    std::wstring path;
    bool builtin = false;
};

struct MonitorInfo {
    int index = 0;
    HMONITOR handle = nullptr;
    RECT bounds{};
    std::wstring deviceName;
    std::wstring label;
    bool primary = false;
};

struct AppSettings {
    unsigned int idleMinutes = 1;
    std::unordered_map<std::wstring, bool> enabledByMonitor;
    std::unordered_map<std::wstring, std::wstring> saverByMonitor;
};

class SettingsStore {
public:
    AppSettings Load() const;
    void Save(const AppSettings& settings) const;
    static std::wstring SanitizeMonitorName(const std::wstring& deviceName);
};

class App;

class OverlayWindow {
public:
    OverlayWindow(App& app, MonitorInfo monitor);
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Close();
    bool IsOpen() const;
    const std::wstring& DeviceName() const;
    bool ContainsScreenPoint(POINT point) const;

private:
    static constexpr wchar_t kClassName[] = L"MultiScreenSaverOverlayWindow";

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static ATOM EnsureClass(HINSTANCE instance);
    bool StartExternalScreenSaver();
    void StopExternalScreenSaver();
    void UpdateAnimation();
    void ResizeHostedSaver() const;
    void Paint(HDC dc) const;

    App& app_;
    MonitorInfo monitor_;
    ScreenSaverOption saverOption_;
    HWND hwnd_ = nullptr;
    HWND hostWindow_ = nullptr;
    PROCESS_INFORMATION saverProcess_{};
    POINT textPosition_{40, 40};
    POINT textVelocity_{5, 4};
};

class SettingsWindow {
public:
    explicit SettingsWindow(App& app);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Show();
    void Hide();
    void Refresh();

private:
    struct MonitorControl {
        std::wstring deviceName;
        HWND checkbox = nullptr;
        HWND combo = nullptr;
    };

    static constexpr wchar_t kClassName[] = L"MultiScreenSaverSettingsWindow";

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static ATOM EnsureClass(HINSTANCE instance);
    void ApplyFonts(HWND control) const;
    void CreateChildControls();
    void RebuildMonitorControls();
    void ApplyTimeoutFromEdit();

    App& app_;
    HWND hwnd_ = nullptr;
    HWND titleLabel_ = nullptr;
    HWND subtitleLabel_ = nullptr;
    HWND sectionLabel_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HBRUSH windowBrush_ = nullptr;
    HWND timeoutEdit_ = nullptr;
    HWND previewButton_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND closeButton_ = nullptr;
    std::vector<MonitorControl> monitorControls_;
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();
    HINSTANCE Instance() const;
    HWND MainWindow() const;

    const std::vector<MonitorInfo>& Monitors() const;
    const std::vector<ScreenSaverOption>& AvailableScreenSavers() const;
    bool IsMonitorEnabled(const std::wstring& deviceName) const;
    unsigned int IdleMinutes() const;
    const ScreenSaverOption& SelectedScreenSaver(const std::wstring& deviceName) const;

    void SetMonitorEnabled(const std::wstring& deviceName, bool enabled);
    void SetIdleMinutes(unsigned int idleMinutes);
    void SetMonitorScreenSaver(const std::wstring& deviceName, const std::wstring& saverId);
    void RefreshMonitors();
    void PreviewEnabledMonitors();
    void DismissMonitorOverlay(const std::wstring& deviceName);
    void ShowSettings();
    void StopAllOverlays();

private:
    static constexpr wchar_t kMainClassName[] = L"MultiScreenSaverMainWindow";

    static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM context);

    LRESULT HandleMainMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateMainWindow();
    void LoadSettings();
    void SaveSettings() const;
    void InstallTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void HandleIdleTick();
    void InstallInputHooks();
    void RemoveInputHooks();
    void HandlePointerInput(POINT point);
    void HandleKeyboardInput();
    const ScreenSaverOption* FindScreenSaverOption(const std::wstring& saverId) const;
    std::vector<ScreenSaverOption> DiscoverScreenSavers() const;
    std::vector<MonitorInfo> EnumerateMonitors() const;

    static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND mainWindow_ = nullptr;
    NOTIFYICONDATAW trayIcon_{};
    HHOOK mouseHook_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    SettingsStore settingsStore_;
    AppSettings settings_;
    std::vector<MonitorInfo> monitors_;
    std::vector<ScreenSaverOption> availableScreenSavers_;
    std::vector<std::unique_ptr<OverlayWindow>> overlays_;
    std::unique_ptr<SettingsWindow> settingsWindow_;
};

}  // namespace mms