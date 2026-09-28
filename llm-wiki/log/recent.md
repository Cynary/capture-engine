# llm-wiki Log

### 2026-09-28 — timestamp-only presentation observation

Added a separate opt-in ring for Present and final-output CPU timestamps. The
normal capture request remains disabled during observation. Native build/tests,
an empty-texture-ring check, and live fixture/game runs passed. A pixel-ID fixture
validated the ETW selection association; arbitrary games still lack pixel IDs,
so their callback association remains an inference. See [debug tools](../debug-tools.md).

### 2026-09-28 — single-threaded D3D11 capture retirement

Overcooked 2 reconnect stalled after 14–15 frames with its render thread waiting
behind background resource destruction. Device flags 0x1 establish that the
capture objects belong to a single-threaded device. Deferred releases now retain
that ownership; Present/Present1/CaptureFrame drain them on the owning thread.
Verbose per-object diagnostics changed scheduling and were removed before the
successful initial + three reconnect runs. Full unit gate and negative ownership
regression passed. Input-driven exit returned the stream to WGC. See
[d3d11-capture-resource-lifetime.md](../d3d11-capture-resource-lifetime.md).


### 2026-09-28 — candidate verification cleanup

The complete verification gate found the colour tests had pushed a source file past 800 lines; they now live unchanged in `test_dxgi_presentation_color.cpp`, which includes the full overlay-compatibility definition needed by its policy headers. Queue recovery remains private to `dx12_hook_ecl.cpp` beside its only caller, avoiding an extra translation unit containing the same broad internal-header declarations. Capture clock fallback uses the already converted signed `slot.timestamp`, avoiding another implicit unsigned-to-signed conversion. A dependency test assertion was wrapped to meet Python’s line limit. Focused tests and the full verification rerun (build 0.1.28) passed: product/unit/Python tool checks, x64 ASan/UBSan and analysis limits. Existing advisory formatting/compiler warnings remain. The automatically tightened analysis baseline records reduced counts, not new allowances. x86 sanitizers remain unavailable in the installed toolchain; this static gate does not launch games. The prior run passed sanitizers but failed style/static-analysis limits, and is not a release pass.

### 2026-09-28 — late D3D12/Streamline attachment review

Three separate startup gaps were repaired. `dx12_hook_ecl.cpp` fills only a missing queue association from a DIRECT queue submitted by dxgi.dll inside this thread’s Present scope, excluding CE-owned submissions and requiring matching devices. `ProcessFrame` defers until that association exists: the earlier candidate submitted against a guessed application queue before the native Present and hit device-removal 0x887A002B. Known queue associations are never replaced by this recovery. Two source-wiring tests protect the bootstrap order and evidence checks because the injected hook is not linked into unit_tests; real late-attachment runs cover GPU behavior.

`Present1` now independently retries transient deep-hook failures, matching Present’s four-attempt policy. A real run observed three unstable-thread-snapshot refusals then success. The focused source regression fails with the previous one-shot installation restored and passes with the retry. It supplements existing quiescence policy tests rather than emulating live thread suspension.

NGX cache paths are canonicalized using the existing provided-DLL-name parser, both for feature discovery/pinning and loader-unload notification. Module addresses are still pinned and unload generations checked; no filename hash, version or game is hardcoded. Status-only DLSS replies can recover missed activation only with successful status, a nonzero completion fence, runtime-fence evidence, and 2–6 actual presents within capability. Explicit options and protected transitions retain precedence. Five focused policy tests cover discovery/rejection and activation boundaries.

Combined candidates passed fresh/late HDR gameplay and a complete HEVC 4:4:4 10-bit PQ stream with 2x DLSS, including resident reconnect and game exit back to WGC. Distinct decoded images establish changing video; controlled moving-pattern tests remain the evidence for intermediate interpolation. The bridge’s automatic-selection guard for already-running unhooked games remains enabled pending wider lifecycle validation. No safety claim is made for arbitrary overlays or drivers.

### 2026-09-28 — initial DXGI colour-space recovery

`dxgi_swapchain_color_query.h` queries the private Windows inspection IID also used by ReShade. It reads `GetColorSpace1` only after a successful `QueryInterface`, validates the enum, releases the interface, and leaves the caller’s output unchanged on failure. No object offsets or DLL addresses are embedded. `DXGIShared::QuerySwapChainColorSpace` prefers successful setter tracking and uses this query only when that tracking is unavailable. Queried reads are not cached: doing so could overwrite a newer concurrent setter. `RecordSwapChainColorSpace` compares only recorded private data so a successful setter still publishes even if the native getter already reports the new value. Diagnostics are bounded to five initial queries per process.

Three native mock-interface tests cover live value changes, unsupported/null inputs, invalid enums and reference balance. A separate real D3D11 flip-discard probe returned SDR → PQ → SDR exactly. The combined native product/unit gate passed. In a fresh Stellar Blade process already in HDR gameplay before injection, the first capture delivered 2,446/2,446 HDR 4K R10 frames without toggling HDR. After enabling 2x DLSS, a reconnect delivered 2,877/2,877 HDR final-output frames. These runs prove metadata/routing, not every generated frame’s pixel correctness or encoder integration. This resolves the initial-declaration limitation noted in the entry below on the tested Windows DXGI implementation; other implementations may not expose the private interface.

### 2026-09-28 — HDR metadata across resident-hook dormancy

`DetourSetColorSpace1` rejected metadata publication while `HookIsShuttingDown()` was true, including the normal dormant interval after a consumer disconnect. The wrapper already recorded successful calls in that interval. Removed the inline-only gate; exactly-once wrapper ownership and HRESULT validation remain. Metadata is swapchain private data, independent of capture resources.

Native combined build/unit gate passed. The source lifecycle regression fails with the old shutdown gate restored and passes with it removed. The hook itself is not linked into the unit executable, so the source assertion is supplemented by a live Stellar Blade disconnect → HDR Off/On → reconnect test: all 2,876 frames carried HDR from the first frame. A subsequent loaded-game probe delivered 2,877 HDR final-output frames with 2x DLSS enabled; this verifies metadata/routing, not the pixel correctness of every interpolated frame. At this stage, initial late attachment still missed declarations made before injection; the subsequent initial-query entry above addresses that separate limitation.

### 2026-09-28 — direct-capture reconnect and preserved-swapchain resize

The original hook terminated a standalone D3D11 producer on reconnect. CDB with
matching private symbols resolved the abort to assigning over a joinable metrics
worker thread. The candidate serializes thread ownership and joins the old worker
before restart, without holding the metrics-data mutex. Combined incremental
product/native-test gates passed; the live producer survived disconnect/reconnect.
No isolated thread-lifecycle unit fixture exists yet; preserve the live reconnect
check as regression evidence rather than claiming unit coverage.

The pinned libvpl 2.17 source incorrectly enables legacy Microsoft CRT shims when
`_MSC_VER` is undefined under MinGW. The narrow build-policy correction passed ten
focused shell/policy tests and the actual cold dependency build.

The preserved-swapchain resize candidate runs tracked D3D12 overlay cleanup before
reconciliation-only resize calls. It does not reset capture transport resources.
An original-hook HDR startup failed with ResizeBuffers DXGI_ERROR_INVALID_CALL.
The candidate also failed with DXGI_ERROR_INVALID_CALL: the actual message was
recovered from its matching dump, rather than inferred from stale Unreal XML.
The later frame-session mutex fix (dbb5e79) removes a separate resize/capture race.
With both changes, three fresh Stellar Blade HDR starts and a DLSS 2x on/off
transition succeeded and exited cleanly. This is bounded startup/lifecycle
validation, not proof against every intermittent failure. Reconcile-only resize
now has source regression coverage for tracked ownership, paired cleanup and
failure diagnostics; live logs establish that the tracked cleanup path ran.

An external consumer also reproduced a fence timeout across resolution changes:
the producer reused a numeric NT handle for a new fence. The cached old fence
stopped at 17; reopening the same handle returned a fence already at 33. Consumers
must compare underlying object identity (or a reliable resource generation), not
only handle numbers. The producer's outstanding-frame lease contract must remain
intact through a generation transition.


### 2026-09-21 - CE's own swapchain flag killed Strange Brigade's startup; DX12 sampler overrides reach nothing

Session `20260921_173511`, build 0.1.6757. The game showed
`Can't recover from driver error. Error Code 80070057`, exited with code 1, and never presented a
frame. The 49 MB `FREEZE` dump is CE's watchdog reacting to that modal box, not the event.

**Root cause.** `backbuffer_count` implements its depth by adding
`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (0x40) to the *application's* creation
descriptor. `dxgi!CDXGISwapChain::ValidateResizeBuffers` XORs the caller's flags with the chain's
creation flags and returns `E_INVALIDARG` on any disagreement in that bit. Proof from the dump: the
chain's stored flags at `swapchain+0x184` were `0x842`, the game's own copy on its stack `0x802`.

The rewrite that hid the flag again lived in `CWrapDXGISwapChain` and in the ResizeBuffers vtable
detour. Steam owned the dxgi Present entry, so CE logged
`keeping the swapchain vtable pristine` and `Preserving real DX12 swapchain identity`, handed the
game the real swapchain, and installed neither. The mutation was unconditional, the compensation was
not - and it was duplicated across six creation paths, which is how they drifted apart.

Fixed in e07c3222: `hook/common/swapchain_flag_policy.h` holds the rule once, the reconciliation
reads the live `GetDesc().Flags` instead of re-deriving intent from the config (a stale "add the
bit" is as fatal as a missing one), a reconcile-only ResizeBuffers claim is installed at the DX12
and DX11 bootstrap independently of the Present-ownership question, and the flag is withheld when
no reconciliation can be established. Validated on hardware in `20260921_175749`:
`ResizeBuffers: Reconciling application resize flags 0x802 -> 0x842`, 10288 frames, clean exit.

**Second finding, from the same session.** Forced AF and `mip_bias=-3.0` did nothing: zero
`DX12 AF:` lines. `PatchIATAllModules("d3d12.dll", "D3D12CreateDevice", ...)` logs `patchResult=0`
because nothing imports it statically, and the injector waits for `d3d12.dll` to be *present* before
injecting (`waitMs=0`, `d3d12=1` on the first poll), so the game had already resolved the export.
Hooking the device CE discovers from the game's command queue (`DX12_PublishNativeLimiterDevice`)
was still too late: in `20260921_175749` the hooks came up at 17:58:01.068 and observed exactly two
static samplers all session, both CE's own overlay root signature.

All `ID3D12Device` objects share one D3D12Core vtable, exactly like `ID3D12CommandQueue` - which is
why `DX12_HookQueueVTable(pQueue)` on the bootstrap queue has always covered the game's pre-existing
queue. The device claim is now made on the WARP bootstrap device too. That claim was removed in
6323ed47 and guarded by a source test; the guarded rule is really "the WARP bootstrap must not become
*application evidence*", and a vtable claim is not that, so the test now asserts the claim exists and
that `MarkD3D12DeviceCreated` still does not. Confirmed on hardware 2026-09-21: forced AF and
`mip_bias` take effect in a DX12 title with the bootstrap vtable claim in place.

`LogSummary` also runs at frame 2000 now, not only at shutdown: "forced AF observed no sampler at
all" is useless information after the process is gone.

### 2026-09-20 - Both fixes validated: Strange Brigade starts, and the sharpen queue switch fired

Session `20260920_225326`, build 0.1.6755, two games back to back. No dump, no `crash.log`, no error
line anywhere, controller exited cleanly.

**The IAT serialization fix holds.** Strange Brigade reached the exact race that killed it an hour
earlier — same module at the same base, same two threads:

```
[22:53:34.845] [T:4D48] IAT: Successfully patched kernel32.dll!CreateProcessA in module 00007FF8B5E00000
[22:53:34.849] [T:4D48] IAT: Successfully patched kernel32.dll!CreateProcessW in module 00007FF8B5E00000
[22:53:34.849] [T:4D48] Late-loaded module steamclient64.dll imports CreateProcess - patched (A=1 W=1)
[22:53:34.961] [T:4C88] IAT: Successfully patched kernel32.dll!LoadLibraryA in module 00007FF8B5E00000
```

It then ran 40 s and logged `PerfLogger: Shutdown, logged 2704 frames`. Previously it died ~50 ms into
this sequence.

**The sharpen queue-change wait fired on hardware for the first time.**

```
[22:55:18.544] Sharpen: DX12 submitting queue changed 0000014AFDE27E90 -> 0000014AD36D00D0;
               chained behind fence value 2690
```

Talos, three seconds after a DLSS-MSFG -> off -> FSR-FG -> off sequence settled. Two genuinely
different queues, one GPU-side `Wait`, and the pass carried on: zero `skipped a frame`, zero
`could not be ordered`, no second `source copy ready` (so nothing was torn down across the switch).
The whole session had exactly three `Sharpen:` lines. This is the hazard `test_sharpen_gpu_timeline.cpp`
could only describe, now observed — and it confirms the two routes really do use different queues once
FG changes state, which the 22:35:12 Talos session could not show because `scQueue == origGame` there.

Overlay row updates stayed continuous across every FG transition; the per-transition counter resets to
`#1` are the overlay state being rebuilt for the new route, not a gap.

**The 104 us vs 12 us `total_us` gap is an accounting artifact, not a cost difference.** `total_us` is
the whole DX12 `ProcessFrame` span (`dx12_hook_process_session_phase1.cpp:13` to
`FrameProcessSession::LogFrameMetrics`). Whether the overlay render is *inside* that span depends
entirely on which route draws the overlay, so **`total_us` is not comparable across routes** — which is
the durable trap here.

Talos drew on the PostSL route (225 `Post-SL overlay SUBMIT`, `render%=100%`) and later the
FFX present-callback bridge; both run from call sites outside `ProcessFrame`. Strange Brigade logged no
`Post-SL overlay SUBMIT` at all — no Streamline or FFX frame generation was active in its run — so its
overlay drew on the normal route, inside the span.

Decisive, within Talos alone: `total_us` was 429 us median over frames 2-20 and 244 us over frames
20-40 while the overlay was still on the **normal** route, then collapsed to 6.5 us at the
`OVERLAY HANDOFF ... route=post-sl prevRoute=normal` at present ~46, and stayed at 6-11 us for the
remaining ~4700 frames. Same game, same second, ~40x from routing alone.

The complement confirms it: Talos's own route counter reports
`[OVERLAY COST] FFX present-callback bridge: ceAvgUs=91 ceMaxUs=223`. 12 + 91 = 103 us against Strange
Brigade's 104 us median, and 223 against its 204 us p99. Same total CE cost, split across two counters
in one title and combined into one in the other.

Ruled out along the way: GPU clocks are the same in both (2865-2940 MHz at 0.920 V), so no downclocking;
`ECL timing/1s` reports `avgMs=0.051` for Strange Brigade against `0.048` for Talos, so CE's
per-ExecuteCommandLists overhead is identical; and `fps_limit_wait_us` is a separate column (9.4 ms
median at Strange Brigade's 90 fps cap), so the limiter is not in `total_us` either. The load difference
is real but irrelevant — Strange Brigade sits at 3-6% CPU and 67% GPU because it is capped, Talos at
10-21% and 11-93%.

### 2026-09-20 - Strange Brigade died in PatchIAT: page protection is process-wide state

Session `20260920_224536`, build 0.1.6754, `StrangeBrigade_DX12.exe`. The game crashed ~50 ms after the
hook thread connected IPC and started `InstallKernel32LoaderHooks`, before rendering a single frame.
`0xC0000005` WRITE to `0x00007FF8B6F6B928`, RIP inside `capture_hook_x64.dll`.

```
capture_hook_x64!IATHook::PatchIAT+0x50d        <- _InterlockedCompareExchangePointer (inlined)
capture_hook_x64!IATHook::PatchIATAllModulesFiltered+0x1a1
capture_hook_x64!InstallKernel32LoaderHooks+0x1a1
capture_hook_x64!HookThread+0x4551
```

`!address 0x7FF8B6F6B928` names it: `steamclient64.dll`, `MEM_IMAGE`, **`PAGE_READONLY`**. CE was doing a
`lock cmpxchg` into a read-only image page.

**Two CE threads were patching the same page.** The log shows them interleaved to the millisecond:

```
[22:46:15.437] [T:5E94] IAT: Patched kernel32.dll!CreateProcessA in module 00007FF8B5E00000
[22:46:15.443] [T:5640] IAT: kernel32.dll!CreateProcessA in module 00007FF8B5E00000 already patched
[22:46:15.449] [T:5640] IAT: Successfully patched kernel32.dll!CreateProcessW in module 00007FF8B5E00000
[22:46:15.449] [T:5E94] <crash>
```

T:5E94 is the hook thread's `PatchIATAllModulesFiltered` sweep. T:5640 is the LoadLibrary hook's late-load
pass — `main_redirect.cpp:PatchLateLoadedCreateProcessImports`, which runs on whichever game thread mapped
the module, here Steam's loader thread mapping `steamclient64.dll`. `CreateProcessA` and `CreateProcessW`
are adjacent thunks in one 4 KB page.

**Root cause.** `PatchIAT` does `VirtualProtect(PAGE_READWRITE)` → CAS → `VirtualProtect(oldProtect)`, and
`g_PatchLock` was taken *after* the unprotect, covering only the `g_PatchedEntries.push_back`. Page
protection is process-wide state, so two of those sequences interleaving on one page destroy each other:
A unprotects, B finishes its own patch and restores `PAGE_READONLY`, A's CAS then writes into a read-only
page. `RestoreIAT` and `ShutdownIATHooks` already held the lock across all three steps — `PatchIAT` was
the one place that did not. Fixed by moving the guard ahead of the first `VirtualProtect`.

The guard starts *after* `TryGetTrackedOriginalForPatchedEntry`, which takes `g_PatchLock` itself.
`g_PatchLock` is a plain `std::mutex` → SRWLOCK under libc++, so a second acquire on the same thread parks
forever — the same trap `ReleaseDX12SharpenResources` hit in 0.1.6741. Making it recursive would hide the
re-entry rather than respect it; `tests/test_iat_patch_serialization.cpp` pins both the ordering and that
it stays non-recursive.

**This is a regression from the unreleased set, not a long-standing bug.** `0ee31cae`
(`fix(ngx): make ngx_ota=off suppress the updater instead of racing it`) introduced the late-load
CreateProcess pass, which is what gave `PatchIAT` a second concurrent caller. Before it, the hook thread's
sweep was effectively the only writer and the missing serialization never showed. It is also why the
crash is timing-dependent and looks title-specific: it needs a module that imports CreateProcess to map
during the sweep. Talos in session `20260920_223512` was fine for exactly that reason.

**The pre-release review missed it.** It covered sharpen, the Vulkan layer, the inline-hook engine and the
release tooling, but never asked what the new NGX-OTA late-load path races against. A new call site for an
existing global-state mutator deserves that question by default.
