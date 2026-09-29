#include "inline_hook.h"
#include "inline_hook_internal.h"
#include "inline_hook_lde.h"
#include "inline_hook_policy.h"
#include "../common/hook_common.h"
#include <cstring>

#ifndef _WIN64
namespace InlineHook {
// The standard x86 frame prologue establishes EBP before the foreign entry
// trampoline returns to the body. Restore the caller's EBP before entering our
// ordinary stdcall detour; its continuation replays the original prologue.
void* InstallDeepHookX86(void* target, void* wrapperFn, TrampolinePublisher publisher, void* publisherContext,
                         int minimumExternalPatchSize, ce::hook_patch::UnstableSnapshotPolicy unstablePolicy) {
    if (!target || !wrapperFn)
        return nullptr;
    std::lock_guard<std::mutex> lock(g_hookMutex);
    for (const auto& hook : g_deepHooks) {
        if (hook.target == target && hook.installed) {
            // Match the x64 contract: a claim for this address does not prove
            // that it belongs to this requested detour.
            return nullptr;
        }
    }
    uint8_t pristine[64]{};
    if (!ReadOrigBytesFromDisk(target, pristine, sizeof(pristine), nullptr))
        return nullptr;
    auto* code = static_cast<uint8_t*>(target);
    const int resume = ce::inline_hook_policy::X86DeepHookFramePrologueLength(pristine, sizeof(pristine));
    // Never overwrite another hook's entry bytes. Other prologue shapes need
    // their own verified unwind rule; guessing would corrupt the caller stack.
    if (resume < minimumExternalPatchSize || resume < 5 ||
        std::memcmp(code + resume, pristine + resume, 8) != 0) {
        HookLogImportant("DeepHook x86: refusing unrecognized or occupied prologue at %p", target);
        return nullptr;
    }
    int displaced = 0;
    while (displaced < 6) {  // pop ebp; jmp rel32
        const int length = GetInstructionLength(pristine + resume + displaced, false);
        if (length <= 0 || resume + displaced + length > 48)
            return nullptr;
        const uint8_t opcode = pristine[resume + displaced];
        // No relative control transfers are expected in this part of a frame
        // prologue. Refuse them rather than relocating an unverified branch.
        if ((opcode >= 0x70 && opcode <= 0x7f) || (opcode >= 0xe0 && opcode <= 0xeb) ||
            (opcode == 0x0f && (pristine[resume + displaced + 1] & 0xf0) == 0x80))
            return nullptr;
        displaced += length;
    }
    if (std::memcmp(code + resume, pristine + resume, displaced) != 0)
        return nullptr;
    auto* trampoline = AllocateWritableTrampolinePage(nullptr);
    if (!trampoline)
        return nullptr;
    const int copied = resume + displaced;
    std::memcpy(trampoline, pristine, copied);
    WriteJump(trampoline + copied, code + copied);
    if (!FinalizeExecutableTrampoline(trampoline, TRAMPOLINE_POOL_SIZE, trampoline, copied + 5)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return nullptr;
    }
    DeepHookEntry entry{};
    entry.target = target;
    entry.hookAddr = code + resume;
    entry.patchSize = displaced;
    entry.trampoline = trampoline;
    std::memcpy(entry.origBytes, code + resume, displaced);
    std::memset(entry.installedBytes, 0x90, displaced);
    entry.installedBytes[0] = 0x5d;  // pop ebp
    entry.installedBytes[1] = 0xe9;
    const uint32_t jump = reinterpret_cast<uintptr_t>(wrapperFn) -
                          reinterpret_cast<uintptr_t>(code + resume + 6);
    std::memcpy(entry.installedBytes + 2, &jump, sizeof(jump));
    try {
        g_deepHooks.push_back(entry);
    } catch (...) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return nullptr;
    }
    if (publisher)
        publisher(trampoline, publisherContext);
    bool installed = false;
    {
        ce::hook_patch::ThreadQuiescence quiescence(code + resume, displaced, unstablePolicy);
        SetLastDeepHookQuiesceFailure(quiescence.FailureReason());
        DWORD protection = 0;
        if (quiescence.IsReady() && std::memcmp(code + resume, entry.origBytes, displaced) == 0 &&
            VirtualProtect(code + resume, displaced, PAGE_EXECUTE_READWRITE, &protection)) {
            std::memcpy(code + resume, entry.installedBytes, displaced);
            DWORD ignored = 0;
            VirtualProtect(code + resume, displaced, protection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), code + resume, displaced);
            installed = true;
        }
    }
    if (!installed) {
        if (publisher)
            publisher(nullptr, publisherContext);
        else
            VirtualFree(trampoline, 0, MEM_RELEASE);
        g_deepHooks.pop_back();
        return nullptr;
    }
    g_deepHooks.back().installed = true;
    HookLogImportant("DeepHook x86: installed below entry at %p+%d continuation=%p", target, resume, trampoline);
    return trampoline;
}
}
#endif
