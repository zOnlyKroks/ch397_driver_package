// CH397/CH398 USB vendor protocol constants.
// Mirrors linux/driver/ch397.c; see docs/protocol.md for the full writeup.
#pragma once

#include <cstdint>
#include <array>

namespace ch397 {

constexpr uint16_t kVendorId = 0x1a86;

enum class ChipVersion { V01, V02 };

struct DeviceModel {
    uint16_t productId;
    ChipVersion version;
    const char* name;
};

// PIDs 0x5398/0xE398 must be running in USB configuration 3; V01 chips
// must be in configuration 1 (see ch397_probe in ch397.c).
constexpr std::array<DeviceModel, 6> kSupportedDevices{ {
    { 0x5394, ChipVersion::V01, "ch336" },
    { 0x5395, ChipVersion::V01, "ch339" },
    { 0x5396, ChipVersion::V01, "ch396" },
    { 0x5397, ChipVersion::V01, "ch397" },
    { 0x5398, ChipVersion::V02, "ch398" },
    { 0xe398, ChipVersion::V02, "ch9153" },
} };

// Vendor control request codes (bRequest).
enum ControlCommand : uint8_t {
    CmdGetInfo        = 0x10,
    CmdReadReg        = 0x11,
    CmdWriteReg       = 0x12,
    CmdReadOther      = 0x15,
    CmdWriteOther     = 0x16,
    CmdIcReset        = 0x18,
    CmdWriteMacCfg    = 0x1E,
    CmdWriteAutoneg   = 0x1F,
    CmdSetMultiPack   = 0x20,
    CmdSetMiscConfig  = 0x21,
    CmdSetWolLinkSpd  = 0x23,
};

struct RegisterMap {
    uint32_t macCfg;
    uint32_t macAddrHigh;
    uint32_t macAddrLow;
    uint32_t macHashHigh;
    uint32_t macHashLow;
    uint32_t macIer;
};

// V01 register map (ch397_regs in ch397.c).
constexpr RegisterMap kRegsV01{
    0x40000700, 0x40000710, 0x40000714, 0x40000730, 0x40000734, 0x4000073c,
};

// V02 register map (ch398_regs in ch397.c).
constexpr RegisterMap kRegsV02{
    0x40024000, 0x40024010, 0x40024014, 0x40024030, 0x40024034, 0x4002403c,
};

constexpr uint32_t kSharedMdioReg = 0x00000740; // CH397_ETH_BMSR

// RX/TX frame headers, 8 bytes each, little-endian (see ch397_rx_header /
// ch397_tx_header in ch397.c). Only the length field is used by the
// initial Windows implementation; offload flags are always zero on TX.
#pragma pack(push, 1)
struct RxHeader {
    uint32_t cmd0; // bits 0-15: frame length
    uint32_t cmd1; // VLAN tag / checksum error flags
};

struct TxHeader {
    uint32_t cmd0; // bits 0-17: frame length
    uint32_t cmd1; // VLAN tag / checksum offload flags (unused, zero-filled)
};
#pragma pack(pop)

constexpr uint32_t kRxLenMask = 0xffffu;
constexpr uint32_t kTxLenMask = 0x3ffffu;

constexpr size_t kEthMinFrame = 60;
constexpr size_t kEthMaxFrame = 1514; // V01 chips; V02 supports jumbo (deferred).

// Interrupt endpoint link-status bits (ch397_intr_event.link_stat).
constexpr uint32_t kLinkReady = (1u << 6);
constexpr uint32_t kLinkSpeedBit = (1u << 7);
constexpr uint32_t kDuplexModeBit = (1u << 0);

inline const RegisterMap& RegistersFor(ChipVersion v) {
    return v == ChipVersion::V01 ? kRegsV01 : kRegsV02;
}

} // namespace ch397
