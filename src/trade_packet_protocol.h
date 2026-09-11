#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace tradepacket {

constexpr std::uint32_t kMagic = 0x34525054u; // TPR4
constexpr std::uint32_t kVersion = 0x00040000u;
constexpr UINT kBootstrapMessage = WM_APP + 0x534;
constexpr wchar_t kMappingPrefix[] = L"Local\\ThanLongTradePacketV4_";
constexpr std::int32_t kTradePacketId = 200053;

enum class TradeState : LONG {
    Unknown = 0,
    Idle = 1,
    Active = 2,
};

struct ObserverSnapshot {
    volatile LONG seq = 0;
    LONG installed = 0;
    LONG installError = 0;
    LONG state = static_cast<LONG>(TradeState::Unknown);
    LONG seenTradePacket = 0;
    LONG tradePacketCount = 0;
    LONG finishSeq = 0;
    LONG closeWithoutActive = 0;
    LONG lastPacketId = 0;
    std::uint64_t lastTradeTick = 0;
    std::uint64_t methodRva = 0;
    wchar_t signature[160]{};
    wchar_t lastPayload[192]{};
    wchar_t detail[320]{};
};

struct SharedBlock {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t targetPid = 0;
    std::uint32_t targetWindowThreadId = 0;
    ObserverSnapshot observer{};
};

inline void MappingName(DWORD pid, wchar_t* output, std::size_t count) {
    if (!output || count == 0) return;
    _snwprintf_s(output, count, _TRUNCATE, L"%s%lu", kMappingPrefix,
                 static_cast<unsigned long>(pid));
}

} // namespace tradepacket
