# ThanLong TEST BÁN ĐỒ — DROP SLEEP

Bản test sạch phát triển từ semantic bag / 42 tọa / internal bridge của source 9.9, với mục tiêu **không có fixed `Sleep()` trong luồng thao tác** và giảm khoảng cách giữa click item → click `BÁN` xuống đúng thời gian client thực tế cần.

## Phạm vi

- Quét client có `GameAssembly.dll` và chọn client.
- `ReadBagPage` semantic scan.
- Giữ classifier 9.9: `EquipPointDb` + `bag_filter_v2_logic::IsWeapon`; item khóa bị loại trước candidate.
- Không dùng `sellable` để quyết định item bán.
- Mapping 9.9 `Position 0..99 -> depth -> physical index 0..41`.
- Dùng 42 tọa + swipe đã gán.
- Một nút chức năng: **TEST BÁN ĐỒ**.
- Hai mode BÁN: scan ảnh `sell.png` hoặc callback dòng `BÁN` nội bộ.

Không có Train, BTĐ, trade MAIN/CON, AutoPath, Telegram, PK, trị liệu, dungeon, scheduler hay license của bản thương mại.

## DROP SLEEP

Source test không dùng `Sleep()`. Sau click item, tool kiểm tra state ngay; thấy `BÁN` là xử lý ngay. Sau click BÁN, vừa xác nhận surface biến mất là sang item tiếp theo. Timeout chỉ là fail-safe.

Mode nội bộ được tối ưu để **tìm thấy và callback `BÁN` trong cùng một bridge request**, tránh một round-trip thừa giữa probe và click.

## 42 tọa

Ưu tiên đọc trực tiếp:

`%LOCALAPPDATA%\ThanLongCleanRoute\WeaponScan.auto.tlscan`

Fallback: `ThanLong_WeaponScan.tlscan` hoặc `test_sell.tlscan` cạnh EXE. Tool không tự reset/đưa tay nải về trạng thái chuẩn trước test.

## Mode 1 — scan ảnh BÁN

Đặt `sell.png` cạnh EXE hoặc chỉnh `Template` trong `test_sell.ini`. ROI càng nhỏ thì probe càng nhanh.

## Mode 2 — dòng BÁN nội bộ

Mode mặc định. Bridge ưu tiên control `BÁN` thuộc popup item, loại shop/QuickSell/SellTab/NPCShop. Ambiguous thì fail-closed.

Xem đầy đủ: [VERSION_REPORT.md](VERSION_REPORT.md)
