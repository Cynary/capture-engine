# llm-wiki Log

### 2026-09-27 - Talos resize fatal is CE's factory-slot claim hiding the swapchain from Steam

- Session `logs/20260927_023858` (0.1.6840, **no FG**; user: also fails without FG, never with the Steam
  overlay disabled): resize `FAILED ... before=[6,6,6,6,6,6]`, trace `gameoverlayrenderer64.dll=+6`
  (`+0x76396 acq=1 rel=0` per buffer), all other modules balanced. The 0.1.6840 ResizeBuffers-body fix was
  necessary but not sufficient.
- Root cause: CE `VTableHook::Create`d factory slots 10/15 at 02:39:06.990 (original = pristine dxgi); the game's
  first factory came at 07.119, and at 07.547 slot 15 still held CE's detour, i.e. Steam never chained in.
  Without CE Steam logs `IWrapDXGIFactory::CreateSwapChain called -> Hooking vtable for swap chain -> Tracking
  new swap chain` and `Releasing all resources for swapchain/device` on every resize: its resize release depends
  on having seen the creation through its own factory slot hook. sl.dlss_g creates the chain via slot 10.
- The user's "with CE" Steam log was CrashReportClient's (Steam's log is rewritten per process); it proves nothing.
- Fix: `DX12FactorySlotHandbackScope` around `createFn` in `Wrapped_CreateDXGIFactory/1/2` (see the coexistence
  page). Closed side paths: `CreateTempSwapChainViaFactorySlot` resolves CE's detour to
  `dx12_hook_s_realCreateSCForHwndAddr`, the historical raw temp call refuses a foreign predecessor, D3D10/11
  probes use `DX12_ResolveInternalProbeCreateSwapChain`. `BackBufferRefTrace` no longer writes swapchain slot 9.
- New diagnostics: `DX12 factory slot handback #N`, `... taken back above <module+rva> (another module hooked it
  ...)`, and `<source>: swapchain vtable ... slot owners` (Present/GetBuffer/ResizeBuffers/Present1/
  ResizeBuffers1 owner + `entryJump->`) on every refused resize and the first two resizes.
- Next repro: expect `taken back above gameoverlayrenderer64.dll+...` for both slots. If the handback logs
  `unchanged` Steam's hook did not run inside CE's forwarded factory create (CE calls the on-disk export RVA;
  Steam's CreateDXGIFactory1 patch is an entry patch, so it should). If resize still fails with slots owned by
  Steam, check whether `[13]ResizeBuffers` shows Steam or `entryJump->capture_hook` (Steam following entry jumps).

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
- **Incomplete, see 2026-09-27:** the slot-13 claim was one of two; CE's factory slots 10/15 hid the chain from Steam.
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
