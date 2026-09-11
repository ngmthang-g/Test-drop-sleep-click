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
    const Il2CppAssembly** (__cdecl* domain_get_assemblies)(Il2CppDomain*, std::size_t*) = nullptr;
    const Il2CppAssembly* (__cdecl* domain_assembly_open)(Il2CppDomain*, const char*) = nullptr;
    const Il2CppImage* (__cdecl* assembly_get_image)(const Il2CppAssembly*) = nullptr;
    const char* (__cdecl* image_get_name)(const Il2CppImage*) = nullptr;
    std::size_t (__cdecl* image_get_class_count)(const Il2CppImage*) = nullptr;
    Il2CppClass* (__cdecl* image_get_class)(const Il2CppImage*, std::size_t) = nullptr;
    Il2CppClass* (__cdecl* class_from_name)(const Il2CppImage*, const char*, const char*) = nullptr;
    const char* (__cdecl* class_get_name)(Il2CppClass*) = nullptr;
    const char* (__cdecl* class_get_namespace)(Il2CppClass*) = nullptr;
    Il2CppClass* (__cdecl* class_get_parent)(Il2CppClass*) = nullptr;
    const MethodInfo* (__cdecl* class_get_method_from_name)(Il2CppClass*, const char*, int) = nullptr;
    const MethodInfo* (__cdecl* class_get_methods)(Il2CppClass*, void**) = nullptr;
    const char* (__cdecl* method_get_name)(const MethodInfo*) = nullptr;
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

void AppendWide(wchar_t* dst, std::size_t cap, const wchar_t* text) {
    if (!dst || !text || cap == 0) return;
    const std::size_t n = std::wcslen(dst);
    if (n + 1 >= cap) return;
    _snwprintf_s(dst + n, cap - n, _TRUNCATE, L"%s", text);
}

void AppendAnsi(wchar_t* dst, std::size_t cap, const char* text) {
    wchar_t tmp[160]{};
    CopyAnsiToWide(tmp, _countof(tmp), text ? text : "");
    AppendWide(dst, cap, tmp);
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
        CopyWide(error, cap, L"GameAssembly.dll chưa sẵn sàng");
        return false;
    }
#define NEED(member, exportName) \
    do { if (!Resolve(g_api.module, exportName, g_api.member)) { \
        CopyWide(error, cap, L"V4.1 thiếu IL2CPP export: " L## #member); return false; } } while (0)
    NEED(domain_get, "il2cpp_domain_get");
    NEED(domain_get_assemblies, "il2cpp_domain_get_assemblies");
    NEED(domain_assembly_open, "il2cpp_domain_assembly_open");
    NEED(assembly_get_image, "il2cpp_assembly_get_image");
    NEED(image_get_name, "il2cpp_image_get_name");
    NEED(image_get_class_count, "il2cpp_image_get_class_count");
    NEED(image_get_class, "il2cpp_image_get_class");
    NEED(class_from_name, "il2cpp_class_from_name");
    NEED(class_get_name, "il2cpp_class_get_name");
    NEED(class_get_namespace, "il2cpp_class_get_namespace");
    NEED(class_get_parent, "il2cpp_class_get_parent");
    NEED(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    NEED(class_get_methods, "il2cpp_class_get_methods");
    NEED(method_get_name, "il2cpp_method_get_name");
    NEED(method_get_param_count, "il2cpp_method_get_param_count");
    NEED(method_get_param, "il2cpp_method_get_param");
    NEED(method_get_return_type, "il2cpp_method_get_return_type");
    NEED(method_get_flags, "il2cpp_method_get_flags");
    NEED(type_get_name, "il2cpp_type_get_name");
    NEED(string_length, "il2cpp_string_length");
    NEED(string_chars, "il2cpp_string_chars");
#undef NEED
    if (!Resolve(g_api.module, "il2cpp_free", g_api.free_fn)) {
        CopyWide(error, cap, L"V4.1 thiếu il2cpp_free");
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

bool IsPacketIdType(const char* type) {
    return Eq(type, "System.Int32") || Eq(type, "System.UInt32");
}

void DescribeMethod(const MethodInfo* method, wchar_t* out, std::size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!method) return;
    const char* name = g_api.method_get_name(method);
    AppendAnsi(out, cap, name ? name : "?");
    AppendWide(out, cap, L"(");
    const std::uint32_t count = g_api.method_get_param_count(method);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (i) AppendWide(out, cap, L", ");
        char type[96]{};
        GetTypeName(g_api.method_get_param(method, i), type, _countof(type));
        AppendAnsi(out, cap, type[0] ? type : "?");
    }
    AppendWide(out, cap, L") -> ");
    char ret[96]{};
    GetTypeName(g_api.method_get_return_type(method), ret, _countof(ret));
    AppendAnsi(out, cap, ret[0] ? ret : "?");
}

void ReadManagedString(Il2CppString* value, wchar_t* out, std::size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!value || !g_api.string_length || !g_api.string_chars) return;
    const std::int32_t len = g_api.string_length(value);
    const wchar_t* chars = g_api.string_chars(value);
    if (!chars || len <= 0) return;
    const std::size_t n = static_cast<std::size_t>(len) < cap - 1
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
                 L"V4.1 • CMD_TRADE_DATA=200053 • data=-1 • TRADE_FINISHED");
    } else {
        g_shared->observer.state = static_cast<LONG>(TradeState::Active);
        CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
                 L"V4.1 • CMD_TRADE_DATA=200053 • Trade ACTIVE/UPDATE");
    }
    EndWrite();
}

enum class ParamOrder { Normal, Reversed };
struct TargetInfo {
    const MethodInfo* method = nullptr;
    ParamOrder order = ParamOrder::Normal;
    bool isStatic = false;
    const Il2CppImage* image = nullptr;
    Il2CppClass* klass = nullptr;
    char imageName[128]{};
    char nameSpace[128]{};
    char className[128]{};
    char p0[96]{};
    char p1[96]{};
    char ret[96]{};
};

bool CheckSafeSignature(const MethodInfo* method, ParamOrder& order,
                        char* p0, std::size_t p0cap,
                        char* p1, std::size_t p1cap,
                        char* ret, std::size_t retcap) {
    if (!method || g_api.method_get_param_count(method) != 2) return false;
    GetTypeName(g_api.method_get_param(method, 0), p0, p0cap);
    GetTypeName(g_api.method_get_param(method, 1), p1, p1cap);
    GetTypeName(g_api.method_get_return_type(method), ret, retcap);
    if (!Eq(ret, "System.Void")) return false;
    if (IsPacketIdType(p0) && Eq(p1, "System.String")) {
        order = ParamOrder::Normal;
        return true;
    }
    if (Eq(p0, "System.String") && IsPacketIdType(p1)) {
        order = ParamOrder::Reversed;
        return true;
    }
    return false;
}

bool FillTarget(const Il2CppImage* image, Il2CppClass* klass, const MethodInfo* method,
                ParamOrder order, TargetInfo& target) {
    if (!image || !klass || !method) return false;
    target = {};
    target.method = method;
    target.order = order;
    target.image = image;
    target.klass = klass;
    const char* imageName = g_api.image_get_name(image);
    const char* ns = g_api.class_get_namespace(klass);
    const char* cn = g_api.class_get_name(klass);
    strncpy_s(target.imageName, imageName ? imageName : "?", _TRUNCATE);
    strncpy_s(target.nameSpace, ns ? ns : "", _TRUNCATE);
    strncpy_s(target.className, cn ? cn : "?", _TRUNCATE);
    GetTypeName(g_api.method_get_param(method, 0), target.p0, _countof(target.p0));
    GetTypeName(g_api.method_get_param(method, 1), target.p1, _countof(target.p1));
    GetTypeName(g_api.method_get_return_type(method), target.ret, _countof(target.ret));
    std::uint32_t iflags = 0;
    const std::uint32_t flags = g_api.method_get_flags(method, &iflags);
    target.isStatic = (flags & 0x0010u) != 0;
    return true;
}

bool SearchClassHierarchy(const Il2CppImage* image, Il2CppClass* start,
                          TargetInfo& target, wchar_t* diagnostic, std::size_t diagCap) {
    for (Il2CppClass* klass = start; klass; klass = g_api.class_get_parent(klass)) {
        void* iter = nullptr;
        while (const MethodInfo* method = g_api.class_get_methods(klass, &iter)) {
            const char* methodName = g_api.method_get_name(method);
            if (!Eq(methodName, "OnReceivePacket")) continue;
            if (diagnostic && diagnostic[0] == 0) DescribeMethod(method, diagnostic, diagCap);
            ParamOrder order = ParamOrder::Normal;
            char p0[96]{}, p1[96]{}, ret[96]{};
            if (CheckSafeSignature(method, order, p0, _countof(p0), p1, _countof(p1), ret, _countof(ret)))
                return FillTarget(image, klass, method, order, target);
        }
    }
    return false;
}

bool SearchImage(const Il2CppImage* image, TargetInfo& target,
                 wchar_t* diagnostic, std::size_t diagCap) {
    if (!image) return false;

    Il2CppClass* exact = g_api.class_from_name(image, "FGStudio.LuaSystem", "LuaSystemManager");
    if (exact && SearchClassHierarchy(image, exact, target, diagnostic, diagCap)) return true;

    const std::size_t count = g_api.image_get_class_count(image);
    for (std::size_t i = 0; i < count; ++i) {
        Il2CppClass* klass = g_api.image_get_class(image, i);
        if (!klass) continue;
        const char* className = g_api.class_get_name(klass);
        if (!Eq(className, "LuaSystemManager")) continue;
        if (SearchClassHierarchy(image, klass, target, diagnostic, diagCap)) return true;
        if (diagnostic && diagnostic[0] == 0) {
            CopyWide(diagnostic, diagCap, L"LuaSystemManager tìm thấy ở namespace=");
            AppendAnsi(diagnostic, diagCap, g_api.class_get_namespace(klass));
            AppendWide(diagnostic, diagCap, L" nhưng không có signature OnReceivePacket an toàn");
        }
    }
    return false;
}

bool ResolveTarget(TargetInfo& target, wchar_t* error, std::size_t cap) {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) {
        CopyWide(error, cap, L"V4.1 • il2cpp_domain_get trả NULL");
        return false;
    }

    wchar_t firstDiagnostic[320]{};
    for (const char* asmName : {"Assembly-CSharp", "Assembly-CSharp.dll"}) {
        const Il2CppAssembly* assembly = g_api.domain_assembly_open(domain, asmName);
        const Il2CppImage* image = assembly ? g_api.assembly_get_image(assembly) : nullptr;
        if (image && SearchImage(image, target, firstDiagnostic, _countof(firstDiagnostic))) return true;
    }

    std::size_t assemblyCount = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &assemblyCount);
    for (std::size_t i = 0; assemblies && i < assemblyCount; ++i) {
        const Il2CppImage* image = g_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        if (SearchImage(image, target, firstDiagnostic, _countof(firstDiagnostic))) return true;
    }

    CopyWide(error, cap, L"V4.1 resolver FAIL • ");
    if (firstDiagnostic[0]) {
        AppendWide(error, cap, firstDiagnostic);
    } else {
        AppendWide(error, cap, L"không tìm thấy LuaSystemManager/OnReceivePacket trong các assembly đã load");
    }
    return false;
}

using InstNormalFn = void (__fastcall*)(void*, std::int32_t, Il2CppString*, const MethodInfo*);
using StaticNormalFn = void (__fastcall*)(std::int32_t, Il2CppString*, const MethodInfo*);
using InstReversedFn = void (__fastcall*)(void*, Il2CppString*, std::int32_t, const MethodInfo*);
using StaticReversedFn = void (__fastcall*)(Il2CppString*, std::int32_t, const MethodInfo*);
InstNormalFn g_instNormal = nullptr;
StaticNormalFn g_staticNormal = nullptr;
InstReversedFn g_instReversed = nullptr;
StaticReversedFn g_staticReversed = nullptr;

void __fastcall HookInstNormal(void* self, std::int32_t packetId, Il2CppString* data, const MethodInfo* mi) {
    __try { ObserveTradePacket(packetId, data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_instNormal(self, packetId, data, mi);
}
void __fastcall HookStaticNormal(std::int32_t packetId, Il2CppString* data, const MethodInfo* mi) {
    __try { ObserveTradePacket(packetId, data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_staticNormal(packetId, data, mi);
}
void __fastcall HookInstReversed(void* self, Il2CppString* data, std::int32_t packetId, const MethodInfo* mi) {
    __try { ObserveTradePacket(packetId, data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_instReversed(self, data, packetId, mi);
}
void __fastcall HookStaticReversed(Il2CppString* data, std::int32_t packetId, const MethodInfo* mi) {
    __try { ObserveTradePacket(packetId, data); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_staticReversed(data, packetId, mi);
}

bool InstallObserver() {
    if (InterlockedCompareExchange(&g_installed, 0, 0) != 0) return true;
    if (!EnsureShared()) return false;

    wchar_t error[320]{};
    if (!LoadApi(error, _countof(error))) {
        SetInstallFailure(error);
        return false;
    }

    TargetInfo targetInfo{};
    if (!ResolveTarget(targetInfo, error, _countof(error))) {
        SetInstallFailure(error);
        return false;
    }

    void* target = *reinterpret_cast<void* const*>(targetInfo.method);
    if (!target) {
        SetInstallFailure(L"V4.1 • MethodInfo tìm thấy nhưng native pointer NULL");
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(target, &mbi, sizeof(mbi)) || mbi.AllocationBase != g_api.module) {
        SetInstallFailure(L"V4.1 • native pointer OnReceivePacket không thuộc GameAssembly.dll");
        return false;
    }

    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        SetInstallFailure(L"V4.1 • MinHook initialize FAIL");
        return false;
    }

    MH_STATUS createStatus = MH_UNKNOWN;
    if (targetInfo.order == ParamOrder::Normal) {
        if (targetInfo.isStatic) {
            createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookStaticNormal),
                                         reinterpret_cast<LPVOID*>(&g_staticNormal));
        } else {
            createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookInstNormal),
                                         reinterpret_cast<LPVOID*>(&g_instNormal));
        }
    } else {
        if (targetInfo.isStatic) {
            createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookStaticReversed),
                                         reinterpret_cast<LPVOID*>(&g_staticReversed));
        } else {
            createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookInstReversed),
                                         reinterpret_cast<LPVOID*>(&g_instReversed));
        }
    }
    if (createStatus != MH_OK) {
        SetInstallFailure(L"V4.1 • MinHook create OnReceivePacket hook FAIL");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        SetInstallFailure(L"V4.1 • MinHook enable OnReceivePacket hook FAIL");
        return false;
    }

    HMODULE pinned = nullptr;
    (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(&InstallObserver), &pinned);

    const std::uint64_t rva = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(target) - reinterpret_cast<std::uintptr_t>(g_api.module));
    wchar_t wImage[128]{}, wNs[128]{}, wClass[128]{}, w0[96]{}, w1[96]{}, wRet[96]{};
    CopyAnsiToWide(wImage, _countof(wImage), targetInfo.imageName);
    CopyAnsiToWide(wNs, _countof(wNs), targetInfo.nameSpace);
    CopyAnsiToWide(wClass, _countof(wClass), targetInfo.className);
    CopyAnsiToWide(w0, _countof(w0), targetInfo.p0);
    CopyAnsiToWide(w1, _countof(w1), targetInfo.p1);
    CopyAnsiToWide(wRet, _countof(wRet), targetInfo.ret);

    BeginWrite();
    g_shared->observer.installed = 1;
    g_shared->observer.installError = 0;
    g_shared->observer.methodRva = rva;
    _snwprintf_s(g_shared->observer.signature, _countof(g_shared->observer.signature), _TRUNCATE,
                 L"V4.1 %s • %s.%s.OnReceivePacket(%s,%s)->%s • %s",
                 wImage, wNs, wClass, w0, w1, wRet, targetInfo.isStatic ? L"static" : L"instance");
    CopyWide(g_shared->observer.detail, _countof(g_shared->observer.detail),
             L"V4.1 packet observer READY • chờ CMD_TRADE_DATA=200053");
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
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
