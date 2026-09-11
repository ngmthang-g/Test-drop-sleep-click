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
using Il2CppString = void;

HANDLE g_mapping = nullptr;
SharedBlock* g_shared = nullptr;
HMODULE g_selfModule = nullptr;
SRWLOCK g_snapshotLock = SRWLOCK_INIT;
volatile LONG g_installed = 0;

struct Api {
    HMODULE module = nullptr;
    Il2CppDomain* (__cdecl* domain_get)() = nullptr;
    const Il2CppAssembly* (__cdecl* domain_assembly_open)(Il2CppDomain*, const char*) = nullptr;
    const Il2CppImage* (__cdecl* assembly_get_image)(const Il2CppAssembly*) = nullptr;
    Il2CppClass* (__cdecl* class_from_name)(const Il2CppImage*, const char*, const char*) = nullptr;
    const MethodInfo* (__cdecl* class_get_method_from_name)(Il2CppClass*, const char*, int) = nullptr;
    std::uint32_t (__cdecl* method_get_param_count)(const MethodInfo*) = nullptr;
    const Il2CppType* (__cdecl* method_get_param)(const MethodInfo*, std::uint32_t) = nullptr;
    const Il2CppType* (__cdecl* method_get_return_type)(const MethodInfo*) = nullptr;
    std::uint32_t (__cdecl* method_get_flags)(const MethodInfo*, std::uint32_t*) = nullptr;
    char* (__cdecl* type_get_name)(const Il2CppType*) = nullptr;
    void (__cdecl* free_fn)(void*) = nullptr;
    std::int32_t (__cdecl* string_length)(Il2CppString*) = nullptr;
    const wchar_t* (__cdecl* string_chars)(Il2CppString*) = nullptr;
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
    InterlockedIncrement(&g_shared->observer.seq); // odd
    MemoryBarrier();
}

void EndWrite() {
    MemoryBarrier();
    InterlockedIncrement(&g_shared->observer.seq); // even
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
        CopyWide(error, cap, L"GameAssembly.dll chưa sẵn sàng");
        return false;
    }
#define NEED(member, exportName) \
    do { if (!Resolve(g_api.module, exportName, g_api.member)) { \
        CopyWide(error, cap, L"Thiếu IL2CPP export cần cho packet observer"); return false; } } while (0)
    NEED(domain_get, "il2cpp_domain_get");
    NEED(domain_assembly_open, "il2cpp_domain_assembly_open");
    NEED(assembly_get_image, "il2cpp_assembly_get_image");
    NEED(class_from_name, "il2cpp_class_from_name");
    NEED(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    NEED(method_get_param_count, "il2cpp_method_get_param_count");
    NEED(method_get_param, "il2cpp_method_get_param");
    NEED(method_get_return_type, "il2cpp_method_get_return_type");
    NEED(method_get_flags, "il2cpp_method_get_flags");
    NEED(type_get_name, "il2cpp_type_get_name");
    NEED(string_length, "il2cpp_string_length");
    NEED(string_chars, "il2cpp_string_chars");
#undef NEED
    if (!Resolve(g_api.module, "il2cpp_free", g_api.free_fn)) {
        CopyWide(error, cap, L"Thiếu il2cpp_free");
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

bool Eq(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

void ReadManagedString(Il2CppString* value, wchar_t* out, std::size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!value || !g_api.string_length || !g_api.string_chars) return;
    const std::int32_t len = g_api.string_length(value);
    const wchar_t* chars = g_api.string_chars(value);
    if (!chars || len <= 0) return;
    const std::size_t n = (static_cast<std::size_t>(len) < cap - 1)
        ? static_cast<std::size_t>(len) : cap - 1;
    std::memcpy(out, chars, n * sizeof(wchar_t));
    out[n] = 0;
}

bool IsClosePayload(Il2CppString* value) {
    if (!value) return false;
    const std::int32_t len = g_api.string_length(value);
    if (len != 2) return false;
    const wchar_t* chars = g_api.string_chars(value);
    return chars && chars[0] == L'-' && chars[1] == L'1';
}

void ObserveTradePacket(std::int32_t packetId, Il2CppString* data) {
    if (!g_shared || packetId != kTradePacketId) return;
    wchar_t payload[192]{};
    bool closed = false;
    __try {
        ReadManagedString(data, payload, _countof(payload));
        closed = IsClosePayload(data);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CopyWide(payload, _countof(payload), L"<string read fault>");
        closed = false;
    }

    BeginWrite();
    const LONG previousState = g_shared->observer.state;
    g_shared->observer.seenTradePacket = 1;
    ++g_shared->observer.tradePacketCount;
    g_shared->observer.lastPacketId = packetId;
    g_shared->observer.lastTradeTick = GetTickCount64();
    CopyWide(g_shared->observer.lastPayload, _countof(g_shared->observer.lastPayload), payload);
    if (closed) {
        if (previousState != static_cast<LONG>(TradeState::Active))
            ++g_shared->observer.closeWithoutActive;
        g_shared->observer.state = static_cast<LONG>(TradeState::Idle);
        ++g_shared->observer.finishSeq;
        CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
                 L"CMD_TRADE_DATA=200053 • data=-1 • TRADE_FINISHED");
    } else {
        g_shared->observer.state = static_cast<LONG>(TradeState::Active);
        CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
                 L"CMD_TRADE_DATA=200053 • phiên Trade đang tồn tại/cập nhật");
    }
    EndWrite();
}

using OnReceivePacketInstanceFn = void (__fastcall*)(void*, std::int32_t, Il2CppString*, const MethodInfo*);
using OnReceivePacketStaticFn = void (__fastcall*)(std::int32_t, Il2CppString*, const MethodInfo*);
OnReceivePacketInstanceFn g_originalInstance = nullptr;
OnReceivePacketStaticFn g_originalStatic = nullptr;
bool g_targetStatic = false;

void __fastcall HookedOnReceivePacketInstance(void* self, std::int32_t packetId,
                                              Il2CppString* data, const MethodInfo* method) {
    __try { ObserveTradePacket(packetId, data); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_originalInstance(self, packetId, data, method);
}

void __fastcall HookedOnReceivePacketStatic(std::int32_t packetId,
                                            Il2CppString* data, const MethodInfo* method) {
    __try { ObserveTradePacket(packetId, data); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_originalStatic(packetId, data, method);
}

bool InstallObserver() {
    if (InterlockedCompareExchange(&g_installed, 0, 0) != 0) return true;
    if (!EnsureShared()) return false;

    wchar_t error[320]{};
    if (!LoadApi(error, _countof(error))) {
        SetInstallFailure(error);
        return false;
    }

    Il2CppDomain* domain = g_api.domain_get();
    const Il2CppAssembly* assembly = domain ? g_api.domain_assembly_open(domain, "Assembly-CSharp") : nullptr;
    const Il2CppImage* image = assembly ? g_api.assembly_get_image(assembly) : nullptr;
    Il2CppClass* manager = image ? g_api.class_from_name(image, "FGStudio.LuaSystem", "LuaSystemManager") : nullptr;
    const MethodInfo* method = manager ? g_api.class_get_method_from_name(manager, "OnReceivePacket", 2) : nullptr;
    if (!method) {
        SetInstallFailure(L"Không resolve được FGStudio.LuaSystem.LuaSystemManager.OnReceivePacket(2)");
        return false;
    }

    if (g_api.method_get_param_count(method) != 2) {
        SetInstallFailure(L"OnReceivePacket không có đúng 2 tham số; fail-closed");
        return false;
    }

    char p0[128]{}, p1[128]{}, ret[128]{};
    GetTypeName(g_api.method_get_param(method, 0), p0, _countof(p0));
    GetTypeName(g_api.method_get_param(method, 1), p1, _countof(p1));
    GetTypeName(g_api.method_get_return_type(method), ret, _countof(ret));
    if (!(Eq(p0, "System.Int32") || Eq(p0, "System.UInt32")) ||
        !Eq(p1, "System.String") || !Eq(ret, "System.Void")) {
        wchar_t signature[300]{};
        wchar_t w0[128]{}, w1[128]{}, wr[128]{};
        CopyAnsiToWide(w0, _countof(w0), p0);
        CopyAnsiToWide(w1, _countof(w1), p1);
        CopyAnsiToWide(wr, _countof(wr), ret);
        _snwprintf_s(signature, _countof(signature), _TRUNCATE,
                     L"OnReceivePacket signature khác dự kiến: (%s, %s) -> %s", w0, w1, wr);
        SetInstallFailure(signature);
        return false;
    }

    std::uint32_t iflags = 0;
    const std::uint32_t flags = g_api.method_get_flags(method, &iflags);
    g_targetStatic = (flags & 0x0010u) != 0; // MethodAttributes.Static

    void* target = *reinterpret_cast<void* const*>(method);
    if (!target) {
        SetInstallFailure(L"OnReceivePacket MethodInfo không có native method pointer");
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.AllocationBase != g_api.module) {
        SetInstallFailure(L"OnReceivePacket native pointer không thuộc GameAssembly.dll");
        return false;
    }

    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        SetInstallFailure(L"MinHook initialize FAIL");
        return false;
    }

    MH_STATUS createStatus = MH_UNKNOWN;
    if (g_targetStatic) {
        createStatus = MH_CreateHook(target,
            reinterpret_cast<LPVOID>(&HookedOnReceivePacketStatic),
            reinterpret_cast<LPVOID*>(&g_originalStatic));
    } else {
        createStatus = MH_CreateHook(target,
            reinterpret_cast<LPVOID>(&HookedOnReceivePacketInstance),
            reinterpret_cast<LPVOID*>(&g_originalInstance));
    }
    if (createStatus != MH_OK) {
        SetInstallFailure(L"MinHook create OnReceivePacket hook FAIL");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        SetInstallFailure(L"MinHook enable OnReceivePacket hook FAIL");
        return false;
    }

    // Keep the target-process bridge resident for the life of the game. This avoids
    // leaving a detour pointing into an unloaded DLL if the test controller exits.
    HMODULE pinned = nullptr;
    (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(&InstallObserver), &pinned);

    wchar_t w0[128]{}, w1[128]{}, wr[128]{};
    CopyAnsiToWide(w0, _countof(w0), p0);
    CopyAnsiToWide(w1, _countof(w1), p1);
    CopyAnsiToWide(wr, _countof(wr), ret);
    const std::uint64_t rva = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(g_api.module));

    BeginWrite();
    g_shared->observer.installed = 1;
    g_shared->observer.installError = 0;
    g_shared->observer.methodRva = rva;
    _snwprintf_s(g_shared->observer.signature, _countof(g_shared->observer.signature), _TRUNCATE,
                 L"LuaSystemManager.OnReceivePacket(%s, %s) -> %s • %s",
                 w0, w1, wr, g_targetStatic ? L"static" : L"instance");
    CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
             L"V4 packet observer READY • không gọi Lua/UI • chỉ nghe OnReceivePacket");
    EndWrite();

    InterlockedExchange(&g_installed, 1);
    return true;
}

} // namespace

extern "C" __declspec(dllexport) LRESULT CALLBACK TltTradeObserverHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && lParam) {
        const MSG* msg = reinterpret_cast<const MSG*>(lParam);
        if (msg->message == kBootstrapMessage) (void)InstallObserver();
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
