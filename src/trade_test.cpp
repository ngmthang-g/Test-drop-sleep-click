#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "protocol.h"

using namespace cleanroute;

namespace {

constexpr wchar_t kTitle[] = L"TEST TRADE STATE • INTERNAL";
constexpr wchar_t kGameModule[] = L"GameAssembly.dll";
constexpr UINT_PTR kClientTimer = 1;
constexpr int IDC_CLIENTS = 200;
constexpr int IDC_START = 201;
constexpr int IDC_STATUS = 202;
constexpr int IDC_LOG = 203;
constexpr int kCloseConfirmSamples = 2;
constexpr int kProbeIntervalMs = 40;

struct GameClient {
    DWORD pid = 0;
    DWORD threadId = 0;
    HWND hwnd = nullptr;
    std::wstring title;
};

std::wstring ExeDir() {
    wchar_t path[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, path, _countof(path));
    return std::filesystem::path(path).parent_path().wstring();
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

BOOL CALLBACK EnumGameWindows(HWND hwnd, LPARAM param) {
    if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) <= 0) return TRUE;
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !tid || !HasModule(pid, kGameModule)) return TRUE;
    auto* out = reinterpret_cast<std::vector<GameClient>*>(param);
    for (const auto& g : *out) if (g.pid == pid) return TRUE;
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, _countof(title));
    out->push_back({pid, tid, hwnd, title});
    return TRUE;
}

std::vector<GameClient> FindClients() {
    std::vector<GameClient> out;
    EnumWindows(EnumGameWindows, reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.pid < b.pid; });
    return out;
}

template <class T>
bool ResolveProc(HMODULE module, const char* name, T& out) {
    FARPROC p = GetProcAddress(module, name);
    if (!p) return false;
    static_assert(sizeof(p) == sizeof(out));
    std::memcpy(&out, &p, sizeof(out));
    return out != nullptr;
}

class BridgeClient {
public:
    ~BridgeClient() { Close(); }

    bool Attach(const GameClient& game, std::wstring& error) {
        Close();
        game_ = game;
        wchar_t mappingName[96]{};
        MappingName(game.pid, mappingName, _countof(mappingName));
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(SharedBlock), mappingName);
        if (!mapping_) {
            error = L"không tạo shared memory";
            return false;
        }
        shared_ = reinterpret_cast<SharedBlock*>(
            MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock)));
        if (!shared_) {
            error = L"không map shared memory";
            Close();
            return false;
        }
        ZeroMemory(shared_, sizeof(*shared_));
        shared_->magic = kMagic;
        shared_->protocolVersion = kProtocolVersion;
        shared_->targetPid = game.pid;
        shared_->targetWindowThreadId = game.threadId;

        const std::wstring dll = ExeDir() + L"\\ThanLongTestSellBridge.dll";
        localDll_ = LoadLibraryW(dll.c_str());
        if (!localDll_) {
            error = L"thiếu/không load được ThanLongTestSellBridge.dll";
            Close();
            return false;
        }
        HOOKPROC proc = nullptr;
        if (!ResolveProc(localDll_, "TlcGetMessageHook", proc)) {
            error = L"Bridge thiếu TlcGetMessageHook";
            Close();
            return false;
        }
        hook_ = SetWindowsHookExW(WH_GETMESSAGE, proc, localDll_, game.threadId);
        if (!hook_) {
            error = L"không hook được client; hãy chạy cùng quyền với game";
            Close();
            return false;
        }
        if (!PostThreadMessageW(game.threadId, kWakeMessage, 0, 0)) {
            error = L"không đánh thức được game thread";
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

    bool Call(Command cmd, Response& out, std::wstring& error, DWORD timeoutMs = 1000) {
        if (!attached_ || !shared_) {
            error = L"bridge chưa attach";
            return false;
        }
        if (shared_->bridgeBusy != 0) {
            error = L"bridge busy";
            return false;
        }
        const LONG next = shared_->requestSeq + 1;
        shared_->request = {};
        shared_->request.command = static_cast<std::uint32_t>(cmd);
        MemoryBarrier();
        InterlockedExchange(&shared_->requestSeq, next);
        if (!PostThreadMessageW(game_.threadId, kWakeMessage, 0, 0)) {
            error = L"wake game thread FAIL";
            return false;
        }
        const ULONGLONG begin = GetTickCount64();
        while (GetTickCount64() - begin < timeoutMs) {
            if (shared_->completedSeq == next) {
                MemoryBarrier();
                out = shared_->response;
                if (!out.ok) {
                    error = out.detail[0] ? out.detail : L"bridge trả lỗi";
                    return false;
                }
                return true;
            }
            if (!SwitchToThread()) YieldProcessor();
        }
        error = L"bridge timeout";
        return false;
    }

private:
    GameClient game_{};
    HANDLE mapping_ = nullptr;
    SharedBlock* shared_ = nullptr;
    HMODULE localDll_ = nullptr;
    HHOOK hook_ = nullptr;
    bool attached_ = false;
};

class App {
public:
    int Run(HINSTANCE inst) {
        inst_ = inst;
        INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&ic);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ThanLongTradeStateTestWnd";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, kTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 570,
                                nullptr, nullptr, inst, this);
        if (!hwnd_) return 1;
        BuildUi();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        SetTimer(hwnd_, kClientTimer, 250, nullptr);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        cancel_ = true;
        if (worker_.joinable()) worker_.join();
        return static_cast<int>(msg.wParam);
    }

private:
    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND clients_ = nullptr;
    HWND start_ = nullptr;
    HWND status_ = nullptr;
    HWND log_ = nullptr;
    std::vector<GameClient> games_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::mutex logMu_;

    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            self = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->Handle(m, w, l) : DefWindowProcW(h, m, w, l);
    }

    void BuildUi() {
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style,
                      int x, int y, int w, int h, int id) {
            HWND z = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
            if (z) SendMessageW(z, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
            return z;
        };

        mk(L"STATIC", L"KIỂM CHỨNG TRADE NỘI BỘ — không đọc item, không OCR, không scan ảnh",
           SS_LEFT, 16, 14, 750, 22, 0);
        mk(L"STATIC", L"Tín hiệu chính: LuaSystemAPI_GUI.FindUI(\"Trade\") tồn tại → OPEN; biến mất → CLOSED.",
           SS_LEFT, 16, 38, 750, 22, 0);
        mk(L"STATIC", L"CLIENT (tự quét GameAssembly.dll):", SS_LEFT, 16, 70, 330, 22, 0);
        clients_ = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                      16, 94, 750, 130, IDC_CLIENTS);
        start_ = mk(L"BUTTON", L"BẮT ĐẦU THEO DÕI TRADE", BS_DEFPUSHBUTTON,
                    16, 238, 300, 42, IDC_START);
        status_ = mk(L"STATIC", L"Sẵn sàng", SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
                     330, 238, 436, 42, IDC_STATUS);
        log_ = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                  16, 294, 750, 220, IDC_LOG);
    }

    void Log(const std::wstring& text) {
        std::lock_guard<std::mutex> lock(logMu_);
        if (!log_) return;
        SYSTEMTIME t{};
        GetLocalTime(&t);
        wchar_t head[32]{};
        swprintf_s(head, L"[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        const std::wstring line = std::wstring(head) + text + L"\r\n";
        const int n = GetWindowTextLengthW(log_);
        SendMessageW(log_, EM_SETSEL, n, n);
        SendMessageW(log_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    }

    void Status(const std::wstring& text) {
        if (status_) SetWindowTextW(status_, text.c_str());
    }

    void RefreshClients() {
        if (running_) return;
        DWORD selectedPid = 0;
        const int sel = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (sel >= 0 && sel < static_cast<int>(games_.size()))
            selectedPid = games_[static_cast<std::size_t>(sel)].pid;

        games_ = FindClients();
        SendMessageW(clients_, LB_RESETCONTENT, 0, 0);
        int select = -1;
        for (std::size_t i = 0; i < games_.size(); ++i) {
            const auto& g = games_[i];
            const std::wstring row = L"PID " + std::to_wstring(g.pid) + L" • " + g.title;
            SendMessageW(clients_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
            if (g.pid == selectedPid) select = static_cast<int>(i);
        }
        if (select < 0 && !games_.empty()) select = 0;
        if (select >= 0) SendMessageW(clients_, LB_SETCURSEL, select, 0);
    }

    bool SelectedGame(GameClient& game) {
        const int sel = static_cast<int>(SendMessageW(clients_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(games_.size())) return false;
        game = games_[static_cast<std::size_t>(sel)];
        return true;
    }

    void Finish(const std::wstring& text) {
        running_ = false;
        cancel_ = false;
        PostMessageW(hwnd_, WM_APP + 10, 0,
                     reinterpret_cast<LPARAM>(new std::wstring(text)));
    }

    void Monitor(GameClient game) {
        BridgeClient bridge;
        std::wstring error;
        if (!bridge.Attach(game, error)) {
            Log(L"ATTACH FAIL • " + error);
            Finish(L"FAIL • không attach được client");
            return;
        }

        Log(L"ATTACH PASS • PID " + std::to_wstring(game.pid));
        Log(L"WAIT_TRADE • hãy mở giao dịch, sau đó hoàn tất hoặc hủy như bình thường");
        Log(L"NOTE • CMD_TRADE_DATA=200053 đã biết ID, nhưng bản test này chưa giả định data=-1; đang chứng minh lifecycle bằng state runtime");
        Status(L"WAIT_TRADE • chưa thấy Trade object");

        bool seenOpen = false;
        bool lastExists = false;
        bool haveLast = false;
        bool exchangeDiagnosticLogged = false;
        int closedSamples = 0;
        int failures = 0;
        ULONGLONG openedAt = 0;

        while (!cancel_) {
            Response r{};
            error.clear();
            if (!bridge.Call(Command::ProbeTradeState, r, error, 1000)) {
                ++failures;
                if (failures == 1 || failures == 3)
                    Log(L"PROBE FAIL " + std::to_wstring(failures) + L"/5 • " + error);
                if (failures >= 5) {
                    Log(L"FAIL • không đọc được semantic Trade state ổn định; không fallback OCR/item/UI pixel");
                    Finish(L"FAIL • ProbeTradeState không hoạt động trên client này");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            failures = 0;

            const bool exists = (r.resultCode & kTradeProbeUiExists) != 0 && r.value0 != 0;
            const bool activeKnown = (r.resultCode & kTradeProbeActiveKnown) != 0;
            const bool active = (r.resultCode & kTradeProbeActive) != 0;
            const bool exchangeKnown = (r.resultCode & kTradeProbeExchangeIdKnown) != 0;

            if (!haveLast || exists != lastExists) {
                if (exists) {
                    std::wstring line = L"TRADE_OPEN • FindUI(\"Trade\") = object";
                    if (activeKnown) line += active ? L" • Active=1" : L" • Active=0";
                    else line += L" • Active=?";
                    if (exchangeKnown) line += L" • ExchangeID=" + std::to_wstring(r.value64_0);
                    else line += L" • ExchangeID=NOT_EXPOSED";
                    Log(line);
                } else if (seenOpen) {
                    Log(L"TRADE_CLOSED_CANDIDATE • FindUI(\"Trade\") = null");
                }
            }

            if (exists) {
                if (!seenOpen) {
                    seenOpen = true;
                    openedAt = GetTickCount64();
                    Status(L"IN_TRADE • Trade object đang tồn tại");
                }
                closedSamples = 0;
                if (!exchangeDiagnosticLogged) {
                    exchangeDiagnosticLogged = true;
                    Log(exchangeKnown
                        ? L"DIAGNOSTIC • ExchangeID đọc được; chỉ ghi log, không dùng làm điều kiện kết thúc"
                        : L"DIAGNOSTIC • ExchangeID không expose qua object này; không ảnh hưởng test lifecycle");
                }
            } else if (seenOpen) {
                ++closedSamples;
                if (closedSamples >= kCloseConfirmSamples) {
                    const ULONGLONG elapsed = openedAt ? GetTickCount64() - openedAt : 0;
                    Log(L"PASS • TRADE_FINISHED • Trade object đã biến mất " +
                        std::to_wstring(kCloseConfirmSamples) + L" mẫu liên tiếp • session observable=" +
                        std::to_wstring(elapsed) + L" ms");
                    Log(L"KẾT LUẬN TEST • có thể dùng OPEN→CLOSED của FindUI(\"Trade\") làm tín hiệu 'phiên trade đã xong', chưa kết luận thành công/thất bại");
                    Finish(L"PASS • TRADE_FINISHED đã được chứng minh runtime");
                    return;
                }
            }

            lastExists = exists;
            haveLast = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kProbeIntervalMs));
        }

        Log(L"STOP • người dùng dừng theo dõi trước khi có kết luận");
        Finish(L"ĐÃ DỪNG • chưa kết luận");
    }

    void StartOrStop() {
        if (running_) {
            cancel_ = true;
            Status(L"Đang dừng theo dõi...");
            EnableWindow(start_, FALSE);
            return;
        }

        GameClient game{};
        if (!SelectedGame(game)) {
            Status(L"Không có client được chọn");
            return;
        }
        if (worker_.joinable()) worker_.join();
        running_ = true;
        cancel_ = false;
        SetWindowTextW(start_, L"DỪNG THEO DÕI");
        EnableWindow(start_, TRUE);
        Status(L"Đang attach bridge...");
        worker_ = std::thread([this, game] { Monitor(game); });
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
            case WM_APP + 10: {
                std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(l));
                if (text) Status(*text);
                SetWindowTextW(start_, L"BẮT ĐẦU THEO DÕI TRADE");
                EnableWindow(start_, TRUE);
                RefreshClients();
                return 0;
            }
            case WM_DESTROY:
                cancel_ = true;
                KillTimer(hwnd_, kClientTimer);
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
    App app;
    return app.Run(h);
}
