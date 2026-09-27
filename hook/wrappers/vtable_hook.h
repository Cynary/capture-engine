/**
 * VTable Hooking Utility
 *
 * Direct VTable patching - no trampoline-based hooking.
 * Simpler and more compatible with other overlays/hooks.
 */

#pragma once

#include <windows.h>
#include <atomic>

namespace VTableHook {

// Status codes
enum Status {
    Success = 0,
    ErrorAlreadyInitialized,
    ErrorNotInitialized,
    ErrorAlreadyCreated,
    ErrorNotCreated,
    ErrorEnabled,
    ErrorDisabled,
    ErrorNotExecutable,
    ErrorUnsupportedFunction,
    ErrorMemoryAlloc,
    ErrorMemoryProtect,
    ErrorModuleNotFound,
    ErrorFunctionNotFound,
    ErrorPatchFailed,
    ErrorUnknown
};

// Initialize the hooking system
Status Initialize();

// Shutdown the hooking system
Status Shutdown();

/**
 * Create a hook for a virtual function table entry.
 *
 * Uses direct
 * VTable pointer patching - the vtable entry is replaced
 * with the detour
 * function pointer. The original is saved to ppOriginal.
 *
 * @param
 * pVTableEntry Address of the VTable entry (e.g. &vtable[10])
 * @param pDetour
 * Pointer to the detour function
 * @param ppOriginal   [Out] Receives the
 * original function pointer
 */
Status Create(void* pVTableEntry, void* pDetour, void** ppOriginal);

/**
 * Remove a hook and restore the original function.
 *
 * @param
 * pVTableEntry Address of the VTable entry that was hooked
 * @param pOriginal
 * The original function pointer to restore
 */
Status Remove(void* pVTableEntry, void* pOriginal);

/**
 * Hand a CE-owned slot back to CE's recorded predecessor for the duration of a
 * call that may let another module hook the slot (see
 * vtable_hook_policy::ReclaimOutcome). Every HandBack that succeeds must be
 * followed by TakeBack on the same thread. Refuses (ErrorPatchFailed) when a
 * follower hooked above CE, and leaves the slot untouched.
 */
Status HandBack(void* pVTableEntry);

/**
 * Take a slot handed back by HandBack again, chaining CE above whatever the slot
 * now holds. The new predecessor is published to *ppOriginal before the detour
 * is reachable again, and returned in *ppPredecessorOut.
 */
Status TakeBack(void* pVTableEntry, void** ppOriginal, void** ppPredecessorOut);

// Enable a hook (no-op - VTable hooks are always enabled)
Status Enable(void* pTarget);

// Disable a hook (no-op - VTable hooks cannot be temporarily disabled)
Status Disable(void* pTarget);

// Convert status to string
const char* StatusToString(Status status);

}  // namespace VTableHook
