from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def replace_between(path: str, start: str, end: str, new_block: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{path}: start marker not found: {start}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"{path}: end marker not found: {end}")
    p.write_text(text[:a] + new_block.rstrip() + "\n\n" + text[b:], encoding="utf-8")


# ---- protocol: extend the existing TEST UI DIRECT target table only ----
replace_once(
    "src/protocol.h",
    "constexpr std::uint32_t kProtocolVersion = 0x00010002u;",
    "constexpr std::uint32_t kProtocolVersion = 0x00010003u;",
)
replace_once(
    "src/protocol.h",
    "    ItemDrop = 10,\n    ItemSell = 11,\n};",
    "    ItemDrop = 10,\n    ItemSell = 11,\n    OpenBag = 12,\n    SwitchToSkills = 13,\n    SwitchToBagUi = 14,\n};",
)
replace_once(
    "src/protocol.h",
    "enum class ActionResult : std::int32_t {\n    None = 0,\n    ActionInvoked = 1,\n};",
    "enum class ActionResult : std::int32_t {\n    None = 0,\n    ActionInvoked = 1,\n    AlreadyInState = 2,\n};",
)

# ---- controller: append 3 rows to the proven startup-fixed UI ----
replace_once(
    "src/controller_chunks/controller_03.txt",
    "    static constexpr int kUiTargetCount=11;",
    "    static constexpr int kUiTargetCount=14;",
)
replace_once(
    "src/controller_chunks/controller_03.txt",
    "            case UiTestTarget::ItemDrop:return L\"Vứt bỏ\";\n            case UiTestTarget::ItemSell:return L\"BÁN\";\n            default:return L\"UNKNOWN\";",
    "            case UiTestTarget::ItemDrop:return L\"Vứt bỏ\";\n            case UiTestTarget::ItemSell:return L\"BÁN\";\n            case UiTestTarget::OpenBag:return L\"MỞ TAY NẢI\";\n            case UiTestTarget::SwitchToSkills:return L\"CHUYỂN → SKILL\";\n            case UiTestTarget::SwitchToBagUi:return L\"CHUYỂN → TAY NẢI\";\n            default:return L\"UNKNOWN\";",
)
# 14 rows need more vertical room, but keep title/startup lifecycle unchanged.
replace_once(
    "src/controller_chunks/controller_03.txt",
    "CW_USEDEFAULT,CW_USEDEFAULT,1040,740,nullptr,nullptr,inst,this);",
    "CW_USEDEFAULT,CW_USEDEFAULT,1040,860,nullptr,nullptr,inst,this);",
)
replace_once(
    "src/controller_chunks/controller_03.txt",
    "tab_=mk(WC_TABCONTROLW,L\"\",WS_CLIPSIBLINGS|WS_TABSTOP,16,174,1000,332,IDC_TAB);",
    "tab_=mk(WC_TABCONTROLW,L\"\",WS_CLIPSIBLINGS|WS_TABSTOP,16,174,1000,420,IDC_TAB);",
)
replace_once(
    "src/controller_chunks/controller_03.txt",
    "status_=mk(L\"STATIC\",L\"Sẵn sàng\",SS_LEFT|SS_CENTERIMAGE|WS_BORDER,16,516,1000,34,IDC_STATUS);\n        log_=mk(L\"EDIT\",L\"\",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,16,560,1000,132,IDC_LOG);",
    "status_=mk(L\"STATIC\",L\"Sẵn sàng\",SS_LEFT|SS_CENTERIMAGE|WS_BORDER,16,606,1000,34,IDC_STATUS);\n        log_=mk(L\"EDIT\",L\"\",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,16,650,1000,150,IDC_LOG);",
)
replace_once(
    "src/controller_chunks/controller_03.txt",
    "        std::wstring state=r.value0?(invoke&&r.resultCode==static_cast<std::int32_t>(ActionResult::ActionInvoked)?L\"DIRECT INVOKED\":L\"FOUND\"):L\"NOT FOUND\";",
    "        std::wstring state;\n        if(!r.value0)state=L\"NOT FOUND\";\n        else if(r.resultCode==static_cast<std::int32_t>(ActionResult::AlreadyInState))state=L\"ALREADY IN STATE\";\n        else if(invoke&&r.resultCode==static_cast<std::int32_t>(ActionResult::ActionInvoked))state=L\"DIRECT INVOKED\";\n        else state=L\"FOUND\";",
)

# ---- bridge: use the exact identities on THIS proven enumerator ----
replace_once(
    "src/bridge_chunks/bridge_13.txt",
    "        case UiTestTarget::ItemDrop: return L\"Vứt bỏ\";\n        case UiTestTarget::ItemSell: return L\"BÁN\";\n        default: return L\"UNKNOWN\";",
    "        case UiTestTarget::ItemDrop: return L\"Vứt bỏ\";\n        case UiTestTarget::ItemSell: return L\"BÁN\";\n        case UiTestTarget::OpenBag: return L\"MỞ TAY NẢI\";\n        case UiTestTarget::SwitchToSkills: return L\"CHUYỂN → SKILL\";\n        case UiTestTarget::SwitchToBagUi: return L\"CHUYỂN → TAY NẢI\";\n        default: return L\"UNKNOWN\";",
)
replace_once(
    "src/bridge_chunks/bridge_13.txt",
    "        case UiTestTarget::ItemSell:\n            return SellActionScore(c);\n        default:\n            return 0;",
    "        case UiTestTarget::ItemSell:\n            return SellActionScore(c);\n        case UiTestTarget::OpenBag: {\n            // DATA-222 BottomIcon_Layout: ButBag -> ButBagClick.\n            const bool exactName = name == L\"butbag\";\n            const bool exactHandler = handler == L\"butbagclick\";\n            if (exactName && exactHandler) return 1800;\n            if (exactHandler) return 1700;\n            if (exactName) return 1600;\n            const bool caption = KeyEqualsAny(text,{L\"tuido\",L\"bag\"}) || HasAny(descendants,{L\"tuido\"});\n            const bool hud = HasAny(parents,{L\"bottomicon\",L\"mainui\",L\"hud\"});\n            return caption && hud ? 1000 : 0;\n        }\n        case UiTestTarget::SwitchToSkills:\n        case UiTestTarget::SwitchToBagUi: {\n            // DATA-222 SkillBar_Layout: both screenshots are the two visual states\n            // of this same physical button. Direction is guarded below.\n            const bool exactName = name == L\"buttonoriginalswitchsite\";\n            const bool exactHandler = handler == L\"buttonoriginalswitchsiteclicked\";\n            if (exactName && exactHandler) return 1800;\n            if (exactHandler) return 1750;\n            return exactName ? 1650 : 0;\n        }\n        default:\n            return 0;",
)
replace_once(
    "src/bridge_chunks/bridge_13.txt",
    "    if (target < UiTestTarget::CloseItemPopup || target > UiTestTarget::ItemSell) {",
    "    if (target < UiTestTarget::CloseItemPopup || target > UiTestTarget::SwitchToBagUi) {",
)

state_helpers = r'''
enum class SkillBarSwitchState : std::int32_t {
    Unknown = 0,
    BagUi = 1,
    Skills = 2,
};

bool IsSkillBarSwitchTarget(UiTestTarget target) {
    return target == UiTestTarget::SwitchToSkills || target == UiTestTarget::SwitchToBagUi;
}

const wchar_t* SkillBarSwitchStateName(SkillBarSwitchState state) {
    switch (state) {
        case SkillBarSwitchState::BagUi: return L"TAY NẢI/MENU (ToggleFirstTab)";
        case SkillBarSwitchState::Skills: return L"SKILL (ToggleSecondTab)";
        default: return L"UNKNOWN";
    }
}

bool ReadSkillBarSwitchState(std::vector<UiControl>& controls, SkillBarSwitchState& state,
                             wchar_t* detail, std::size_t cap) {
    state = SkillBarSwitchState::Unknown;
    UiControl* first = nullptr;
    UiControl* second = nullptr;
    int firstCount = 0, secondCount = 0;
    for (UiControl& c : controls) {
        if (c.kind != UiKind::Toggle) continue;
        const std::wstring name = FoldKey(c.labels.name);
        if (name == L"togglefirsttab") { first = &c; ++firstCount; }
        else if (name == L"togglesecondtab") { second = &c; ++secondCount; }
    }
    if (firstCount != 1 || secondCount != 1 || !first || !second) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • cần đúng 1 ToggleFirstTab + 1 ToggleSecondTab; không callback");
        return false;
    }
    std::int32_t firstSelected = 0, secondSelected = 0;
    wchar_t ignored[128]{};
    if (!ScalarGetter(first->klass, "get_Selected", first->object, firstSelected, ignored, _countof(ignored)) ||
        !ScalarGetter(second->klass, "get_Selected", second->object, secondSelected, ignored, _countof(ignored))) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • không đọc được get_Selected; không callback");
        return false;
    }
    if ((firstSelected != 0) == (secondSelected != 0)) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • hai tab có trạng thái Selected không hợp lệ; không callback");
        return false;
    }
    state = firstSelected ? SkillBarSwitchState::BagUi : SkillBarSwitchState::Skills;
    return true;
}

bool TargetAlreadyInState(UiTestTarget target, SkillBarSwitchState state) {
    return (target == UiTestTarget::SwitchToSkills && state == SkillBarSwitchState::Skills) ||
           (target == UiTestTarget::SwitchToBagUi && state == SkillBarSwitchState::BagUi);
}
'''
replace_once(
    "src/bridge_chunks/bridge_13.txt",
    "void AppendFingerprint(const UiControl& c, wchar_t* detail, std::size_t cap) {",
    state_helpers + "\nvoid AppendFingerprint(const UiControl& c, wchar_t* detail, std::size_t cap) {",
)

new_probe = r'''bool ProbeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {
    std::vector<UiControl> controls; std::size_t index = 0; int count = 0, score = 0; bool found = false;
    if (!FindUiTestTarget(target, controls, index, count, score, found, detail, cap)) return false;
    response.value0 = found ? 1 : 0; response.value1 = count; response.value64_0 = score;
    if (!found) return true;
    SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • FOUND • candidates="); AppendInt(detail, cap, count);
    Append(detail, cap, L" • score="); AppendInt(detail, cap, score); AppendFingerprint(controls[index], detail, cap);
    if (IsSkillBarSwitchTarget(target)) {
        SkillBarSwitchState state{};
        wchar_t stateDetail[256]{};
        if (!ReadSkillBarSwitchState(controls, state, stateDetail, _countof(stateDetail))) {
            Append(detail, cap, L" • "); Append(detail, cap, stateDetail);
            return true; // Recognition succeeded; direction state is diagnostic here.
        }
        response.value64_1 = static_cast<std::int64_t>(state);
        Append(detail, cap, L" • Current="); Append(detail, cap, SkillBarSwitchStateName(state));
        if (TargetAlreadyInState(target, state)) response.resultCode = static_cast<std::int32_t>(ActionResult::AlreadyInState);
    }
    return true;
}'''
replace_between(
    "src/bridge_chunks/bridge_13.txt",
    "bool ProbeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {",
    "bool InvokeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {",
    new_probe,
)

new_invoke = r'''bool InvokeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {
    std::vector<UiControl> controls; std::size_t index = 0; int count = 0, score = 0; bool found = false;
    if (!FindUiTestTarget(target, controls, index, count, score, found, detail, cap)) return false;
    response.value0 = found ? 1 : 0; response.value1 = count; response.value64_0 = score;
    if (!found) return true;
    UiControl& selected = controls[index];
    std::wstring name = selected.labels.name, text = selected.labels.text, handler = selected.labels.handler, ancestors = selected.labels.ancestors;

    SkillBarSwitchState beforeState = SkillBarSwitchState::Unknown;
    if (IsSkillBarSwitchTarget(target)) {
        if (!ReadSkillBarSwitchState(controls, beforeState, detail, cap)) return false;
        response.value64_1 = static_cast<std::int64_t>(beforeState);
        if (TargetAlreadyInState(target, beforeState)) {
            response.resultCode = static_cast<std::int32_t>(ActionResult::AlreadyInState);
            SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • ALREADY IN STATE • Current=");
            Append(detail, cap, SkillBarSwitchStateName(beforeState));
            Append(detail, cap, L" • không callback để tránh toggle ngược");
            AppendFingerprint(selected, detail, cap);
            return true;
        }
    }

    if (!InvokeControl(selected, detail, cap)) return false;
    response.resultCode = static_cast<std::int32_t>(ActionResult::ActionInvoked);
    SetText(detail, cap, UiTestTargetName(target)); Append(detail, cap, L" • DIRECT INVOKED • candidates="); AppendInt(detail, cap, count);
    Append(detail, cap, L" • score="); AppendInt(detail, cap, score);
    if (IsSkillBarSwitchTarget(target)) {
        Append(detail, cap, L" • Before="); Append(detail, cap, SkillBarSwitchStateName(beforeState));
    }
    Append(detail, cap, L" • Name="); Append(detail, cap, name.c_str());
    Append(detail, cap, L" • Text="); Append(detail, cap, text.c_str());
    Append(detail, cap, L" • Handler="); Append(detail, cap, handler.c_str());
    Append(detail, cap, L" • Ancestors="); Append(detail, cap, ancestors.c_str());
    return true;
}'''
replace_between(
    "src/bridge_chunks/bridge_13.txt",
    "bool InvokeUiTestTarget(UiTestTarget target, Response& response, wchar_t* detail, std::size_t cap) {",
    "// Lightweight semantic Trade watcher.",
    new_invoke,
)

print("UI icon targets patch applied on Test-drop-sleep-click startup-fixed base")
