# Long-running review — 2026-10-24

Static review of the current fixed-30-second, ordinary-shell health build.
No app/game test, ADB reset, VM restart or production config mutation was run.
This review originally made no source changes. The follow-up fixes below were
subsequently implemented at the user's request; earlier findings are preserved
as descriptions of the previous build.

## Follow-up resolution

- Added WaitingAdb/passive 30-second retry and boot/PID checks before resuming.
- Added Verified/Absent/Unknown context classification, framed successful reads
  and stable process identity. Unknown reads cannot authorize game/VM restart.
- Added a fixed five-entry per-LD rolling 30-minute restart-attempt budget.
- Added worker/startup/window-procedure exception boundaries and allocation-free
  stop notification, plus command-client cleanup during exception unwinding.
- Original per-session ADB reset caps and fail-closed uncertain-write behavior
  remain. No new live/outage/storm test was run; the user will test the build.
- Final offline follow-up passed 122 pure checks and Clang static analysis of the
  backend/UI. Added earliest-deadline fairness, cancellation of stale automatic
  Reconnect work when its switch is turned OFF, transient metadata retry and
  uint64 inode support. The user requested removal of Windows image readiness
  matching and the bottom log/history panel; both were removed. Runtime/endurance
  behavior remains to be observed by the user.

## Findings

1. **ADB failure can permanently stop automatic monitoring for that row.**
   `ensureAdb` reports `Phase::Failed` on an unknown transport, exhausted repair
   budget, or unsuccessful recovery (`backend.cpp:188–195`). `monitor` and the
   worker's health scheduling only include `Phase::Connected`; automatic job
   selection only includes `Phase::Queued` (`backend.cpp:157`, `359–365`). A row
   that fails ADB verification can therefore remain stopped after ADB comes back,
   until manually toggled. The two-reset-per-session limit is intentional; the
   missing separate passive recovery state is the resilience issue. Retain the
   reset budget, and distinguish transport wait from permanent configuration/SU
   failures before adding a passive recheck.

2. **Context verification errors are conflated with absence of a loaded library.**
   `context` returns the same false result for failed maps/memory reads, ambiguous
   matches, wrong build note and a genuinely absent module (`backend.cpp:211–217`).
   Initial connection can then reopen the game (`328–330`), and a 60-second
   timeout with a working echo can lead to a VM restart (`336–348`). ADB echo
   success does not prove root maps/memory verification succeeded. Classify
   Unknown, Absent and Verified separately, so uncertain reads cannot authorize
   a restart.

3. **VM restart budget resets per connection call.**
   The local `restarts` counter starts at zero in every `connect` invocation
   (`backend.cpp:322`). Five restarts per invocation do not impose a rolling
   limit across repeated successful-connect/crash/reconnect cycles. Repeated
   faults can therefore create restart churn over hours. A per-LD rolling budget
   would address this, but its limit/cooldown changes current recovery policy and
   has not been added by this review.

4. **Uncaught allocation/standard exceptions can terminate the worker/app.**
   The thread entry directly calls `loop()` (`backend.cpp:83`). Its allocations,
   stream operations, copies and notifications have no outer exception boundary.
   JSON decoding catches exceptions, but that does not cover the worker. Under
   commit/memory exhaustion, an escaping C++ exception terminates the process.
   A controlled worker failure path should halt mutation and notify the UI with
   a minimal allocation-free signal. Catching C++ exceptions would not make
   access violations or external LD/native-library faults safe to continue.

## Manual connection timing

Rapid ON toggles for different LDs enqueue work; a single worker performs it
serially. The global Play delay applies only to batch jobs. Manual/reconnect jobs
use `nextConnect_`, set to connection-start time plus five seconds (`366`). If a
connection lasts more than five seconds, the next manual request may start
immediately after it completes. This is not five seconds after completion.
Re-toggling the same row is OFF/cancellation, not an extra ON request.

## Bounded resources observed in code

- UI logs capped at 200 entries; parsed LD lists/settings at 512 entries.
- Command output capped at 1 MiB with finite command deadlines.
- Handles use RAII; BCrypt hash/provider and Direct2D/DirectWrite/GDI resources
  have explicit release paths. No definite unbounded leak was identified here.
- Stable health probes are serialized, ordinary-shell only, every 30 seconds,
  with a three-second probe timeout. Unknown responses are not game-exit proof.
- No continuous rendering loop or periodic device-list refresh.

These are static findings. No long-duration RAM/handle trend or end-to-end
outage/storm test was performed. Native library, game, emulator and display-driver
behavior is outside this controller-source review.
