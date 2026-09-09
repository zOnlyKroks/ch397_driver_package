// Watches for CH397/CH398 device arrival and removal via SetupAPI, using
// the WinUSB device interface GUID declared in windows/driver/ch397.inf.
#pragma once

#include <windows.h>
#include <functional>
#include <string>
#include <thread>
#include <atomic>

namespace ch397 {

// {6E36A4A1-1F0C-4A6D-9D1B-2C1E4C9F2B77} — must match Dev_AddReg in ch397.inf.
extern const GUID kDeviceInterfaceGuid;

class DeviceWatcher {
public:
    using ArrivalCallback = std::function<void(const std::wstring& devicePath)>;
    using RemovalCallback = std::function<void(const std::wstring& devicePath)>;

    DeviceWatcher(ArrivalCallback onArrival, RemovalCallback onRemoval);
    ~DeviceWatcher();

    // Enumerates already-connected devices, then starts a background
    // thread that listens for WM_DEVICECHANGE-equivalent notifications via
    // a hidden message-only window.
    bool Start();
    void Stop();

    // Called by the internal window procedure; not for external use.
    void NotifyDeviceChange(bool arrived, const std::wstring& path);

private:
    void RunMessageLoop();
    void EnumerateExisting();

    ArrivalCallback onArrival_;
    RemovalCallback onRemoval_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    HWND messageWindow_ = nullptr;
    HDEVNOTIFY notificationHandle_ = nullptr;
};

} // namespace ch397
