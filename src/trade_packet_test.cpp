#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include "trade_packet_protocol.h"

using namespace tradepacket;

namespace {

constexpr wchar_t kTitle[] = L"TEST TRADE STATE V4 - CMD_TRADE_DATA 200053";
constexpr wchar_t kWindowClass[] = L"ThanLongTradePacketV4Wnd";
constexpr wchar_t kGameModule[] = L"GameAssembly.dll";
constexpr int IDC_CLIENTS = 200;
constexpr int IDC_START = 201;
constexpr int IDC_STATUS = 202;
constexpr int IDC_LOG = 203;
constexpr UINT_PTR kTimer = 1;
constexpr int kMaxClients = 64;

struct GameClient {
    DWORD pid = 0;
    DWORD threadId = 0;
    HWND hwnd = nullptr;
    wchar_t title[256]{};
};

void AppendLine(HWND edit, const wchar_t* text) {
    if (!edit) return;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t line[1400]{};
    _snwprintf_s(line, _countof(line), _TRUNCATE,
                 L"[%02u:%02u:%02u.%03u] %s\r\n",
                 t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text ? text : L"");
    const int n = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, n, n);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line));
}

bool GetExeDir(wchar_t* out, std::size_t cap) {
    if (!out || cap < 4) return false;
    const DWORD n = GetModuleFileNameW(nullptr, out, static_cast<DWORD>(cap));
    if (!n || n >= cap) return false;
    for (std::size_t i = n; i > 0; --i) {
        if (out[i - 1] == L'\\' || out[i - 1] == L'/') {
            out[i - 1] = 0;
            return true;
        }
    }
    out[0] = L'.'; out[1] = 0;
    return true;
}

bool BuildLocalPath(const wchar_t* fileName, wchar_t* out, std::size_t cap) {
    wchar_t dir[MAX_PATH * 4]{};
    if (!GetExeDir(dir, _countof(dir)) || !fileName || !out || cap == 0) return false;
    return _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", dir, fileName) >= 0;
}

bool HasModule(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W e{}; e.dwSize = sizeof(e);
    bool found = false;
    if (Module32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szModule, name) == 0) { found = true; break; }
        } while (Module32NextW(snap, &e));
    }
    CloseHandle(snap);
    return found;
}

struct ScanContext { GameClient* clients; int count; int capacity; };
BOOL CALLBACK EnumGameWindows(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<ScanContext*>(param);
    if (!ctx || ctx->count >= ctx->capacity) return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) <= 0) return TRUE;
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !tid || !HasModule(pid, kGameModule)) return TRUE;
    for (int i = 0; i < ctx->count; ++i) if (ctx->clients[i].pid == pid) return TRUE;
    auto& g = ctx->clients[ctx->count++];
    g.pid = pid; g.threadId = tid; g.hwnd = hwnd;
    GetWindowTextW(hwnd, g.title, _countof(g.title));
    return TRUE;
}

template <typename T>
bool ResolveProc(HMODULE module, const char* name, T& out) {
    out = nullptr;
    FARPROC p = module ? GetProcAddress(module, name) : nullptr;
    if (!p) return false;
    static_assert(sizeof(p) == sizeof(out), "pointer size mismatch");
    std::memcpy(&out, &p, sizeof(out));
    return out != nullptr;
}

class ObserverClient {
public:
    ~ObserverClient() { Close(); }

    bool Attach(const GameClient& game, wchar_t* error, std::size_t cap) {
        Close();
        game_ = game;
        wchar_t mappingName[96]{};
        MappingName(game.pid, mappingName, _countof(mappingName));
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      0, sizeof(SharedBlock), mappingName);
        if (!mapping_) return Fail(error, cap, L"không tạo shared memory");
        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        shared_ = reinterpret_cast<SharedBlock*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
        if (!shared_) { Fail(error, cap, L"không map shared memory"); Close(); return false; }

        if (!existed) {
            ZeroMemory(shared_, sizeof(*shared_));
            shared_->magic = kMagic;
            shared_->version = kVersion;
            shared_->targetPid = game.pid;
            shared_->targetWindowThreadId = game.threadId;
        } else if (shared_->magic != kMagic || shared_->version != kVersion ||
                   shared_->targetPid != game.pid) {
            Fail(error, cap, L"đang có bridge V4 khác phiên bản trong game; restart game rồi test lại");
            Close();
            return false;
        }

        ObserverSnapshot snap{};
        if (ReadSnapshot(snap) && snap.installed) {
            attached_ = true;
            return true;
        }

        wchar_t dllPath[MAX_PATH * 4]{};
        if (!BuildLocalPath(L"ThanLongTradePacketBridge.dll", dllPath, _countof(dllPath))) {
            Fail(error, cap, L"không tạo đường dẫn bridge DLL"); Close(); return false;
        }
        localDll_ = LoadLibraryW(dllPath);
        if (!localDll_) {
            _snwprintf_s(error, cap, _TRUNCATE, L"LoadLibrary bridge FAIL • %lu",
                         static_cast<unsigned long>(GetLastError()));
            Close(); return false;
        }
        HOOKPROC proc = nullptr;
        if (!ResolveProc(localDll_, "TltTradeObserverHook", proc)) {
            Fail(error, cap, L"bridge thiếu TltTradeObserverHook"); Close(); return false;
        }
        hook_ = SetWindowsHookExW(WH_GETMESSAGE, proc, localDll_, game.threadId);
        if (!hook_) {
            _snwprintf_s(error, cap, _TRUNCATE, L"SetWindowsHookEx FAIL • %lu",
                         static_cast<unsigned long>(GetLastError()));
            Close(); return false;
        }
        if (!PostThreadMessageW(game.threadId, kBootstrapMessage, 0, 0)) {
            Fail(error, cap, L"không wake game thread để cài observer"); Close(); return false;
        }

        const ULONGLONG begin = GetTickCount64();
        while (GetTickCount64() - begin < 3000) {
            if (ReadSnapshot(snap)) {
                if (snap.installError) {
                    _snwprintf_s(error, cap, _TRUNCATE, L"%s",
                                 snap.detail[0] ? snap.detail : L"observer install FAIL");
                    CloseBootstrapOnly();
                    return false;
                }
                if (snap.installed) {
                    attached_ = true;
                    CloseBootstrapOnly();
                    return true;
                }
            }
            Sleep(10);
        }
        Fail(error, cap, L"timeout khi cài packet observer");
        CloseBootstrapOnly();
        return false;
    }

    bool ReadSnapshot(ObserverSnapshot& out) const {
        if (!shared_) return false;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const LONG before = shared_->observer.seq;
            if (before & 1) { YieldProcessor(); continue; }
            MemoryBarrier();
            std::memcpy(&out, const_cast<const ObserverSnapshot*>(&shared_->observer), sizeof(out));
            MemoryBarrier();
            const LONG after = shared_->observer.seq;
            if (before == after && !(after & 1)) return true;
        }
        return false;
    }

    void Close() {
        CloseBootstrapOnly();
        if (shared_) UnmapViewOfFile(shared_);
        if (mapping_) CloseHandle(mapping_);
        shared_ = nullptr;
        mapping_ = nullptr;
        attached_ = false;
    }

    bool Attached() const { return attached_; }

private:
    void CloseBootstrapOnly() {
        if (hook_) { UnhookWindowsHookEx(hook_); hook_ = nullptr; }
        if (localDll_) { FreeLibrary(localDll_); localDll_ = nullptr; }
    }
    static bool Fail(wchar_t* error, std::size_t cap, const wchar_t* text) {
        if (error && cap) _snwprintf_s(error, cap, _TRUNCATE, L"%s", text ? text : L"error");
        return false;
    }

    GameClient game_{};
    HANDLE mapping_ = nullptr;
    SharedBlock* shared_ = nullptr;
    HMODULE localDll_ = nullptr;
    HHOOK hook_ = nullptr;
    bool attached_ = false;
};

class App {
public:
    int Run(HINSTANCE instance) {
        inst_ = instance;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;
        hwnd_ = CreateWindowExW(0, kWindowClass, kTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 930, 670,
                                nullptr, nullptr, inst_, this);
        if (!hwnd_) return 1;
        if (!BuildUi()) return 1;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        SetTimer(hwnd_, kTimer, 100, nullptr);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        observer_.Close();
        return static_cast<int>(msg.wParam);
    }

private:
    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND clients_ = nullptr;
    HWND start_ = nullptr;
    HWND status_ = nullptr;
    HWND log_ = nullptr;
    GameClient games_[kMaxClients]{};
    int gameCount_ = 0;
    ObserverClient observer_{};
    bool monitoring_ = false;
    DWORD lastScanTick_ = 0;
    LONG lastState_ = static_cast<LONG>(TradeState::Unknown);
    LONG lastFinishSeq_ = 0;
    LONG lastTradePacketCount_ = 0;
    wchar_t lastDetail_[320]{};

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<App*>(cs->lpCreateParams);
            if (self) self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->Handle(msg, wParam, lParam)
                    : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool BuildUi() {
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                      int x, int y, int w, int h, int id) {
            HWND z = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
            if (z && font) SendMessageW(z, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return z;
        };
        mk(L"STATIC", L"V4: nghe trực tiếp LuaSystemManager.OnReceivePacket • chỉ lọc CMD_TRADE_DATA=200053",
           SS_LEFT, 14, 12, 890, 22, 0);
        mk(L"STATIC", L"Không gọi Lua/UI, không quét item, không OCR, không poll trạng thái game. data == -1 => TRADE_FINISHED.",
           SS_LEFT, 14, 36, 890, 22, 0);
        mk(L"STATIC", L"CLIENT (GameAssembly.dll):", SS_LEFT, 14, 70, 300, 22, 0);
        clients_ = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                      14, 94, 890, 145, IDC_CLIENTS);
        start_ = mk(L"BUTTON", L"BẮT ĐẦU THEO DÕI V4", BS_DEFPUSHBUTTON,
                    14, 252, 305, 42, IDC_START);
        status_ = mk(L"STATIC", L"CHƯA CHẠY", SS_LEFT | SS_SUNKEN,
                     332, 252, 572, 62, IDC_STATUS);
        log_ = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                  ES_AUTOVSCROLL | ES_READONLY,
                  14, 326, 890, 290, IDC_LOG);
        return clients_ && start_ && status_ && log_;
    }

    void RefreshClients() {
        DWORD selectedPid = 0;
        const int old = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (old >= 0 && old < gameCount_) selectedPid = games_[old].pid;
        ScanContext ctx{games_, 0, kMaxClients};
        EnumWindows(EnumGameWindows, reinterpret_cast<LPARAM>(&ctx));
        gameCount_ = ctx.count;
        SendMessageW(clients_, LB_RESETCONTENT, 0, 0);
        int select = -1;
        for (int i = 0; i < gameCount_; ++i) {
            wchar_t row[520]{};
            _snwprintf_s(row, _countof(row), _TRUNCATE, L"PID %lu • %s",
                         static_cast<unsigned long>(games_[i].pid), games_[i].title);
            SendMessageW(clients_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row));
            if (games_[i].pid == selectedPid) select = i;
        }
        if (select < 0 && gameCount_ > 0) select = 0;
        if (select >= 0) SendMessageW(clients_, LB_SETCURSEL, select, 0);
    }

    bool SelectedGame(GameClient& game) {
        const int index = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (index < 0 || index >= gameCount_) return false;
        game = games_[index];
        return true;
    }

    void StartMonitor() {
        if (monitoring_) return;
        GameClient game{};
        if (!SelectedGame(game)) {
            MessageBoxW(hwnd_, L"Chưa chọn client game.", kTitle, MB_ICONWARNING);
            return;
        }
        wchar_t error[512]{};
        if (!observer_.Attach(game, error, _countof(error))) {
            wchar_t line[700]{};
            _snwprintf_s(line, _countof(line), _TRUNCATE, L"ATTACH FAIL • %s", error);
            AppendLine(log_, line);
            SetWindowTextW(status_, L"FAIL • không cài được packet observer");
            return;
        }
        ObserverSnapshot snap{};
        observer_.ReadSnapshot(snap);
        lastState_ = snap.state;
        lastFinishSeq_ = snap.finishSeq;
        lastTradePacketCount_ = snap.tradePacketCount;
        _snwprintf_s(lastDetail_, _countof(lastDetail_), _TRUNCATE, L"%s", snap.detail);
        monitoring_ = true;
        EnableWindow(clients_, FALSE);
        EnableWindow(start_, FALSE);

        wchar_t line[900]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
                     L"OBSERVER READY • PID %lu • RVA 0x%llX",
                     static_cast<unsigned long>(game.pid),
                     static_cast<unsigned long long>(snap.methodRva));
        AppendLine(log_, line);
        if (snap.signature[0]) AppendLine(log_, snap.signature);
        AppendLine(log_, L"WAIT • thực hiện giao dịch bình thường. V4 chỉ báo khi nhận 200053 và data=-1.");
    }

    void PollMonitor() {
        if (!monitoring_) return;
        ObserverSnapshot snap{};
        if (!observer_.ReadSnapshot(snap)) return;

        if (_wcsicmp(lastDetail_, snap.detail) != 0) {
            _snwprintf_s(lastDetail_, _countof(lastDetail_), _TRUNCATE, L"%s", snap.detail);
            if (snap.detail[0]) AppendLine(log_, snap.detail);
        }

        if (snap.tradePacketCount != lastTradePacketCount_) {
            lastTradePacketCount_ = snap.tradePacketCount;
            if (snap.state == static_cast<LONG>(TradeState::Active) &&
                lastState_ != static_cast<LONG>(TradeState::Active)) {
                wchar_t line[700]{};
                _snwprintf_s(line, _countof(line), _TRUNCATE,
                             L"TRADE_ACTIVE • CMD_TRADE_DATA=200053 • data=%s",
                             snap.lastPayload[0] ? snap.lastPayload : L"<empty>");
                AppendLine(log_, line);
            }
        }

        if (snap.finishSeq != lastFinishSeq_) {
            lastFinishSeq_ = snap.finishSeq;
            AppendLine(log_, L"PASS • TRADE_FINISHED • CMD_TRADE_DATA=200053 • data=-1 • không phân biệt thành công/hủy");
        }
        lastState_ = snap.state;

        const wchar_t* stateText = L"WAIT/UNKNOWN";
        if (snap.state == static_cast<LONG>(TradeState::Active)) stateText = L"TRADE ACTIVE";
        else if (snap.state == static_cast<LONG>(TradeState::Idle) && snap.seenTradePacket) stateText = L"TRADE CLOSED/IDLE";
        wchar_t status[900]{};
        _snwprintf_s(status, _countof(status), _TRUNCATE,
                     L"%s • trade packets=%ld • finish=%ld • last=%s",
                     stateText, snap.tradePacketCount, snap.finishSeq,
                     snap.lastPayload[0] ? snap.lastPayload : L"-");
        SetWindowTextW(status_, status);
    }

    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_START && HIWORD(wParam) == BN_CLICKED) {
                StartMonitor();
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kTimer) {
                const DWORD now = GetTickCount();
                if (!monitoring_ && now - lastScanTick_ >= 500) {
                    lastScanTick_ = now;
                    RefreshClients();
                }
                PollMonitor();
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd_, kTimer);
            observer_.Close();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
};

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    App app;
    return app.Run(instance);
}
