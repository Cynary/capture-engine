# llm-wiki Log

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

### 2026-09-26 - Inject capture froze after alt-tab: fence reserve pinned the last ring lease

- Session `logs/20260926_050614` (DOOM Eternal Vulkan, 0.1.6832): alt-tab at 05:08:00.6, game stopped
  presenting; refocus at 05:08:25.8 recreated the swapchain (same handle). Media `wIdx` stuck at 3849 to the
  end; `IBuf=1`, `Duplicate frame=3847` every second, `holdWithCandidate=0`. The layer called
  `InitializeCapture` on every present (10k times) and returned silently.
- Deadlock: media's live CFR selector always withholds the newest buffered frame as "GPU/fence reserve"
  (`GetMinBufferedInjectFrames` >= 1), so a stalled source leaves frame 3848 buffered with its ring lease.
  The Vulkan layer (like DX11/DX9 `HasOutstandingCaptureFrameLeases`) retires a swapchain generation only
  when `frameRing.readIndex == writeIndex`. Producer waits for the lease, media waits for a newer frame.
- Fix: `ReleaseSettledInjectTailFrames` (source_state.h, unit-tested) runs only when no candidate is
  selectable and releases the oldest reserve frames whose copy is complete or unknown (unknown goes to the
  encoder's own non-blocking fence check); provably pending frames stay protected. Completion comes from new
  `MediaEngine_QueryInjectFrameCopyCompletion`, answered from the encoder's cached fence only.
- Diagnostics: `Fence reserve released ...` (rate-limited), `ReserveReleaseTicks=` at the end of
  `[Inject CFR QUALITY SUMMARY]`, and the layer now logs `Deferring capture for swapchain ... until retired
  generation ... drains (ringRead ringWrite copiesComplete)`.
- Hardware check pending: alt-tab out/in during a DOOM recording; video must resume, no deferral flood.
  Stale-risk: the layer's `InitializeCapture(...)`/contract lines still log on every deferred retry.

### 2026-09-26 - Vulkan registration repair deleted both live HKCU entries on every start

- `HKCU\Software` is shared between WOW64 views; `RepairOwnedRegistrations` pruned HKCU/64 and HKCU/32 as
  separate keys, each retaining only its own architecture, so each pass deleted the other's live entry.
  Log proof (`logs/20260926_044427`): both removals at .710, rewrites at .728 after staging.
- Fix: `BuildRepairScopes` (public, unit-tested) — one HKCU scope (view Default, retains both), HKLM 64/32
  per architecture when elevated. OS premise probed by a test. Hardware check: next CE start should log
  `Owned-entry repair HKCU/shared: retaining 2 ..., pruning 0` and no `Removed superseded` lines.

### 2026-09-26 - Degraded completions name the track (audio vs video)

- Every degraded save said "video degraded": mediaengine folded audio causes (lost device, content holes,
  overrun loss, dead worker) into one `lastOutputDegraded` bool, which `CompleteRecordingFinalization` mapped to
  `kRecordingHealthFlagVideoDegraded`.
- Now `MediaEngine_GetLastOutputDegradedFlags` (replaces `MediaEngine_WasLastOutputDegraded`) returns video 0x10 /
  new audio 0x40 (`kRecordingHealthFlagAudioDegraded`, latched, never drives the live warning). Manifest gains
  `recording_degraded=`, the finalization log `degraded=`, mediaengine logs `[OutputHealth] ... scope=`.
- `OverlayNotificationType` 11-14 (saved/stream-ended x audio/audio+video); 5/9 keep meaning video.
  SHARED_MEMORY_VERSION 64->65 (layout unchanged, but an old hook would drop 11-14 silently).
- Texts in one table (`common/output_completion_notification.h`) for hook and pseudo overlay; hook width list
  iterates it. Found in passing: the hook's idle-only check was a numeric range 3..10 that would have let 11-14
  cover an active recording; now `IsRecordingFinalizationNotification`.
- Hardware check pending: an audio-only loss should show "Recording saved - audio degraded" in both overlays.

### 2026-09-26 - Session 20260926_041008: "degraded" was a driver re-delivery, not loss or overload

- 4 min DXGI-dup recording (HotS), 0.1.6828 (includes 6c102592). Overlay: "Recording saved - video degraded",
  `flags=0x10 cause=none`, debt 0, `backpressure=0 skipped=0`, CFR coverage complete, enc ~0.2 ms: NOT
  encoder overload. Sole trigger: `overrunLostSamples=480` on src 0 (192 kHz system loopback).
- Mechanism: loopback resumed after ~25 s idle with 10 out-of-domain QPCs (6c102592 chained them correctly,
  `contiguous=1`), but the engine delivered devPos 16275840 TWICE, the second with DATA_DISCONTINUITY. The
  repeat broke contiguity, re-anchored 10 ms early and was fully overlap-trimmed ~19 ms behind the write
  cursor, i.e. ~300 ms AHEAD of the exported cursor. Timeline confirms it was extra content: the chain end
  met the first valid QPC within 0.8 ms; keeping it would have forced a ~9 ms overlap trim there instead.
- Bug: `ServiceSourceIngestStarvation` counted every fully destroyed packet as consumer overrun, violating
  the `RecordingAudioLossEvidence` contract. Fix: `SplitFullyOverlappedPacket` — only the part behind the
  exported cursor is overrun/starvation (and can drive the last-resort resync); the rest is `dedup=` in
  `[STOP AUDIO INGEST]` plus a capped `Re-delivered source range de-duplicated` line. Analyzer regex accepts it.
- Follow-up done same day: audio loss now has its own bit and text (next entry).

### 2026-09-26 - Session 20260926_030958: rejected-timestamp burst loss, drain flapping, pre-live slow-loop noise

- 4 min DXGI-dup recording (HotS), 0.1.6826. Late-join fix verified (4 joins, `preservedGap`~15700,
  `writeMinusEncoded ~ ringAvail`, `compDelta=0`). Overlay said degraded: `flags=0x10 cause=none`, only
  `overrunLostSamples=480` (10 ms, system loopback). Not capacity: debt 0, no pressure flags.
- **Loss**: loopback resumed after ~27 s idle; the 192 kHz driver stamped 6 packets 27.5 s in the past (stale
  base + devPos). `SanitizeCaptureQpcPosition` replaced each with read time; 3 drained in one burst got the
  same instant and overlapped. Fix: `CaptureQpcSubstitution` (read time minus duration, never before the
  previous contiguous rejected packet, capped at the future tolerance); both system and app capture loops.
  Exact true timing is unknowable; this only guarantees CE never collapses contiguous audio.
- **Drain flapping**: app buffer steady ~335 ms, target alternated 270/330 ms at ~3 Hz (screen-content lag
  jitter while source-starved) -> 715 AppDrain transitions, up to 0.5% speed-up. The drain's bandwidth is its
  10 s window (0.5% = 5 ms/s), so it now uses `TrailingPeakHold` over that window (two half-window buckets,
  source encoded samples as clock, reset with the drain state). Tier-1 unchanged.
- Slow-loop line fired twice pre-live (`dominant=startup`, 200 ms, cpu 0) = encoder startup; now INFO pre-live.
- Also: 35% duplicates were source-limited (static screen), not CE.
- Hardware runs pending: a recording across silence->sound transitions (`starve=0`, `contiguous=1` lines),
  and AppDrain transition count on loading screens.

### 2026-09-26 - Session 20260926_012955 review: late app join early audio, encoder QoS scope, loop stage cost

- Session: 5 DXGI-dup 4K120 AV1 recordings (HotS, Fortnite), longest r0002 34.5 min. All healthy: sample-exact
  track lengths, complete CFR coverage, no trims/underruns. Two real defects plus one diagnostics gap found.
- **Late app join discarded the CFR content-delay lead** (`ComputeLateAppSourceJoin`, 2026-06-14 design that
  predates the active-delay reservoir). Fortnite joined r0002 at 28 min with `packetStart` 15675 samples ahead of
  the track cursor; the join moved `qpcAlignedWrittenSamples` to `packetStart-480` without writing silence, so the
  FIFO ring encoded it ~317 ms early (`[AppDiag] place writeMinusEncoded~15000` vs `ringAvail~500`,
  appAudioDelay avg 68 ms vs target ~350). Tier-1 read the deficit as drift and pinned `compDelta=-240`
  (-500 ppm, `sat=1`). r0003-r0005 (Fortnite already running at start) were normal. Fix: join cursor never
  passes the live edge; the regular placement writes the lead as silence. `qjoin` now counts the absence skipped
  from the source's pre-stitch write cursor (analyzer backlog rule keys on `qjoin>0`), `qjoinKeep` the lead.
- **Stale tier-1 after an app went quiet**: HotS kept `compDelta=-240` for 7 min after closing because the drift
  update is skipped below `kMinCompensationBufferSamples` and only unexpected underruns cleared it. Expected
  timeline-silence padding now clears it (`ShouldClearRateCompensationForExpectedSilence`, logs once).
- **Encoder thread MMCSS was reverted immediately**: `ScopedMmcssTask` lived in `MediaEncoderSession::Init()`
  since the 2026-08-05 session split, so every loop since ran without "Pro Audio" while logging "Thread QoS
  enabled". Moved to `EncoderThreadFunc`; source test guards it. Not proven to be the stall cause.
- **Unexplained encoder stalls** (r0002 02:03:07-16 when Fortnite took focus at 85% CPU, r0004 02:28:03 at ~30%
  CPU): `Timer skip-ahead` 150-620 ms, `Catchup budget exceeded ... elapsed=174ms`, capture delivery gap 164 ms at
  the same instants, encode EMA 1-4 ms; CFR dropped visual debt (peak 1492 ms) = visible stutter, audio sync
  held. New `[EncoderThread] Slow loop iteration` line (phase breakdown + thread CPU time) should name the phase
  on the next run. Open: blocked vs preempted, and whether the MMCSS fix alone removes it.
- Hardware runs pending: late-joining app (start a game mid-recording): `qjoinKeep` ~ content delay,
  `writeMinusEncoded ~ ringAvail`, `compDelta=0`; and any `Slow loop iteration` lines.
