# TEST BÁN ĐỒ — DROP SLEEP

## 1. Mục đích của version

Đây là bản test tối giản được tách từ nền source 9.9 để kiểm chứng cơ chế thao tác **không dùng fixed `Sleep()` chờ UI**, với mục tiêu rút khoảng thời gian giữa click item và click `BÁN` xuống mức ngắn nhất mà trạng thái thực tế của client cho phép.

Version này không phải bản Auto 9.9 thu nhỏ. Nó chỉ giữ những thành phần cần thiết để thực hiện bài test bán vũ khí.

## 2. Nguồn logic được giữ

- Nền semantic bag / bridge / internal click lấy từ source 9.9.
- Cơ chế nhận diện item giữ đúng classifier của 9.9: `EquipPointDb` + `bag_filter_v2_logic::IsWeapon`.
- Mapping Position tay nải sang 42 tọa dùng nguyên quy tắc 9.9.
- Tài liệu DROP SLEEP được dùng để thay các khoảng chờ cố định bằng state-driven probing: trạng thái xuất hiện là xử lý ngay; timeout chỉ là fail-safe.
- Source 4.9 được dùng để đối chiếu và xác nhận luồng FILTER V3 cũ vẫn còn `Sleep(delay)`; timing của 4.9 không được bê sang bản này.

## 3. Những gì version giữ lại

- Quét client có `GameAssembly.dll` và cho chọn client.
- Shared-memory bridge + hook vào đúng game window thread.
- `ReadBagPage` để đọc semantic tay nải.
- `EquipPointDb` của 9.9, nén lossless theo các dải ItemID liên tiếp. File gốc có 22.763 dòng dữ liệu và 22.760 ItemID duy nhất; lookup sau nén giữ nguyên kết quả cho toàn bộ ItemID.
- `bag_filter_v2_logic::IsWeapon`.
- Bảo vệ item khóa bằng chính dữ liệu `bound` của semantic bag trước khi đưa vào danh sách bán.
- 42 tọa đã gán + `swipe_start` / `swipe_end`.
- Mapping Position 0..99 sang depth + physical index.
- Hidden/internal click và drag.
- Capture client + template matching chỉ dành cho mode scan ảnh `BÁN`.
- UI tối giản và log thời gian thao tác.

## 4. Những gì đã loại bỏ

Không mang sang version test các logic Auto 9.9 không liên quan: Train, BTĐ, MAIN/CON trade, AutoPath, Telegram, PK, trị liệu, dungeon, scheduler, party, map routing, revive, license/key và các UI tương ứng.

## 5. Điều kiện xác định item cần bán

Không phát triển classifier mới và không thêm điều kiện `sellable` vào quyết định bán.

Luồng quyết định đúng theo cơ chế 9.9 nhưng đảo nhánh cần xử lý:

1. Đọc `Position`, `ItemID`, `bound`, `isEquip` từ semantic bag.
2. Tra `EquipPoint` bằng DB của 9.9.
3. Item khóa bị loại.
4. Gọi `bag_filter_v2_logic::IsWeapon`.
5. Chỉ các item có `isEquip == true`, EquipPoint đã biết và `EquipPoint == 0` được đưa vào danh sách Position cần bán.

Trường `sellable` nếu có trong snapshot bridge không được dùng để quyết định candidate.

## 6. Mapping 100 Position sang 42 tọa

Giữ nguyên logic 9.9:

- Position 0..41 → depth 0 → physical 0..41.
- Position 42..83 → depth 1 → physical 0..41.
- Position 84..99 → depth 2 → physical 21..36.

Tool không tự đưa tay nải về một trạng thái chuẩn trước khi test. Nó dùng đúng trạng thái/coordinate config mà source 9.9 đã lưu và chỉ vuốt khi mapping Position yêu cầu depth tiếp theo.

## 7. Luồng `TEST BÁN ĐỒ`

1. Bấm `TEST BÁN ĐỒ`.
2. Lấy client đang chọn.
3. Attach bridge.
4. Quét semantic toàn tay nải.
5. Xác định danh sách Position là vũ khí không khóa theo classifier 9.9.
6. Sort/normalize Position bằng logic 9.9.
7. Với từng Position: tính depth + physical index, vuốt nếu cần, click đúng tọa item.
8. Popup item xuất hiện → xử lý `BÁN` theo mode đã chọn.
9. Xác nhận surface `BÁN` đã biến mất rồi chuyển item kế tiếp.
10. Hết danh sách → DONE.

## 8. Cơ chế DROP SLEEP

Source test không chứa lời gọi `Sleep()`.

### Bridge request

Controller ghi request vào shared memory, wake game thread rồi kiểm tra `completedSeq`. Khi chưa có kết quả, nó dùng `SwitchToThread()` / `YieldProcessor()` để nhường CPU nhưng không chèn khoảng delay cố định.

### Sau click item

Không có `Sleep(50/100/500 ms)`. Worker probe trạng thái ngay và lặp cho đến khi thấy `BÁN`. Khi thấy thì xử lý ngay trong vòng đó.

### Sau click BÁN

Không chờ thời gian chết. Tool probe trạng thái `BÁN`; vừa biến mất là chuyển item kế tiếp. Timeout chỉ dùng để dừng fail-safe nếu UI không chuyển trạng thái.

## 9. Hai mode BÁN

### Mode 1 — SCAN ẢNH BÁN

- Đọc `sell.png` cạnh EXE hoặc đường dẫn khai báo trong `test_sell.ini`.
- Capture client, scan ROI và click tâm ảnh `BÁN` ngay khi match đạt threshold.
- Sau click, tiếp tục scan cho đến khi ảnh `BÁN` biến mất.
- ROI càng nhỏ thì tốc độ càng cao.
- Bản build không tự có `sell.png`; người test phải đặt ảnh mẫu phù hợp nếu dùng mode này.

### Mode 2 — DÒNG BÁN NỘI BỘ

- Đây là mode mặc định và là đường ưu tiên về tốc độ.
- Bridge enumerate UI runtime và chỉ chấp nhận surface phù hợp popup item.
- Loại các surface `Bán vật phẩm`, QuickSell, SellTab, NPCShop/shop để tránh callback nhầm.
- Nếu không có candidate: probe tiếp ngay.
- Nếu có nhiều candidate ngang mức tin cậy: fail-closed, không chọn đoán.
- Tối ưu mới: **phát hiện và callback `BÁN` trong cùng một bridge request**, không còn probe một lần rồi gọi bridge lần hai để click. Điều này loại bỏ một round-trip và một lần enumerate UI giữa phát hiện và click.

## 10. Cấu hình 42 tọa

Ưu tiên dùng trực tiếp file của 9.9:

`%LOCALAPPDATA%\ThanLongCleanRoute\WeaponScan.auto.tlscan`

Fallback:

- `ThanLong_WeaponScan.tlscan` cạnh EXE.
- `test_sell.tlscan` cạnh EXE.

Chấp nhận format `TL_SCAN_AUTO_V1`, `TL_SCAN_AUTO_V2` và `TL_SCAN_V5`.

## 11. File cấu hình scan ảnh

`test_sell.ini`:

- `Template=sell.png`
- `RoiX/RoiY/RoiW/RoiH`
- `BaseW/BaseH` để scale ROI theo client size.
- `Threshold` mặc định 88%.

`RoiW=0` hoặc `RoiH=0` nghĩa là scan toàn client.

## 12. Thành phần build

- `ThanLongTestSell.exe` — UI/controller test.
- `ThanLongTestSellBridge.dll` — bridge được hook vào game thread.
- `test_sell.ini` — cấu hình mode ảnh.
- `README.md`.
- `VERSION_REPORT.md` — tài liệu này.

Build x64 Release bằng MSVC. CMake dùng static MSVC runtime (`/MT`) để giảm phụ thuộc `MSVCP140.dll` trên máy test.

## 13. Tiêu chí kiểm tra source/build

- Windows x64 build phải PASS trên GitHub Actions.
- Workflow tự fail nếu phát hiện `Sleep(` trong `src`.
- Không có fixed delay chờ popup trong luồng bán.
- Candidate bán không dùng `sellable`.
- Internal mode phát hiện + callback BÁN trong cùng probe.
- Mode nội bộ fail-closed khi ambiguity.

## 14. Giới hạn của bản test

- Đây là bản test cơ chế, chưa phải bản production.
- Mode ảnh cần `sell.png` đúng giao diện thực tế.
- Mode nội bộ phụ thuộc surface UI/runtime của client hiện tại; nếu game đổi class/text/callback, log sẽ báo không tìm thấy hoặc ambiguity thay vì click đoán.
- Timeout 1,2 giây không phải delay bắt buộc; nếu popup xuất hiện sau vài ms thì tool xử lý ngay lúc đó.
