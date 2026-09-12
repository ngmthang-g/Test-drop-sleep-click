from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_protocol_exposes_ui_test_targets_and_commands():
    text = read("src/protocol.h")
    assert "enum class UiTestTarget" in text
    for name in [
        "CloseItemPopup", "CloseBag", "CloseTrade", "TradeConfirm",
        "TradeTabEquip", "ItemPutOn", "TradeLock", "TradeUnlock",
        "TradeSubmit", "ItemDrop", "ItemSell",
        "OpenBag", "SwitchToSkills", "SwitchToBagUi"
    ]:
        assert name in text
    assert "ProbeUiTestTarget" in text
    assert "InvokeUiTestTarget" in text
    assert "AlreadyInState" in text


def test_bridge_has_probe_and_invoke_with_fail_closed_selection():
    text = read("src/bridge_chunks/bridge_13.txt")
    assert "FindUiTestTarget" in text
    assert "ProbeUiTestTarget" in text
    assert "InvokeUiTestTarget" in text
    assert "AMBIGUOUS" in text
    assert "InvokeControl" in text
    assert "Name=" in text and "Text=" in text and "Handler=" in text and "Ancestors=" in text


def test_new_icon_targets_use_exact_runtime_fingerprints_and_direction_guard():
    text = read("src/bridge_chunks/bridge_13.txt")
    # Exact identities recovered from DATA-222 Interface.unity3d.
    for token in [
        "butbag", "butbagclick",
        "buttonoriginalswitchsite", "buttonoriginalswitchsiteclicked",
        "togglefirsttab", "togglesecondtab",
        "get_Selected", "ReadSkillBarSwitchState",
        "AlreadyInState", "không callback để tránh toggle ngược",
    ]:
        assert token in text
    # The two screenshots are two states of one physical switch control.
    assert "SwitchToSkills" in text
    assert "SwitchToBagUi" in text


def test_switch_targets_fallback_to_wide_runtime_scan_and_promote_callable_parent():
    text = read("src/bridge_chunks/bridge_13.txt")
    for token in [
        "EnumerateActiveUiObjects",
        "PromoteActiveUiToCallable",
        "FindSwitchTargetFromActiveUi",
        "ReadSkillBarSwitchStateLive",
        "activeObjects=",
        "hints=",
    ]:
        assert token in text
    # Narrow callable scan remains first; wide scan is a switch-target-only fallback.
    assert "if (!EnumerateControls(controls, detail, cap)) return false;" in text
    assert "if (IsSkillBarSwitchTarget(target)" in text
    # Live state detection must not depend on the narrow EnumerateControls toggle list.
    assert "ReadSkillBarSwitchStateLive" in text


def test_controller_has_separate_test_ui_tab_and_all_target_rows():
    text = read("src/controller_chunks/controller_03.txt") + read("src/controller_chunks/controller_04.txt")
    assert "TEST UI DIRECT" in text
    assert "WC_TABCONTROL" in text
    for label in [
        "X popup item", "X Tay nải", "X Giao dịch", "Xác nhận giao dịch",
        "Tab Trang bị", "Đặt lên", "Khóa", "Bỏ khóa", "Giao dịch",
        "Vứt bỏ", "BÁN", "MỞ TAY NẢI", "CHUYỂN → SKILL", "CHUYỂN → TAY NẢI"
    ]:
        assert label in text
    assert "kUiTargetCount=14" in text
    assert "NHẬN DIỆN" in text
    assert "TEST DIRECT" in text
    assert "RunUiDirectTest" in text
    assert "AlreadyInState" in text


def test_existing_sell_flow_remains_present():
    text = read("src/controller_chunks/controller_04.txt")
    assert "TEST BÁN ĐỒ" in read("src/controller_chunks/controller_03.txt")
    assert "RunTest" in text
    assert "ClickItemSellAction" in text


def test_window_creation_dispatches_with_live_hwnd():
    wndproc = read("src/controller_chunks/controller_03.txt")
    handler = read("src/controller_chunks/controller_04.txt")
    # During WM_NCCREATE, App::hwnd_ has not yet received CreateWindowExW's return value.
    # The live HWND supplied by Windows must therefore travel through the dispatcher.
    assert "self->Handle(h,m,w,l)" in wndproc
    assert "LRESULT Handle(HWND h,UINT m,WPARAM w,LPARAM l)" in handler
    assert "return DefWindowProcW(h,m,w,l);" in handler
