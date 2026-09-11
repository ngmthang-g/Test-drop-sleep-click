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
        "TradeSubmit", "ItemDrop", "ItemSell"
    ]:
        assert name in text
    assert "ProbeUiTestTarget" in text
    assert "InvokeUiTestTarget" in text


def test_bridge_has_probe_and_invoke_with_fail_closed_selection():
    text = read("src/bridge_chunks/bridge_13.txt")
    assert "FindUiTestTarget" in text
    assert "ProbeUiTestTarget" in text
    assert "InvokeUiTestTarget" in text
    assert "AMBIGUOUS" in text
    assert "InvokeControl" in text
    assert "Name=" in text and "Text=" in text and "Handler=" in text and "Ancestors=" in text


def test_controller_has_separate_test_ui_tab_and_all_target_rows():
    text = read("src/controller_chunks/controller_03.txt") + read("src/controller_chunks/controller_04.txt")
    assert "TEST UI DIRECT" in text
    assert "WC_TABCONTROL" in text
    for label in [
        "X popup item", "X Tay nải", "X Giao dịch", "Xác nhận giao dịch",
        "Tab Trang bị", "Đặt lên", "Khóa", "Bỏ khóa", "Giao dịch",
        "Vứt bỏ", "BÁN"
    ]:
        assert label in text
    assert "NHẬN DIỆN" in text
    assert "TEST DIRECT" in text
    assert "RunUiDirectTest" in text


def test_existing_sell_flow_remains_present():
    text = read("src/controller_chunks/controller_04.txt")
    assert "TEST BÁN ĐỒ" in read("src/controller_chunks/controller_03.txt")
    assert "RunTest" in text
    assert "ClickItemSellAction" in text
