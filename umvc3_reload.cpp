// umvc3_reload.cpp
// UMvC3 Live Character File Reloader
// Massive thanks to Gneiss and EternalYoshi for making it possible


#include "utils\addr.h"
#include "utils/MemoryMgr.h"
#include "utils/Trampoline.h"
#include "utils/Patterns.h"
#include <Windows.h>


using namespace Memory::VP;

// addresses 
uint64_t GetsGameUI = 0x140001AC0; // umvc3.exe+1AC0
uint64_t FUN_1402C0540 = 0x1402C0540; // umvc3.exe+2C0540
uint64_t FUN_1403F8700 = 0x1403F8700; // umvc3.exe+3F8700
uint64_t GetBattleSettings = 0x140004700; // umvc3.exe+4700
uint64_t FUN_14024B530 = 0x14024B530; // umvc3.exe+24B530

// Hook site: the CALL at umvc3.exe+2EB336 originally calls umvc3.exe+3AA0
// Hook the call site directly using InjectHook(HookType::Call)
static constexpr uintptr_t HOOK_CALL_SITE = 0x1402EB336;

// naked assembly to exactly mirror the CE script sequence:
__attribute__((naked)) void HookedContinue()
{
    _asm
    {
        sub     rsp, 0x28           // shadow space + alignment

        call    GetsGameUI          // call umvc3.exe+1AC0

        xorps   xmm2, xmm2         // xorps xmm2,xmm2
        xor edx, edx            // xor edx,edx
        mov     rcx, rax            // mov rcx,rax

        call    FUN_1402C0540       // call umvc3.exe+2C0540

        call    GetsGameUI          // call umvc3.exe+1AC0

        mov     dl, 01h             // mov dl,01
        mov     rcx, rax            // mov rcx,rax

        call    FUN_1403F8700       // call umvc3.exe+3F8700

        call    GetBattleSettings   // call umvc3.exe+4700

        mov     rcx, rax            // mov rcx,rax
        call    FUN_14024B530       // call umvc3.exe+24B530 (loadtest)

        add     rsp, 0x28
        ret
    }
}


// Install hook via Trampoline + InjectHook

void OnInitializeHook()
{
    Trampoline* tramp = Trampoline::MakeTrampoline(GetModuleHandle(nullptr));
    InjectHook(HOOK_CALL_SITE, tramp->Jump(HookedContinue), HookType::Call);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)OnInitializeHook, hModule, 0, nullptr);
        break;
    }
    return TRUE;
}
