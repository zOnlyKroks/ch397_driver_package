#include "netif_bridge.h"

#include <vector>
#include <cstring>

namespace ch397 {

namespace {

constexpr wchar_t kAdapterName[] = L"CH397 Ethernet";
constexpr wchar_t kAdapterTunnelType[] = L"CH397";
constexpr DWORD kRingCapacity = 0x400000; // 4 MiB, within Wintun's allowed range.

// Function pointer table resolved from wintun.dll at runtime; Wintun does
// not ship an import library (see third_party/wintun/README.md).
struct WintunApi {
    WINTUN_CREATE_ADAPTER_FUNC* CreateAdapter = nullptr;
    WINTUN_CLOSE_ADAPTER_FUNC* CloseAdapter = nullptr;
    WINTUN_START_SESSION_FUNC* StartSession = nullptr;
    WINTUN_END_SESSION_FUNC* EndSession = nullptr;
    WINTUN_RECEIVE_PACKET_FUNC* ReceivePacket = nullptr;
    WINTUN_RELEASE_RECEIVE_PACKET_FUNC* ReleaseReceivePacket = nullptr;
    WINTUN_ALLOCATE_SEND_PACKET_FUNC* AllocateSendPacket = nullptr;
    WINTUN_SEND_PACKET_FUNC* SendPacket = nullptr;
} g_wintun;

template <typename T>
bool Resolve(HMODULE module, const char* name, T*& out) {
    out = reinterpret_cast<T*>(GetProcAddress(module, name));
    return out != nullptr;
}

bool LoadWintun(HMODULE& module) {
    module = LoadLibraryExW(L"wintun.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    if (!module)
        return false;

    return Resolve(module, "WintunCreateAdapter", g_wintun.CreateAdapter) &&
           Resolve(module, "WintunCloseAdapter", g_wintun.CloseAdapter) &&
           Resolve(module, "WintunStartSession", g_wintun.StartSession) &&
           Resolve(module, "WintunEndSession", g_wintun.EndSession) &&
           Resolve(module, "WintunReceivePacket", g_wintun.ReceivePacket) &&
           Resolve(module, "WintunReleaseReceivePacket", g_wintun.ReleaseReceivePacket) &&
           Resolve(module, "WintunAllocateSendPacket", g_wintun.AllocateSendPacket) &&
           Resolve(module, "WintunSendPacket", g_wintun.SendPacket);
}

} // namespace

NetifBridge::NetifBridge(std::shared_ptr<UsbTransport> transport)
    : transport_(std::move(transport)) {}

NetifBridge::~NetifBridge() { Stop(); }

bool NetifBridge::Start() {
    if (!LoadWintun(wintunModule_))
        return false;

    adapter_ = g_wintun.CreateAdapter(kAdapterName, kAdapterTunnelType, nullptr);
    if (!adapter_)
        return false;

    session_ = g_wintun.StartSession(adapter_, kRingCapacity);
    if (!session_) {
        g_wintun.CloseAdapter(adapter_);
        adapter_ = nullptr;
        return false;
    }

    running_ = true;
    usbToTunThread_ = std::thread(&NetifBridge::PumpUsbToTun, this);
    tunToUsbThread_ = std::thread(&NetifBridge::PumpTunToUsb, this);
    linkPollThread_ = std::thread(&NetifBridge::PollLink, this);
    return true;
}

void NetifBridge::Stop() {
    running_ = false;

    if (usbToTunThread_.joinable())
        usbToTunThread_.join();
    if (tunToUsbThread_.joinable())
        tunToUsbThread_.join();
    if (linkPollThread_.joinable())
        linkPollThread_.join();

    if (session_) {
        g_wintun.EndSession(session_);
        session_ = nullptr;
    }
    if (adapter_) {
        g_wintun.CloseAdapter(adapter_);
        adapter_ = nullptr;
    }
    if (wintunModule_) {
        FreeLibrary(wintunModule_);
        wintunModule_ = nullptr;
    }
}

// Device -> host: strip the CH397 RX header off each frame in a bulk
// transfer and hand the raw Ethernet frame to Wintun.
void NetifBridge::PumpUsbToTun() {
    std::vector<uint8_t> rxBuffer(64 * 1024);

    while (running_) {
        std::vector<uint8_t> buffer = rxBuffer;
        if (!transport_->BulkRead(buffer, /*timeoutMs=*/200) || buffer.empty())
            continue;

        size_t offset = 0;
        while (offset + sizeof(RxHeader) <= buffer.size()) {
            RxHeader header;
            std::memcpy(&header, buffer.data() + offset, sizeof(header));
            uint32_t frameLen = header.cmd0 & kRxLenMask;
            offset += sizeof(header);

            if (frameLen == 0 || offset + frameLen > buffer.size())
                break;

            BYTE* dst = g_wintun.AllocateSendPacket(session_, frameLen);
            if (dst) {
                std::memcpy(dst, buffer.data() + offset, frameLen);
                g_wintun.SendPacket(session_, dst);
            }

            // RX packets are 4-byte aligned within the aggregate buffer.
            offset += (frameLen + 3u) & ~3u;
        }
    }
}

// Host -> device: wrap each frame from Wintun with the CH397 TX header and
// send it as its own bulk OUT transfer (no aggregation in this first pass).
void NetifBridge::PumpTunToUsb() {
    while (running_) {
        DWORD packetSize = 0;
        BYTE* packet = g_wintun.ReceivePacket(session_, &packetSize);
        if (!packet) {
            Sleep(1);
            continue;
        }

        std::vector<uint8_t> frame(sizeof(TxHeader) + packetSize);
        TxHeader header{};
        header.cmd0 = packetSize & kTxLenMask;
        header.cmd1 = 0; // no VLAN/checksum offload in this first pass.
        std::memcpy(frame.data(), &header, sizeof(header));
        std::memcpy(frame.data() + sizeof(header), packet, packetSize);

        g_wintun.ReleaseReceivePacket(session_, packet);

        transport_->BulkWrite(frame.data(), static_cast<uint32_t>(frame.size()), 500);
    }
}

// Polls the interrupt endpoint for link-status changes. A future pass can
// surface this via NDIS media-connect-status equivalents; for now it just
// keeps the interrupt endpoint drained (device-side buffering is limited).
void NetifBridge::PollLink() {
    std::vector<uint8_t> intrBuffer(64);

    while (running_) {
        std::vector<uint8_t> buffer = intrBuffer;
        transport_->InterruptRead(buffer, /*timeoutMs=*/500);
    }
}

} // namespace ch397
