// dllmain.cpp
#include "pch.h"
#include <windows.h>
#include <stdint.h>
#include <cmath>

int toggleKey = 0;

// configurable default values
uint16_t cfg_initial_fov = 15000;
uint16_t cfg_max_fov = 24000;
float    cfg_max_speed = 80.0f;
int      cfg_graph_type = 1;

//patch original writer address
BYTE originalBytes[7] = { 0x66, 0x89, 0x81, 0xE4, 0x00, 0x00, 0x00 };
BYTE nopBytes[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

uintptr_t patchAddr = 0x00492E3A;
bool patched = false;

void PatchBytes(bool enable)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)patchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect);

    if (enable)
        memcpy((void*)patchAddr, nopBytes, 7);
    else
        memcpy((void*)patchAddr, originalBytes, 7);

    VirtualProtect((LPVOID)patchAddr, 7, oldProtect, &oldProtect);
}

void LoadConfig()
{
    // hotkey
    toggleKey = GetPrivateProfileIntA(
        "hotkeys",
        "toggle_fov",
        118,
        ".\\DFconfig.ini"
    );

    // fov settings
    cfg_initial_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "initial_fov",
        15000,
        ".\\DFconfig.ini"
    );

    cfg_max_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "max_fov",
        24000,
        ".\\DFconfig.ini"
    );

    cfg_graph_type = GetPrivateProfileIntA(
        "settings",
        "graph_type",
        1,
        ".\\DFconfig.ini"
    );

    // speed limit
    cfg_max_speed = (float)GetPrivateProfileIntA(
        "speed",
        "max_speed",
        80,
        ".\\DFconfig.ini"
    );
}

bool effectEnabled = false;

float ApplyGraph(float t)
{
    // clamp t 0..1
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (cfg_graph_type)
    {
    case 0:
        // linear
        return t;

    case 1:
        // cubic in
        return t * t * t;

    case 2:
        // cubic out
        return 1.0f - powf(1.0f - t, 3.0f);

    default:
        return t;
    }
}

DWORD WINAPI FovThread(void*)
{
    volatile float* speed = (float*)0x00A8E178;
    volatile uint16_t* fov = (uint16_t*)0x00B1D604;

    while (true)
    {
        if (effectEnabled)
        {
            float s = *speed;
            if (s < 0) s = 0;
            if (s > cfg_max_speed) s = cfg_max_speed;

            float t = s / cfg_max_speed;

            float eased = ApplyGraph(t);

            float f = cfg_initial_fov + (cfg_max_fov - cfg_initial_fov) * eased;

            if (f > cfg_max_fov) f = (float)cfg_max_fov;
            if (f < cfg_initial_fov) f = (float)cfg_initial_fov;

            *fov = (uint16_t)f;
        }

        SwitchToThread();
    }
}

DWORD WINAPI HotkeyThread(void*)
{
    LoadConfig();

    while (true)
    {
        if (GetAsyncKeyState(toggleKey) & 1)
        {
            effectEnabled = !effectEnabled;
            PatchBytes(effectEnabled);
        }
        Sleep(1);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        CreateThread(nullptr, 0, FovThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    }
    return TRUE;
}

//
// 06-12-2025: update: no random stutters (patch original fov writer address)
// 26-12-2025: update: sleep(1) instead of sleep(20) in hotkey input reading thread, apparently 20 made it unresponsive