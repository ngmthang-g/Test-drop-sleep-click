# TEST TRADE STATE — INTERNAL

Mục tiêu duy nhất của EXE này là kiểm chứng một tín hiệu nội bộ rất nhẹ để biết **phiên giao dịch đã kết thúc hay chưa**. Tool không cố kết luận giao dịch thành công hay thất bại.

## Tín hiệu đang kiểm chứng

Primary state:

```text
LuaSystemAPI_GUI.FindUI("Trade")
```

Quy ước test:

```text
null   -> chưa có / không còn Trade session observable
object -> Trade đang mở

đã từng object
  -> sau đó null 2 mẫu liên tiếp
  -> TRADE_FINISHED
```

`ActiveInHierarchy` và `ExchangeID` chỉ được thử đọc để ghi telemetry. Chúng **không phải điều kiện bắt buộc** và không được dùng để suy thành công/thất bại.

## Vì sao chưa dùng `data == "-1"` làm chân lý

DATA 222 xác nhận `CMD_TRADE_DATA = 200053`, nhưng chỉ biết packet ID chưa đủ để chứng minh hướng/payload/lifecycle. Bản test này cố tình không gắn giả định `-1 = finished` vào production logic trước khi có runtime proof.

## Cách test

1. Chạy `ThanLongTradeStateTest.exe` cùng quyền với game.
2. Chọn đúng client/PID.
3. Bấm **BẮT ĐẦU THEO DÕI TRADE** trước hoặc trong khi bảng giao dịch đang mở.
4. Thực hiện một giao dịch bình thường rồi hoàn tất hoặc hủy.
5. Xem log:
   - `TRADE_OPEN` = `FindUI("Trade")` trả object.
   - `TRADE_CLOSED_CANDIDATE` = lần đầu object biến mất.
   - `PASS • TRADE_FINISHED` = object biến mất 2 mẫu liên tiếp sau khi đã từng mở.

Nếu `ProbeTradeState` lỗi 5 lần liên tiếp, tool dừng FAIL và **không fallback** sang OCR, scan ảnh hay đọc item.

## Tải CPU

Observer lấy mẫu mỗi 40 ms, mỗi mẫu chỉ gọi semantic `FindUI("Trade")` và thử đọc 2 field/property nhỏ. Log chỉ ghi khi state đổi. Không quét túi và không ghi ID item.

## Phạm vi kết luận

PASS chỉ chứng minh được:

```text
Trade OPEN -> Trade CLOSED
```

Nó **không** chứng minh:

```text
trade success
số item đã chuyển
MAIN đã nhận đủ
CON đã mất item
```

Nếu runtime test PASS ổn định ở cả giao dịch thành công và hủy, tín hiệu này phù hợp để dùng sau này cho điều kiện “CON chỉ đi bước tiếp theo khi phiên Trade đã đóng”.
