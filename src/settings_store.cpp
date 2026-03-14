#include "app.h"

#include <algorithm>
#include <cwctype>

namespace {

constexpr wchar_t kRegistryPath[] = L"Software\\MultiScreenSaver";
constexpr wchar_t kIdleMinutesValue[] = L"IdleMinutes";
constexpr wchar_t kMonitorEnabledPrefix[] = L"MonitorEnabled_";
constexpr wchar_t kMonitorSaverPrefix[] = L"MonitorSaver_";

DWORD ReadDwordValue(HKEY key, const wchar_t* valueName, DWORD defaultValue) {
    DWORD value = defaultValue;
    DWORD type = REG_DWORD;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD) {
        return defaultValue;
    }
    return value;
}

void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value) {
    RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

}  // namespace

namespace mms {

AppSettings SettingsStore::Load() const {
    AppSettings settings;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return settings;
    }

    settings.idleMinutes = std::max(1u, static_cast<unsigned int>(ReadDwordValue(key, kIdleMinutesValue, 1)));

    DWORD valueCount = 0;
    DWORD maxValueName = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueName, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        std::wstring valueName(maxValueName + 1, L'\0');
        for (DWORD index = 0; index < valueCount; ++index) {
            DWORD nameSize = static_cast<DWORD>(valueName.size());
            DWORD type = 0;
            BYTE data[1024]{};
            DWORD dataSize = sizeof(data);
            if (RegEnumValueW(key, index, valueName.data(), &nameSize, nullptr, &type, data, &dataSize) != ERROR_SUCCESS) {
                continue;
            }
            valueName.resize(nameSize);
            if (type == REG_DWORD && valueName.rfind(kMonitorEnabledPrefix, 0) == 0 && dataSize >= sizeof(DWORD)) {
                const DWORD enabledValue = *reinterpret_cast<const DWORD*>(data);
                settings.enabledByMonitor.emplace(valueName.substr(std::size(kMonitorEnabledPrefix) - 1), enabledValue != 0);
            }
            if ((type == REG_SZ || type == REG_EXPAND_SZ) && valueName.rfind(kMonitorSaverPrefix, 0) == 0) {
                const wchar_t* saverValue = reinterpret_cast<const wchar_t*>(data);
                settings.saverByMonitor.emplace(valueName.substr(std::size(kMonitorSaverPrefix) - 1), saverValue);
            }
            valueName.resize(maxValueName + 1);
        }
    }

    RegCloseKey(key);
    return settings;
}

void SettingsStore::Save(const AppSettings& settings) const {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return;
    }

    WriteDwordValue(key, kIdleMinutesValue, settings.idleMinutes);
    for (const auto& [deviceName, enabled] : settings.enabledByMonitor) {
        const std::wstring valueName = std::wstring(kMonitorEnabledPrefix) + SanitizeMonitorName(deviceName);
        WriteDwordValue(key, valueName.c_str(), enabled ? 1u : 0u);
    }
    for (const auto& [deviceName, saverId] : settings.saverByMonitor) {
        const std::wstring valueName = std::wstring(kMonitorSaverPrefix) + SanitizeMonitorName(deviceName);
        RegSetValueExW(key, valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(saverId.c_str()), static_cast<DWORD>((saverId.size() + 1) * sizeof(wchar_t)));
    }

    RegCloseKey(key);
}

std::wstring SettingsStore::SanitizeMonitorName(const std::wstring& deviceName) {
    std::wstring sanitized;
    sanitized.reserve(deviceName.size());
    for (wchar_t ch : deviceName) {
        sanitized.push_back(std::iswalnum(ch) ? ch : L'_');
    }
    return sanitized;
}

}  // namespace mms