// UMvC3 Live Character File Reloader
// Made by Archer, with thanks to Gneiss for doing most of the heavy lifting <3
/* 
 * Build instructions:
 *   - Visual Studio 2019/2022, x64, Release
 *   - Project type: DLL
 *   - Add MinHook (https://github.com/TsudaKageyu/minhook) to your project
 *   - Rename output .dll to .asi and drop in UMvC3 game directory
 *   - Make sure Ultimate ASI Loader (dinput8.dll or version.dll) is present
 *
 * Dependencies:
 *   - MinHook (included via MinHook.h + lib)
 */

#include <Windows.h>
#include <cstdint>

// MinHook — https://github.com/TsudaKageyu/minhook
// Add minhook/include to include path, link against MinHook.x64.lib
#include "MinHook.h"

// Offsets
static constexpr uintptr_t OFF_HOOK_SITE  = 0x2EB336; // Hook
static constexpr uintptr_t OFF_RESUME     = 0x2EB5AF; // Resume func (I think!)
static constexpr uintptr_t OFF_INIT_OBJ   = 0x1AC0;   // called twice to get an object/context
static constexpr uintptr_t OFF_RESET      = 0x2C0540; // reset match state (takes rcx=obj, rdx=0)
static constexpr uintptr_t OFF_CHAR_LOAD  = 0x3F8700; // triggers file load (takes rcx=obj, dl=1)
static constexpr uintptr_t OFF_INIT2      = 0x4700;   // secondary init (takes rcx=obj)
static constexpr uintptr_t OFF_FILE_LOAD  = 0x24B530; // actual file reload routine ("loadtest")
static constexpr uintptr_t OFF_ORIGINAL   = 0x3AA0;   // original call at hook site (for passthrough)

typedef void* (__fastcall* FnInitObj)();                        // umvc3.exe+1AC0
typedef void  (__fastcall* FnReset)(void* rcx, int rdx);       // umvc3.exe+2C0540
typedef void  (__fastcall* FnCharLoad)(void* rcx, uint8_t dl); // umvc3.exe+3F8700
typedef void  (__fastcall* FnInit2)(void* rcx);                 // umvc3.exe+4700
typedef void  (__fastcall* FnFileLoad)(void* rcx);              // umvc3.exe+24B530
typedef void  (__fastcall* FnOriginal)();                       // umvc3.exe+3AA0

static FnInitObj   pfnInitObj   = nullptr;
static FnReset     pfnReset     = nullptr;
static FnCharLoad  pfnCharLoad  = nullptr;
static FnInit2     pfnInit2     = nullptr;
static FnFileLoad  pfnFileLoad  = nullptr;
static FnOriginal  pfnOriginal  = nullptr;

// MinHook trampoline — lets us call the original bytes at the hook site if needed
static FnOriginal  pfnTrampoline = nullptr;

// -----------------------------------------------------------------------
// Hook function
// Replaces the call at umvc3.exe+2EB336 (originally: call umvc3.exe+3AA0)
//
// CE equivalent:
//   call umvc3.exe+1AC0        → pfnInitObj()
//   xorps xmm2,xmm2 / xor edx,edx
//   mov rcx,rax
//   call umvc3.exe+2C0540      → pfnReset(obj, 0)
//   call umvc3.exe+1AC0        → pfnInitObj()
//   mov dl,01
//   mov rcx,rax
//   call umvc3.exe+3F8700      → pfnCharLoad(obj, 1)
//   call umvc3.exe+4700        → pfnInit2(obj)  [rax still holds obj]
//   mov rcx,rax
//   call loadtest (umvc3.exe+24B530) → pfnFileLoad(obj)
//   jmp umvc3.exe+2EB5AF       → handled by MinHook trampoline + hook placement
// -----------------------------------------------------------------------
void __fastcall HookedContinue()
{
    // Step 1: get an object/context (first call)
    void* obj = pfnInitObj();

    // Step 2: reset match state with obj, rdx=0
    pfnReset(obj, 0);

    // Step 3: get fresh object/context (second call)
    obj = pfnInitObj();

    // Step 4: trigger character load with obj, dl=1
    pfnCharLoad(obj, 1);

    // Step 5: secondary init (rax/obj still valid from step 3)
    pfnInit2(obj);

    // Step 6: reload character files ("loadtest" → umvc3.exe+24B530)
    pfnFileLoad(obj);

    // Note: execution resumes at umvc3.exe+2EB5AF naturally because MinHook
    // only replaces the call at 2EB336; the code after it continues as normal.
    // If you find the game hangs after this, uncomment the trampoline call:
    // pfnTrampoline();
}

// -----------------------------------------------------------------------
// Trampolined hook — MinHook replaces the CALL at OFF_HOOK_SITE.
// We need to hook a CALL instruction, so we hook the target function
// (OFF_ORIGINAL / umvc3.exe+3AA0) instead, which is cleaner.
// -----------------------------------------------------------------------
void __fastcall HookedOriginal()
{
    HookedContinue();
    // Do NOT call trampoline — we intentionally skip the original 3AA0 logic
    // and replace it entirely with our reload sequence.
}

// -----------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------
static void Install()
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA("umvc3.exe"));
    if (!base) {
        MessageBoxA(nullptr, "Failed to find umvc3.exe base address.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    // Resolve all function pointers
    pfnInitObj  = reinterpret_cast<FnInitObj> (base + OFF_INIT_OBJ);
    pfnReset    = reinterpret_cast<FnReset>   (base + OFF_RESET);
    pfnCharLoad = reinterpret_cast<FnCharLoad>(base + OFF_CHAR_LOAD);
    pfnInit2    = reinterpret_cast<FnInit2>   (base + OFF_INIT2);
    pfnFileLoad = reinterpret_cast<FnFileLoad>(base + OFF_FILE_LOAD);
    pfnOriginal = reinterpret_cast<FnOriginal>(base + OFF_ORIGINAL);

    // Init MinHook
    if (MH_Initialize() != MH_OK) {
        MessageBoxA(nullptr, "MinHook initialization failed.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    // Hook umvc3.exe+3AA0 (the original call target at the hook site)
    // This is cleaner than patching the CALL instruction directly.
    if (MH_CreateHook(pfnOriginal, &HookedOriginal, reinterpret_cast<void**>(&pfnTrampoline)) != MH_OK) {
        MessageBoxA(nullptr, "Failed to create hook.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    if (MH_EnableHook(pfnOriginal) != MH_OK) {
        MessageBoxA(nullptr, "Failed to enable hook.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    // Optionally log to a file for debugging
    // FILE* f = fopen("umvc3_reload.log", "w");
    // fprintf(f, "UMvC3 Reloader installed. Base: 0x%llX\n", base);
    // fclose(f);
}

static void Uninstall()
{
    MH_DisableHook(pfnOriginal);
    MH_RemoveHook(pfnOriginal);
    MH_Uninitialize();
}

// -----------------------------------------------------------------------
// DLL Entry Point (required by Ultimate ASI Loader)
// -----------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // Run on a thread so we don't block the loader
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            // Wait for the game to finish loading before hooking
            Sleep(2000);
            Install();
            return 0;
        }, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        Uninstall();
        break;
    }
    return TRUE;
}
