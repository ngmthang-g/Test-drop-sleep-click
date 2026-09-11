#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace cleanroute {

constexpr std::uint32_t kMagic = 0x54534253u; // TSBS
constexpr std::uint32_t kProtocolVersion = 0x00010001u;
constexpr UINT kWakeMessage = WM_APP + 0x531;
constexpr wchar_t kMappingPrefix[] = L"Local\\ThanLongTestSell_";

enum class Command : std::uint32_t {
    None = 0,
    ReadBagPage = 19,
    ClickInternalPointRawTest = 26,
    DragInternalPoint = 27,
    ClickItemSellAction = 28,
    ProbeItemSellAction = 29,
    ProbeTradeState = 30,
};

enum class ActionResult : std::int32_t {
    None = 0,
    ActionInvoked = 1,
};

// ProbeTradeState resultCode bit flags. The only primary lifecycle signal is
// FindUI("Trade") existence. ActiveInHierarchy/ExchangeID are diagnostics only.
constexpr std::int32_t kTradeProbeFindUiOk = 1 << 0;
constexpr std::int32_t kTradeProbeUiExists = 1 << 1;
constexpr std::int32_t kTradeProbeActiveKnown = 1 << 2;
constexpr std::int32_t kTradeProbeActive = 1 << 3;
constexpr std::int32_t kTradeProbeExchangeIdKnown = 1 << 4;

struct BagItemSnapshot {
    std::int64_t instanceID = 0;
    std::int32_t itemID = 0;
    std::int32_t site = 0;
    std::int32_t position = -1;
    std::int32_t quantity = 0;
    std::int32_t bound = 0;
    std::int32_t throwable = 0;
    std::int32_t sellable = 0;
    std::int32_t isEquip = 0;
    std::int32_t isWeapon = 0;
    std::int32_t itemTypeCode = 0;
    std::int32_t equipTypeCode = 0;
    wchar_t name[96]{};
    wchar_t itemType[32]{};
    wchar_t equipType[32]{};
};

constexpr std::size_t kBagPageCapacity = 20;

struct BagPageSnapshot {
    std::int32_t totalCount = 0;
    std::int32_t pageStart = 0;
    std::int32_t pageCount = 0;
    std::int32_t freeBagSpace = -1;
    BagItemSnapshot items[kBagPageCapacity]{};
};

struct Request {
    std::uint32_t command = 0;
    std::int32_t arg0 = 0;
    std::int32_t arg1 = 0;
    std::int32_t arg2 = 0;
};

struct Response {
    std::int32_t ok = 0;
    std::int32_t resultCode = 0;
    std::int32_t value0 = 0;
    std::int32_t value1 = 0;
    std::int64_t value64_0 = 0;
    std::int64_t value64_1 = 0;
    BagPageSnapshot bagPage{};
    wchar_t detail[512]{};
};

struct SharedBlock {
    std::uint32_t magic = kMagic;
    std::uint32_t protocolVersion = kProtocolVersion;
    std::uint32_t targetPid = 0;
    std::uint32_t targetWindowThreadId = 0;
    volatile LONG requestSeq = 0;
    volatile LONG completedSeq = 0;
    volatile LONG bridgeLoaded = 0;
    volatile LONG bridgeBusy = 0;
    Request request{};
    Response response{};
};

inline void MappingName(DWORD pid, wchar_t* output, std::size_t count) {
    if (!output || count == 0) return;
    wsprintfW(output, L"%s%lu", kMappingPrefix, static_cast<unsigned long>(pid));
}

} // namespace cleanroute
