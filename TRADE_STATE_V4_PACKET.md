# Trade state V4 — passive CMD_TRADE_DATA observer

Mục tiêu duy nhất của bản test này là kiểm chứng một tín hiệu nhẹ để biết **phiên giao dịch đã kết thúc**. Nó không xác định thành công/thất bại và không đếm item.

## Căn cứ từ DATA 222

- `CMD_TRADE_DATA = 200053`.
- `Trade:LoadData(data)` đóng Trade khi `data == "-1"`.
- `LuaSystemManager.OnReceivePacket` là đường nhận packet vào Lua/runtime.

V4 vì vậy không gọi `FindUI`, `GetScript`, không quét bag/item, không OCR và không poll Lua. Bridge chỉ detour đúng `LuaSystemManager.OnReceivePacket(Int32,String)`, lọc packet 200053 rồi copy trạng thái tối thiểu sang shared memory.

## State machine

```text
200053 + data != "-1"  -> ACTIVE
200053 + data == "-1"  -> TRADE_FINISHED / IDLE
```

`TRADE_FINISHED` chỉ có nghĩa phiên Trade đã đóng. Thành công, hủy chủ động, đối phương hủy hoặc lỗi server đều có thể cùng dẫn tới trạng thái đóng; bản test cố ý không phân biệt.

## Cách test

1. Mở game và vào nhân vật.
2. Đặt `ThanLongTradeStateTest.exe` và `ThanLongTradePacketBridge.dll` cùng thư mục.
3. Chạy EXE cùng mức quyền với game.
4. Chọn đúng PID và bấm **BẮT ĐẦU THEO DÕI V4**.
5. Thực hiện ít nhất một giao dịch hoàn tất và một giao dịch hủy.
6. Kỳ vọng log có `TRADE_ACTIVE` khi 200053 cập nhật phiên và `PASS • TRADE_FINISHED • ... data=-1` khi phiên đóng.

## Fail-closed

Bridge chỉ cài hook nếu runtime metadata xác nhận chính xác:

`FGStudio.LuaSystem.LuaSystemManager.OnReceivePacket(System.Int32/System.UInt32, System.String) -> System.Void`.

Nếu signature khác, bridge không hook và tool báo lỗi. Không có fallback OCR/UI/item.

## Lưu ý khi thay build

Bridge V4 tự pin trong process game sau khi hook thành công để không để detour trỏ vào DLL đã unload. Vì vậy nếu thay một build DLL V4 mới trong lúc game vẫn đang chạy, hãy restart game trước khi test build mới.
