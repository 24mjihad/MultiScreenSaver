#include "app.h"

#include <CommCtrl.h>
#include <uxtheme.h>

#include <string>

namespace {

constexpr int kWindowWidth = 780;
constexpr int kWindowHeight = 560;
constexpr int kBaseMonitorCheckboxId = 2000;
constexpr int kBaseMonitorComboId = 2600;
constexpr int kTimeoutEditId = 2100;
constexpr int kPreviewButtonId = 2101;
constexpr int kRefreshButtonId = 2102;
constexpr int kCloseButtonId = 2103;

std::wstring ToWide(unsigned int value) {
    return std::to_wstring(value);
}

}  // namespace

namespace mms {

constexpr wchar_t SettingsWindow::kClassName[];

SettingsWindow::SettingsWindow(App& app) : app_(app) {
    titleFont_ = CreateFontW(30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    bodyFont_ = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    smallFont_ = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    windowBrush_ = CreateSolidBrush(RGB(243, 246, 250));
}

SettingsWindow::~SettingsWindow() {
    Hide();
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    if (bodyFont_ != nullptr) {
        DeleteObject(bodyFont_);
    }
    if (smallFont_ != nullptr) {
        DeleteObject(smallFont_);
    }
    if (windowBrush_ != nullptr) {
        DeleteObject(windowBrush_);
    }
}

bool SettingsWindow::Create(HINSTANCE instance) {
    if (hwnd_ != nullptr) {
        return true;
    }

    if (EnsureClass(instance) == 0) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"MultiScreenSaver Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        app_.MainWindow(),
        nullptr,
        instance,
        this);

    return hwnd_ != nullptr;
}

void SettingsWindow::Show() {
    if (hwnd_ == nullptr) {
        return;
    }
    Refresh();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void SettingsWindow::Refresh() {
    if (hwnd_ == nullptr) {
        return;
    }
    if (timeoutEdit_ != nullptr) {
        SetWindowTextW(timeoutEdit_, ToWide(app_.IdleMinutes()).c_str());
    }
    RebuildMonitorControls();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

ATOM SettingsWindow::EnsureClass(HINSTANCE instance) {
    static ATOM atom = 0;
    if (atom != 0) {
        return atom;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    atom = RegisterClassExW(&wc);
    return atom;
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateChildControls();
        Refresh();
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(30, 41, 59));
        return reinterpret_cast<LRESULT>(windowBrush_);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(15, 23, 42));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, windowBrush_);
        return 1;
    }
    case WM_COMMAND: {
        const WORD controlId = LOWORD(wParam);
        const WORD notifyCode = HIWORD(wParam);

        if (controlId == kPreviewButtonId && notifyCode == BN_CLICKED) {
            ApplyTimeoutFromEdit();
            app_.PreviewEnabledMonitors();
            return 0;
        }
        if (controlId == kRefreshButtonId && notifyCode == BN_CLICKED) {
            ApplyTimeoutFromEdit();
            app_.RefreshMonitors();
            Refresh();
            return 0;
        }
        if (controlId == kCloseButtonId && notifyCode == BN_CLICKED) {
            ApplyTimeoutFromEdit();
            Hide();
            return 0;
        }
        if (controlId == kTimeoutEditId && notifyCode == EN_KILLFOCUS) {
            ApplyTimeoutFromEdit();
            return 0;
        }
        if (controlId >= kBaseMonitorCheckboxId && controlId < kBaseMonitorCheckboxId + 512 && notifyCode == BN_CLICKED) {
            const size_t index = static_cast<size_t>(controlId - kBaseMonitorCheckboxId);
            if (index < monitorControls_.size()) {
                const bool enabled = SendMessageW(monitorControls_[index].checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                app_.SetMonitorEnabled(monitorControls_[index].deviceName, enabled);
            }
            return 0;
        }
        if (controlId >= kBaseMonitorComboId && controlId < kBaseMonitorComboId + 512 && notifyCode == CBN_SELCHANGE) {
            const size_t index = static_cast<size_t>(controlId - kBaseMonitorComboId);
            if (index < monitorControls_.size()) {
                const LRESULT selection = SendMessageW(monitorControls_[index].combo, CB_GETCURSEL, 0, 0);
                if (selection != CB_ERR) {
                    const auto& savers = app_.AvailableScreenSavers();
                    if (static_cast<size_t>(selection) < savers.size()) {
                        app_.SetMonitorScreenSaver(monitorControls_[index].deviceName, savers[static_cast<size_t>(selection)].id);
                    }
                }
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        ApplyTimeoutFromEdit();
        Hide();
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void SettingsWindow::ApplyFonts(HWND control) const {
    if (control != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_ != nullptr ? smallFont_ : bodyFont_), TRUE);
        SetWindowTheme(control, L"Explorer", nullptr);
    }
}

void SettingsWindow::CreateChildControls() {
    titleLabel_ = CreateWindowExW(0, WC_STATICW, L"MultiScreenSaver", WS_CHILD | WS_VISIBLE,
        28, 24, 260, 34, hwnd_, nullptr, app_.Instance(), nullptr);
    SendMessageW(titleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);

    subtitleLabel_ = CreateWindowExW(0, WC_STATICW, L"Choose which monitors participate and which Windows .scr file each one should host.", WS_CHILD | WS_VISIBLE,
        30, 66, 680, 24, hwnd_, nullptr, app_.Instance(), nullptr);
    SendMessageW(subtitleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont_), TRUE);

    CreateWindowExW(0, WC_STATICW, L"Idle timeout (minutes)", WS_CHILD | WS_VISIBLE,
        30, 118, 150, 24, hwnd_, nullptr, app_.Instance(), nullptr);

    timeoutEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        L"1",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
        190,
        114,
        90,
        28,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTimeoutEditId)),
        app_.Instance(),
        nullptr);
    ApplyFonts(timeoutEdit_);

    previewButton_ = CreateWindowExW(0, WC_BUTTONW, L"Preview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        30, 474, 110, 34, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviewButtonId)), app_.Instance(), nullptr);
    refreshButton_ = CreateWindowExW(0, WC_BUTTONW, L"Refresh monitors", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        154, 474, 150, 34, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButtonId)), app_.Instance(), nullptr);
    closeButton_ = CreateWindowExW(0, WC_BUTTONW, L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        640, 474, 110, 34, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)), app_.Instance(), nullptr);
    ApplyFonts(previewButton_);
    ApplyFonts(refreshButton_);
    ApplyFonts(closeButton_);

    sectionLabel_ = CreateWindowExW(0, WC_STATICW, L"Per-monitor configuration", WS_CHILD | WS_VISIBLE,
        30, 172, 260, 24, hwnd_, nullptr, app_.Instance(), nullptr);
    SendMessageW(sectionLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
}

void SettingsWindow::RebuildMonitorControls() {
    for (const MonitorControl& control : monitorControls_) {
        if (control.checkbox != nullptr) {
            DestroyWindow(control.checkbox);
        }
        if (control.combo != nullptr) {
            DestroyWindow(control.combo);
        }
    }
    monitorControls_.clear();

    int y = 214;
    int index = 0;
    const auto& savers = app_.AvailableScreenSavers();
    for (const auto& monitor : app_.Monitors()) {
        const std::wstring text = monitor.label + L"  [" + monitor.deviceName + L"]";
        HWND checkbox = CreateWindowExW(
            0,
            WC_BUTTONW,
            text.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            34,
            y,
            300,
            24,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBaseMonitorCheckboxId + index)),
            app_.Instance(),
            nullptr);
        ApplyFonts(checkbox);

        HWND combo = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            360,
            y - 3,
            360,
            220,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBaseMonitorComboId + index)),
            app_.Instance(),
            nullptr);
        ApplyFonts(combo);

        int selectedIndex = 0;
        const auto& selectedSaver = app_.SelectedScreenSaver(monitor.deviceName);
        for (size_t saverIndex = 0; saverIndex < savers.size(); ++saverIndex) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(savers[saverIndex].label.c_str()));
            if (savers[saverIndex].id == selectedSaver.id) {
                selectedIndex = static_cast<int>(saverIndex);
            }
        }
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selectedIndex), 0);

        SendMessageW(checkbox, BM_SETCHECK, app_.IsMonitorEnabled(monitor.deviceName) ? BST_CHECKED : BST_UNCHECKED, 0);
        monitorControls_.push_back(MonitorControl{ monitor.deviceName, checkbox, combo });
        y += 52;
        ++index;
    }
}

void SettingsWindow::ApplyTimeoutFromEdit() {
    if (timeoutEdit_ == nullptr) {
        return;
    }

    wchar_t buffer[32]{};
    GetWindowTextW(timeoutEdit_, buffer, static_cast<int>(std::size(buffer)));
    unsigned int value = _wtoi(buffer);
    if (value == 0) {
        value = 1;
    }

    app_.SetIdleMinutes(value);
    SetWindowTextW(timeoutEdit_, ToWide(app_.IdleMinutes()).c_str());
}

}  // namespace mms