// Bridges Ethernet frames between a WinUSB CH397/CH398 device and a Wintun
// virtual network adapter, applying the CH397 TX/RX frame headers.
#pragma once

#include <windows.h>
#include <thread>
#include <atomic>
#include <memory>
#include <array>
#include <string>

#include "usb_transport.h"
#include "wintun.h" // vendored, see third_party/wintun/README.md

namespace ch397 {

class NetifBridge {
public:
    explicit NetifBridge(std::shared_ptr<UsbTransport> transport);
    ~NetifBridge();

    NetifBridge(const NetifBridge&) = delete;
    NetifBridge& operator=(const NetifBridge&) = delete;

    // Loads wintun.dll, creates/opens the "CH397 Ethernet" adapter, reads
    // the device MAC and link state, and starts the pump + link-poll
    // threads. Returns false if any step fails.
    bool Start();
    void Stop();

private:
    void PumpUsbToTun();
    void PumpTunToUsb();
    void PollLink();

    std::shared_ptr<UsbTransport> transport_;

    HMODULE wintunModule_ = nullptr;
    WINTUN_ADAPTER_HANDLE adapter_ = nullptr;
    WINTUN_SESSION_HANDLE session_ = nullptr;

    std::atomic<bool> running_{ false };
    std::thread usbToTunThread_;
    std::thread tunToUsbThread_;
    std::thread linkPollThread_;
};

} // namespace ch397
