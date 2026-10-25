// dllmain.cpp NFS Carbon Dynamic FOV 
// Target: Need for Speed Carbon v1.4

#include "pch.h"
#include <windows.h>
#include "DynamicFov.h"

// -----------------------------------------------------------------------------
// DLL entry
// -----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
