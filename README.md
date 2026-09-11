# ThanLong TEST BÁN ĐỒ — DROP SLEEP

Bản test sạch phát triển từ semantic bag / 42 tọa / internal bridge của source 9.9, với mục tiêu **không có fixed `Sleep()` trong luồng thao tác** và giảm khoảng cách giữa click item → click `BÁN` xuống đúng thời gian client thực tế cần.

## Phạm vi

- Quét client có `GameAssembly.dll` và chọn client.
- `ReadBagPage` semantic scan.
- Giữ classifier 9.9: `EquipPointDb` + `bag_filter_v2_logic::IsWeapon`; item khóa bị loại trước candidate.
- Không dùng `sellable` để quyết định item bán.
- Mapping 9.9 `Position 0..99 -> depth -> physical index 0..41`.
- Dùng 42 tọa + swipe đã gán.
- Tab **TEST BÁN ĐỒ** giữ nguyên luồng cũ.
- Tab **TEST UI DIRECT** dùng để nhận diện và callback trực tiếp từng control UI, không gán tọa độ.
- Hai mode BÁN: scan ảnh `sell.png` hoặc callback dòng `BÁN` nội bộ.

Không có Train, BTĐ, chuỗi trade MAIN/CON tự động, AutoPath, Telegram, PK, trị liệu, dungeon, scheduler hay license của bản thương mại.

## TEST UI DIRECT

Tab này là mini-lab để kiểm tra từng control trước khi ghép vào chuỗi automation. Mỗi dòng có hai nút:

- **NHẬN DIỆN**: chỉ scan live `UIObject`, không tác động game. Log trả `FOUND`, `NOT FOUND` hoặc `AMBIGUOUS`, số candidate và fingerprint `Name/Text/Handler/Ancestors`.
- **TEST DIRECT**: scan lại object live rồi gọi callback bằng `InvokeControl`; không giữ pointer UI cũ và không dùng tọa độ.

Các target hiện có:

1. X popup item
2. X Tay nải
3. X Giao dịch
4. Xác nhận giao dịch
5. Tab Trang bị
6. Đặt lên
7. Khóa
8. Bỏ khóa
9. Giao dịch
10. Vứt bỏ
11. BÁN

Selector ưu tiên context/hierarchy + Name/Handler; Text chỉ là tín hiệu nhận diện bổ sung. Nếu hai candidate cùng mức tin cậy, tool **fail-closed** và không callback.

## DROP SLEEP

Source `ThanLongTestSell` không dùng `Sleep()` trong luồng thao tác. Sau click item, tool kiểm tra state ngay; thấy `BÁN` là xử lý ngay. Sau click BÁN, vừa xác nhận surface biến mất là sang item tiếp theo. Timeout chỉ là fail-safe.

Mode nội bộ được tối ưu để **tìm thấy và callback `BÁN` trong cùng một bridge request**, tránh một round-trip thừa giữa probe và click.

## 42 tọa

Ưu tiên đọc trực tiếp:

`%LOCALAPPDATA%\ThanLongCleanRoute\WeaponScan.auto.tlscan`

Fallback: `ThanLong_WeaponScan.tlscan` hoặc `test_sell.tlscan` cạnh EXE. Tool không tự reset/đưa tay nải về trạng thái chuẩn trước test.

## Mode 1 — scan ảnh BÁN

Đặt `sell.png` cạnh EXE hoặc chỉnh `Template` trong `test_sell.ini`. ROI càng nhỏ thì probe càng nhanh.

## Mode 2 — dòng BÁN nội bộ

Mode mặc định. Bridge ưu tiên control `BÁN` thuộc popup item, loại shop/QuickSell/SellTab/NPCShop. Ambiguous thì fail-closed.

## Trạng thái build

Tab TEST UI DIRECT đã qua source-contract và Windows MSVC x64 build trong GitHub Actions run `34635684854`. **Runtime nhận diện/direct vẫn cần test trên client thật**; build pass không được coi là runtime pass.

Xem thêm: [VERSION_REPORT.md](VERSION_REPORT.md)
