// WinUSB transport for the CH397/CH398 vendor protocol.
#pragma once

#include <windows.h>
#include <winusb.h>
#include <string>
#include <vector>
#include <array>
#include <optional>

#include "ch397_protocol.h"

namespace ch397 {

class UsbTransport {
public:
    UsbTransport() = default;
    ~UsbTransport();

    UsbTransport(const UsbTransport&) = delete;
    UsbTransport& operator=(const UsbTransport&) = delete;

    // Opens the WinUSB handle for a device at the given SetupAPI device
    // interface path (as produced by DeviceWatcher) and identifies which
    // supported chip it is based on the descriptor's product ID.
    bool Open(const std::wstring& devicePath);
    void Close();

    bool IsOpen() const { return winusbHandle_ != nullptr; }
    ChipVersion Version() const { return version_; }

    // Vendor control transfers; mirror ch397_read()/ch397_write() in
    // ch397.c. `reg` packs a 32-bit register address the same way
    // (low 16 bits -> wValue, high 16 bits -> wIndex).
    bool ReadRegister(uint8_t cmd, uint32_t reg, uint16_t length, void* data);
    bool WriteRegister(uint8_t cmd, uint32_t reg, uint16_t length, const void* data);

    bool ReadRegister32(uint8_t cmd, uint32_t reg, uint32_t& value);
    bool WriteRegister32(uint8_t cmd, uint32_t reg, uint32_t value);

    bool GetDeviceInfo(std::array<uint8_t, 8>& info);
    bool GetMacAddress(std::array<uint8_t, 6>& mac);
    bool SetMacAddress(const std::array<uint8_t, 6>& mac);

    // Bulk data path.
    bool BulkRead(std::vector<uint8_t>& buffer, DWORD timeoutMs);
    bool BulkWrite(const uint8_t* data, uint32_t length, DWORD timeoutMs);

    // Interrupt endpoint (link status events), non-blocking with timeout.
    bool InterruptRead(std::vector<uint8_t>& buffer, DWORD timeoutMs);

    const RegisterMap& Registers() const { return RegistersFor(version_); }

private:
    bool ResolveEndpoints();

    HANDLE deviceHandle_ = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE winusbHandle_ = nullptr;
    ChipVersion version_ = ChipVersion::V01;
    uint16_t productId_ = 0;

    UCHAR bulkInPipe_ = 0;
    UCHAR bulkOutPipe_ = 0;
    UCHAR interruptInPipe_ = 0;
};

} // namespace ch397
