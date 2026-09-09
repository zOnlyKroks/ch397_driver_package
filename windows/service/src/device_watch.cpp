#include "device_watch.h"

#include <setupapi.h>
#include <dbt.h>
#include <cwchar>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace ch397 {

// Must match [Dev_AddReg] in windows/driver/ch397.inf.
const GUID kDeviceInterfaceGuid = {
    0x6e36a4a1, 0x1f0c, 0x4a6d, { 0x9d, 0x1b, 0x2c, 0x1e, 0x4c, 0x9f, 0x2b, 0x77 }
};

namespace {

constexpr wchar_t kWindowClassName[] = L"Ch397DeviceWatcherWindow";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<DeviceWatcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_DEVICECHANGE && self &&
        (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
        auto* hdr = reinterpret_cast<DEV_BROADCAST_HDR*>(lParam);
        if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            auto* devInterface = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_W*>(hdr);
            std::wstring path(devInterface->dbcc_name);
            self->NotifyDeviceChange(wParam == DBT_DEVICEARRIVAL, path);
        }
        return TRUE;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

DeviceWatcher::DeviceWatcher(ArrivalCallback onArrival, RemovalCallback onRemoval)
    : onArrival_(std::move(onArrival)), onRemoval_(std::move(onRemoval)) {}

DeviceWatcher::~DeviceWatcher() { Stop(); }

void DeviceWatcher::EnumerateExisting() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(
        &kDeviceInterfaceGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return;

    SP_DEVICE_INTERFACE_DATA ifaceData{};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(devInfo, nullptr, &kDeviceInterfaceGuid, index, &ifaceData);
         ++index) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0)
            continue;

        std::vector<uint8_t> buffer(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, requiredSize,
                                              nullptr, nullptr) &&
            onArrival_) {
            onArrival_(detail->DevicePath);
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}

bool DeviceWatcher::Start() {
    running_ = true;
    thread_ = std::thread(&DeviceWatcher::RunMessageLoop, this);
    return true;
}

void DeviceWatcher::RunMessageLoop() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    RegisterClassW(&wc);

    messageWindow_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(messageWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = kDeviceInterfaceGuid;
    notificationHandle_ = RegisterDeviceNotificationW(
        messageWindow_, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    EnumerateExisting();

    MSG msg;
    while (running_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void DeviceWatcher::NotifyDeviceChange(bool arrived, const std::wstring& path) {
    if (arrived && onArrival_)
        onArrival_(path);
    else if (!arrived && onRemoval_)
        onRemoval_(path);
}

void DeviceWatcher::Stop() {
    running_ = false;

    if (notificationHandle_) {
        UnregisterDeviceNotification(notificationHandle_);
        notificationHandle_ = nullptr;
    }
    if (messageWindow_) {
        PostMessageW(messageWindow_, WM_QUIT, 0, 0);
    }
    if (thread_.joinable())
        thread_.join();
}

} // namespace ch397
