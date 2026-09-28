# Direct capture investigation archive

### 2026-09-20 - Pre-release review of v0.1.6652..HEAD: the sharpen pass has two submit queues

Release-readiness review of the whole unreleased set (264 files, ~33k insertions). `--verify` passed
clean at 0.1.6748 (unit tests, x64 ASan/UBSan, lint) before any change; `coverage.integration_tests`,
`coverage.fuzz` and `coverage.test_apps` are `not_run`/`compiled_not_executed` as always, so nothing
here is evidence about a real present path. Sharpen still has **no hardware run at all**.

**The finding that matters: `D3D12Pass` assumes one submitting queue and there are two.**
`dx12_hook_process_session_draw_main.cpp:356` submits on the game's queue;
`dx12_hook_postsl_render_submit.cpp:44` submits on `submittedQueue`, which `Chunk3` resolves to the
game's queue *or* Streamline's `scQueue`. A DLSS-G activation switches CE between the two routes inside
one swapchain generation. One `ID3D12Fence` signalled from two queues gives completion values that are
not ordered against each other, so `GetCompletedValue()` can pass a value whose work is still running:
the allocator recorded for it gets `Reset()` under the GPU, and the single `sourceCopy_` texture is
written by one queue while the other still reads it.

The fix is a GPU-side `queue->Wait(fence_, fenceValue_)` issued once per switch, which re-establishes a
single ordered timeline. No CPU stall, no per-frame cost. If the `Wait` is refused the pass resets
itself rather than submitting into a timeline it cannot reason about.

Two more in the same file, same root cause (D3D12 keeps no reference for a submitted command list):
- `EnsurePipelineState` released the old PSO the moment `sharpen=cas` became `sharpen=rcas`. Replaced
  objects now go on a deferred-release list keyed by fence value.
- `EnsureSourceCopy` rewrote the one **shader-visible** SRV descriptor. A descriptor cannot be deferred
  the way a resource can, so that rebuild now waits for the timeline to drain by *skipping frames*,
  never by blocking the present thread. (`DX12OverlayState::Cleanup` already drained the resize path,
  so this is the belt to that suspenders.)

**Vulkan sharpen:** `vkResetFences` succeeded and `vkQueueSubmit` then failed left the slot marked in
flight against a fence nothing would ever signal — permanently retired. `kSharpenSlotCount` (3) of those
and sharpening was off for the session, logging only "every command buffer is still in flight". Also
`sourceInitialized` was set at *record* time, so an aborted submit made the next frame's barrier declare
`SHADER_READ_ONLY_OPTIMAL` for an image still in `UNDEFINED`; and `layer_sharpen_g_States[device]`
inserted an entry before the dispatch-table null check.

**New:** `hook/common/sharpen_gpu_timeline.h` holds those rules as pure logic, the way
`sharpen_policy.h` holds the decision rules — ordering bugs are exactly what can be checked without a
GPU. `tests/test_sharpen_gpu_timeline.cpp` covers them; the failed-submit ring case fails on the
previous revision.

**Reversed on inspection:** `RestoreOwnedEntryPatch` taking the `kAcceptSuspendedSet` fallback looked
like an unwarranted relaxation of *un*patching. It is the opposite. `Remove`/`RemoveAll` both log
"leaving it installed" on refusal and keep the entry, so a refused restore leaves CE's jump patched into
a game CE is unloading out from under. `hook_patch_transaction.h` already argues the residual race is
identical in both modes because `IsRangeSafe()` is what actually gates the write. Behaviour kept; the
relaxed path is now reported in the log (after the transaction has resumed every peer — logging inside
the suspended window can deadlock). That report pushed `inline_hook.cpp` to 825 lines and the file-size
preflight refused it, which is the gate working: the live-code writes moved into
`inline_hook_entry_patch.cpp` (213 lines) and `inline_hook.cpp` came back to 640. The boundary is real
rather than arbitrary — one unit writes running code under quiescence, the other owns the bookkeeping
around those writes — and `RestoreOwnedEntryPatch`/`InstalledEntryBytesMatch`/`OwnsInstalledEntryBytes`
lost their internal linkage to `inline_hook_internal.h` to cross it.

**Release process.** The build number is a local counter (`build_io.py:bump_and_write_build_version`),
so the release version does not exist until the runner has built it — `## Unreleased` is therefore still
un-promoted when the tag is cut, and the `--generate-release-notes` fallback to `## Unreleased` is the
normal path, not a degraded one. Promotion stays the operator's post-release step; the job summary now
prints the exact command. What is closed is the failure mode: once `## Unreleased` states
`Changes since [v<tag>]`, the generator refuses to publish those entries under that same version again.
Also: notes are generated *before* the tag is pushed (a generator failure used to strand a tag), and
`--promote-release --version <v>` — the form `changelog-guidelines.md` documented — aborted with
"expected one argument" and is now accepted alongside `--promote-release <v>`.

**Changelog gaps found and filled:** `561b286e` (config save respawning inject, ~7 s overlay blackout,
desktop "NOT RECORDING" warning, present-thread deadlock) had no entry at all; neither did the WER
dialog suppression in `4415c786`. The sharpen entry named `sharpen_intensity` as the strength control
while `config.ini.template` ships `sharpen_contrast`/`sharpen_amount` — and intensity is the mix weight,
not the strength. `dlss_fg_preset` (0c2a299e) remains absent from every release section; pre-existing,
still not backdated.

**`FpsLimiterTest` flakiness, addressed but not eliminated.** Six `EXPECT_LT(elapsedMs, 100.0)` upper
bounds were failing under host load on clean HEAD too (~1 in 6 full-suite runs, a different test name
each time). What those bounds exist to catch is a blocking wait on the remote-limiter release event,
which would cost that event's whole timeout — hundreds of milliseconds, not tens — so 100 ms was never
the discriminator, it was just tight enough to catch an ordinary scheduling stall. Raised to 500 ms with
the reasoning written at each site, and `elapsedMs` is now a `RecordProperty` so a genuine slowdown is
still visible. `GateEveryPresentStaysNonBlockingWhenInactive` additionally gained the exact,
load-independent form of its claim: `EXPECT_EQ(limiter.GetLastWaitUs(), 0)`. 5/5 clean repeats after.

This is a weaker timing assumption, not the absence of one. The structural fix is a counter for "waited
on the remote release event" that every one of these tests could assert zero on; note also that
`releaseEventName`/`requestEventName` currently have **no first-party consumer** outside
`shared_memory_layout.h` and this suite, so these tests are guarding a path that would have to be
reintroduced. Worth resolving one way or the other. b53f78f6 did the same conversion for
`SmartWait_Accuracy` and is the pattern to follow.


## 2026-09-28 — measured capture clock alongside presentation schedule

Shared-memory ABI 63 adds FrameSlot.captureObservedQpc. DX12/Vulkan final-output
plans sample the actual callback QPC separately from the existing virtual
presentation clock. Producers propagate this measurement; ordinary capture uses
its existing measured timestamp, and a generated output lacking the measurement
returns zero. This avoids pretending recording schedule timestamps are measured
capture times. Pacing and recording timestamps remain unchanged. A matching
streaming helper can now trace callback-to-ready-to-publish without subtracting
a future synthetic timestamp. Windows clean product/unit/Python gate passed. Live DX12 FG capture produced
11,410 measured frames with no clock-order violations, including 3,828 final
outputs. Steady menu callback-to-helper-publish mean/p99 0.560/0.662 ms; the
separate virtual schedule led the callback by 33.6 ms. Not a gameplay benchmark.
