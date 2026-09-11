#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include "protocol.h"

using namespace cleanroute;

namespace {
constexpr wchar_t kTitle[] = L"TEST TRADE STATE V3 - GetScript / ExchangeID";
constexpr wchar_t kGameModule[] = L"GameAssembly.dll";
constexpr wchar_t kWindowClass[] = L"ThanLongTradeStateV3Wnd";
constexpr UINT_PTR kClientTimer = 1;
constexpr UINT kMsgLog = WM_APP + 20;
constexpr UINT kMsgStatus = WM_APP + 21;
constexpr UINT kMsgFinished = WM_APP + 22;
constexpr int IDC_CLIENTS = 200;
constexpr int IDC_START = 201;
constexpr int IDC_STATUS = 202;
constexpr int IDC_LOG = 203;
constexpr DWORD kProbeIntervalMs = 80;
constexpr int kCloseConfirmSamples = 2;
constexpr int kMaxClients = 64;

struct GameClient { DWORD pid=0; DWORD threadId=0; HWND hwnd=nullptr; wchar_t title[256]{}; };

bool GetExeDir(wchar_t* out, std::size_t cap) {
    if (!out || cap < 4) return false;
    const DWORD n=GetModuleFileNameW(nullptr,out,static_cast<DWORD>(cap));
    if (!n || n>=cap) return false;
    for (std::size_t i=n;i>0;--i) if(out[i-1]==L'\\'||out[i-1]==L'/'){out[i-1]=0;return true;}
    out[0]=L'.';out[1]=0;return true;
}

bool BuildLocalPath(const wchar_t* fileName,wchar_t* out,std::size_t cap){
    wchar_t dir[MAX_PATH*4]{}; if(!GetExeDir(dir,_countof(dir))||!fileName||!out||!cap)return false;
    return _snwprintf_s(out,cap,_TRUNCATE,L"%s\\%s",dir,fileName)>=0;
}

bool HasModule(DWORD pid,const wchar_t* name){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE)return false; MODULEENTRY32W e{};e.dwSize=sizeof(e);bool found=false;
    if(Module32FirstW(snap,&e))do{if(_wcsicmp(e.szModule,name)==0){found=true;break;}}while(Module32NextW(snap,&e));
    CloseHandle(snap);return found;
}

struct ScanContext { GameClient* clients=nullptr; int count=0; int capacity=0; };
BOOL CALLBACK EnumGameWindows(HWND hwnd,LPARAM param){
    auto* ctx=reinterpret_cast<ScanContext*>(param); if(!ctx||ctx->count>=ctx->capacity)return TRUE;
    if(!IsWindowVisible(hwnd)||GetWindowTextLengthW(hwnd)<=0)return TRUE;
    DWORD pid=0;DWORD tid=GetWindowThreadProcessId(hwnd,&pid);if(!pid||!tid||!HasModule(pid,kGameModule))return TRUE;
    for(int i=0;i<ctx->count;++i)if(ctx->clients[i].pid==pid)return TRUE;
    auto& g=ctx->clients[ctx->count++];g.pid=pid;g.threadId=tid;g.hwnd=hwnd;GetWindowTextW(hwnd,g.title,_countof(g.title));return TRUE;
}

template<class T> bool ResolveProc(HMODULE module,const char* name,T& out){
    out=nullptr;FARPROC p=module?GetProcAddress(module,name):nullptr;if(!p)return false;static_assert(sizeof(p)==sizeof(out),"pointer size");std::memcpy(&out,&p,sizeof(out));return out!=nullptr;
}

class BridgeClient {
public:
    ~BridgeClient(){Close();}
    bool Attach(const GameClient& game,wchar_t* error,std::size_t cap){
        Close();game_=game;wchar_t mappingName[96]{};MappingName(game.pid,mappingName,_countof(mappingName));
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedBlock),mappingName);
        if(!mapping_)return Fail(error,cap,L"không tạo shared memory");
        shared_=reinterpret_cast<SharedBlock*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedBlock)));
        if(!shared_){Fail(error,cap,L"không map shared memory");Close();return false;}
        ZeroMemory(shared_,sizeof(*shared_));shared_->magic=kMagic;shared_->protocolVersion=kProtocolVersion;shared_->targetPid=game.pid;shared_->targetWindowThreadId=game.threadId;
        wchar_t dllPath[MAX_PATH*4]{};if(!BuildLocalPath(L"ThanLongTestSellBridge.dll",dllPath,_countof(dllPath))){Fail(error,cap,L"không tạo đường dẫn DLL");Close();return false;}
        localDll_=LoadLibraryW(dllPath);if(!localDll_){_snwprintf_s(error,cap,_TRUNCATE,L"LoadLibrary bridge FAIL • %lu",static_cast<unsigned long>(GetLastError()));Close();return false;}
        HOOKPROC proc=nullptr;if(!ResolveProc(localDll_,"TlcGetMessageHook",proc)){Fail(error,cap,L"bridge thiếu TlcGetMessageHook");Close();return false;}
        hook_=SetWindowsHookExW(WH_GETMESSAGE,proc,localDll_,game.threadId);if(!hook_){_snwprintf_s(error,cap,_TRUNCATE,L"SetWindowsHookEx FAIL • %lu",static_cast<unsigned long>(GetLastError()));Close();return false;}
        if(!PostThreadMessageW(game.threadId,kWakeMessage,0,0)){Fail(error,cap,L"không wake game thread");Close();return false;}
        attached_=true;return true;
    }
    bool Call(Command cmd,Response& out,wchar_t* error,std::size_t cap,DWORD timeoutMs=1000){
        if(!attached_||!shared_)return Fail(error,cap,L"bridge chưa attach");
        if(shared_->bridgeBusy!=0)return Fail(error,cap,L"bridge busy");
        const LONG next=shared_->requestSeq+1;shared_->request={};shared_->request.command=static_cast<std::uint32_t>(cmd);MemoryBarrier();InterlockedExchange(&shared_->requestSeq,next);
        if(!PostThreadMessageW(game_.threadId,kWakeMessage,0,0))return Fail(error,cap,L"wake FAIL");
        const ULONGLONG begin=GetTickCount64();
        while(GetTickCount64()-begin<timeoutMs){
            if(shared_->completedSeq==next){MemoryBarrier();out=shared_->response;if(!out.ok){_snwprintf_s(error,cap,_TRUNCATE,L"%s",out.detail[0]?out.detail:L"bridge trả lỗi");return false;}return true;}
            if(!SwitchToThread())YieldProcessor();
        }
        return Fail(error,cap,L"bridge timeout");
    }
    void Close(){if(hook_)UnhookWindowsHookEx(hook_);if(localDll_)FreeLibrary(localDll_);if(shared_)UnmapViewOfFile(shared_);if(mapping_)CloseHandle(mapping_);hook_=nullptr;localDll_=nullptr;shared_=nullptr;mapping_=nullptr;attached_=false;}
private:
    static bool Fail(wchar_t* e,std::size_t cap,const wchar_t* t){if(e&&cap)_snwprintf_s(e,cap,_TRUNCATE,L"%s",t?t:L"error");return false;}
    GameClient game_{};HANDLE mapping_=nullptr;SharedBlock* shared_=nullptr;HMODULE localDll_=nullptr;HHOOK hook_=nullptr;bool attached_=false;
};

wchar_t* DupText(const wchar_t* text){if(!text)text=L"";const std::size_t n=std::wcslen(text);auto* p=reinterpret_cast<wchar_t*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(n+1)*sizeof(wchar_t)));if(p)std::memcpy(p,text,(n+1)*sizeof(wchar_t));return p;}

class App {
public:
    int Run(HINSTANCE inst){
        inst_=inst;cancelEvent_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!cancelEvent_)return 1;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=WndProc;wc.hInstance=inst_;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=kWindowClass;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
        hwnd_=CreateWindowExW(0,kWindowClass,kTitle,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,900,650,nullptr,nullptr,inst_,this);if(!hwnd_)return 1;
        if(!BuildUi())return 1;ShowWindow(hwnd_,SW_SHOW);UpdateWindow(hwnd_);SetTimer(hwnd_,kClientTimer,250,nullptr);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}StopWorker(true);if(cancelEvent_)CloseHandle(cancelEvent_);return static_cast<int>(msg.wParam);
    }
private:
    HINSTANCE inst_=nullptr;HWND hwnd_=nullptr,clients_=nullptr,start_=nullptr,status_=nullptr,log_=nullptr;GameClient games_[kMaxClients]{};int gameCount_=0;GameClient activeGame_{};HANDLE worker_=nullptr,cancelEvent_=nullptr;volatile LONG running_=0;
    static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){App* self=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);self=static_cast<App*>(cs->lpCreateParams);if(self)self->hwnd_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->Handle(m,w,l):DefWindowProcW(h,m,w,l);}
    bool BuildUi(){
        HFONT font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));auto mk=[&](const wchar_t* cls,const wchar_t* txt,DWORD style,int x,int y,int w,int h,int id){HWND z=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|style,x,y,w,h,hwnd_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst_,nullptr);if(z&&font)SendMessageW(z,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return z;};
        mk(L"STATIC",L"TRADE V3 • đọc LuaSystemManager.GetScript('Trade') + ExchangeID • không đọc item",SS_LEFT,14,12,850,22,0);
        mk(L"STATIC",L"Dòng trạng thái dưới đây sẽ hiện CHI TIẾT probe thật để không còn test mù.",SS_LEFT,14,36,850,22,0);
        mk(L"STATIC",L"CLIENT (GameAssembly.dll):",SS_LEFT,14,68,300,22,0);
        clients_=mk(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,14,92,850,140,IDC_CLIENTS);
        start_=mk(L"BUTTON",L"BẮT ĐẦU THEO DÕI V3",BS_DEFPUSHBUTTON,14,245,300,42,IDC_START);
        status_=mk(L"STATIC",L"CHƯA CHẠY",SS_LEFT|SS_SUNKEN,326,245,538,58,IDC_STATUS);
        log_=mk(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,14,318,850,270,IDC_LOG);
        return clients_&&start_&&status_&&log_;
    }
    void AppendLog(const wchar_t* text){SYSTEMTIME t{};GetLocalTime(&t);wchar_t line[1600]{};_snwprintf_s(line,_countof(line),_TRUNCATE,L"[%02u:%02u:%02u.%03u] %s\r\n",t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text?text:L"");int n=GetWindowTextLengthW(log_);SendMessageW(log_,EM_SETSEL,n,n);SendMessageW(log_,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(line));}
    void SetStatus(const wchar_t* text){SetWindowTextW(status_,text?text:L"");}
    void PostOwned(UINT msg,const wchar_t* text){wchar_t* p=DupText(text);if(!p)return;if(!PostMessageW(hwnd_,msg,0,reinterpret_cast<LPARAM>(p)))HeapFree(GetProcessHeap(),0,p);}
    void PostLog(const wchar_t* t){PostOwned(kMsgLog,t);}void PostStatus(const wchar_t* t){PostOwned(kMsgStatus,t);}void PostFinished(const wchar_t* t){PostOwned(kMsgFinished,t);}
    void RefreshClients(){if(InterlockedCompareExchange(&running_,0,0)!=0)return;DWORD selectedPid=0;int old=static_cast<int>(SendMessageW(clients_,LB_GETCURSEL,0,0));if(old>=0&&old<gameCount_)selectedPid=games_[old].pid;ScanContext ctx{games_,0,kMaxClients};EnumWindows(EnumGameWindows,reinterpret_cast<LPARAM>(&ctx));gameCount_=ctx.count;SendMessageW(clients_,LB_RESETCONTENT,0,0);int sel=-1;for(int i=0;i<gameCount_;++i){wchar_t row[520]{};_snwprintf_s(row,_countof(row),_TRUNCATE,L"PID %lu • %s",static_cast<unsigned long>(games_[i].pid),games_[i].title);SendMessageW(clients_,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row));if(games_[i].pid==selectedPid)sel=i;}if(sel<0&&gameCount_>0)sel=0;if(sel>=0)SendMessageW(clients_,LB_SETCURSEL,sel,0);}
    bool SelectedGame(GameClient& g){int sel=static_cast<int>(SendMessageW(clients_,LB_GETCURSEL,0,0));if(sel<0||sel>=gameCount_)return false;g=games_[sel];return true;}
    static DWORD WINAPI WorkerEntry(LPVOID p){auto* self=static_cast<App*>(p);if(self)self->Monitor(self->activeGame_);return 0;}
    bool Cancelled(DWORD ms)const{return cancelEvent_&&WaitForSingleObject(cancelEvent_,ms)==WAIT_OBJECT_0;}
    void Monitor(GameClient game){
        BridgeClient bridge;wchar_t error[512]{},line[1600]{};if(!bridge.Attach(game,error,_countof(error))){_snwprintf_s(line,_countof(line),_TRUNCATE,L"ATTACH FAIL • %s",error);PostLog(line);InterlockedExchange(&running_,0);PostFinished(L"FAIL • attach");return;}
        _snwprintf_s(line,_countof(line),_TRUNCATE,L"ATTACH PASS • PID %lu • V3 bridge",static_cast<unsigned long>(game.pid));PostLog(line);PostLog(L"WAIT • mở giao dịch rồi khóa/hoàn tất hoặc hủy; V3 sẽ log detail probe");
        bool seenOpen=false,lastExists=false,haveLast=false;int closedSamples=0,failures=0;ULONGLONG openedAt=0,lastDiagAt=0;wchar_t lastDetail[512]{};
        while(!Cancelled(0)){
            Response r{};error[0]=0;if(!bridge.Call(Command::ProbeTradeState,r,error,_countof(error),1000)){
                ++failures;_snwprintf_s(line,_countof(line),_TRUNCATE,L"PROBE FAIL %d/5 • %s",failures,error);if(failures==1||failures==3||failures>=5)PostLog(line);if(failures>=5){InterlockedExchange(&running_,0);PostFinished(L"FAIL • ProbeTradeState");return;}if(Cancelled(150))break;continue;
            }
            failures=0;const bool exists=(r.resultCode&kTradeProbeUiExists)!=0&&r.value0!=0;const bool exchangeKnown=(r.resultCode&kTradeProbeExchangeIdKnown)!=0;
            wchar_t status[1200]{};_snwprintf_s(status,_countof(status),_TRUNCATE,L"%s • %s",exists?L"IN_TRADE":L"WAIT",r.detail[0]?r.detail:L"no detail");PostStatus(status);
            const ULONGLONG now=GetTickCount64();if(_wcsicmp(lastDetail,r.detail)!=0||now-lastDiagAt>=2000){_snwprintf_s(line,_countof(line),_TRUNCATE,L"PROBE • exists=%d • code=0x%X • value0=%d • ExID=%lld • %s",exists?1:0,static_cast<unsigned>(r.resultCode),r.value0,static_cast<long long>(r.value64_0),r.detail);PostLog(line);_snwprintf_s(lastDetail,_countof(lastDetail),_TRUNCATE,L"%s",r.detail);lastDiagAt=now;}
            if(!haveLast||exists!=lastExists){if(exists){_snwprintf_s(line,_countof(line),_TRUNCATE,L"TRADE_OPEN • ExchangeID=%s%lld",exchangeKnown?L"":L"? ",static_cast<long long>(r.value64_0));PostLog(line);}else if(seenOpen)PostLog(L"TRADE_CLOSED_CANDIDATE");}
            if(exists){if(!seenOpen){seenOpen=true;openedAt=now;}closedSamples=0;}else if(seenOpen){++closedSamples;if(closedSamples>=kCloseConfirmSamples){_snwprintf_s(line,_countof(line),_TRUNCATE,L"PASS • TRADE_FINISHED • session=%llu ms",static_cast<unsigned long long>(openedAt?now-openedAt:0));PostLog(line);seenOpen=false;closedSamples=0;openedAt=0;}}
            lastExists=exists;haveLast=true;if(Cancelled(kProbeIntervalMs))break;
        }
        PostLog(L"STOP • đã dừng theo dõi");InterlockedExchange(&running_,0);PostFinished(L"ĐÃ DỪNG");
    }
    void StartOrStop(){if(InterlockedCompareExchange(&running_,0,0)!=0){SetEvent(cancelEvent_);SetStatus(L"Đang dừng...");EnableWindow(start_,FALSE);return;}if(!SelectedGame(activeGame_)){SetStatus(L"Không có client");return;}StopWorker(false);ResetEvent(cancelEvent_);InterlockedExchange(&running_,1);SetWindowTextW(start_,L"DỪNG THEO DÕI");SetStatus(L"Đang attach V3 bridge...");worker_=CreateThread(nullptr,0,WorkerEntry,this,0,nullptr);if(!worker_){InterlockedExchange(&running_,0);AppendLog(L"START FAIL • CreateThread");}}
    void StopWorker(bool closing){if(cancelEvent_)SetEvent(cancelEvent_);if(worker_){WaitForSingleObject(worker_,closing?1500:INFINITE);CloseHandle(worker_);worker_=nullptr;}InterlockedExchange(&running_,0);}
    void FreePosted(LPARAM l){if(l)HeapFree(GetProcessHeap(),0,reinterpret_cast<void*>(l));}
    LRESULT Handle(UINT m,WPARAM w,LPARAM l){switch(m){case WM_COMMAND:if(LOWORD(w)==IDC_START){StartOrStop();return 0;}break;case WM_TIMER:if(w==kClientTimer){KillTimer(hwnd_,kClientTimer);RefreshClients();if(IsWindow(hwnd_))SetTimer(hwnd_,kClientTimer,1000,nullptr);return 0;}break;case kMsgLog:AppendLog(reinterpret_cast<const wchar_t*>(l));FreePosted(l);return 0;case kMsgStatus:SetStatus(reinterpret_cast<const wchar_t*>(l));FreePosted(l);return 0;case kMsgFinished:SetStatus(reinterpret_cast<const wchar_t*>(l));FreePosted(l);SetWindowTextW(start_,L"BẮT ĐẦU THEO DÕI V3");EnableWindow(start_,TRUE);RefreshClients();return 0;case WM_DESTROY:KillTimer(hwnd_,kClientTimer);if(cancelEvent_)SetEvent(cancelEvent_);PostQuitMessage(0);return 0;default:break;}return DefWindowProcW(hwnd_,m,w,l);}
};
}

int APIENTRY wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int){App app;return app.Run(h);}
