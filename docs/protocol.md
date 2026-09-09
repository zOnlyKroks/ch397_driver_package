# CH397/CH398 USB vendor protocol

This documents the USB vendor protocol implemented by WCH's Linux driver
(`linux/driver/ch397.c`), used as the reference for the Windows user-mode
implementation in `windows/service/`. Line references point at
`linux/driver/ch397.c`.

## Device identification

- USB VID: `0x1a86`
- PIDs and register map ("version"):
  | PID | Chip | Register map |
  |-----|------|---------------|
  | `0x5394` | ch336 | V01 (`ch397_regs`) |
  | `0x5395` | ch339 | V01 (`ch397_regs`) |
  | `0x5396` | ch396 | V01 (`ch397_regs`) |
  | `0x5397` | ch397 | V01 (`ch397_regs`) |
  | `0x5398` | ch398 | V02 (`ch398_regs`), gigabit |
  | `0xe398` | ch9153 | V02 (`ch398_regs`), gigabit |

  CH398 devices (`0x5398`/`0xe398`) enumerate in a non-default USB
  configuration and must be switched to configuration value `3` before the
  data/control endpoints are usable (`ch397_probe`, ch397.c:4716-4752). The
  V01 chips must be in configuration `1`.

## Endpoints

Each device interface exposes:
- Control endpoint 0 — vendor-specific requests (see below)
- One bulk IN endpoint — RX Ethernet frames
- One bulk OUT endpoint — TX Ethernet frames
- One interrupt IN endpoint — link/stat events (`ch397_check_endpoints`,
  ch397.c:4669-4714)

## Vendor control requests

All requests are `bmRequestType = USB_TYPE_VENDOR | USB_RECIP_DEVICE`,
direction IN or OUT depending on the command. `wValue` holds the low 16
bits of a 32-bit register address, `wIndex` holds the high 16 bits
(`ch397_read`/`ch397_write`, ch397.c:875-968):

| Command | Value | Direction | Purpose |
|---------|-------|-----------|---------|
| `CMD_GET_INFO` | `0x10` | IN | Read 8-byte device info (`ch397_info`) |
| `CMD_RD_REG` | `0x11` | IN | Read a device register |
| `CMD_WR_REG` | `0x12` | OUT | Write a device register |
| `CMD_RD_OTH` | `0x15` | IN | Read PHY/MDIO-mapped register |
| `CMD_WR_OTH` | `0x16` | OUT | Write PHY/MDIO-mapped register |
| `CMD_IC_RESET` | `0x18` | OUT | Reset the chip |
| `CMD_WR_ETH_MACCFG` | `0x1E` | OUT | Write MAC config |
| `CMD_WR_ETH_AUTONEG` | `0x1F` | OUT | Configure autonegotiation |
| `CMD_SET_MULTI_PACK` | `0x20` | OUT | Configure multicast hash filter |
| `CMD_SET_MISC_CONFIG` | `0x21` | OUT | Misc config register |
| `CMD_SET_WOL_LINKSPD` | `0x23` | OUT | Wake-on-LAN link speed config |

Timeouts: 500 ms for both directions; a 3 ms delay (`CH397_USB_DELAY`) is
inserted after each control transfer.

## Key registers (V01 / ch397_regs, ch397.c:123-153)

| Register | Address |
|----------|---------|
| `ETH_MAC_CFG` | `0x40000700` |
| `ETH_MAC_H` (MAC high 16 bits) | `0x40000710` |
| `ETH_MAC_L` (MAC low 32 bits) | `0x40000714` |
| `ETH_MAC_HTHR` (multicast hash high) | `0x40000730` |
| `ETH_MAC_HTLR` (multicast hash low) | `0x40000734` |
| `ETH_BMSR` (shared MDIO/PHY access via `CMD_RD_OTH`/`CMD_WR_OTH`) | `0x00000740` |
| `ETH_MACIER` (interrupt/WOL enable bits) | `0x4000073c` |

V02 / ch398_regs (ch397.c:154-194) uses the same field layout at different
base addresses (`0x40024xxx`).

MAC address is read/written as: 4 bytes at `mac_addrl`, 2 bytes at
`mac_addrh` (`ch397_get_mac_address`/`__ch397_set_mac_address`,
ch397.c:3658-3692).

## RX/TX framing

Each bulk transfer contains one or more frames, each prefixed by an 8-byte
header (`ch397_rx_header`/`ch397_tx_header`, ch397.c:258-290):

- RX header: `cmd_0` (bits 0-15 = frame length), `cmd_1` (VLAN tag,
  checksum-error flags).
- TX header: `cmd_0` (bits 0-17 = frame length, TSO flags, TCP header
  offset), `cmd_1` (VLAN tag, per-protocol checksum-offload flags, MSS for
  TSO). TX buffers must be 4-byte aligned; V02 high-speed mode requires an
  extra 4-byte `TX_LS` trailer per USB packet.

The initial Windows implementation only needs the length field from each
header on RX, and can zero out all offload flags on TX (no TSO/checksum
offload in the first pass — see `windows/README.md`).

## Interrupt endpoint events

`ch397_intr_event` (V01) / `ch398_intr_event` (V02), ch397.c:366-391:
link status bit (`LINK_RDY`), duplex bit, link speed bit(s), plus
running RX/TX packet counters and RX overflow/CRC error counters. Polled
at `bInterval` reported by the interrupt endpoint descriptor.

## Multicast / promiscuous mode

Set via `CMD_SET_MULTI_PACK` writes to `mac_hthr`/`mac_htlr`: promiscuous
sets an "receive-all" bit, multicast addresses are hashed with CRC32
(bit-reversed, top 6 bits) into a 64-bit filter split across the two hash
registers (`_ch397_set_rx_mode`, ch397.c:3739-3799).
