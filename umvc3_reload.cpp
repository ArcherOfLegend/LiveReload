// UMvC3 Live Character File Reloader
// Made by Archer, with thanks to Gneiss for doing most of the heavy lifting <3

#include <Windows.h>
#include <cstdint>
#include <cstdio>

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

// MinHook trampoline
static FnOriginal  pfnTrampoline = nullptr;

void __fastcall HookedContinue()
{
    // get an object/context (first call)
    void* obj = pfnInitObj();

    // reset match state with obj, rdx=0
    pfnReset(obj, 0);

    // get fresh object/context (second call)
    obj = pfnInitObj();

    // trigger character load with obj, dl=1
    pfnCharLoad(obj, 1);

    // secondary init (rax/obj still valid from step 3)
    pfnInit2(obj);

    // reload character files ("loadtest" → umvc3.exe+24B530)
    pfnFileLoad(obj);

    // execution resumes at umvc3.exe+2EB5AF naturally because MinHook
    // only replaces the call at 2EB336, the code after it continues as normal.
    // If you find the game hangs after this, uncomment the trampoline call:
    // pfnTrampoline();
}


void __fastcall HookedOriginal()
{
    HookedContinue();
    // Do NOT call trampoline cuase I intentionally skipped the original 3AA0 logic
    // and replace it entirely with the reload sequence.
}

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
    if (MH_CreateHook(pfnOriginal, &HookedOriginal, reinterpret_cast<void**>(&pfnTrampoline)) != MH_OK) {
        MessageBoxA(nullptr, "Failed to create hook.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    if (MH_EnableHook(pfnOriginal) != MH_OK) {
        MessageBoxA(nullptr, "Failed to enable hook.", "UMvC3 Reloader", MB_ICONERROR);
        return;
    }

    // Optionally log to a file for debugging
    FILE* f = fopen("umvc3_reload.log", "w");
    fprintf(f, "UMvC3 Reloader installed. Base: 0x%llX\n", base);
    fclose(f);
}

static void Uninstall()
{
    MH_DisableHook(pfnOriginal);
    MH_RemoveHook(pfnOriginal);
    MH_Uninitialize();
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // Run on a thread so the loader isn't blocked
		// This is to ensure the game has loaded before we try to hook

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
