#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "../hook/common/dx12_factory_slot_policy.h"

#include "source_fragment_reader.h"

namespace {

using ce::dx12_factory_slot::HasForeignEntryJump;
using ce::dx12_factory_slot::ShouldInvokeSavedCreateSwapChainForHwndSlot;

std::string ReadSource(const std::filesystem::path& relativePath) {
    return ce::test_source::ReadLogicalSource(std::filesystem::current_path() / relativePath);
}

TEST(Dx12FactorySlotPolicyTest, SavedSlotMayOnlyRunOnObjectsOfItsOwnVtable) {
    void* savedVtable = reinterpret_cast<void*>(0x1000);
    void* foreignVtable = reinterpret_cast<void*>(0x2000);
    void* factoryObject[1] = {savedVtable};

    EXPECT_TRUE(ShouldInvokeSavedCreateSwapChainForHwndSlot(savedVtable, static_cast<const void*>(factoryObject)));
    factoryObject[0] = foreignVtable;
    EXPECT_FALSE(ShouldInvokeSavedCreateSwapChainForHwndSlot(savedVtable, static_cast<const void*>(factoryObject)));
    EXPECT_FALSE(ShouldInvokeSavedCreateSwapChainForHwndSlot(nullptr, static_cast<const void*>(factoryObject)));
    EXPECT_FALSE(ShouldInvokeSavedCreateSwapChainForHwndSlot(savedVtable, nullptr));
    EXPECT_FALSE(ShouldInvokeSavedCreateSwapChainForHwndSlot(nullptr, nullptr));
}

TEST(Dx12FactorySlotPolicyTest, ForeignEntryJumpShapesAreRecognizedOnlyAtEntry) {
    const uint8_t relativeJump[] = {0xE9, 0x00, 0x00, 0x00, 0x00};
    const uint8_t indirectJump[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    const uint8_t plainProlog[] = {0x48, 0x83, 0xEC, 0x28, 0xE8};

    EXPECT_TRUE(HasForeignEntryJump(relativeJump));
    EXPECT_TRUE(HasForeignEntryJump(indirectJump));
    EXPECT_FALSE(HasForeignEntryJump(plainProlog));
    EXPECT_FALSE(HasForeignEntryJump(nullptr));
}

TEST(Dx12FactorySlotPolicyTest, HookInstallerCapturesSavedSlotVtableWithTheSlotValue) {
    const std::string source = ReadSource("hook/apis/dx12_hook_hook_install.cpp");
    ASSERT_FALSE(source.empty());

    const size_t slotSave = source.find("dx12_hook_s_realCreateSCForHwndAddr = realCreateSCForHwndAddr;");
    const size_t vtableSave = source.find("dx12_hook_s_savedCreateSwapChainForHwndVtable = vtable;");
    const size_t slotPatch = source.find("Hooked global CreateSwapChainForHwnd at vtable[15]");
    ASSERT_NE(slotSave, std::string::npos);
    ASSERT_NE(vtableSave, std::string::npos);
    ASSERT_NE(slotPatch, std::string::npos);
    EXPECT_LT(slotSave, vtableSave);
    EXPECT_LT(vtableSave, slotPatch);
}

TEST(Dx12FactorySlotPolicyTest, TempSwapchainBypassesFactoryExportPatchAndGuardsTheRawSlotCall) {
    const std::string source = ReadSource("hook/apis/dx12_hook_hook_install.cpp");
    ASSERT_FALSE(source.empty());

    const size_t exportBypass = source.find(
        "Bypassing foreign entry patch on CreateDXGIFactory1 at %p");
    const size_t factoryCreate =
        source.find("pCreateFactory(IID_PPV_ARGS(&pFactory))", exportBypass);
    const size_t vtableGuard = source.find("ShouldInvokeSavedCreateSwapChainForHwndSlot(", factoryCreate);
    const size_t rawCall = source.find(
        "dx12_hook_oCreateSwapChainForHwndGlobal(pFactory, pQueue, hwnd");
    ASSERT_NE(exportBypass, std::string::npos);
    ASSERT_NE(factoryCreate, std::string::npos);
    ASSERT_NE(vtableGuard, std::string::npos);
    ASSERT_NE(rawCall, std::string::npos);
    EXPECT_LT(exportBypass, factoryCreate);
    EXPECT_LT(factoryCreate, vtableGuard);
    EXPECT_LT(vtableGuard, rawCall);
}

}  // namespace

namespace {

using ce::dx12_factory_slot::kMaxFactorySlotHandbacksWithoutForeignHook;
using ce::dx12_factory_slot::ResolveTempSwapChainFactorySlot;
using ce::dx12_factory_slot::ShouldHandBackFactorySlotsAroundFactoryCreate;

// logs/20260927_023858: CE claimed factory slots 10/15 at 02:39:06.990, the game's
// first factory followed at 07.119, and slot 15 still held CE's detour at 07.547 -
// Steam never hooked it, never tracked the chain, and held one reference on each back
// buffer through every resize. The handback applies exactly while an overlay could
// still install below CE.
TEST(Dx12FactorySlotPolicyTest, FactorySlotsAreHandedBackOnlyWhileAnOverlayCanStillHookBelowCE) {
    EXPECT_TRUE(ShouldHandBackFactorySlotsAroundFactoryCreate(true, true, true, 0));
    EXPECT_FALSE(ShouldHandBackFactorySlotsAroundFactoryCreate(false, true, true, 0)) << "CE owns no slot";
    EXPECT_FALSE(ShouldHandBackFactorySlotsAroundFactoryCreate(true, false, true, 0))
        << "no overlay: nothing would hook the slots";
    EXPECT_FALSE(ShouldHandBackFactorySlotsAroundFactoryCreate(true, true, false, 0))
        << "an overlay already sits below CE";
    EXPECT_TRUE(ShouldHandBackFactorySlotsAroundFactoryCreate(true, true, true,
                                                              kMaxFactorySlotHandbacksWithoutForeignHook - 1));
    EXPECT_FALSE(ShouldHandBackFactorySlotsAroundFactoryCreate(true, true, true,
                                                               kMaxFactorySlotHandbacksWithoutForeignHook))
        << "handbacks that change nothing are bounded";
}

// After a handback CE's predecessor can be Steam's handler; the temp swapchain must still
// resolve CE's own detour to the system function and enter no overlay code.
TEST(Dx12FactorySlotPolicyTest, TempSwapchainResolvesCEsDetourToTheSystemFunctionNotThePredecessor) {
    int systemStorage = 0;
    int detourStorage = 0;
    int overlayStorage = 0;
    const void* systemFunction = &systemStorage;
    const void* detour = &detourStorage;
    const void* overlayHook = &overlayStorage;

    EXPECT_EQ(ResolveTempSwapChainFactorySlot(detour, detour, systemFunction), systemFunction);
    EXPECT_EQ(ResolveTempSwapChainFactorySlot(overlayHook, detour, systemFunction), overlayHook)
        << "a foreign slot owner stays visible so the caller refuses it";
    EXPECT_EQ(ResolveTempSwapChainFactorySlot(systemFunction, detour, systemFunction), systemFunction);
}

// Every CE route that forwards the application's CreateDXGIFactory* hands the slots back
// around the real call, and the install records what it claimed.
TEST(Dx12FactorySlotPolicyTest, ApplicationFactoryCreatesHandTheSlotsBackAroundTheRealCall) {
    const std::string wrappers = ReadSource("hook/wrappers/wrapper_hooks.cpp");
    ASSERT_FALSE(wrappers.empty());
    struct Route {
        const char* function;
        const char* scope;
        const char* call;
    };
    const Route routes[] = {
        {"HRESULT WINAPI Wrapped_CreateDXGIFactory(",
         "DX12FactorySlotHandbackScope factorySlotHandback(\"CreateDXGIFactory\");",
         "hr = createFn(riid, (void**)&pRealFactory);"},
        {"HRESULT WINAPI Wrapped_CreateDXGIFactory1(",
         "DX12FactorySlotHandbackScope factorySlotHandback(\"CreateDXGIFactory1\");",
         "hr = createFn(riid, (void**)&pRealFactory);"},
        {"HRESULT WINAPI Wrapped_CreateDXGIFactory2(",
         "DX12FactorySlotHandbackScope factorySlotHandback(\"CreateDXGIFactory2\");",
         "hr = createFn(Flags, riid, (void**)&pRealFactory);"},
    };
    for (const Route& route : routes) {
        const size_t function = wrappers.find(route.function);
        ASSERT_NE(function, std::string::npos) << route.function;
        const size_t scope = wrappers.find(route.scope, function);
        const size_t call = wrappers.find(route.call, function);
        ASSERT_NE(scope, std::string::npos) << route.function;
        ASSERT_NE(call, std::string::npos) << route.function;
        EXPECT_LT(scope, call) << route.function;
        EXPECT_EQ(wrappers.find("HRESULT WINAPI Wrapped_", function + 1) > call, true) << route.function;
    }

    const std::string install = ReadSource("hook/apis/dx12_hook_hook_install.cpp");
    const size_t claim = install.find("Hooked global CreateSwapChainForHwnd at vtable[15]");
    const size_t note = install.find("DX12_NoteFactorySlotsClaimed(vtable, createSwapChainClaimed, "
                                     "createSwapChainForHwndClaimed);");
    ASSERT_NE(claim, std::string::npos);
    ASSERT_NE(note, std::string::npos);
    EXPECT_LT(claim, note);

    // The unguarded historical temp-swapchain call never runs a foreign predecessor.
    const size_t guard =
        install.find("const bool predecessorIsSystemDxgi = DXGIShared::IsAddressInsideSystemDXGI(");
    const size_t rawCall = install.find("dx12_hook_oCreateSwapChainForHwndGlobal(pFactory, pQueue, hwnd");
    ASSERT_NE(guard, std::string::npos);
    ASSERT_NE(rawCall, std::string::npos);
    EXPECT_LT(guard, rawCall);

    // Reclaim publishes the new predecessor into the storage the detours forward through.
    const std::string handback = ReadSource("hook/apis/dx12_hook_factory_slot_handback.cpp");
    EXPECT_NE(
        handback.find("ReclaimSlot(&vtable[10], reinterpret_cast<void**>(&dx12_hook_oCreateSwapChainGlobal)"),
        std::string::npos);
    EXPECT_NE(handback.find("reinterpret_cast<void**>(&dx12_hook_oCreateSwapChainForHwndGlobal)"),
              std::string::npos);
    EXPECT_NE(handback.find("g_HandbackMutex.try_lock()"), std::string::npos) << "a handback never waits";

    // CE's hidden D3D10/11 probe swapchains keep bypassing an overlay CreateSwapChain handler that a
    // handback placed below CE.
    const std::string create = ReadSource("hook/apis/dx12_hook_swapchain_create.cpp");
    const size_t probe =
        create.find("if (DX12_IsInternalDXGISwapchainProbe()) {\n    HookLog(\"DetourCreateSwapChainGlobal");
    ASSERT_NE(probe, std::string::npos);
    const size_t probeCall = create.find(
        "return DX12_ResolveInternalProbeCreateSwapChain()(pThis, pDevice, pDesc, ppSwapChain);", probe);
    ASSERT_NE(probeCall, std::string::npos);
    EXPECT_LT(probeCall, create.find("HookLog(\"DetourCreateSwapChainGlobal: CALLED", probe));
}

}  // namespace
