# Trade state V4.1 — semantic packet resolver

Bản V4.1 giữ nguyên mục tiêu của V4: chỉ cần biết **phiên giao dịch đã kết thúc**, không phân biệt thành công/hủy và không theo dõi item.

## Vì sao có V4.1

V4 đầu tiên đã inject/bootstrap được vào game nhưng fail ở bước resolve cứng:

`FGStudio.LuaSystem.LuaSystemManager.OnReceivePacket(2)`.

V4.1 không còn giả định cứng namespace + nơi khai báo method. Resolver mới:

1. thử `Assembly-CSharp` và `Assembly-CSharp.dll`;
2. nếu cần, duyệt toàn bộ assembly đang load;
3. tìm class có tên `LuaSystemManager` dù namespace thay đổi;
4. duyệt method trên class và parent class;
5. chỉ hook khi tìm được đúng `OnReceivePacket` với một trong hai signature an toàn:
   - `(System.Int32/System.UInt32, System.String) -> System.Void`
   - `(System.String, System.Int32/System.UInt32) -> System.Void`
6. nếu không khớp thì **fail-closed** và in signature thực tế đầu tiên vào log để không test mù.

## Trade signal

Vẫn chỉ lọc:

`CMD_TRADE_DATA = 200053`

State thử nghiệm:

```text
200053 + data != "-1" -> ACTIVE/UPDATE
200053 + data == "-1" -> TRADE_FINISHED / IDLE
```

`data == "-1"` là dấu đóng Trade đã được xác nhận từ shipped `Trade:LoadData(data)` trong DATA 222.

## Cách test

1. **Restart client game trước khi test V4.1** để loại bridge cũ còn nằm trong process.
2. Đặt `ThanLongTradeStateTest.exe` và `ThanLongTradePacketBridge.dll` cùng thư mục.
3. Chạy EXE cùng mức quyền với game.
4. Chọn đúng PID và bấm `BẮT ĐẦU THEO DÕI V4`.
5. Nếu attach PASS, log đầu tiên sẽ cho biết assembly/namespace/signature/RVA thực tế mà V4.1 resolve được.
6. Làm một giao dịch hoàn tất và một giao dịch hủy.
7. Kỳ vọng khi Trade đóng có `PASS • TRADE_FINISHED`.

Nếu attach vẫn FAIL, chụp nguyên dòng `ATTACH FAIL`. V4.1 sẽ ghi chi tiết hơn, ví dụ namespace thực tế hoặc signature `OnReceivePacket(...)` mà runtime đang có. Không được tự nới hook sang signature chưa biết vì có nguy cơ sai ABI/crash game.
