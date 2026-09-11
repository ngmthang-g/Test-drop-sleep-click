#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include "protocol.h"

using namespace cleanroute;

namespace {

constexpr wchar_t kTitle[] = L"TEST TRADE STATE - STARTUP SAFE";
constexpr wchar_t kGameModule[] = L"GameAssembly.dll";
constexpr wchar_t kWindowClass[] = L"ThanLongTradeStateSafeWnd";
constexpr UINT_PTR kClientTimer = 1;
constexpr UINT kMsgLog = WM_APP + 20;
constexpr UINT kMsgStatus = WM_APP + 21;
constexpr UINT kMsgFinished = WM_APP + 22;
constexpr int IDC_CLIENTS = 200;
constexpr int IDC_START = 201;
constexpr int IDC_STATUS = 202;
constexpr int IDC_LOG = 203;
constexpr int kCloseConfirmSamples = 2;
constexpr DWORD kProbeIntervalMs = 40;
constexpr int kMaxClients = 64;

struct GameClient {
    DWORD pid = 0;
    DWORD threadId = 0;
    HWND hwnd = nullptr;
    wchar_t title[256]{};
};

bool GetExeDir(wchar_t* out, std::size_t cap) {
    if (!out || cap < 4) return false;
    const DWORD n = GetModuleFileNameW(nullptr, out, static_cast<DWORD>(cap));
    if (n == 0 || n >= cap) return false;
    for (std::size_t i = static_cast<std::size_t>(n); i > 0; --i) {
        if (out[i - 1] == L'\\' || out[i - 1] == L'/') {
            out[i - 1] = 0;
            return true;
        }
    }
    out[0] = L'.';
    out[1] = 0;
    return true;
}

bool BuildLocalPath(const wchar_t* fileName, wchar_t* out, std::size_t cap) {
    wchar_t dir[MAX_PATH * 4]{};
    if (!GetExeDir(dir, _countof(dir)) || !fileName || !out || cap == 0) return false;
    const int rc = _snwprintf_s(out, cap, _TRUNCATE, L"%s\\%s", dir, fileName);
    return rc >= 0;
}

void StartupLog(const char* text) {
    if (!text) return;
    wchar_t path[MAX_PATH * 4]{};
    if (!BuildLocalPath(L"trade_state_startup.log", path, _countof(path))) return;
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    const char crlf[] = "\r\n";
    WriteFile(f, crlf, 2, &written, nullptr);
    CloseHandle(f);
}

int StartupFail(const wchar_t* step, DWORD errorCode) {
    char line[160]{};
    _snprintf_s(line, _countof(line), _TRUNCATE, "FAIL error=%lu",
                static_cast<unsigned long>(errorCode));
    StartupLog(line);
    wchar_t msg[512]{};
    _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                 L"Không mở được TEST TRADE STATE.\r\n\r\nBước lỗi: %s\r\nGetLastError: %lu\r\n\r\n"
                 L"Xem file trade_state_startup.log cạnh EXE.",
                 step ? step : L"unknown", static_cast<unsigned long>(errorCode));
    MessageBoxW(nullptr, msg, L"Trade State startup error", MB_OK | MB_ICONERROR | MB_TOPMOST);
    return 1;
}

bool HasModule(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W e{};
    e.dwSize = sizeof(e);
    bool found = false;
    if (Module32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szModule, name) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &e));
    }
    CloseHandle(snap);
    return found;
}

struct ScanContext {
    GameClient* clients = nullptr;
    int count = 0;
    int capacity = 0;
};

BOOL CALLBACK EnumGameWindows(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<ScanContext*>(param);
    if (!ctx || ctx->count >= ctx->capacity) return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) <= 0) return TRUE;
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !tid || !HasModule(pid, kGameModule)) return TRUE;
    for (int i = 0; i < ctx->count; ++i) {
        if (ctx->clients[i].pid == pid) return TRUE;
    }
    GameClient& g = ctx->clients[ctx->count++];
    g.pid = pid;
    g.threadId = tid;
    g.hwnd = hwnd;
    GetWindowTextW(hwnd, g.title, _countof(g.title));
    return TRUE;
}

template <class T>
bool ResolveProc(HMODULE module, const char* name, T& out) {
    out = nullptr;
    FARPROC p = module ? GetProcAddress(module, name) : nullptr;
    if (!p) return false;
    static_assert(sizeof(p) == sizeof(out), "pointer size mismatch");
    std::memcpy(&out, &p, sizeof(out));
    return out != nullptr;
}

class BridgeClient {
public:
    ~BridgeClient() { Close(); }

    bool Attach(const GameClient& game, wchar_t* error, std::size_t cap) {
        Close();
        game_ = game;
        wchar_t mappingName[96]{};
        MappingName(game.pid, mappingName, _countof(mappingName));
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(SharedBlock), mappingName);
        if (!mapping_) return Fail(error, cap, L"không tạo shared memory");
        shared_ = reinterpret_cast<SharedBlock*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
        if (!shared_) {
            Fail(error, cap, L"không map shared memory");
            Close();
            return false;
        }
        ZeroMemory(shared_, sizeof(*shared_));
        shared_->magic = kMagic;
        shared_->protocolVersion = kProtocolVersion;
        shared_->targetPid = game.pid;
        shared_->targetWindowThreadId = game.threadId;

        wchar_t dllPath[MAX_PATH * 4]{};
        if (!BuildLocalPath(L"ThanLongTestSellBridge.dll", dllPath, _countof(dllPath))) {
            Fail(error, cap, L"không tạo được đường dẫn bridge DLL");
            Close();
            return false;
        }
        localDll_ = LoadLibraryW(dllPath);
        if (!localDll_) {
            if (error && cap) {
                _snwprintf_s(error, cap, _TRUNCATE, L"không load bridge DLL • GetLastError=%lu",
                             static_cast<unsigned long>(GetLastError()));
            }
            Close();
            return false;
        }
        HOOKPROC proc = nullptr;
        if (!ResolveProc(localDll_, "TlcGetMessageHook", proc)) {
            Fail(error, cap, L"Bridge thiếu TlcGetMessageHook");
            Close();
            return false;
        }
        hook_ = SetWindowsHookExW(WH_GETMESSAGE, proc, localDll_, game.threadId);
        if (!hook_) {
            if (error && cap) {
                _snwprintf_s(error, cap, _TRUNCATE, L"không hook được client • GetLastError=%lu",
                             static_cast<unsigned long>(GetLastError()));
            }
            Close();
            return false;
        }
        if (!PostThreadMessageW(game.threadId, kWakeMessage, 0, 0)) {
            Fail(error, cap, L"không đánh thức được game thread");
            Close();
            return false;
        }
        attached_ = true;
        return true;
    }

    void Close() {
        if (hook_) UnhookWindowsHookEx(hook_);
        if (localDll_) FreeLibrary(localDll_);
        if (shared_) UnmapViewOfFile(shared_);
        if (mapping_) CloseHandle(mapping_);
        hook_ = nullptr;
        localDll_ = nullptr;
        shared_ = nullptr;
        mapping_ = nullptr;
        attached_ = false;
    }

    bool Call(Command cmd, Response& out, wchar_t* error, std::size_t cap, DWORD timeoutMs = 1000) {
        if (!attached_ || !shared_) return Fail(error, cap, L"bridge chưa attach");
        if (shared_->bridgeBusy != 0) return Fail(error, cap, L"bridge busy");
        const LONG next = shared_->requestSeq + 1;
        shared_->request = {};
        shared_->request.command = static_cast<std::uint32_t>(cmd);
        MemoryBarrier();
        InterlockedExchange(&shared_->requestSeq, next);
        if (!PostThreadMessageW(game_.threadId, kWakeMessage, 0, 0))
            return Fail(error, cap, L"wake game thread FAIL");
        const ULONGLONG begin = GetTickCount64();
        while (GetTickCount64() - begin < timeoutMs) {
            if (shared_->completedSeq == next) {
                MemoryBarrier();
                out = shared_->response;
                if (!out.ok) {
                    if (error && cap) {
                        if (out.detail[0]) _snwprintf_s(error, cap, _TRUNCATE, L"%s", out.detail);
                        else _snwprintf_s(error, cap, _TRUNCATE, L"bridge trả lỗi");
                    }
                    return false;
                }
                return true;
            }
            if (!SwitchToThread()) YieldProcessor();
        }
        return Fail(error, cap, L"bridge timeout");
    }

private:
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

wchar_t* DupText(const wchar_t* text) {
    if (!text) text = L"";
    const std::size_t n = std::wcslen(text);
    auto* p = reinterpret_cast<wchar_t*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (n + 1) * sizeof(wchar_t)));
    if (!p) return nullptr;
    std::memcpy(p, text, (n + 1) * sizeof(wchar_t));
    return p;
}

class App {
public:
    int Run(HINSTANCE inst) {
        StartupLog("ENTRY");
        inst_ = inst;
        cancelEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!cancelEvent_) return StartupFail(L"CreateEvent", GetLastError());

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&wc)) {
            const DWORD e = GetLastError();
            if (e != ERROR_CLASS_ALREADY_EXISTS) return StartupFail(L"RegisterClassExW", e);
        }
        StartupLog("CLASS_OK");

        hwnd_ = CreateWindowExW(0, kWindowClass, kTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 570,
                                nullptr, nullptr, inst_, this);
        if (!hwnd_) return StartupFail(L"CreateWindowExW", GetLastError());
        StartupLog("WINDOW_OK");
        if (!BuildUi()) return StartupFail(L"BuildUi", GetLastError());
        StartupLog("UI_OK");

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        SetForegroundWindow(hwnd_);
        StartupLog("SHOWN");
        SetTimer(hwnd_, kClientTimer, 250, nullptr);

        MSG msg{};
        for (;;) {
            const BOOL rc = GetMessageW(&msg, nullptr, 0, 0);
            if (rc == 0) break;
            if (rc < 0) {
                StartupLog("GETMESSAGE_FAIL");
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        StopWorker(true);
        if (cancelEvent_) CloseHandle(cancelEvent_);
        cancelEvent_ = nullptr;
        StartupLog("EXIT");
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
    GameClient activeGame_{};
    HANDLE worker_ = nullptr;
    HANDLE cancelEvent_ = nullptr;
    volatile LONG running_ = 0;

    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            self = static_cast<App*>(cs->lpCreateParams);
            if (self) self->hwnd_ = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
    }

    bool BuildUi() {
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                      int x, int y, int w, int h, int id) -> HWND {
            HWND z = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
            if (z && font) SendMessageW(z, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return z;
        };
        if (!mk(L"STATIC", L"TEST TRADE STATE - đọc trạng thái nội bộ, không đọc ID item",
                SS_LEFT, 16, 14, 750, 22, 0)) return false;
        if (!mk(L"STATIC", L"Tín hiệu test: FindUI(\"Trade\") OPEN -> CLOSED.",
                SS_LEFT, 16, 38, 750, 22, 0)) return false;
        if (!mk(L"STATIC", L"CLIENT (tự quét GameAssembly.dll):",
                SS_LEFT, 16, 70, 330, 22, 0)) return false;
        clients_ = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                      16, 94, 750, 130, IDC_CLIENTS);
        start_ = mk(L"BUTTON", L"BẮT ĐẦU THEO DÕI TRADE", BS_DEFPUSHBUTTON,
                    16, 238, 300, 42, IDC_START);
        status_ = mk(L"STATIC", L"Sẵn sàng", SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
                     330, 238, 436, 42, IDC_STATUS);
        log_ = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                  16, 294, 750, 220, IDC_LOG);
        return clients_ && start_ && status_ && log_;
    }

    void AppendLog(const wchar_t* text) {
        if (!log_) return;
        SYSTEMTIME t{};
        GetLocalTime(&t);
        wchar_t line[1200]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE,
                     L"[%02u:%02u:%02u.%03u] %s\r\n",
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                     text ? text : L"");
        const int n = GetWindowTextLengthW(log_);
        SendMessageW(log_, EM_SETSEL, n, n);
        SendMessageW(log_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line));
    }

    void SetStatus(const wchar_t* text) {
        if (status_) SetWindowTextW(status_, text ? text : L"");
    }

    void PostOwned(UINT message, const wchar_t* text) {
        wchar_t* copy = DupText(text);
        if (!copy) return;
        if (!PostMessageW(hwnd_, message, 0, reinterpret_cast<LPARAM>(copy)))
            HeapFree(GetProcessHeap(), 0, copy);
    }
    void PostLog(const wchar_t* text) { PostOwned(kMsgLog, text); }
    void PostStatus(const wchar_t* text) { PostOwned(kMsgStatus, text); }
    void PostFinished(const wchar_t* text) { PostOwned(kMsgFinished, text); }

    void RefreshClients() {
        if (InterlockedCompareExchange(&running_, 0, 0) != 0) return;
        DWORD selectedPid = 0;
        const int oldSel = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (oldSel >= 0 && oldSel < gameCount_) selectedPid = games_[oldSel].pid;
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
        const int sel = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= gameCount_) return false;
        game = games_[sel];
        return true;
    }

    static DWORD WINAPI WorkerEntry(LPVOID param) {
        auto* self = static_cast<App*>(param);
        if (self) self->Monitor(self->activeGame_);
        return 0;
    }

    bool Cancelled(DWORD waitMs) const {
        return cancelEvent_ && WaitForSingleObject(cancelEvent_, waitMs) == WAIT_OBJECT_0;
    }

    void Monitor(GameClient game) {
        BridgeClient bridge;
        wchar_t error[512]{};
        if (!bridge.Attach(game, error, _countof(error))) {
            wchar_t line[800]{};
            _snwprintf_s(line, _countof(line), _TRUNCATE, L"ATTACH FAIL • %s", error);
            PostLog(line);
            InterlockedExchange(&running_, 0);
            PostFinished(L"FAIL • không attach được client");
            return;
        }

        wchar_t line[1000]{};
        _snwprintf_s(line, _countof(line), _TRUNCATE, L"ATTACH PASS • PID %lu",
                     static_cast<unsigned long>(game.pid));
        PostLog(line);
        PostLog(L"WAIT_TRADE • hãy mở giao dịch, sau đó hoàn tất hoặc hủy");
        PostLog(L"NOTE • chỉ chứng minh Trade OPEN -> CLOSED, không đọc item");
        PostStatus(L"WAIT_TRADE • chưa thấy Trade object");

        bool seenOpen = false;
        bool lastExists = false;
        bool haveLast = false;
        bool exchangeLogged = false;
        int closedSamples = 0;
        int failures = 0;
        ULONGLONG openedAt = 0;

        while (!Cancelled(0)) {
            Response r{};
            error[0] = 0;
            if (!bridge.Call(Command::ProbeTradeState, r, error, _countof(error), 1000)) {
                ++failures;
                if (failures == 1 || failures == 3) {
                    _snwprintf_s(line, _countof(line), _TRUNCATE,
                                 L"PROBE FAIL %d/5 • %s", failures, error);
                    PostLog(line);
                }
                if (failures >= 5) {
                    PostLog(L"FAIL • không đọc được semantic Trade state ổn định");
                    InterlockedExchange(&running_, 0);
                    PostFinished(L"FAIL • ProbeTradeState không hoạt động");
                    return;
                }
                if (Cancelled(100)) break;
                continue;
            }

            failures = 0;
            const bool exists = (r.resultCode & kTradeProbeUiExists) != 0 && r.value0 != 0;
            const bool activeKnown = (r.resultCode & kTradeProbeActiveKnown) != 0;
            const bool active = (r.resultCode & kTradeProbeActive) != 0;
            const bool exchangeKnown = (r.resultCode & kTradeProbeExchangeIdKnown) != 0;

            if (!haveLast || exists != lastExists) {
                if (exists) {
                    if (exchangeKnown) {
                        _snwprintf_s(line, _countof(line), _TRUNCATE,
                                     L"TRADE_OPEN • Active=%s • ExchangeID=%lld",
                                     activeKnown ? (active ? L"1" : L"0") : L"?",
                                     static_cast<long long>(r.value64_0));
                    } else {
                        _snwprintf_s(line, _countof(line), _TRUNCATE,
                                     L"TRADE_OPEN • Active=%s • ExchangeID=NOT_EXPOSED",
                                     activeKnown ? (active ? L"1" : L"0") : L"?");
                    }
                    PostLog(line);
                } else if (seenOpen) {
                    PostLog(L"TRADE_CLOSED_CANDIDATE • FindUI(\"Trade\") = null");
                }
            }

            if (exists) {
                if (!seenOpen) {
                    seenOpen = true;
                    openedAt = GetTickCount64();
                    PostStatus(L"IN_TRADE • Trade object đang tồn tại");
                }
                closedSamples = 0;
                if (!exchangeLogged) {
                    exchangeLogged = true;
                    PostLog(exchangeKnown
                        ? L"DIAGNOSTIC • ExchangeID đọc được, chỉ ghi log"
                        : L"DIAGNOSTIC • ExchangeID không expose, không ảnh hưởng test");
                }
            } else if (seenOpen) {
                ++closedSamples;
                if (closedSamples >= kCloseConfirmSamples) {
                    const ULONGLONG elapsed = openedAt ? GetTickCount64() - openedAt : 0;
                    _snwprintf_s(line, _countof(line), _TRUNCATE,
                                 L"PASS • TRADE_FINISHED • CLOSED %d mẫu • session=%llu ms",
                                 kCloseConfirmSamples, static_cast<unsigned long long>(elapsed));
                    PostLog(line);
                    PostLog(L"KẾT LUẬN • có thể dùng Trade OPEN -> CLOSED làm tín hiệu phiên trade đã xong");
                    InterlockedExchange(&running_, 0);
                    PostFinished(L"PASS • TRADE_FINISHED");
                    return;
                }
            }

            lastExists = exists;
            haveLast = true;
            if (Cancelled(kProbeIntervalMs)) break;
        }

        PostLog(L"STOP • đã dừng theo dõi");
        InterlockedExchange(&running_, 0);
        PostFinished(L"ĐÃ DỪNG • chưa kết luận");
    }

    void StartOrStop() {
        if (InterlockedCompareExchange(&running_, 0, 0) != 0) {
            if (cancelEvent_) SetEvent(cancelEvent_);
            SetStatus(L"Đang dừng theo dõi...");
            EnableWindow(start_, FALSE);
            return;
        }
        if (!SelectedGame(activeGame_)) {
            SetStatus(L"Không có client được chọn");
            return;
        }
        StopWorker(false);
        ResetEvent(cancelEvent_);
        InterlockedExchange(&running_, 1);
        SetWindowTextW(start_, L"DỪNG THEO DÕI");
        SetStatus(L"Đang attach bridge...");
        worker_ = CreateThread(nullptr, 0, WorkerEntry, this, 0, nullptr);
        if (!worker_) {
            InterlockedExchange(&running_, 0);
            SetStatus(L"Không tạo được worker thread");
            AppendLog(L"START FAIL • CreateThread");
        }
    }

    void StopWorker(bool closing) {
        if (cancelEvent_) SetEvent(cancelEvent_);
        if (worker_) {
            WaitForSingleObject(worker_, closing ? 1500 : INFINITE);
            CloseHandle(worker_);
            worker_ = nullptr;
        }
        InterlockedExchange(&running_, 0);
    }

    void FreePosted(LPARAM l) {
        if (l) HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(l));
    }

    LRESULT Handle(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
            case WM_COMMAND:
                if (LOWORD(w) == IDC_START) {
                    StartOrStop();
                    return 0;
                }
                break;
            case WM_TIMER:
                if (w == kClientTimer) {
                    KillTimer(hwnd_, kClientTimer);
                    RefreshClients();
                    if (IsWindow(hwnd_)) SetTimer(hwnd_, kClientTimer, 1000, nullptr);
                    return 0;
                }
                break;
            case kMsgLog:
                AppendLog(reinterpret_cast<const wchar_t*>(l));
                FreePosted(l);
                return 0;
            case kMsgStatus:
                SetStatus(reinterpret_cast<const wchar_t*>(l));
                FreePosted(l);
                return 0;
            case kMsgFinished:
                SetStatus(reinterpret_cast<const wchar_t*>(l));
                FreePosted(l);
                SetWindowTextW(start_, L"BẮT ĐẦU THEO DÕI TRADE");
                EnableWindow(start_, TRUE);
                RefreshClients();
                return 0;
            case WM_DESTROY:
                KillTimer(hwnd_, kClientTimer);
                if (cancelEvent_) SetEvent(cancelEvent_);
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd_, m, w, l);
    }
};

} // namespace

int APIENTRY wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
    StartupLog("WMAIN");
    try {
        App app;
        return app.Run(h);
    } catch (...) {
        StartupLog("UNHANDLED_CPP_EXCEPTION");
        MessageBoxW(nullptr,
                    L"Tool gặp exception ngay khi khởi động.\r\nXem trade_state_startup.log cạnh EXE.",
                    L"Trade State startup exception",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 2;
    }
}
