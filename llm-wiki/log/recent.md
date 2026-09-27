# llm-wiki Log

### 2026-09-27 - Recording-start latency: probe early stop, truthful live timing, WGC reserve-wait finding

- `logs/20260927_195021` (0.1.6844, DXGI-dup desktop + Brave audio, first recording of the session): hotkey ->
  media live 5453 ms, not the logged 6375 ms (`CheckChildProcessHealth` polls once per second). Split: spawn 0.05 s,
  `[AVSyncProbe]` 3.17 s, engine/D3D/dup init 0.28 s, start + 19 audio sources 0.24 s, pre-live warmup 0.55 s,
  encoder prewarm 0.16 s, `WGC startup delay-reserve wait` 1.00 s (budget exhausted, `partial_span_timeout`).
- Probe: every shot captured the fixed 620 ms window (`capFrames=119040` at 192 kHz) with the marker at 95 ms.
  Shots now stop at `DetectCompletedMarkerCenterFrame` (burst + 40 ms decayed guard); measured value unchanged,
  full window kept as the bound. Shot spread 5.4 ms (engine-period Start jitter), so an adaptive 3-shot exit
  was rejected. Expected probe ~0.8 s on this endpoint - **not hardware-verified**; check `stop=marker_complete`,
  `shotMs=` ~150-250 and `probeMs=` in the next first-of-session recording, latency still ~33 ms.
- Controller `Recording is live` now uses media's `recordingStartTime` stamp (`ResolveRecordingStartupTiming`).
- **Open (not changed):** `SelectWgcStartupReserveCandidate` takes the frame NEAREST `latest - target` and
  rejects it when younger than `target - tol` (tol = min(half output interval, 5 ms)). For a steady source the
  phase `target mod period` is constant, so it fails on every evaluation: synthetic check with target 332.9 ms
  never succeeds at 25/28/31/40 fps. Independently, `startupReserveBelowLowWater` needs
  `ceil(delay / outputInterval)` = 40 newer frames, which a sub-output-rate source cannot supply within the
  delay. Net: sub-CFR sources (desktop, 30 fps video) likely always wait the full 1 s smoothness-attempt budget.
  In this log the timeout contract realized 320.6 ms vs 332.9 target and the extra ~0.6 s only discarded frames.
  Caution: the 1 s budget is deliberate (`GetWgcStartupReserveWaitBudgetQpc`) and the 250/500 ms input-rate
  windows feeding smoothness decisions fill during the wait - validate before shortening.

### 2026-09-27 - Session 20260927_040737 review: clean; two logging gaps closed

- 0.1.6843, 10 recordings (r0001 inject Talos with FSR FG, r0002-r0010 WGC): all `healthy`, CFR coverage
  `missing=0`, post-mux audio/video ends within 1 us, no underruns/trims, no ERROR lines.
- r0005: mux write queue grew to 421/512 MB over ~45 s (output on a network share) and drained in ~5 s; no
  backpressure, but only `QUEUE STATS` INFO recorded it. Added band/recovery warnings with writer attribution
  and a rate-limited slow-write line (`mux_queue_pressure.h`).
- r0001: swapchain rebuild -> transport generation 1 -> 2 with `frameIndex` restarting at 1 logged one
  `Inject lineage regression` + 16 `Texture slot reuse anomaly` false positives. Checks are now per
  generation (`inject_lineage.h`); the stale `lastEncodedFrameByTextureIndex` also was never reset per session.
- Not hardware-verified yet: expect `Inject lineage restarted ... generation 1 -> 2` instead of those warnings,
  and `Mux write queue reached 25%` on the next slow-output session.

### 2026-09-27 - Talos "windowed" start fatal: hidden-window create dropped the swapchain queue

- `logs/20260927_034946` (0.1.6842): the resize fix above is confirmed on hardware (4K -> 1440p with Steam, no crash).
- New case, with or without Steam: Talos options say windowed, actual borderless native 4K. The Streamline swapchain
  was created while the HWND was hidden -> `Invisible-window swapchain ... bypassing` -> no `Swapchain queue captured`
  (in `logs/20260927_031545` the same create was visible and `scQ=` its create queue). First visible Present chose `path=primaryQ` (render queue),
  `Reinit SUBMIT #1 ... devRemoved=0x887A002B`. UE's fatal path used TerminateProcess, hence no CE `.dmp`.
- Fix: park the create queue and evidence, promote them on the first visible Present (see `dx12-injection-bootstrap.md`).
- Next repro: expect `Parked create-time queue ownership ...`, then `First visible Present of hidden-window swapchain
  ... promote ... presentedQueue=same`, `Swapchain queue captured`, and `ProcessFrame ... path=scQueue`.

### 2026-09-27 - Talos resize fatal: CE's hooks sat where Steam patches (slot, then function entry)

- `logs/20260927_031545` (0.1.6841, no FG): still `FAILED ... [6,6,6,6,6,6]`, `gameoverlayrenderer64.dll=+6`. The
  0.1.6841 factory-slot handback ran twice (`CreateDXGIFactory`, `CreateDXGIFactory2`) and both times logged
  `unchanged - nothing hooked it`: **Steam does not hook factory vtable slots**; hypothesis disproved and reverted.
- Decisive: the new `slot owners` line. All five swapchain slots point into dxgi, but `[8]Present` and `[22]Present1`
  start with `E9` into a private RWX page right after dxgi's image (Steam's relay page; `CreateSwapChainForHwnd`
  already jumped into it at CE start), while `[13]ResizeBuffers` jumped to `capture_hook+0xDE860`. Steam hooks
  by patching function ENTRIES and skips one already jumping into another module. Present was fine only because
  CE hooks it below the entry (deep body). `logs/20260927_023858` (0.1.6840) is the same shape.
- Fix: `InstallResizeReconciliationHooks` installs ResizeBuffers/ResizeBuffers1 as deep body hooks
  (`kAssumedForeignEntryPatchSize=14`, like Present without a visible jump); with a third-party overlay loaded a
  refused body hook takes no site (reconciliation unavailable, so no waitable flag), otherwise the entry prepend
  stays the fallback (`resize_reconcile_hook_policy.h`). Both methods must be hooked before the flag may be added.
  `BackBufferRefTrace` GetBuffer is a deep body hook too. The `slot owners` diagnostic stays.
- The user's "with CE" Steam log was CrashReportClient's (Steam rewrites the file per process).
- Next repro: expect `resize flag reconciliation ready (... site=body-below-entry ...)`, and at the first resize
  `[13]ResizeBuffers=dxgi... entryJump->` into the same relay page as Present, then no refused resize. If the body
  hook is refused, the line says why (`refused (<reason>)`).

### 2026-09-26 - Talos FSR FG resize fatal without recording; 50 s dump freeze; 0x4000 is UE's fatal assert

- Session `logs/20260926_094906` (0.1.6837, no recording): `ResizeBuffers ... FAILED hr=0x887A0001
  sc=<real FFX chain> before=[3,3,3] after=[3,3,3] capture(active=0 thisChain=0)`. **Refutes the entry below:**
  capture is not the holder. [3,3,3] appears only once FSR FG has run on the chain (the two earlier resizes of the
  same chain with FG never enabled probed [0,0,0]). Neither dump had heap (CE assert dump stack-only; the user's
  manual dump was `MiniDumpWithDataSegs` only), so the holder is still unnamed. The earlier "081620 resize worked"
  baseline is unproven: no probe existed then and UE logs no ResizeBuffers on success.
- Noted but not causal-proven: every failing run injected early (CE DXGI factory wrapper created at startup, CE
  preloaded `sl.dlss_g.dll`); the one "working" run injected late. The game's and npi `sl.*` DLLs are byte-identical.
- Holder diagnostics: `hook/common/resize_reference_holders.{h,cpp}`. First refused D3D12 resize with foreign refs
  -> reads all committed writable private/image memory via `ReadProcessMemory(self)` (skips WC/NOCACHE/guard/mapped,
  skips its own buffer), logs every slot holding a back-buffer pointer with the nearest preceding code-image pointer
  (vtable -> owning module) or `global:<module>`, then `slots by owner:`. dxgi/d3d12core owners include DXGI's own
  bookkeeping. Next repro: read `ResizeReferenceHolders:` lines.
- **50 s freeze = CE:** UE's crash reporter ran an in-process `MiniDumpWriteDump` (hooked by CE, passed through);
  dbgcore suspended every thread while CE's hook thread was inside Steam's `LoadLibraryExW` hook
  (`RefreshThirdPartyOverlayIdentityCache` -> `GetFileVersionInfoW(System32\d3d9.dll)`, triggered by the dbghelp
  load notification), holding Steam's lock; dbgcore's per-module `GetFileVersionInfoSizeExW` then waited on it.
  Proof: watchdog fired at 50.5 s against a 30 s timeout and the hook thread logged nothing until the same instant.
  Fix: `ModuleReadVersionResource` reads RT_VERSION from the mapping (FindResource/LoadResource); all HMODULE
  version helpers in `dll_utils.h` use it. Side fact: from an unmanifested exe the kernel32 FILE read says 6.2
  (version lie), the mapping says 10.0.
- Follow-up same day: user-confirmed the resize works WITHOUT CE. `logs/20260926_191350` (0.1.6838) and `_192017`
  (overlay hidden + sharpen off): still [3,3,3]. Pointer scan found only raw pointers (47 in a vtable-less 32-byte
  record ring in a private region; driver/runtime tracking), so it cannot identify counted holders. Added
  `hook/common/backbuffer_reference_trace.{h,cpp}`: after every successful D3D12 resize, hooks the buffers' resource
  vtable (QI/AddRef/Release) and the DXGI swapchain vtable GetBuffer (slot 9; nested refs inside GetBuffer belong to
  its caller), tallies per return address for the registered buffers only; a refused resize logs
  `BackBufferRefTrace: bbN <module>+rva acq= rel=` and `net references by module`. Hook DLL is pinned, so the
  patched slots are never left dangling. Next repro: read those lines.
- **Incomplete, see 2026-09-27:** moving CE from slot 13 to the entry of ResizeBuffers still blocked Steam, which patches entries.
- **ROOT CAUSE (logs/20260926_192858, 0.1.6839 trace):** `gameoverlayrenderer64.dll+0x76396 acq=1 rel=0` on each
  real back buffer; AMD FSR, D3D12Core and CE balanced. D3D12 swapchain buffers share ONE refcount across the chain
  (trace baselines `[5,4,3,2,1,0]`), so `[3,3,3]` = 3 refs total = Steam's one per buffer. Steam hooks swapchains
  by rewriting vtable slots (strings: "Hooking vtable for swap chain", `DXGISwapChain_ResizeBuffers`, "points to
  another module, skipping hooks", "clobbering real VTable function from another object, ignoring"); CE's
  `InstallResizeReconciliationHooks` (e07c3222, Strange Brigade flag fix) had claimed slots 13/39 at bootstrap, so
  Steam skipped ResizeBuffers and never released. Fix: reconciliation now inline-patches dxgi's
  `CDXGISwapChain::ResizeBuffers/ResizeBuffers1` bodies (only if the slot points into dxgi.dll) and leaves the
  slots pristine; Steam's slot hook runs first and reaches CE via the original. Rule: **never occupy a DXGI
  swapchain vtable slot a slot-hooking overlay needs** (Present already followed it). Hardware run pending.
- **0x4000 is UE's fatal assertion, not ensure()**: all three recorded 0x4000s preceded `appError ... Fatal error`.
  Renamed `kUe5AssertExceptionCode`; helper scope is now an enum `ExternalDumpScope {kRich,kStacks,kFatalAssert}`;
  `kFatalAssertDumpType` = stacks + handles (mutex owners) + indirectly referenced memory + memory info, no data segs.

### 2026-09-26 - Talos resize fatal while recording under FSR FG: capture tied the back buffers

- **Superseded by the entry above: capture is NOT the holder** (same [3,3,3] with capture unbound).

- Sessions `logs/20260926_083506` and `090625`: resolution change during an FSR FG recording ->
  `SwapChain1->ResizeBuffers` `DXGI_ERROR_INVALID_CALL` -> Unreal appError fatal. The 0.1.6836 probe line:
  `D3D12 resize FAILED ... buffers=3 backBufferRefsHeldByOthers before=[3,3,3] after=[3,3,3]
  capture(active=1 thisChain=1 frames=394 lastCopyAgeMs=745)`. Unreal's own logs show the same resize with FG on
  succeeding in `081620`, where capture was armed but never copied. Capture copying is the delta.
- The two `ok` resizes in `090625` (`[0,0,0]`) ran before the chain's first Present, so they are not a baseline.
- Ruled out: every CE `GetBuffer` is balanced; AMD's proxy releases every real-buffer reference
  (`verifyBackbufferDuplicateResources` keeps only buffer 0 and drops it in `destroyReplacementResources`); AMD's pooled
  lists stay recorded across resize, so a merely recorded list is not the holder; capture copies on the same queue
  as AMD's presenter (`scQueue`); CE hooks no command-list methods; D3D12Core's ECL map is cleared per call. The exact
  holder inside the capture path is still unproven.
- Change: every D3D12 resize path (reconcile claim, full detours including the nested, CE-wrapper and first-resize
  shortcuts, and `CWrapDXGISwapChain`) calls `DX12_ReleaseCaptureForSwapChainResize` before forwarding.
  `SharedCaptureD3D12::ReleaseForSwapChainResize` waits (bounded 1 s) for the capture fence, then `Reset()`s. If media
  still leases frames, it keeps the textures and drops the list, allocators and swapchain binding. Capture
  re-initializes at the new size on the next Present.
- Diagnostics: the resize line adds `afterCaptureRelease=[..]` and `captureRelease(waited timedOut texturesKept
  fence)`. `CaptureFrame` logs `back-buffer references held by others bb= entry= recorded= executed= signaled=`
  for the first 3 copies per generation and whenever a stage adds references. If `afterCaptureRelease` still shows
  holds, the holder is outside capture-owned objects, and the stage line names the call that takes them.
- Same session family: helper "quick assert" dumps are stack-only now (158 MB / 18 s freeze before), 675f1e09.

### 2026-09-26 - Talos FSR FG recording stuck in preparation: capture starved by transparent ECL

- Session `logs/20260926_081620` (Talos Reawakened, FSR FG via app present callback, 0.1.6834): r0002 never went
  live (`Stop accepted as pre-live cancellation ... liveFrames=0`); media published encoder KMT textures, but the
  ring `wIdx` stayed 7023 for 38 s. The hook logged `Capture state changed: ENABLED` and no capture line after
  it; every present logged `cmdLists=0 isReal=0 consReal=0`.
- Cause (since 34f48aff, 2026-09-06): `ShouldTransparentForwardNativeFSRCallbackEcl` forwards ECL and returns
  before `dx12_hook_g_CommandListsExecutedThisFrame` is counted, so ProcessFrame saw `count==0` on every present,
  classified all as interpolated, and `processCapture = !isInterpolatedFrame && ...` never held.
- Fix: `present_association` stages the FFX callback's `isGeneratedFrame` for the Present it precedes on the same
  thread (`ConsumePresentFrameVerdict`, cleared by the next callback and by `Reset()`); both ProcessFrame entry
  points consume it beside the count, and capture uses
  `dx12_overlay_policy::IsApplicationRenderedPresentForCapture` (callback verdict wins when known). Other
  `isInterpolatedFrame` consumers (heuristics, skip policies) are deliberately unchanged.
- Diagnostics: `Present callback verdict decides base capture over the command-list count` (first 5, then every
  3000th). Hardware check pending: Talos FSR FG recording must go live and contain base-rate frames.
- Open: with `capture_include_overlay=false`, callback-route frames already carry the overlay at ProcessFrame
  time, and `captureBeforeOverlay` only runs inside the overlay draw gate. Not addressed here.

### 2026-09-26 - Shared capture transport generation (handle-value reuse)

- Media keyed opened shared textures and the fence only by (source PID, handle value). DX12
  `SharedCaptureD3D12::Initialize` -> `Reset()` closes the old handles right before `CreateSharedHandle` for the
  new ones, so the NT handle table can hand back the same values: media would keep its opened OLD textures
  (stale video) and its OLD fence (new values 1,2,.. read as complete). Same for Vulkan resize (texture cache
  entries destroyed before re-creation) and CaptureBase re-init. Found by code reading; no session proves it.
- Fix (SHARED_MEMORY_VERSION 66, FrameSlot 56 bytes): `SharedMemoryLayout::BeginTransportGeneration()` runs BEFORE
  any handle store; producers stamp `FrameSlot::transportGeneration` (CaptureBase `PublishToSharedMemory`, DX12
  on new capture generation or a mapping whose generation is not ours, Vulkan `LayerIPC_SetTextures/SetFence` +
  `LayerIPC_BeginTransportGeneration` before `encoderTextures.SetFenceHandle`). Media ingest reads
  gen/handles/gen (`common/inject_transport_snapshot.h`) and drops a frame whose stamp is not current (log
  `Dropping frame=... from transport generation`); encoder `MediaEngine_SetInjectTransportGeneration` drops its
  opened textures/fence on change (log `Inject transport generation a -> b`).
- Invariant: only the stamping producer may begin a generation; media must never bump (it would strand every
  later frame of a producer that publishes handles only at init).
