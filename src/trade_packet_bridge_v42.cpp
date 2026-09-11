#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include <cstring>
#include <MinHook.h>
#include "trade_packet_protocol.h"

using namespace tradepacket;

namespace {

using Il2CppDomain = void;
using Il2CppAssembly = void;
using Il2CppImage = void;
using Il2CppClass = void;
using Il2CppType = void;
using MethodInfo = void;
using Il2CppArray = void;

HANDLE g_mapping = nullptr;
SharedBlock* g_shared = nullptr;
SRWLOCK g_snapshotLock = SRWLOCK_INIT;
volatile LONG g_installed = 0;
volatile LONG64 g_rawPacketCount = 0;
volatile LONG g_seenAnyRaw = 0;
volatile LONG g_seenTradeRaw = 0;
ULONGLONG g_lastSampleTick = 0;

struct Api {
    HMODULE module = nullptr;
    Il2CppDomain* (__cdecl* domain_get)() = nullptr;
    const Il2CppAssembly** (__cdecl* domain_get_assemblies)(Il2CppDomain*, std::size_t*) = nullptr;
    const Il2CppImage* (__cdecl* assembly_get_image)(const Il2CppAssembly*) = nullptr;
    std::size_t (__cdecl* image_get_class_count)(const Il2CppImage*) = nullptr;
    Il2CppClass* (__cdecl* image_get_class)(const Il2CppImage*, std::size_t) = nullptr;
    const char* (__cdecl* class_get_name)(Il2CppClass*) = nullptr;
    const char* (__cdecl* class_get_namespace)(Il2CppClass*) = nullptr;
    Il2CppClass* (__cdecl* class_get_parent)(Il2CppClass*) = nullptr;
    const MethodInfo* (__cdecl* class_get_methods)(Il2CppClass*, void**) = nullptr;
    const char* (__cdecl* method_get_name)(const MethodInfo*) = nullptr;
    std::uint32_t (__cdecl* method_get_param_count)(const MethodInfo*) = nullptr;
    const Il2CppType* (__cdecl* method_get_param)(const MethodInfo*, std::uint32_t) = nullptr;
    const Il2CppType* (__cdecl* method_get_return_type)(const MethodInfo*) = nullptr;
    std::uint32_t (__cdecl* method_get_flags)(const MethodInfo*, std::uint32_t*) = nullptr;
    char* (__cdecl* type_get_name)(const Il2CppType*) = nullptr;
    void (__cdecl* free_fn)(void*) = nullptr;
    std::uintptr_t (__cdecl* array_length)(Il2CppArray*) = nullptr;
};

Api g_api{};

template <typename T>
bool Resolve(HMODULE module, const char* name, T& out) {
    out = nullptr;
    FARPROC p = module ? GetProcAddress(module, name) : nullptr;
    if (!p) return false;
    static_assert(sizeof(p) == sizeof(out), "pointer size mismatch");
    std::memcpy(&out, &p, sizeof(out));
    return out != nullptr;
}

bool Eq(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

void CopyWide(wchar_t* dst, std::size_t cap, const wchar_t* src) {
    if (!dst || cap == 0) return;
    if (!src) src = L"";
    _snwprintf_s(dst, cap, _TRUNCATE, L"%s", src);
}

void CopyAnsiToWide(wchar_t* dst, std::size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src) return;
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, static_cast<int>(cap));
    dst[cap - 1] = 0;
}

void BeginWrite() {
    AcquireSRWLockExclusive(&g_snapshotLock);
    InterlockedIncrement(&g_shared->observer.seq);
    MemoryBarrier();
}

void EndWrite() {
    MemoryBarrier();
    InterlockedIncrement(&g_shared->observer.seq);
    ReleaseSRWLockExclusive(&g_snapshotLock);
}

void SetInstallFailure(const wchar_t* detail) {
    if (!g_shared) return;
    BeginWrite();
    g_shared->observer.installError = 1;
    g_shared->observer.installed = 0;
    CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail), detail);
    EndWrite();
}

bool EnsureShared() {
    if (g_shared) return true;
    wchar_t name[96]{};
    MappingName(GetCurrentProcessId(), name, _countof(name));
    g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!g_mapping) return false;
    g_shared = reinterpret_cast<SharedBlock*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
    if (!g_shared || g_shared->magic != kMagic || g_shared->version != kVersion ||
        g_shared->targetPid != GetCurrentProcessId()) {
        if (g_shared) UnmapViewOfFile(g_shared);
        if (g_mapping) CloseHandle(g_mapping);
        g_shared = nullptr;
        g_mapping = nullptr;
        return false;
    }
    return true;
}

bool LoadApi(wchar_t* error, std::size_t cap) {
    if (g_api.module) return true;
    g_api.module = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_api.module) {
        CopyWide(error, cap, L"V4.2 • GameAssembly.dll chưa sẵn sàng");
        return false;
    }
#define NEED(member, exportName) \
    do { if (!Resolve(g_api.module, exportName, g_api.member)) { \
        _snwprintf_s(error, cap, _TRUNCATE, L"V4.2 • thiếu IL2CPP export: %S", exportName); return false; } } while (0)
    NEED(domain_get, "il2cpp_domain_get");
    NEED(domain_get_assemblies, "il2cpp_domain_get_assemblies");
    NEED(assembly_get_image, "il2cpp_assembly_get_image");
    NEED(image_get_class_count, "il2cpp_image_get_class_count");
    NEED(image_get_class, "il2cpp_image_get_class");
    NEED(class_get_name, "il2cpp_class_get_name");
    NEED(class_get_namespace, "il2cpp_class_get_namespace");
    NEED(class_get_parent, "il2cpp_class_get_parent");
    NEED(class_get_methods, "il2cpp_class_get_methods");
    NEED(method_get_name, "il2cpp_method_get_name");
    NEED(method_get_param_count, "il2cpp_method_get_param_count");
    NEED(method_get_param, "il2cpp_method_get_param");
    NEED(method_get_return_type, "il2cpp_method_get_return_type");
    NEED(method_get_flags, "il2cpp_method_get_flags");
    NEED(type_get_name, "il2cpp_type_get_name");
    NEED(array_length, "il2cpp_array_length");
#undef NEED
    if (!Resolve(g_api.module, "il2cpp_free", g_api.free_fn)) {
        CopyWide(error, cap, L"V4.2 • thiếu il2cpp_free");
        return false;
    }
    return true;
}

bool GetTypeName(const Il2CppType* type, char* out, std::size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = 0;
    if (!type || !g_api.type_get_name) return false;
    char* name = g_api.type_get_name(type);
    if (!name) return false;
    strncpy_s(out, cap, name, _TRUNCATE);
    g_api.free_fn(name);
    return out[0] != 0;
}

struct TargetInfo {
    const MethodInfo* method = nullptr;
    bool isStatic = false;
    char nameSpace[128]{};
    char className[128]{};
    char param[96]{};
};

bool CheckByteArrayMethod(const MethodInfo* method, char* param, std::size_t paramCap) {
    if (!method || g_api.method_get_param_count(method) != 1) return false;
    char ret[96]{};
    GetTypeName(g_api.method_get_param(method, 0), param, paramCap);
    GetTypeName(g_api.method_get_return_type(method), ret, _countof(ret));
    return Eq(param, "System.Byte[]") && Eq(ret, "System.Void");
}

bool ResolveTarget(TargetInfo& out, wchar_t* error, std::size_t cap) {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) {
        CopyWide(error, cap, L"V4.2 • il2cpp_domain_get trả NULL");
        return false;
    }

    std::size_t assemblyCount = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &assemblyCount);
    wchar_t firstSeen[260]{};
    for (std::size_t ai = 0; assemblies && ai < assemblyCount; ++ai) {
        const Il2CppImage* image = g_api.assembly_get_image(assemblies[ai]);
        if (!image) continue;
        const std::size_t classCount = g_api.image_get_class_count(image);
        for (std::size_t ci = 0; ci < classCount; ++ci) {
            Il2CppClass* start = g_api.image_get_class(image, ci);
            if (!start || !Eq(g_api.class_get_name(start), "LuaSystemManager")) continue;
            for (Il2CppClass* klass = start; klass; klass = g_api.class_get_parent(klass)) {
                void* iter = nullptr;
                while (const MethodInfo* method = g_api.class_get_methods(klass, &iter)) {
                    if (!Eq(g_api.method_get_name(method), "OnReceivePacket")) continue;
                    const std::uint32_t pc = g_api.method_get_param_count(method);
                    char p0[96]{};
                    if (pc >= 1) GetTypeName(g_api.method_get_param(method, 0), p0, _countof(p0));
                    if (!firstSeen[0]) {
                        wchar_t wp0[96]{};
                        CopyAnsiToWide(wp0, _countof(wp0), p0);
                        _snwprintf_s(firstSeen, _countof(firstSeen), _TRUNCATE,
                                     L"OnReceivePacket(%s) • paramCount=%u", wp0, pc);
                    }
                    char param[96]{};
                    if (!CheckByteArrayMethod(method, param, _countof(param))) continue;
                    out.method = method;
                    strncpy_s(out.nameSpace, g_api.class_get_namespace(klass) ? g_api.class_get_namespace(klass) : "", _TRUNCATE);
                    strncpy_s(out.className, g_api.class_get_name(klass) ? g_api.class_get_name(klass) : "LuaSystemManager", _TRUNCATE);
                    strncpy_s(out.param, param, _TRUNCATE);
                    std::uint32_t iflags = 0;
                    const std::uint32_t flags = g_api.method_get_flags(method, &iflags);
                    out.isStatic = (flags & 0x0010u) != 0;
                    return true;
                }
            }
        }
    }

    if (firstSeen[0])
        _snwprintf_s(error, cap, _TRUNCATE, L"V4.2 resolver FAIL • %s", firstSeen);
    else
        CopyWide(error, cap, L"V4.2 resolver FAIL • không tìm thấy LuaSystemManager.OnReceivePacket");
    return false;
}

// Standard IL2CPP x64 array header: object(klass,monitor) + bounds + max_length.
struct ArrayHeader64 {
    void* klass;
    void* monitor;
    void* bounds;
    std::uintptr_t maxLength;
};

bool Match(const std::uint8_t* bytes, std::size_t len, std::size_t at,
           const std::uint8_t* pattern, std::size_t patternLen) {
    return bytes && pattern && at + patternLen <= len &&
           std::memcmp(bytes + at, pattern, patternLen) == 0;
}

enum class CmdEncoding { None, LE32, BE32, ASCII, UTF16LE, VARINT };

const wchar_t* CmdEncodingName(CmdEncoding e) {
    switch (e) {
    case CmdEncoding::LE32: return L"LE32";
    case CmdEncoding::BE32: return L"BE32";
    case CmdEncoding::ASCII: return L"ASCII";
    case CmdEncoding::UTF16LE: return L"UTF16LE";
    case CmdEncoding::VARINT: return L"VARINT";
    default: return L"NONE";
    }
}

bool FindTradeId(const std::uint8_t* bytes, std::size_t len,
                 std::size_t& offset, std::size_t& patternLen, CmdEncoding& encoding) {
    static const std::uint8_t le32[] = {0x75, 0x0D, 0x03, 0x00};
    static const std::uint8_t be32[] = {0x00, 0x03, 0x0D, 0x75};
    static const std::uint8_t ascii[] = {'2','0','0','0','5','3'};
    static const std::uint8_t utf16le[] = {'2',0,'0',0,'0',0,'0',0,'5',0,'3',0};
    static const std::uint8_t varint[] = {0xF5, 0x9A, 0x0C};
    struct P { const std::uint8_t* p; std::size_t n; CmdEncoding e; };
    const P patterns[] = {
        {le32, sizeof(le32), CmdEncoding::LE32},
        {be32, sizeof(be32), CmdEncoding::BE32},
        {ascii, sizeof(ascii), CmdEncoding::ASCII},
        {utf16le, sizeof(utf16le), CmdEncoding::UTF16LE},
        {varint, sizeof(varint), CmdEncoding::VARINT}
    };
    for (std::size_t i = 0; i < len; ++i) {
        for (const auto& p : patterns) {
            if (Match(bytes, len, i, p.p, p.n)) {
                offset = i;
                patternLen = p.n;
                encoding = p.e;
                return true;
            }
        }
    }
    return false;
}

bool FindCloseAfter(const std::uint8_t* bytes, std::size_t len, std::size_t begin,
                    std::size_t& closeOffset) {
    static const std::uint8_t ascii[] = {'-','1'};
    static const std::uint8_t utf16le[] = {'-',0,'1',0};
    if (begin >= len) return false;
    const std::size_t limit = (begin + 768 < len) ? begin + 768 : len;
    for (std::size_t i = begin; i < limit; ++i) {
        if (Match(bytes, len, i, ascii, sizeof(ascii)) ||
            Match(bytes, len, i, utf16le, sizeof(utf16le))) {
            closeOffset = i;
            return true;
        }
    }
    return false;
}

void FormatHexWindow(const std::uint8_t* bytes, std::size_t len, std::size_t center,
                     wchar_t* out, std::size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!bytes || !len) return;
    std::size_t begin = center > 24 ? center - 24 : 0;
    std::size_t end = begin + 56;
    if (end > len) end = len;
    std::size_t pos = 0;
    for (std::size_t i = begin; i < end && pos + 4 < cap; ++i) {
        const int w = _snwprintf_s(out + pos, cap - pos, _TRUNCATE, L"%02X ", bytes[i]);
        if (w <= 0) break;
        pos += static_cast<std::size_t>(w);
    }
}

void WriteLiveSample(const std::uint8_t* bytes, std::size_t len) {
    if (!g_shared || !bytes || !len || InterlockedCompareExchange(&g_seenTradeRaw, 0, 0)) return;
    wchar_t sample[192]{};
    FormatHexWindow(bytes, len, 0, sample, _countof(sample));
    const LONGLONG raw = InterlockedCompareExchange64(&g_rawPacketCount, 0, 0);
    const bool first = InterlockedCompareExchange(&g_seenAnyRaw, 1, 0) == 0;
    BeginWrite();
    CopyWide(g_shared->observer.lastPayload, _countof(g_shared->observer.lastPayload), sample);
    g_shared->observer.lastTradeTick = GetTickCount64();
    if (first) {
        _snwprintf_s(g_shared->observer.detail, _countof(g_shared->observer.detail), _TRUNCATE,
                     L"V4.2 LIVE • OnReceivePacket(System.Byte[]) đang nhận raw packet • raw=%lld • len=%llu",
                     raw, static_cast<unsigned long long>(len));
    }
    EndWrite();
}

void ObserveRawPacket(Il2CppArray* array) {
    if (!g_shared || !array || !g_api.array_length) return;
    InterlockedIncrement64(&g_rawPacketCount);

    __try {
        const std::uintptr_t rawLen = g_api.array_length(array);
        if (rawLen == 0 || rawLen > 16u * 1024u * 1024u) return;
        const auto* header = reinterpret_cast<const ArrayHeader64*>(array);
        if (header->maxLength != rawLen) return;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(array) + sizeof(ArrayHeader64);
        const std::size_t len = static_cast<std::size_t>(rawLen);
        const std::size_t scanLen = len < 8192 ? len : 8192;

        std::size_t cmdOffset = 0, cmdLen = 0;
        CmdEncoding encoding = CmdEncoding::None;
        if (!FindTradeId(bytes, scanLen, cmdOffset, cmdLen, encoding)) {
            const ULONGLONG now = GetTickCount64();
            if (InterlockedCompareExchange(&g_seenAnyRaw, 0, 0) == 0 || now - g_lastSampleTick >= 500) {
                g_lastSampleTick = now;
                WriteLiveSample(bytes, scanLen);
            }
            return;
        }

        InterlockedExchange(&g_seenTradeRaw, 1);
        std::size_t closeOffset = 0;
        const bool closed = FindCloseAfter(bytes, scanLen, cmdOffset + cmdLen, closeOffset);
        wchar_t sample[192]{};
        FormatHexWindow(bytes, scanLen, cmdOffset, sample, _countof(sample));

        BeginWrite();
        const LONG previousState = g_shared->observer.state;
        g_shared->observer.seenTradePacket = 1;
        ++g_shared->observer.tradePacketCount;
        g_shared->observer.lastPacketId = kTradePacketId;
        g_shared->observer.lastTradeTick = GetTickCount64();
        CopyWide(g_shared->observer.lastPayload, _countof(g_shared->observer.lastPayload), sample);
        if (closed) {
            if (previousState != static_cast<LONG>(TradeState::Active))
                ++g_shared->observer.closeWithoutActive;
            g_shared->observer.state = static_cast<LONG>(TradeState::Idle);
            ++g_shared->observer.finishSeq;
            _snwprintf_s(g_shared->observer.detail, _countof(g_shared->observer.detail), _TRUNCATE,
                         L"V4.2 • RAW 200053=%s@%llu • -1@%llu • TRADE_FINISHED candidate • len=%llu",
                         CmdEncodingName(encoding),
                         static_cast<unsigned long long>(cmdOffset),
                         static_cast<unsigned long long>(closeOffset),
                         static_cast<unsigned long long>(len));
        } else {
            g_shared->observer.state = static_cast<LONG>(TradeState::Active);
            _snwprintf_s(g_shared->observer.detail, _countof(g_shared->observer.detail), _TRUNCATE,
                         L"V4.2 • RAW 200053=%s@%llu • TRADE_ACTIVE candidate • len=%llu",
                         CmdEncodingName(encoding),
                         static_cast<unsigned long long>(cmdOffset),
                         static_cast<unsigned long long>(len));
        }
        EndWrite();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Passive observer must never take the game down because a diagnostic read failed.
    }
}

using InstFn = void (__fastcall*)(void*, Il2CppArray*, const MethodInfo*);
using StaticFn = void (__fastcall*)(Il2CppArray*, const MethodInfo*);
InstFn g_originalInst = nullptr;
StaticFn g_originalStatic = nullptr;
bool g_targetStatic = false;

void __fastcall HookInst(void* self, Il2CppArray* data, const MethodInfo* mi) {
    __try { ObserveRawPacket(data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_originalInst(self, data, mi);
}

void __fastcall HookStatic(Il2CppArray* data, const MethodInfo* mi) {
    __try { ObserveRawPacket(data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_originalStatic(data, mi);
}

bool InstallObserver() {
    if (InterlockedCompareExchange(&g_installed, 0, 0) != 0) return true;
    if (!EnsureShared()) return false;

    wchar_t error[320]{};
    if (!LoadApi(error, _countof(error))) {
        SetInstallFailure(error);
        return false;
    }

    TargetInfo info{};
    if (!ResolveTarget(info, error, _countof(error))) {
        SetInstallFailure(error);
        return false;
    }

    void* target = *reinterpret_cast<void* const*>(info.method);
    if (!target) {
        SetInstallFailure(L"V4.2 • OnReceivePacket(byte[]) native pointer NULL");
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.AllocationBase != g_api.module) {
        SetInstallFailure(L"V4.2 • native pointer không thuộc GameAssembly.dll");
        return false;
    }

    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        SetInstallFailure(L"V4.2 • MinHook initialize FAIL");
        return false;
    }

    g_targetStatic = info.isStatic;
    MH_STATUS createStatus = MH_UNKNOWN;
    if (g_targetStatic) {
        createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookStatic),
                                     reinterpret_cast<LPVOID*>(&g_originalStatic));
    } else {
        createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookInst),
                                     reinterpret_cast<LPVOID*>(&g_originalInst));
    }
    if (createStatus != MH_OK) {
        SetInstallFailure(L"V4.2 • MinHook create OnReceivePacket(byte[]) FAIL");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        SetInstallFailure(L"V4.2 • MinHook enable OnReceivePacket(byte[]) FAIL");
        return false;
    }

    HMODULE pinned = nullptr;
    (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(&InstallObserver), &pinned);

    const std::uint64_t rva = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(g_api.module));
    wchar_t wNs[128]{}, wClass[128]{};
    CopyAnsiToWide(wNs, _countof(wNs), info.nameSpace);
    CopyAnsiToWide(wClass, _countof(wClass), info.className);

    BeginWrite();
    g_shared->observer.installed = 1;
    g_shared->observer.installError = 0;
    g_shared->observer.state = static_cast<LONG>(TradeState::Unknown);
    g_shared->observer.methodRva = rva;
    _snwprintf_s(g_shared->observer.signature, _countof(g_shared->observer.signature), _TRUNCATE,
                 L"V4.2 %s.%s.OnReceivePacket(System.Byte[])->System.Void • %s",
                 wNs, wClass, g_targetStatic ? L"static" : L"instance");
    CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
             L"V4.2 READY • hook đúng OnReceivePacket(System.Byte[]) • chờ raw packet");
    EndWrite();
    InterlockedExchange(&g_installed, 1);
    return true;
}

} // namespace

extern "C" __declspec(dllexport) LRESULT CALLBACK TltTradeObserverHook(int code, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (code >= 0 && lParam) {
        const MSG* msg = reinterpret_cast<const MSG*>(lParam);
        if (msg->message == kBootstrapMessage) (void)InstallObserver();
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
