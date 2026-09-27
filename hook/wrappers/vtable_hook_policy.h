#pragma once

namespace ce::vtable_hook_policy {

inline bool ShouldReclaimRestoredSlot(const void* current, const void* detour, const void* predecessor) {
    return predecessor && current != detour && current == predecessor;
}

inline bool ShouldPreserveForeignFollower(const void* current, const void* detour, const void* predecessor) {
    return predecessor && current != detour && current != predecessor;
}

// Whether the pointer CE is about to keep as "the original" is code from
// outside the module that owns the vtable - that is, another injector's detour
// rather than the implementation the vtable shipped with.
//
// This is the precondition for the cycle that froze Gothic II in session
// 20260916_011148: CE and Steam's gameoverlayrenderer each hooked
// IDirectDrawSurface7's Flip slot, and installed in that order each one's
// saved original was the other's detour. Nothing here prevents the second
// installer from doing that - CE does not control another process-wide
// injector - but CE knows at its own install time that it is chaining into
// foreign code, which is the one fact the recursion itself cannot report. The
// caller logs it, with the module named, so a future occurrence is attributable
// without a dump of the hung process.
//
// A vtable that does not live inside a module image (a wrapper object's
// heap-allocated vtable) has no owning module to compare against, so nothing is
// claimed for it.
inline bool SavedOriginalIsForeignChain(const void* entryModule, const void* vtableModule, const void* selfModule) {
    return entryModule != nullptr && vtableModule != nullptr && entryModule != vtableModule &&
           entryModule != selfModule;
}

// What a slot CE handed back to its predecessor (VTableHook::HandBack) holds when
// CE takes it again (VTableHook::TakeBack).
//
// The handback exists for slot-hooking overlays that refuse a slot pointing
// into another module: while CE's detour sits in the slot they never install
// their own hook there. Talos Reawakened (logs/20260927_023858): CE claimed the
// DXGI factory's CreateSwapChain slots before the game's first factory, the
// Steam overlay therefore never saw the swapchain being created, never hooked
// its ResizeBuffers, never released its back-buffer references, and every
// resolution change was refused. Handing the slot back while the game's factory
// is created lets such an overlay hook it; CE then chains above whatever the
// slot holds.
enum class ReclaimOutcome {
    kPredecessorUnchanged,  // nobody hooked meanwhile: CE's predecessor stays what it was
    kForeignHookInstalled,  // another module hooked the slot meanwhile: it becomes CE's predecessor
    kAlreadyOwned,          // the slot already holds CE's detour: nothing to take back
};

inline ReclaimOutcome ClassifyReclaim(const void* current, const void* detour, const void* yieldedTo) {
    if (current == detour) {
        return ReclaimOutcome::kAlreadyOwned;
    }
    return current == yieldedTo ? ReclaimOutcome::kPredecessorUnchanged : ReclaimOutcome::kForeignHookInstalled;
}

// A slot can only be handed back from CE's own detour to CE's recorded
// predecessor. Anything else in the slot means a follower hooked above CE; it
// still reaches CE through its saved original, and taking the slot from it
// would unhook it.
inline bool CanYieldSlot(const void* current, const void* detour, const void* predecessor) {
    return predecessor != nullptr && detour != nullptr && current == detour;
}

}  // namespace ce::vtable_hook_policy
