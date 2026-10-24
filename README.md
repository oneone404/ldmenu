# LDMenu

Native Windows C++17 app, using Win32 + Direct2D + DirectWrite.
Default/minimum window 800×450, ratio 16:9. No WebView, .NET or JavaScript runtime.
Source directory: `D:\App\LDMenu`.

## Trạng thái & Tiến độ hiện tại (Snapshot trước khi tối ưu — 2026-10-25)

Bản snapshot này ghi lại trạng thái hoạt động đầy đủ tính năng của **LDMenu** trước khi thực hiện đợt tối ưu hóa gộp lệnh ADB/LDConsole:

- **Giao diện (UI Direct2D 800×450, tỉ lệ 16:9):**
  - Hiển thị danh sách các máy ảo LDPlayer đang mở (`ldconsole list2`), tự ẩn/hiện thanh cuộn tím (`#B026FF`), hỗ trợ phím tắt `F5` làm mới.
  - Nút **Play / Pause** điều khiển kết nối hàng loạt theo danh sách LD đã chọn và thời gian chờ (`Delay Giữa Các LD`) cấu hình trong tab **Cấu Hình Chung**.
  - Popup **Cấu Hình** gồm 2 tab (**Cấu Hình Chung** & **Cấu Hình LD**) và popup cài đặt riêng cho từng LD (**Chức Năng**: `OFF`, `Bọ Thành Tích`, `Bọ All Map`; **Lọc Bọ**; **Reconnect**; **Bán**; nút **Reset** đỏ và **Save**).
  - Nút **Quick Actions (tia sét)** cho từng LD: chạy trực tiếp `OFF (Reset All)`, `Câu Cá` (kèm lọc danh sách tối đa 64 ID và `BÁN NHANH`), hoặc `Farm All` (kèm lọc ID) thông qua giao thức mailbox mà không ghi đè cấu hình tự động trong `LDMenu.json`.
- **Cơ chế kết nối, nâng quyền Root & theo dõi sức khỏe (Backend):**
  - Tự động kiểm tra `boot_id`, nâng quyền daemon `adbd` lên `UID 0` một lần duy nhất bằng `adb -s emulator-<port> root` trong `Engine::ensureRoot` (không gọi lặp `su -c` qua LDMask, tránh thông báo Toast *"Shell dùng quyền superuser"*).
  - Xác minh tiến trình `com.vng.playtogether`, kiểm tra loader Zygisk (`LDLogin 1.30` tích hợp hoặc `zygisk_il2cppdumper`), kiểm tra SHA256 và GNU Build ID của `libmod-arm64.so`, tự nạp lại thư viện khi cần và giới hạn tối đa 5 lần restart máy ảo trong 30 phút.
  - Health monitor chạy định kỳ mỗi 30 giây bằng shell thường (`LDMENU_HEALTH_V1`), tự động Reconnect sau 2 lần xác nhận mất tiến trình liên tiếp và tự phục hồi khi ADB ngắt kết nối tạm thời (`WaitingAdb`).
  - Vượt qua 164/164 bài kiểm tra nội bộ (`--self-test`).
- **Các hạng mục đã tối ưu hóa (Post-optimization — `v1.1-optimized`):**
  1. **Gộp lệnh ADB & bỏ gọi lặp `ldconsole.exe list2` (`fresh()`):**
     - `Engine::resetAll`: Gộp 9 lần ghi 4-byte + delay 100ms + đọc lại 52 byte kiểm chứng vào **1 lệnh `adb shell` duy nhất** (giảm từ 19 tiến trình con xuống còn 1 lệnh `adb.exe`).
     - `Engine::word(..., checkVm)`: Bỏ gọi lặp `ldconsole list2` trước mỗi từ 4-byte trong `apply`, `runtimeAction` và `applyIds` sau khi đã xác minh `fresh` ở đầu thao tác.
     - `Engine::waitFilter`: Gộp đọc metadata vector (`0x118798`, 24 byte) + latch (`0x1187f8`, 1 byte) trong 1 lệnh `adb shell`, cache `/proc/<pid>/maps` và gộp đọc mảng phần tử heap + metadata đối chiếu sau cùng vào 1 lệnh `adb shell`.
  2. **Tối ưu `inspectContext`, `connected` & `controlMatches`:**
     - Gộp bước kiểm tra `stat` trước + đọc 36-byte GNU Build ID (`base+0x2d0`) + `stat` sau vào 1 lệnh `adb shell`, đồng thời trả luôn `maps` và `startTicks` đã xác minh cho `connected`, `runtimeAction`, `apply` để không phải gọi lại `cat /proc/<pid>/maps` và `cat /proc/<pid>/stat`.
  3. **Nhớ trạng thái `rootReady` theo `bootId` & dọn mã `useSu`:**
     - Kiểm tra đồng thời `boot_id` và `id -u` ngay trong `Engine::ensureAdb`, giúp `Engine::ensureRoot` trả về ngay lập tức (0 lệnh `adb.exe` phát sinh thêm) khi `adbd` đã ở quyền `UID 0`.
  4. **Đọc vét pipe trong `Engine::command`:**
     - Kiểm tra trạng thái thoát tiến trình trước khi `PeekNamedPipe` để đảm bảo không bỏ sót dữ liệu stdout cuối cùng của các lệnh ngắn.
  5. **Sửa UX hover nền tối Popup (`main.cpp`):**
     - Tách mã hit-test vùng nền tối bên ngoài popup thành `-19` (vẫn đóng popup khi click nhưng không làm sáng nhầm nút `X` `-6` khi rê chuột ngoài bảng).

## Quick actions and Reset All (2026-10-24)

Each LD now has a separate lightning button. Its popup title is only the LD name;
the function list starts at OFF and offers Câu Cá and Farm All. The bottom action
is an icon without text. Selecting Câu Cá immediately shows the ID input and
BÁN NHANH switch; Farm All shows only the ID input; OFF hides both. Selection and
editing do not send game commands. Clicking the action icon is required.

Quick actions do not change LDMenu.json or use automatic Reconnect monitoring.
The first run or switching functions performs the original nine-word Reset All
sequence first. Reapplying the same verified live function updates the ID filter
and sell flag without a full reset. OFF + action performs Reset All on that LD
without deleting its saved automatic profile. A later explicit Play or automatic
settings apply resumes the saved profile.

The red Reset icon in per-LD settings stages defaults plus a runtime reset until
Save. Close without Save does neither. In global Cấu Hình LD, Reset similarly
stages defaults and enables Áp Dụng Cho LD Đang Mở; Save persists those defaults
for the open LDs and queues their runtime reset. Turning that scope switch OFF
before Save limits the change to defaults. Cấu Hình Chung Reset keeps its original
Play-target/delay behavior and does not queue runtime resets.

ID input accepts an empty value (no ID filter) or 1–2147483647 values separated by
commas, up to 64 IDs. IDs are applied through the original mailbox protocol, with
bounded readback of the game-consumed list and latch. Runtime actions retain
boot/PID/start-time, pinned ELF/build and writable-bound checks. Unknown outcomes
are not automatically replayed. New native behavior has not been tested in-game;
the user will test it. Static evidence: diagnostics/NPH-quick-actions-findings-20261024.md.

## Files

- C:\Users\OOP\Documents\XuanZhi9\Pictures\LDMenu.exe
- Adjacent LDMenu.json: saved per-device settings.
- Adjacent LDMenu\libmod-arm64.so: the existing private pinned library.
  Do not publish this library or the research captures.

The display includes only running LDs, using real index/name from ldconsole list2.
The sidebar, preview badge, logo and summary cards were removed.
The list uses the available height; there is no bottom log panel or retained
global event history. Each LD row shows its latest status/detail. The list
supports wheel scrolling and draggable purple scrollbars, shown only
while scrolling/hovering their edge and hidden after 1.1 seconds of inactivity.
The top refresh icon refreshes the device list; F5 also refreshes.
The Play icon to the left of the global gear connects the saved selected LDs
that are currently running. It never launches closed VMs or toggles an already
enabled connection OFF. Repeated clicks do not enqueue the same enabled LD again.
The rounded Play triangle is slightly smaller; Pause keeps its original size.
The Play button switches to Pause during a batch. Pause is graceful:
finish the current connection, wait the configured delay, then cancel only the
remaining queued Play targets. Already connected devices keep their connection,
features and Reconnect monitoring. Repeated Pause clicks do not extend the delay.
The icon returns to Play immediately when Pause is requested, while the worker
finishes gracefully. Pressing Play again during this interval cancels the pending
pause and continues the same queue without recreating or duplicating jobs.
The batch delay is counted after a connection finishes, before the next one starts.
Manual/reconnect requests keep their existing
five-second start spacing. UI and settling timers remain available during the gap.

The adjacent gear opens one popup with two tabs. It always opens Cấu Hình Chung:
the currently running LD checklist under Áp Dụng Cho:, an All checkbox, and an
editable Delay Giữa Các LD (s): field in the right column with its input below.
The batch delay is entered in
seconds (0–3600, default 5). Saved selections match exact LD index AND name;
closed LDs are hidden and skipped by Play. Cấu Hình LD contains the existing
default Bọ/Filter/Sell/Reconnect and post-connect delay (60 seconds initially).
Save persists drafts from both tabs together. Reset resets only the current tab.
Save defaults without overwriting
individual saved profiles, or explicitly enable Apply To Running LDs before
saving to update all currently running devices in one atomic file write.
Closed devices keep their individual saved profiles. New/unsaved connections
use the global defaults. Existing pending settling deadlines are preserved.
The main table has no LD/status/connection headings or Log label, and no numeric
index badges before device names. Internal LD identity checks still use real indices.
The accent is bright cyberpunk purple (#B026FF) on black.
Close/minimize/maximize buttons have compact hit areas; close hover is purple.

## Device controls

Connection toggle ON queues connection to that exact device.
OFF cancels/detaches control without closing its game.
The gear to the right opens settings for that device:

- Function: OFF, Bọ Thành Tích, Bọ All Map.
- Filter: OFF, Xanh Lá, Xanh Dương, Tím, Sự Kiện, Chỉ Thẻ, All Bọ.
- Reconnect: automatically reconnect after two consecutive lost-context checks.
- BÁN: saved sell preference.
- Refresh icon: reset the draft to all OFF.
- Save icon: atomically persist the draft in LDMenu.json.
- Close/Escape: discard the draft.

Reset and Save sit immediately left of the popup Close icon in both editors.
Unsaved changes are not applied.
All settings default to OFF. Main connection switches are not persisted as ON;
opening the app does not automatically start game commands.
Saved entries are matched by BOTH exact index and name; settings for closed LDs
remain on disk. Config.exe / Config.json are independent and are not modified.
The same singleton prevents Config and LDMenu controllers running together.

## Connection and automatic settings

1. Revalidate the exact LD identity and ADB. If ADB is demonstrably missing,
   repair the shared ADB server at most twice per app session, 60 seconds apart.
   Unknown/unauthorized/ambiguous transports do not trigger a blind repair.
2. Bind the direct ADB transport to the LD console route by matching Android
   boot IDs. If the ADB shell is not UID 0, request Magisk SU once (up to 60 seconds
   for a Shell permission prompt). Carry `su -c` through context reads, controls
   and readiness checks. Stable health monitoring uses an ordinary ADB shell.
   Do not require NPH to initialize root
   ADB first or promote the whole ADB daemon. Each later shell command checks that
   boot ID again inside the privileged shell. Verify the game
   context and known ARM64 module GNU build ID.
   Only a verified absence of the module permits reopening. Unknown/malformed
   maps, inaccessible memory, mismatched build IDs and ambiguous matches wait
   30 seconds for another inspection without closing the game or restarting LD.
   For a verified absence, validate root, enabled LDNPH loader, VNG launcher and the local
   library SHA256; push the pinned library and reopen that LD's VNG game.
3. Allow up to 60 seconds for loading, finishing early once verified.
   Only a fresh complete absence proof after a load timeout permits restarting
   that exact VM. A fixed five-entry rolling budget permits at most five restart
   attempts per LD in 30 minutes across all reconnect invocations in this app
   session. Refresh/manual toggles do not clear it; reopening the app does.
   An exhausted budget waits until its oldest attempt expires. Verify exit/new backend
   identity and wait 30 seconds for boot before retrying.
4. A validated existing game control block is compared to saved Bọ/Filter/Sell.
   If it already matches, attach Reconnect monitoring without resetting or writing
   those controls. Both connection and application paths check for this case.
   An unknown/unreadable state is not accepted as matching. A different state
   follows the normal guarded application path. After a successful connection,
   a non-default Bọ/Filter/Sell configuration is
   scheduled after the configured delay (60 seconds initially). As in the reviewed
   NPH connection path, readiness uses the exact game process, known loaded
   library and a time delay, not image matching. No screenshot or backpack-button
   check remains in Windows LDMenu, so a popup covering that image cannot block
   application. Per-write identity/build-ID checks still apply. Apply Bọ before
   Filter before Sell. The separate Android LDLogin bot is unchanged.
5. Before applying, validate the known module build ID, writable mapping and ELF
   writable PT_LOAD segment; an adjacent anonymous BSS mapping is supported.
   Every word write checks VM identity, Android boot ID, game process start time
   and module build note and reads back its result in the same remote command.
   A final read confirms function/filter/sell together. Unconfirmed writes stop;
   they are not blindly repeated.
6. Saving changed settings while connected queues a new application; the initial
   60-second settling deadline is preserved. Reset + Save clears running
   function/filter/sell. Saving during an apply cancels the old job.
7. Reconnect requires both Connection ON and saved Reconnect ON. One ordinary-shell
   probe per connected LD checks Android boot ID, exact saved PID, process start
   ticks and process state. Multiple same-name game PIDs are supported; a reused
   PID or zombie is not accepted as the saved process. Two consecutive confirmed
   missing-process probes queue the existing ADB/identity/connect flow. A verified
   changed Android boot ID also queues revalidation. Transport/parse/time-out
   errors are unknown, not evidence that the game has exited. After two unknown
   probes, recheck ADB through the existing bounded repair path without assuming
   that the game should be closed. The controller refreshes internal VM identity
   before attaching again, without refreshing the displayed list.
   After reconnect, the saved per-LD settings are applied after the same delay.
   Failed/unknown mutations do not start an endless restart/reapply loop.

ADB verification/reset failures enter WaitingAdb instead of a permanent Failed
row. Retry uses an ordinary shell every 30 seconds; once it responds, rebind the
transport to ldconsole and verify the saved process identity before resuming.
An unchanged live game resumes monitoring/pending apply without reopening it.
A missing/replaced game is reconnected only if saved Reconnect is ON; an initial
user-requested connection with no attached game can continue regardless of that
flag. The shared ADB reset budget is still two per app session, at least 60 seconds
apart. Once exhausted, passive recovery continues. Explicit config/root failures
and uncertain feature writes remain stopped rather than blindly repeated.

Connection, application and delayed retry jobs share earliest-deadline selection.
A slow earlier row cannot keep taking turns ahead of older pending work on later
rows. Ties favor a queued connection. Reconnect jobs remember their automatic
origin: turning Reconnect OFF cancels stale automatic work and invalidates its
generation, while an explicit manual connection remains valid with Reconnect OFF.
All this operational state is kept outside JSON.

The worker entry, thread startup and window procedure have C++ exception
boundaries. On allocation/internal exception, an atomic failure code and native
Windows notification halt further control. An allocation-free command guard
terminates the current transient command client while unwinding, then handle
RAII releases its resources. The app can be closed and reopened after the cause
is addressed. This does not promise recovery from OS termination, access
violations, emulator faults or native game-library crashes.

One worker serializes mutations. Connection starts are at least five seconds
apart. UI remains responsive and can cancel. No launch with less than 2 GiB free
commit. The displayed device list is loaded once on startup and refreshed only
by the top Refresh icon or F5. Connected-game health probes run sequentially every
30 seconds in every window state, with a 3-second limit for each lightweight query.
No background-dependent game logic or health interval remains. No probes overlap
and the next cycle is scheduled after the current cycle completes. The worker
sleeps until an actual event/deadline when no LD is connected, instead of waking
for an empty health sweep. Probes do not refresh the displayed device list.
Explicit connect/write operations still validate LD identity when necessary.
No continuous rendering loop: paint on input/state changes, one temporary timer
for scrollbar visibility. Minimize releases Direct2D rendering resources.
Closing LDMenu does not close games, VMs or NPH.

Saving an unchanged profile/global configuration does not rewrite JSON or queue
another game apply. Changing only Reconnect updates the controller state without
resetting/reapplying Bọ/Filter/Sell. Other feature changes retain identity/build-ID
and write/read-back validation. Ordinary stable health queries never call SU or
read maps/memory; initial attach and feature mutations still perform those checks.

Dependencies: installed D:\LDPlayer\LDPlayer9, Magisk SU/root ADB, Play Together VNG and
enabled LDLogin 1.30 with its pinned integrated loader, or the original standalone
zygisk_il2cppdumper/LDNPH loader. Both enabled simultaneously are rejected before
opening a fresh game. The merged loader's two 64-bit libraries are hash checked.
No Windows NPH host,
UI, activation or keys are read by this development build.

Pinned SHA256:
1eb769fb80291fae60215276575acecc5363c59e9433965014c6767ffefecd2a

Pinned GNU build ID:
575b332244c836fd556484dbb827dd799110e439

## Build / validation

powershell -File D:\App\LDMenu\build.ps1

-OutputPath D:\App\artifacts\LDMenu.exe builds a separate developer copy.
Build runs the pure parser/configuration/protocol checks with --self-test.
`-SkipTests` builds without running these checks, for user-requested manual testing.
The EXE and window use the embedded multi-size rounded white logo resource.
tests/integration_ld2.cpp is a separate opt-in live LD-2 regression driver;
it does not ship as the main app. See TESTS.md for actual validation evidence.

Earlier visual reference selected by the user:
https://github.com/maotaw-cmd/c-/blob/main/playhookUI.cpp

