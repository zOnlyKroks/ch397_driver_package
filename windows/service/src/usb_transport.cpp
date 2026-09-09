#include "usb_transport.h"

#include <setupapi.h>
#include <usb.h>

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "setupapi.lib")

namespace ch397 {

namespace {

constexpr DWORD kControlTimeoutMs = 500; // matches CH397_USB_CTRL_*_TIMEOUT

bool LookupVersion(uint16_t pid, ChipVersion& version) {
    for (const auto& model : kSupportedDevices) {
        if (model.productId == pid) {
            version = model.version;
            return true;
        }
    }
    return false;
}

} // namespace

UsbTransport::~UsbTransport() { Close(); }

bool UsbTransport::Open(const std::wstring& devicePath) {
    Close();

    deviceHandle_ = CreateFileW(
        devicePath.c_str(), GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (deviceHandle_ == INVALID_HANDLE_VALUE)
        return false;

    if (!WinUsb_Initialize(deviceHandle_, &winusbHandle_)) {
        Close();
        return false;
    }

    USB_DEVICE_DESCRIPTOR desc{};
    ULONG transferred = 0;
    if (!WinUsb_GetDescriptor(winusbHandle_, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                               reinterpret_cast<PUCHAR>(&desc), sizeof(desc),
                               &transferred)) {
        Close();
        return false;
    }

    if (desc.idVendor != kVendorId || !LookupVersion(desc.idProduct, version_)) {
        Close();
        return false;
    }
    productId_ = desc.idProduct;

    if (!ResolveEndpoints()) {
        Close();
        return false;
    }

    return true;
}

void UsbTransport::Close() {
    if (winusbHandle_) {
        WinUsb_Free(winusbHandle_);
        winusbHandle_ = nullptr;
    }
    if (deviceHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(deviceHandle_);
        deviceHandle_ = INVALID_HANDLE_VALUE;
    }
}

bool UsbTransport::ResolveEndpoints() {
    USB_INTERFACE_DESCRIPTOR ifaceDesc{};
    if (!WinUsb_QueryInterfaceSettings(winusbHandle_, 0, &ifaceDesc))
        return false;

    bulkInPipe_ = bulkOutPipe_ = interruptInPipe_ = 0;

    for (UCHAR i = 0; i < ifaceDesc.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe{};
        if (!WinUsb_QueryPipe(winusbHandle_, 0, i, &pipe))
            continue;

        if (pipe.PipeType == UsbdPipeTypeBulk) {
            if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId) && !bulkInPipe_)
                bulkInPipe_ = pipe.PipeId;
            else if (USB_ENDPOINT_DIRECTION_OUT(pipe.PipeId) && !bulkOutPipe_)
                bulkOutPipe_ = pipe.PipeId;
        } else if (pipe.PipeType == UsbdPipeTypeInterrupt) {
            if (USB_ENDPOINT_DIRECTION_IN(pipe.PipeId) && !interruptInPipe_)
                interruptInPipe_ = pipe.PipeId;
        }
    }

    return bulkInPipe_ && bulkOutPipe_ && interruptInPipe_;
}

bool UsbTransport::ReadRegister(uint8_t cmd, uint32_t reg, uint16_t length, void* data) {
    WINUSB_SETUP_PACKET packet{};
    packet.RequestType = 0x80 /* IN */ | 0x40 /* vendor */ | 0x00 /* device */;
    packet.Request = cmd;
    packet.Value = static_cast<uint16_t>(reg & 0xFFFF);
    packet.Index = static_cast<uint16_t>((reg >> 16) & 0xFFFF);
    packet.Length = length;

    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(winusbHandle_, packet,
                                 static_cast<PUCHAR>(data), length,
                                 &transferred, nullptr))
        return false;

    return transferred == length;
}

bool UsbTransport::WriteRegister(uint8_t cmd, uint32_t reg, uint16_t length, const void* data) {
    WINUSB_SETUP_PACKET packet{};
    packet.RequestType = 0x00 /* OUT */ | 0x40 /* vendor */ | 0x00 /* device */;
    packet.Request = cmd;
    packet.Value = static_cast<uint16_t>(reg & 0xFFFF);
    packet.Index = static_cast<uint16_t>((reg >> 16) & 0xFFFF);
    packet.Length = length;

    ULONG transferred = 0;
    if (!WinUsb_ControlTransfer(winusbHandle_, packet,
                                 const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                                 length, &transferred, nullptr))
        return false;

    return transferred == length;
}

bool UsbTransport::ReadRegister32(uint8_t cmd, uint32_t reg, uint32_t& value) {
    uint32_t tmp = 0;
    if (!ReadRegister(cmd, reg, sizeof(tmp), &tmp))
        return false;
    value = tmp;
    return true;
}

bool UsbTransport::WriteRegister32(uint8_t cmd, uint32_t reg, uint32_t value) {
    return WriteRegister(cmd, reg, sizeof(value), &value);
}

bool UsbTransport::GetDeviceInfo(std::array<uint8_t, 8>& info) {
    return ReadRegister(CmdGetInfo, 0x00, static_cast<uint16_t>(info.size()), info.data());
}

bool UsbTransport::GetMacAddress(std::array<uint8_t, 6>& mac) {
    const auto& regs = Registers();
    // Low 4 bytes then high 2 bytes, matching ch397_get_mac_address.
    if (!ReadRegister(CmdReadReg, regs.macAddrLow, 4, mac.data()))
        return false;
    return ReadRegister(CmdReadReg, regs.macAddrHigh, 2, mac.data() + 4);
}

bool UsbTransport::SetMacAddress(const std::array<uint8_t, 6>& mac) {
    const auto& regs = Registers();
    uint8_t low[4] = { mac[0], mac[1], mac[2], mac[3] };
    uint8_t high[4] = { mac[4], mac[5], 0, 0 };
    if (!WriteRegister(CmdWriteReg, regs.macAddrLow, 4, low))
        return false;
    return WriteRegister(CmdWriteReg, regs.macAddrHigh, 4, high);
}

bool UsbTransport::BulkRead(std::vector<uint8_t>& buffer, DWORD timeoutMs) {
    WinUsb_SetPipePolicy(winusbHandle_, bulkInPipe_, PIPE_TRANSFER_TIMEOUT,
                          sizeof(timeoutMs), &timeoutMs);

    ULONG transferred = 0;
    if (!WinUsb_ReadPipe(winusbHandle_, bulkInPipe_, buffer.data(),
                          static_cast<ULONG>(buffer.size()), &transferred, nullptr))
        return false;

    buffer.resize(transferred);
    return true;
}

bool UsbTransport::BulkWrite(const uint8_t* data, uint32_t length, DWORD timeoutMs) {
    WinUsb_SetPipePolicy(winusbHandle_, bulkOutPipe_, PIPE_TRANSFER_TIMEOUT,
                          sizeof(timeoutMs), &timeoutMs);

    ULONG transferred = 0;
    if (!WinUsb_WritePipe(winusbHandle_, bulkOutPipe_,
                           const_cast<PUCHAR>(data), length, &transferred, nullptr))
        return false;

    return transferred == length;
}

bool UsbTransport::InterruptRead(std::vector<uint8_t>& buffer, DWORD timeoutMs) {
    WinUsb_SetPipePolicy(winusbHandle_, interruptInPipe_, PIPE_TRANSFER_TIMEOUT,
                          sizeof(timeoutMs), &timeoutMs);

    ULONG transferred = 0;
    if (!WinUsb_ReadPipe(winusbHandle_, interruptInPipe_, buffer.data(),
                          static_cast<ULONG>(buffer.size()), &transferred, nullptr))
        return false;

    buffer.resize(transferred);
    return true;
}

} // namespace ch397
