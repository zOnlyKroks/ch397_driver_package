// ch397svc: Windows service that watches for CH397/CH398 USB devices and
// bridges each one to a Wintun virtual network adapter. See
// docs/protocol.md for the USB protocol and the top-level windows/README.md
// for build/install instructions.
#include <windows.h>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <cstdio>

#include "device_watch.h"
#include "usb_transport.h"
#include "netif_bridge.h"

namespace {

constexpr wchar_t kServiceName[] = L"ch397svc";
constexpr wchar_t kServiceDisplayName[] = L"CH397 USB Ethernet Bridge";

SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
HANDLE g_stopEvent = nullptr;

std::mutex g_devicesMutex;
std::map<std::wstring, std::unique_ptr<ch397::NetifBridge>> g_activeDevices;

void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP;
    SetServiceStatus(g_statusHandle, &g_status);
}

void OnDeviceArrival(const std::wstring& devicePath) {
    auto transport = std::make_shared<ch397::UsbTransport>();
    if (!transport->Open(devicePath))
        return; // Not one of our devices, or already claimed.

    auto bridge = std::make_unique<ch397::NetifBridge>(transport);
    if (!bridge->Start())
        return;

    std::lock_guard<std::mutex> lock(g_devicesMutex);
    g_activeDevices[devicePath] = std::move(bridge);
}

void OnDeviceRemoval(const std::wstring& devicePath) {
    std::lock_guard<std::mutex> lock(g_devicesMutex);
    g_activeDevices.erase(devicePath);
}

DWORD WINAPI ServiceCtrlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    if (control == SERVICE_CONTROL_STOP) {
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
        SetEvent(g_stopEvent);
    }
    return NO_ERROR;
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceCtrlHandler, nullptr);
    if (!g_statusHandle)
        return;

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwServiceSpecificExitCode = 0;
    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        ReportStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    ch397::DeviceWatcher watcher(OnDeviceArrival, OnDeviceRemoval);
    if (!watcher.Start()) {
        ReportStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    ReportStatus(SERVICE_RUNNING);

    WaitForSingleObject(g_stopEvent, INFINITE);

    watcher.Stop();
    {
        std::lock_guard<std::mutex> lock(g_devicesMutex);
        g_activeDevices.clear();
    }

    ReportStatus(SERVICE_STOPPED);
}

bool InstallService() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm)
        return false;

    SC_HANDLE service = CreateServiceW(
        scm, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        path, nullptr, nullptr, nullptr, nullptr, nullptr);

    bool ok = service != nullptr;
    if (service)
        CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return ok;
}

bool UninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;

    SC_HANDLE service = OpenServiceW(scm, kServiceName, DELETE);
    bool ok = service && DeleteService(service);
    if (service)
        CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return ok;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc > 1 && _wcsicmp(argv[1], L"--install") == 0) {
        bool ok = InstallService();
        wprintf(L"%s\n", ok ? L"Service installed." : L"Service install failed.");
        return ok ? 0 : 1;
    }
    if (argc > 1 && _wcsicmp(argv[1], L"--uninstall") == 0) {
        bool ok = UninstallService();
        wprintf(L"%s\n", ok ? L"Service uninstalled." : L"Service uninstall failed.");
        return ok ? 0 : 1;
    }

    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<LPWSTR>(kServiceName), ServiceMain },
        { nullptr, nullptr },
    };

    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // Not launched by the SCM (e.g. run directly for debugging); fall
        // back to running the same logic on the console thread.
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            ch397::DeviceWatcher watcher(OnDeviceArrival, OnDeviceRemoval);
            watcher.Start();
            WaitForSingleObject(g_stopEvent, INFINITE);
        }
        return 1;
    }

    return 0;
}
