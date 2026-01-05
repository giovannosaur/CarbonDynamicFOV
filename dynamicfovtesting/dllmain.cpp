// dllmain.cpp
#include "pch.h"
#include <windows.h>
#include <stdint.h>
#include <cmath>

// =========================
// addresses & game refs
// =========================
void(*Hud_ShowMessage)(char* Message) = (void(*)(char*))0x65B1B0;

int* GameState = (int*)0xA99BBC;        // 6 = gameplay
uint8_t* NisState = (uint8_t*)0x00B4D964;

volatile float* speedAddr = (float*)0x00A8E178;
volatile uint16_t* fovAddr = (uint16_t*)0x00B1D604;

// original fov writer patch
BYTE originalBytes[7] = { 0x66, 0x89, 0x81, 0xE4, 0x00, 0x00, 0x00 };
BYTE nopBytes[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
uintptr_t patchAddr = 0x00492E3A;

// =========================
// config
// =========================
int      cfg_toggleKey = 118;
uint16_t cfg_initial_fov = 15000;
uint16_t cfg_max_fov = 24000;
float    cfg_max_speed = 80.0f;
int      cfg_graph_type = 1;
bool     cfg_showHudMsg = true;
bool     cfg_permanentEnable = false;

// =========================
// runtime state
// =========================
bool userWantsEnabled = false;
bool effectActive = false;
bool lastPatchState = false;

// =========================
// helpers
// =========================
void PatchBytes(bool enable)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)patchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect);

    memcpy((void*)patchAddr, enable ? nopBytes : originalBytes, 7);

    VirtualProtect((LPVOID)patchAddr, 7, oldProtect, &oldProtect);
}

void UpdatePatch(bool shouldEnable)
{
    if (shouldEnable != lastPatchState)
    {
        PatchBytes(shouldEnable);
        lastPatchState = shouldEnable;
    }
}

float ApplyGraph(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (cfg_graph_type)
    {
    case 0: return t;
    case 1: return t * t * t;
    case 2: return 1.0f - powf(1.0f - t, 3.0f);
    default: return t;
    }
}

void LoadConfig()
{
    cfg_toggleKey = GetPrivateProfileIntA("hotkeys", "toggle_fov", 118, ".\\DFconfig.ini");

    cfg_initial_fov = (uint16_t)GetPrivateProfileIntA("settings", "initial_fov", 15000, ".\\DFconfig.ini");
    cfg_max_fov = (uint16_t)GetPrivateProfileIntA("settings", "max_fov", 24000, ".\\DFconfig.ini");
    cfg_graph_type = GetPrivateProfileIntA("settings", "graph_type", 1, ".\\DFconfig.ini");

    cfg_max_speed = (float)GetPrivateProfileIntA("speed", "max_speed", 80, ".\\DFconfig.ini");

    cfg_showHudMsg = GetPrivateProfileIntA("settings", "showHudMessage", 1, ".\\DFconfig.ini") != 0;
    cfg_permanentEnable = GetPrivateProfileIntA("settings", "permanentEnable", 0, ".\\DFconfig.ini") != 0;

    userWantsEnabled = cfg_permanentEnable;
}

// =========================
// main thread
// =========================
DWORD WINAPI MainThread(void*)
{
    LoadConfig();

    while (true)
    {
        bool inGameplay = (*GameState == 6);
        bool inNIS = (*NisState != 0);

        bool gameAllowsFov = inGameplay && !inNIS;

        // hotkey only valid in gameplay
        if (inGameplay && (GetAsyncKeyState(cfg_toggleKey) & 1))
        {
            userWantsEnabled = !userWantsEnabled;

            if (cfg_showHudMsg)
            {
                Hud_ShowMessage(userWantsEnabled
                    ? (char*)"^Dynamic FOV enabled"
                    : (char*)"^Dynamic FOV disabled");
            }
        }

        effectActive = userWantsEnabled && gameAllowsFov;

        UpdatePatch(effectActive);

        if (effectActive)
        {
            float s = *speedAddr;
            if (s < 0) s = 0;
            if (s > cfg_max_speed) s = cfg_max_speed;

            float t = s / cfg_max_speed;
            float eased = ApplyGraph(t);

            float f = cfg_initial_fov +
                (cfg_max_fov - cfg_initial_fov) * eased;

            if (f > cfg_max_fov) f = (float)cfg_max_fov;
            if (f < cfg_initial_fov) f = (float)cfg_initial_fov;

            *fovAddr = (uint16_t)f;
        }

        SwitchToThread();
    }
}

// =========================
// dll entry
// =========================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}


//
// 06-12-2025: update: no random stutters (patch original fov writer address)
// 26-12-2025: update: sleep(1) instead of sleep(20) in hotkey input reading thread, apparently 20 made it unresponsive
// 05-01-2025: update: large code structure change (game state check, permanent enable option, hud string when enable/disable, no more hotkey thread - put it in the loop instead)