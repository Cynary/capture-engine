# D3D11 capture resource lifetime

Last verified: 2026-09-28.

## Ownership

`DX11Capture::Init` in `hook/apis/dx11_hook_capture_init.cpp` records the capture
thread when a device has `D3D11_CREATE_DEVICE_SINGLETHREADED` (or the equivalent
D3D10 flag). `Cleanup` transfers its owned references to `DeferredReleaseQueue`
with that thread ID. This includes textures, queries, fences and context refs.
It does not query the device from the cleanup worker.

`DeferredReleaseQueue::Process` releases only unrestricted entries. Present,
Present1 and CaptureFrame drain entries belonging to the calling thread through
`ProcessThreadAffine`. The latter covers wrapper/shared DXGI paths that bypass
the API-specific detour. COM Release runs outside the queue mutex so re-entry
can enqueue more work. An empty affine queue costs an atomic read on Present.

The shutdown flag also covers consumer dormancy, so thread-affine retirement
must happen before the present detour's dormant return. This retirement owns
its references independently of the disconnected shared-memory session. If the
owner never presents again, its entries stay retained until process exit;
releasing them on an arbitrary worker is not a safe fallback.

## Observed failure and tests

Overcooked 2 is a 32-bit D3D11 game whose device flags were 0x1. Two candidate
reconnect runs stalled after 14–15 frames. Native stacks placed the hook worker
inside resource destruction in the NVIDIA D3D11 driver, with the game render
thread waiting on the D3D11 device lock. A verbose release trace perturbed the
failure, so that trace was removed before testing the fix.

Four native mock-COM tests cover worker rejection, owner-only release, release
re-entry and process-exit abandonment. Removing ownership from queued entries
makes the regression suite fail. A source-wiring test checks capture cleanup
and the wrapper-path drain; full unit tests passed after the product build.
The corrected binary passed initial capture and three reconnects of the same
game process, each exceeding 1,200 frames, then input-driven menu exit and
return to desktop capture on the existing stream.

The streaming host also needs orderly helper shutdown. Closing its control pipe
and waiting before forced termination prevented the fence-removal errors caused
by killing a helper mid-capture. That change alone did not fix the single-threaded
resource-release stall. These are separate lifecycle fixes.

Full static verification passed for build 0.1.31, including native units, Python
tooling tests, x64 ASan/UBSan and warning limits. Existing advisory formatting
and compiler warnings remain; x86 sanitizers are unavailable in this toolchain.
D3D12 HDR/FrameGen regression on the final binaries remains outstanding. These
D3D11 runs do not prove that path.
