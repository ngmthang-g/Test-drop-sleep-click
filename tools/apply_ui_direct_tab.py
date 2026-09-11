from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def load(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def save(rel, text):
    (ROOT / rel).write_text(text, encoding="utf-8", newline="\n")


def require_replace(text, old, new, label):
    if old not in text:
        raise SystemExit(f"anchor missing: {label}")
    return text.replace(old, new, 1)

# 1) Protocol: version bump + explicit TEST UI target vocabulary + two generic commands.
p = load("src/protocol.h")
p = require_replace(p, "constexpr std::uint32_t kProtocolVersion = 0x00010001u;",
                    "constexpr std::uint32_t kProtocolVersion = 0x00010002u;", "protocol version")
p = require_replace(p,
"    ProbeTradeState = 30,\n};\n\nenum class ActionResult",
"    ProbeTradeState = 30,\n    ProbeUiTestTarget = 31,\n    InvokeUiTestTarget = 32,\n};\n\nenum class UiTestTarget : std::int32_t {\n    CloseItemPopup = 1,\n    CloseBag = 2,\n    CloseTrade = 3,\n    TradeConfirm = 4,\n    TradeTabEquip = 5,\n    ItemPutOn = 6,\n    TradeLock = 7,\n    TradeUnlock = 8,\n    TradeSubmit = 9,\n    ItemDrop = 10,\n    ItemSell = 11,\n};\n\nenum class ActionResult", "protocol commands/targets")
save("src/protocol.h", p)

# 2) Enrich fingerprints: buttons/toggles also expose ClickHandler/PointerClickHandler when available.
b = load("src/bridge_chunks/bridge_11.txt")
old = '''    (void)ReadUiString(object, klass, "Name", output.labels.name);\n    if (kind != UiKind::Rect) (void)ReadUiString(object, klass, "Text", output.labels.text);\n    if (kind == UiKind::Rect)\n        (void)ReadUiString(object, klass, "PointerClickHandler", output.labels.handler);\n    return true;'''
new = '''    (void)ReadUiString(object, klass, "Name", output.labels.name);\n    (void)ReadUiString(object, klass, "Tag", output.tag);\n    if (kind != UiKind::Rect) {\n        (void)ReadUiString(object, klass, "Text", output.labels.text);\n        if (!ReadUiString(object, klass, "ClickHandler", output.labels.handler))\n            (void)ReadUiString(object, klass, "PointerClickHandler", output.labels.handler);\n    } else {\n        (void)ReadUiString(object, klass, "PointerClickHandler", output.labels.handler);\n    }\n    return true;'''
b = require_replace(b, old, new, "ReadBasicControl fingerprint")
save("src/bridge_chunks/bridge_11.txt", b)

# 3) Generic live selector/invoker. No coordinates; every invoke re-enumerates fresh controls.
b = load("src/bridge_chunks/bridge_13.txt")
anchor = "// Lightweight semantic Trade watcher. This deliberately does not inspect bag items,"
insert = r'''
const wchar_t* UiTestTargetName(UiTestTarget target) {
    switch (target) {
        case UiTestTarget::CloseItemPopup: return L"X popup item";
        case UiTestTarget::CloseBag: return L"X Tay nải";
        case UiTestTarget::CloseTrade: return L"X Giao dịch";
        case UiTestTarget::TradeConfirm: return L"Xác nhận giao dịch";
        case UiTestTarget::TradeTabEquip: return L"Tab Trang bị";
        case UiTestTarget::ItemPutOn: return L"Đặt lên";
        case UiTestTarget::TradeLock: return L"Khóa";
        case UiTestTarget::TradeUnlock: return L"Bỏ khóa";
        case UiTestTarget::TradeSubmit: return L"Giao dịch";
        case UiTestTarget::ItemDrop: return L"Vứt bỏ";
        case UiTestTarget::ItemSell: return L"BÁN";
        default: return L"UNKNOWN";
    }
}

bool KeyEqualsAny(const std::wstring& key, std::initializer_list<const wchar_t*> values) {
    for (const wchar_t* v : values) if (v && key == v) return true;
    return false;
}

int UiTestScore(UiTestTarget target, UiControl& c) {
    (void)ReadAncestors(c);
    CollectDescendantLabels(c);
    const std::wstring text = FoldKey(c.labels.text);
    const std::wstring name = FoldKey(c.labels.name);
    const std::wstring handler = FoldKey(c.labels.handler);
    const std::wstring tag = FoldKey(c.tag);
    const std::wstring parents = FoldKey(c.labels.ancestors);
    const std::wstring descendants = FoldKey(c.labels.descendants);
    const std::wstring visible = text + name + descendants;
    const std::wstring all = visible + handler + tag + parents;

    const bool tradeCtx = HasAny(parents + name, {L"trade", L"exchange", L"giaodich"});
    const bool bagCtx = HasAny(parents + name, {L"roleinfo", L"bag", L"package", L"inventory", L"tuido"});
    const bool itemCtx = HasAny(parents + name, {L"item", L"tips", L"tip", L"detail", L"popup", L"equipinfo", L"iteminfo"});
    const bool popupCtx = HasAny(parents + name, {L"tips", L"tip", L"detail", L"popup", L"dialog", L"message", L"notice"});
    const bool closeMarker = KeyEqualsAny(text, {L"x", L"close"}) ||
        HasAny(name + handler + descendants, {L"close", L"btnclose", L"buttonclose", L"closebutton", L"exit", L"quit"});

    switch (target) {
        case UiTestTarget::CloseItemPopup:
            if (!closeMarker) return 0;
            if (itemCtx && popupCtx) return 1200;
            if (itemCtx && !bagCtx && !tradeCtx) return 950;
            return 0;
        case UiTestTarget::CloseBag:
            return closeMarker && bagCtx && !tradeCtx ? 1150 : 0;
        case UiTestTarget::CloseTrade:
            return closeMarker && tradeCtx ? 1200 : 0;
        case UiTestTarget::TradeConfirm: {
            const bool label = KeyEqualsAny(text, {L"xacnhan", L"dongy", L"confirm"}) ||
                KeyEqualsAny(name, {L"xacnhan", L"confirm"}) ||
                HasAny(descendants, {L"xacnhan", L"dongy"});
            if (!label) return 0;
            if (tradeCtx || HasAny(all, {L"trade", L"exchange", L"giaodich", L"request"})) return 1200;
            return popupCtx ? 900 : 700;
        }
        case UiTestTarget::TradeTabEquip: {
            const bool label = KeyEqualsAny(text, {L"trangbi", L"equip"}) || KeyEqualsAny(name, {L"trangbi", L"equip"}) ||
                HasAny(descendants, {L"trangbi"});
            return label && tradeCtx ? 1200 : 0;
        }
        case UiTestTarget::ItemPutOn: {
            const bool label = KeyEqualsAny(text, {L"datlen", L"puton"}) || KeyEqualsAny(name, {L"datlen", L"puton"}) ||
                HasAny(descendants, {L"datlen"});
            return label && (itemCtx || bagCtx) ? 1150 : 0;
        }
        case UiTestTarget::TradeLock: {
            const bool unlock = KeyEqualsAny(text, {L"bokhoa", L"unlock"}) || HasAny(descendants, {L"bokhoa"});
            const bool label = KeyEqualsAny(text, {L"khoa", L"lock"}) || KeyEqualsAny(name, {L"khoa", L"lock"}) || HasAny(descendants, {L"khoa"});
            return label && !unlock && tradeCtx ? 1200 : 0;
        }
        case UiTestTarget::TradeUnlock: {
            const bool label = KeyEqualsAny(text, {L"bokhoa", L"unlock"}) || KeyEqualsAny(name, {L"bokhoa", L"unlock"}) || HasAny(descendants, {L"bokhoa"});
            return label && tradeCtx ? 1200 : 0;
        }
        case UiTestTarget::TradeSubmit: {
            const bool label = KeyEqualsAny(text, {L"giaodich", L"trade"}) || KeyEqualsAny(name, {L"giaodich", L"trade"}) || HasAny(descendants, {L"giaodich"});
            return label && tradeCtx ? 1200 : 0;
        }
        case UiTestTarget::ItemDrop: {
            const bool label = KeyEqualsAny(text, {L"vutbo", L"drop", L"discard"}) || KeyEqualsAny(name, {L"vutbo", L"drop", L"discard"}) || HasAny(descendants, {L"vutbo"});
            return label && (itemCtx || bagCtx) ? 1150 : 0;
        }
        case UiTestTarget::ItemSell:
            return SellActionScore(c);
        default:
            return 0;
    }
}

void AppendFingerprint(const UiControl& c, wchar_t* detail, std::size_t cap) {
    Append(detail, cap, L" • Name="); Append(detail, cap, c.labels.name.c_str());
    Append(detail, cap, L" • Text="); Append(detail, cap, c.labels.text.c_str());
    Append(detail, cap, L" • Handler="); Append(detail, cap, c.labels.handler.c_str());
    Append(detail, cap, L" • Ancestors="); Append(detail, cap, c.labels.ancestors.c_str());
}

bool FindUiTestTarget(UiTestTarget target, std::vector<UiControl>& controls, std::size_t& selectedIndex,
                      int& candidateCount, int& selectedScore, bool& found,
                      wchar_t* detail, std::size_t cap) {
    controls.clear(); selectedIndex = 0; candidateCount = 0; selectedScore = 0; found = false;
    if (target < UiTestTarget::CloseItemPopup || target > UiTestTarget::ItemSell) {
        SetText(detail, cap, L"TEST UI target không hợp lệ"); return false;
    }
    if (!EnumerateControls(controls, detail, cap)) return false;
    struct Candidate { std::size_t index; int score; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const int score = UiTestScore(target, controls[i]);
        if (score > 0) candidates.push_back({i, score});
    }
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return reinterpret_cast<std::uintptr_t>(controls[a.index].object) < reinterpret_cast<std::uintptr_t>(controls[b.index].object);
    });
    candidateCount = static_cast<int>(candidates.size());
    if (candidates.empty()) {
        SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • NOT FOUND"); return true;
    }
    if (candidates.size() > 1 && candidates[0].score == candidates[1].score &&
        controls[candidates[0].index].object != controls[candidates[1].index].object) {
        SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • AMBIGUOUS • candidates=");
        AppendInt(detail, cap, candidateCount); Append(detail, cap, L" • topScore="); AppendInt(detail, cap, candidates[0].score);
        return false;
    }
    selectedIndex = candidates[0].index;
    selectedScore = candidates[0].score;
    found = true;
    return true;
}

bool ProbeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {
    std::vector<UiControl> controls; std::size_t index = 0; int count = 0, score = 0; bool found = false;
    if (!FindUiTestTarget(target, controls, index, count, score, found, detail, cap)) return false;
    response.value0 = found ? 1 : 0; response.value1 = count; response.value64_0 = score;
    if (!found) return true;
    SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • FOUND • candidates="); AppendInt(detail, cap, count);
    Append(detail, cap, L" • score="); AppendInt(detail, cap, score); AppendFingerprint(controls[index], detail, cap);
    return true;
}

bool InvokeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {
    std::vector<UiControl> controls; std::size_t index = 0; int count = 0, score = 0; bool found = false;
    if (!FindUiTestTarget(target, controls, index, count, score, found, detail, cap)) return false;
    response.value0 = found ? 1 : 0; response.value1 = count; response.value64_0 = score;
    if (!found) return true;
    UiControl& selected = controls[index];
    std::wstring name = selected.labels.name, text = selected.labels.text, handler = selected.labels.handler, ancestors = selected.labels.ancestors;
    if (!InvokeControl(selected, detail, cap)) return false;
    response.resultCode = static_cast<std::int32_t>(ActionResult::ActionInvoked);
    SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • DIRECT INVOKED • candidates="); AppendInt(detail, cap, count);
    Append(detail, cap, L" • score="); AppendInt(detail, cap, score);
    Append(detail, cap, L" • Name="); Append(detail, cap, name.c_str());
    Append(detail, cap, L" • Text="); Append(detail, cap, text.c_str());
    Append(detail, cap, L" • Handler="); Append(detail, cap, handler.c_str());
    Append(detail, cap, L" • Ancestors="); Append(detail, cap, ancestors.c_str());
    return true;
}

'''
if anchor not in b:
    raise SystemExit("anchor missing: bridge target insertion")
b = b.replace(anchor, insert + anchor, 1)
old_switch = '''            case Command::ProbeTradeState:\n                ok=ProbeTradeState(r,detail,_countof(detail)); break;\n            default:'''
new_switch = '''            case Command::ProbeTradeState:\n                ok=ProbeTradeState(r,detail,_countof(detail)); break;\n            case Command::ProbeUiTestTarget:\n                ok=ProbeUiTestTarget(static_cast<UiTestTarget>(g_shared->request.arg0),r,detail,_countof(detail)); break;\n            case Command::InvokeUiTestTarget:\n                ok=InvokeUiTestTarget(static_cast<UiTestTarget>(g_shared->request.arg0),r,detail,_countof(detail)); break;\n            default:'''
b = require_replace(b, old_switch, new_switch, "bridge command switch")
save("src/bridge_chunks/bridge_13.txt", b)

# 4) Controller: real tab control; original sell flow is page 0, UI direct laboratory is page 1.
c = load("src/controller_chunks/controller_03.txt")
c = require_replace(c,
"CW_USEDEFAULT,CW_USEDEFAULT,790,600,nullptr,nullptr,inst,this);",
"CW_USEDEFAULT,CW_USEDEFAULT,1040,740,nullptr,nullptr,inst,this);", "window size")
c = require_replace(c,
"    HINSTANCE inst_=nullptr;HWND hwnd_=nullptr,clients_=nullptr,status_=nullptr,log_=nullptr;std::vector<GameClient> games_;ScanConfig cfg_{};EquipPointDb db_{};std::thread worker_;std::atomic<bool> running_{false},cancel_{false};std::mutex logMu_;",
"    HINSTANCE inst_=nullptr;HWND hwnd_=nullptr,clients_=nullptr,status_=nullptr,log_=nullptr,tab_=nullptr;std::vector<HWND> sellPageControls_,uiTestControls_;std::vector<GameClient> games_;ScanConfig cfg_{};EquipPointDb db_{};std::thread worker_;std::atomic<bool> running_{false},cancel_{false};std::mutex logMu_;\n    static constexpr int IDC_TAB=200;\n    static constexpr int IDC_UI_PROBE_BASE=300;\n    static constexpr int IDC_UI_DIRECT_BASE=400;\n    static constexpr int kUiTargetCount=11;", "controller fields")
pattern = re.compile(r"    void BuildUi\(\)\{.*?\}\n    void LoadConfig", re.S)
replacement = r'''    static const wchar_t* UiTargetLabel(UiTestTarget target){
        switch(target){
            case UiTestTarget::CloseItemPopup:return L"X popup item";
            case UiTestTarget::CloseBag:return L"X Tay nải";
            case UiTestTarget::CloseTrade:return L"X Giao dịch";
            case UiTestTarget::TradeConfirm:return L"Xác nhận giao dịch";
            case UiTestTarget::TradeTabEquip:return L"Tab Trang bị";
            case UiTestTarget::ItemPutOn:return L"Đặt lên";
            case UiTestTarget::TradeLock:return L"Khóa";
            case UiTestTarget::TradeUnlock:return L"Bỏ khóa";
            case UiTestTarget::TradeSubmit:return L"Giao dịch";
            case UiTestTarget::ItemDrop:return L"Vứt bỏ";
            case UiTestTarget::ItemSell:return L"BÁN";
            default:return L"UNKNOWN";
        }
    }
    void ShowPage(int page){
        const bool sell=page==0;
        for(HWND h:sellPageControls_)if(h)ShowWindow(h,sell?SW_SHOW:SW_HIDE);
        for(HWND h:uiTestControls_)if(h)ShowWindow(h,sell?SW_HIDE:SW_SHOW);
    }
    void BuildUi(){
        HFONT f=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto mk=[&](const wchar_t*cls,const wchar_t*txt,DWORD st,int x,int y,int w,int h,int id){HWND z=CreateWindowExW(0,cls,txt,WS_CHILD|WS_VISIBLE|st,x,y,w,h,hwnd_,(HMENU)(INT_PTR)id,inst_,nullptr);if(z)SendMessageW(z,WM_SETFONT,(WPARAM)f,TRUE);return z;};
        mk(L"STATIC",L"CLIENT (tự quét GameAssembly.dll):",SS_LEFT,16,14,300,22,0);
        clients_=mk(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,16,38,1000,126,IDC_CLIENTS);
        tab_=mk(WC_TABCONTROLW,L"",WS_CLIPSIBLINGS|WS_TABSTOP,16,174,1000,332,IDC_TAB);
        TCITEMW ti{};ti.mask=TCIF_TEXT;ti.pszText=const_cast<wchar_t*>(L"TEST BÁN ĐỒ");TabCtrl_InsertItem(tab_,0,&ti);ti.pszText=const_cast<wchar_t*>(L"TEST UI DIRECT");TabCtrl_InsertItem(tab_,1,&ti);
        sellPageControls_.push_back(mk(L"BUTTON",L"1. SCAN ẢNH BÁN",BS_AUTORADIOBUTTON|WS_GROUP,38,218,180,26,IDC_MODE_IMAGE));
        sellPageControls_.push_back(mk(L"BUTTON",L"2. DÒNG BÁN NỘI BỘ",BS_AUTORADIOBUTTON,238,218,220,26,IDC_MODE_INTERNAL));
        CheckRadioButton(hwnd_,IDC_MODE_IMAGE,IDC_MODE_INTERNAL,IDC_MODE_INTERNAL);
        sellPageControls_.push_back(mk(L"BUTTON",L"TEST BÁN ĐỒ",BS_DEFPUSHBUTTON,735,212,255,42,IDC_TEST_SELL));
        for(int i=0;i<kUiTargetCount;++i){
            const UiTestTarget target=static_cast<UiTestTarget>(i+1);const int y=207+i*26;
            uiTestControls_.push_back(mk(L"STATIC",UiTargetLabel(target),SS_LEFT|SS_CENTERIMAGE,38,y,250,23,0));
            uiTestControls_.push_back(mk(L"BUTTON",L"NHẬN DIỆN",BS_PUSHBUTTON,300,y,150,23,IDC_UI_PROBE_BASE+i));
            uiTestControls_.push_back(mk(L"BUTTON",L"TEST DIRECT",BS_PUSHBUTTON,462,y,150,23,IDC_UI_DIRECT_BASE+i));
        }
        status_=mk(L"STATIC",L"Sẵn sàng",SS_LEFT|SS_CENTERIMAGE|WS_BORDER,16,516,1000,34,IDC_STATUS);
        log_=mk(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,16,560,1000,132,IDC_LOG);
        TabCtrl_SetCurSel(tab_,0);ShowPage(0);
    }
    void RunUiDirectTest(UiTestTarget target,bool invoke){
        if(running_){Status(L"Đang chạy TEST BÁN ĐỒ; chờ hoàn tất trước khi TEST UI");return;}
        GameClient g{};if(!SelectedGame(g)){Status(L"Không có client được chọn");return;}
        BridgeClient b;std::wstring err;if(!b.Attach(g,err)){Log(std::wstring(invoke?L"TEST DIRECT ":L"NHẬN DIỆN ")+UiTargetLabel(target)+L" FAIL • "+err);Status(L"UI TEST FAIL");return;}
        Response r{};const Command cmd=invoke?Command::InvokeUiTestTarget:Command::ProbeUiTestTarget;
        if(!b.Call(cmd,static_cast<int>(target),0,0,r,err,1400)){Log(std::wstring(invoke?L"TEST DIRECT ":L"NHẬN DIỆN ")+UiTargetLabel(target)+L" FAIL • "+err);Status(L"UI TEST FAIL");return;}
        std::wstring state=r.value0?(invoke&&r.resultCode==static_cast<std::int32_t>(ActionResult::ActionInvoked)?L"DIRECT INVOKED":L"FOUND"):L"NOT FOUND";
        Log(std::wstring(invoke?L"TEST DIRECT ":L"NHẬN DIỆN ")+UiTargetLabel(target)+L" • "+state+L" • candidates="+std::to_wstring(r.value1)+L" • "+r.detail);
        Status(std::wstring(L"TEST UI • ")+UiTargetLabel(target)+L" • "+state);
    }
    void LoadConfig'''
c2, n = pattern.subn(replacement, c, count=1)
if n != 1:
    raise SystemExit("anchor missing: BuildUi block")
save("src/controller_chunks/controller_03.txt", c2)

c = load("src/controller_chunks/controller_04.txt")
pattern = re.compile(r"    LRESULT Handle\(UINT m,WPARAM w,LPARAM l\)\{switch\(m\)\{.*?return DefWindowProcW\(hwnd_,m,w,l\);\}\n\};", re.S)
replacement = r'''    LRESULT Handle(UINT m,WPARAM w,LPARAM l){
        switch(m){
            case WM_COMMAND:{
                const int id=LOWORD(w);
                if(id==IDC_TEST_SELL){Start();return 0;}
                if(id>=IDC_UI_PROBE_BASE&&id<IDC_UI_PROBE_BASE+kUiTargetCount){RunUiDirectTest(static_cast<UiTestTarget>(id-IDC_UI_PROBE_BASE+1),false);return 0;}
                if(id>=IDC_UI_DIRECT_BASE&&id<IDC_UI_DIRECT_BASE+kUiTargetCount){RunUiDirectTest(static_cast<UiTestTarget>(id-IDC_UI_DIRECT_BASE+1),true);return 0;}
                break;
            }
            case WM_NOTIFY:{
                const auto* hdr=reinterpret_cast<const NMHDR*>(l);
                if(hdr&&hdr->hwndFrom==tab_&&hdr->code==TCN_SELCHANGE){ShowPage(TabCtrl_GetCurSel(tab_));return 0;}
                break;
            }
            case WM_TIMER:if(w==kClientTimer){KillTimer(hwnd_,kClientTimer);RefreshClients();if(IsWindow(hwnd_))SetTimer(hwnd_,kClientTimer,1000,nullptr);return 0;}break;
            case WM_APP+10:{std::unique_ptr<std::wstring>s(reinterpret_cast<std::wstring*>(l));if(s)Status(*s);RefreshClients();return 0;}
            case WM_DESTROY:cancel_=true;KillTimer(hwnd_,kClientTimer);PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(hwnd_,m,w,l);
    }
};'''
c2, n = pattern.subn(replacement, c, count=1)
if n != 1:
    raise SystemExit("anchor missing: Handle block")
save("src/controller_chunks/controller_04.txt", c2)

print("TEST UI DIRECT patch applied")
