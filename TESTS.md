# Verification — 2026-10-24

## Selected-LD Play and two-tab settings (latest)

- Delivered EXE: 1,036,288 bytes; SHA256
  58AE2733A337019AC96FA31391ADD9EF55B792E2A387168F19CB402BA1D36815.
- 65/65 pure/in-memory checks pass. Added saved Play selection/delay round-trip,
  Unicode target names, duplicate/invalid targets and delay limits. A controller
  without a worker thread verifies Play queues selected open LDs, skips closed
  devices, preserves existing attached PID/state, rejects stale index/name targets
  and is idempotent for already-enabled rows. These tests issue no ADB/game actions.
- Visually inspected the installed 800×450 app: Play is immediately left of the
  global gear; numeric badges are removed from the LD list.
- Global settings opens Cấu Hình Chung first; checklist showed only LD-1–LD-4,
  matching the four running instances. Cấu Hình LD retains the existing saved
  function/filter/reconnect settings in the same popup.
- Actual native numeric editor accepted a draft delay of 7 seconds; selected LD-2,
  switched to Cấu Hình LD and back, and confirmed both draft values remained.
  Reset only the general tab, then saved: JSON records delay 5 / empty Play list,
  while preserving the user's Bọ Thành Tích / Xanh Lá / Reconnect defaults and all
  10 individual profiles. No live Play/game actions were performed in this update.
- Build now compiles each source separately before linking, after one transient
  compiler allocation failure. The completed sequential build and checks pass.

## Global settings and compact neon UI (latest)

- Build passes 54/54 checks, including legacy JSON without global metadata,
  global defaults/delay round-trip, preserving individual entries, and rejecting
  invalid global function/delay values.
- Delivered LDMenu.exe: 1,012,224 bytes. SHA256:
  A1A11D03E004827AE7636D1BD73EE5676270DA74DDF5C00133C3F55C4B806489.
- Read the user's reference window: 800×450. New default/minimum uses that same
  size and maintains 16:9. Visually inspected the new window, no table headings
  and no Log label, bright #B026FF accent and top-level global settings gear.
- Visually inspected global popup fitting 800×450 and the renamed filter list:
  OFF / Xanh Lá / Xanh Dương / Tím / Sự Kiện / Chỉ Thẻ / All Bọ.
- Verified global default Save through the actual UI: LDMenu.json contains the
  global object with 60-second delay and OFF defaults, retaining the existing
  individual device entry. Apply To Running LDs remained OFF during this test.
- Save/Reset buttons moved directly left of Close in both popup variants.
- No live bulk overwrite or game actions were used for this UI/configuration
  update. Current individual settings were preserved.

## LDMenu release candidate (latest)

- Renamed delivered EXE/config/data directory to LDMenu. Pure checks 48/48 passed.
  Added JSON round-trip, Unicode device names, duplicate/invalid/oversized fields,
  allowed control values, writable mappings including adjacent BSS, and ELF writable
  PT_LOAD validation. Latest delivered EXE is 1,005,568 bytes, SHA256:
  29B960F9C5C47A6F81AAE183967CAD3AE5ACB0B71AC372F064C34FC3D2E2B3CE.
- Inspected the compact native UI: one device list, only running LDs, no sidebar,
  embedded log, icon-only refresh, gear to the right of each connection switch.
- Live UI configuration test on LD-2: selected Bọ All Map / All Bọ / Reconnect / Bán,
  saved and read back the exact JSON values, reopened the editor, reset to all OFF
  and saved. Connection remained OFF during this UI test. Delivered LDMenu.json
  keeps the default OFF settings; other LD defaults are also OFF.
- Live backend LD-2 test confirmed Bọ Thành Tích, > Xanh Lá and Sell OFF by reading
  back the known ARM64 module control fields. A separate scoped game force-stop
  was detected and the game/library were automatically reopened and verified.
- The fresh-game apply test then encountered a Unity/Houdini crash while enabling
  the function. This was NOT treated as a passing reconnect/apply regression.
  Added an in-world screen-readiness guard: the existing native image matcher must
  see the backpack button twice, 5 seconds apart, before enabling saved features;
  keep waiting while loading/login screens remain. An actual game disappearance
  during application can queue reconnect when enabled, instead of remaining failed.
- Optimized memory writes to one guarded remote request per word, with VM, boot ID,
  process start-time and known module build-note checks and immediate readback.
- The final gated build passes all 48 pure checks. Its complete live fresh-game
  reconnect/apply flow is not yet confirmed. The user stopped Computer Use with
  Escape during the final inspection; the temporary integration runner was stopped
  and no further Computer Use actions were issued. No game/VM was left under the
  temporary test controller. The new EXE is ready for user testing.
- Live Sell ON and Bọ All Map game behavior have not been tested in this iteration.
  No ADB outage or VM crash was deliberately induced. Existing Config.exe and
  Config.json were not modified. Previous LDControl.exe/data were moved out of
  Pictures to D:\App\artifacts\retired-ldcontrol.

## Earlier preview history

- Backend pure checks29/29: list parsing including CRCRLF, duplicate indices,
  missing/overflow VM PID, exact ADB alias, offline/missing/unknown/duplicate/
  unauthorized classification, strict launcher parsing, unique library mappings,
  GNU build-ID validation, nested Windows argument quoting and60s timing constants.
- Read-only listing returned indices0–10; index0closed, LD-1–LD-10running. Exact
  names/index/VM PIDs matched current `ldconsole list2`.
- Native CLI attach test passed on LD-2: original gamePID29584, base0x06fc0000.
- Actual UI switch test: toggled only LD-2; UI changed through checking to success,
  displayed PID29584 and one connected device. Dark purple rendering was inspected.
- Fresh-game test: force-stopped only VNG game on LD-2, then invoked the new native
  connection path. Push/remote hash/load/mapping/build-ID validation passed.
  New gamePID31565, base0x06fc0000; no VM restart. Log:
  `D:\App\diagnostics\LDControl-fresh-connect-LD2.log`.
- Final build attach recheck passed on PID31565. Original Config.exe SHA256 stayed
  8D59109552390235F5BE15F36D8AFBDD1A48BCA6367B7EF7C12D43827132B3B5.
- Delivered preview is868352bytes, SHA256
  2CAB6937B86FB2EFDA4E3DF9EA5DB6C2F144960C903FAE76156AE7E74F5DE396.
  Final imports contain no WinHttp/InternetConnect, ReadProcessMemory,
  WriteProcessMemory or CreateRemoteThread. Verification record:
  `D:\App\diagnostics\LDControl-preview-verification.json`.
- Final16:9 dimensions, no-border DWM attributes and60s settings are in source.
  The earlier UI was visually inspected; after the user stopped Computer Use with
  Escape, no further Computer Use actions or screenshots were performed.

No live ADB outage or VM restart was deliberately induced while other LDs/NPH
were running. Those recovery branches have code/parser checks, not a complete
end-to-end outage test. Only LD-2 received game actions; the other LDs were listed.
Game action behavior for the earlier native worker was confirmed by the user;
this UI preview currently exposes only connection, not feature controls.

An early pre16:9 UI sample used about65MiB private memory and54MiB working set
with Direct2D/driver resources loaded. This is a short observation, not a leak
or long-duration stability test. The sub1MiB command-line worker observation
does not describe the GUI's memory consumption.

## Focus border follow-up

The user reported a transient white native border while clicking across active/
inactive windows. Added WM_NCACTIVATE handling with DefWindowProc lParam=-1 to
suppress its non-client repaint, while preserving normal activation processing.
Disabled DWM non-client rendering for the custom frame; WM_NCPAINT remains
handled by the application. Kept normal WM_ACTIVATE focus behavior.
Reference: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate

Rebuilt29/29 pure checks and replaced only LDControl.exe after graceful close.
Updated EXE SHA256:
E9562D5AB02D8D7413BA6610B2226CF9AC43BA1F3B42A94890426EE70E3C2860.
No game command, ADB reset or VM restart was run for this UI-only fix.
Visually inspected the updated16:9 window after deactivation to Explorer and
activation back to LD Control. No white border appeared in those snapshots;
the exact mouse-held transient reported by the user still needs their recheck.
# 2026-10-24 — SU bootstrap and integrated LDLogin loader

The user requested manual game/UI testing, so this build used `-SkipTests` and no
automated game test was run. Earlier self-test totals below describe earlier builds.

Read-only diagnosis found LD-1/5 ADB shells already UID 0, while LD-6/10 were UID
2000. `su -c id` on LD-6 succeeded as UID 0. The old `openGame` required direct
UID 0 and did not request SU. The updated connection/application paths request
SU when needed and preserve that mode and the Android boot ID for health probes.
Fresh game startup recognizes the pinned loader bundled into LDLogin 1.30 and
rejects an enabled standalone duplicate.

Built and deployed EXE: 1,083,392 bytes, SHA256
`4809a3a6c562d78e730744c075ed22d8ebc3ee711d62ecf964cafaeee6b63333`.
The icon is embedded in the executable and registered on the window class.

At the user's explicit request, closed VNG/Haegin games on LD-1 through LD-10,
installed LDLogin 1.30 with the exact existing four loader binaries, then rebooted
each VM. Verified 10/10 changed Android boot IDs, boot completion, versionCode 31,
enabled new module, absent standalone `zygisk_il2cppdumper`, expected loader
digests, and unchanged per-device bot-settings SHA256. No game functions were
enabled for a test. Deployment audit and private settings backups stay under
`D:\App\artifacts\ldlogin-v1.30-deployment`.

LDLogin ZIP: 682,123 bytes, SHA256
`e271ebcc75153c790043596566c970a39ff6b740d9222124d1a33584678717e3`.
Source commit `e36503fc`; published latest release `ldlogin-v1.30` includes all
eight current app/module/update assets, whose uploaded SHA256 digests matched
their local files. The separate LDNPH catalog row is hidden.

# 2026-10-24 — ordinary-shell health monitoring, fixed 30-second interval

Built with `-SkipTests` at the user's request to test the app themselves. No new
automated tests, game kills, setting mutations, ADB resets or VM restarts were
run for this change. Compilation/linking completed without reported warnings.
This is not a long-duration stability or crash-prevention claim.

- Stable probes use one bounded ordinary ADB command per connected LD, checking
  boot ID + exact PID + process start ticks + state. No SU/maps probe is scheduled
  after application has finished. Both window states use the same 30-second period.
- Empty/failed/malformed/timeout responses are unknown; they do not count as a
  confirmed game exit. Known process loss needs two consecutive probes. Unknown
  ADB responses enter the bounded transport verification path after two cycles.
- The worker stops periodic health wakeups when no connected row exists, and its
  monitor snapshot no longer copies the bounded 200-line UI log on each sweep.
- Unchanged config saves do not queue application or rewrite JSON. Reconnect-only
  changes do not run feature/readiness application. Existing feature write guards
  remain, with the saved process start tick also required before feature writes.
- Connection refreshes internal VM identity after a changed VM/boot while the
  displayed LD list still refreshes only on user request/startup.

Latest EXE: 1,070,080 bytes, SHA256
`7dec5cfdf3a81ec01d04257968fb10f2d114a0f1614520aefb9a18b40d6dc498`.
Older counts and observations below describe previous builds.

# 2026-10-24 — long-running resilience follow-up

Built with `-SkipTests` per the user's manual-test preference. No tests were added
or run, and no game actions, ADB resets or VM restarts were performed for this
Windows build. Compile/link completed without reported warnings after correcting
one formatting warning in the restart-budget helper.

- ADB transient failures retain a retry deadline and recover passively without
  dropping an attached healthy game. Expired reset quota does not stop probes.
- Root context inspection distinguishes a proven absent module from unreadable,
  ambiguous, malformed, wrong-build or changed-process results. Only proven
  absence can authorize game opening or a load-timeout VM restart.
- Five restart-attempt timestamps per LD are retained across reconnect calls and
  manual toggles/refresh for a rolling 30-minute session budget. No unbounded
  history is retained. Cooldown jobs sleep until their deadline.
- Worker/startup/window-callback exceptions halt control and notify Windows via
  an atomic failure code. Command-process cleanup runs during exception unwind.
- This is a source/build review; waiting/cooldown/fault paths need manual/runtime
  validation. It is not proof of long-run stability or prevention of LD crashes.

Latest EXE: 1,080,320 bytes, SHA256
`1bd946abd1bd482e17497849c7570c44054c0d72c638af2c32fbe50ca52a7999`.

# 2026-10-24 — final offline checks and requested UI/readiness changes

Built and ran 122/122 pure internal checks. They do not start the UI, call ADB,
touch game/user settings, reboot VMs or perform network requests. Added regressions
exercise process-start/PID reuse, multiple same-name PIDs, unknown/failed health
responses, zombie processes, map ambiguity, full uint64 inode parsing, rolling
restart budget/expiry, scheduler fairness/deadlines, canceled stale automatic
Reconnect jobs versus valid manual jobs, and the worker halt state.

Clang static analysis completed for backend.cpp and main.cpp with exit code 0
and no emitted diagnostics. Compilation/linking also completed without warnings.
This is not a live game test or hours-long endurance/RAM-trend measurement.

Requested changes: removed the log panel and the backend history vector; each LD
keeps only its latest status/detail. The LD list now uses the available height.
Removed all Windows screenshot/backpack matching and its bundled finder/template
dependencies. Configuration application uses exact PID/start-time + validated
library + configured delay, retaining all mutation/readback checks.

Additional final fixes: earliest-deadline scheduling prevents later LDs being
starved; ambiguous LD metadata during ADB recovery remains in retry state; turning
Reconnect OFF cancels old automatic jobs without canceling manual requests; valid
64-bit inode values do not become malformed-map results. Status equality includes
the library base. Restart/cancellation regressions cover these controller paths.

Latest EXE: 1,077,760 bytes, SHA256
`3b82a6117314119a6fcf86e8973e228985de215f5786c9bc44b4d1bd84bb2f78`.
Earlier build sizes/counts and image-readiness observations below are historical.

## Quick controls / reset integration — 2026-10-24

- Current build: 164/164 pure checks passed. They do not call ADB or change user
  settings/game state. Includes OFF default/validation, input overflow/injection
  rejection, 64-ID bound, original fishing/farm/reset sequences, vector/range
  validation, cancelling stale apply work, reset with no game never scheduling an
  opener, manual mode excluding Reconnect, and retaining the automatic profile.
- Before the final OFF-only polish, Clang analysis of backend.cpp and main.cpp
  completed with no diagnostics. Compilation after the polish has no warnings.
- Preview capture confirmed the 800×450 list has the separate lightning button,
  and global Cấu Hình LD shows the red Reset icon. The user then inspected the
  Fishing popup and requested OFF-first/title/icon-only adjustments. Those are
  included in the final build; no additional UI input or game test was run after
  the user's request to self-test.
- No live Reset All, Câu Cá, Farm All or ID filtering command was dispatched by
  the assistant. Original NPH and all LD/game processes were left running.
- App PNG is the latest supplied `... (1)-photoretrica.png`, copied unchanged;
  multi-size ICO is regenerated from that exact image.

