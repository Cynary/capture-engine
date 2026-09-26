#pragma once

#include "dx12_hook_internal.h"

#include "../common/present_callback_association.h"

namespace ce::dx12_ecl_forward {

extern thread_local int recursionDepth;

ExecuteCommandListsPtr ResolveRecursionBreakTarget(ID3D12CommandQueue* queue);
void TransparentNativeFSRCallback(ID3D12CommandQueue* queue, UINT numCommandLists,
                                  ID3D12CommandList* const* commandLists);

// Whether this Present carries an application-rendered frame for base capture,
// from the command lists counted since the previous Present and the verdict the
// present callback staged for it (dx12_overlay_policy::IsApplicationRenderedPresentForCapture).
// Logs, rate-limited, whenever the callback overrules the count.
bool IsApplicationRenderedPresentForCapture(int eclSubmissionCount,
                                            const present_association::PresentFrameVerdict& verdict);

}  // namespace ce::dx12_ecl_forward
