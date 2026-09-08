# ThanLong TEST BÁN ĐỒ — no fixed Sleep

Bản test sạch phát triển từ cơ chế semantic bag / 42 tọa của source 9.9.

## Phạm vi duy nhất

- Tự quét các client có `GameAssembly.dll` và cho chọn client.
- Đọc semantic tay nải theo `ReadBagPage`.
- Giữ nguyên classifier 9.9: dùng `EquipPointDb` + `bag_filter_v2_logic::IsWeapon`; item khóa bị loại trước khi lập danh sách bán.
- Giữ nguyên mapping 9.9 `Position 0..99 -> depth -> physical index 0..41`.
- Chỉ có một nút chức năng: **TEST BÁN ĐỒ**.
- Hai cách BÁN: scan ảnh `sell.png`, hoặc callback dòng `BÁN` nội bộ.

Không có Train, BTĐ, giao dịch MAIN/CON, AutoPath, Telegram, PK, trị liệu, dungeon, scheduler hay license của bản thương mại.

## Cơ chế bỏ Sleep

Luồng thao tác không dùng fixed `Sleep()` để chờ UI. Sau click item, worker probe trạng thái liên tục; dòng/ảnh BÁN xuất hiện là click ngay. Sau click BÁN, tool probe cho tới khi trạng thái BÁN biến mất rồi sang item kế tiếp. Timeout 1.2 giây chỉ là fail-safe, không phải thời gian bắt buộc phải chờ.

Bridge request cũng không dùng `Sleep(2)`; controller dùng `SwitchToThread()/YieldProcessor()` giữa các lần kiểm tra để không chèn delay cố định.

## 42 tọa độ

Ưu tiên đọc trực tiếp cấu hình 9.9 hiện có:

`%LOCALAPPDATA%\ThanLongCleanRoute\WeaponScan.auto.tlscan`

Nếu không có, thử `ThanLong_WeaponScan.tlscan` rồi `test_sell.tlscan` cạnh EXE. Cả `TL_SCAN_AUTO_V1/V2` và `TL_SCAN_V5` đều được nhận. Tool lấy `step0..step41`, `swipe_start`, `swipe_end`; không reset/đưa tay nải về trạng thái chuẩn trước test.

## Mode 1 — scan ảnh BÁN

Đặt `sell.png` cạnh EXE. `test_sell.ini` có ROI và threshold. ROI càng nhỏ thì probe càng nhanh. `RoiW=0/RoiH=0` nghĩa là scan full client.

## Mode 2 — dòng BÁN nội bộ

Bridge enumerate UI runtime, ưu tiên control có text/name chính xác `BÁN`, loại các surface `Bán vật phẩm`, QuickSell/SellTab/NPCShop. Nếu có nhiều candidate ngang nhau thì fail-closed, không click đoán.
