#include "pch.h"
#include "DynamicFov.h"
#include "GameAddresses.h"
#include "Config.h"
#include "Utils.h"
#include "PlayerEngine.h"
#include "CameraShake.h"

#include <windows.h>
#include <stdint.h>
#include <cmath>
#include <cstring>

// Original FOV writer patch. Restoring these bytes resumes the game's own write.
static BYTE originalBytes[7] = { 0x66, 0x89, 0x81, 0xE4, 0x00, 0x00, 0x00 };
static BYTE nopBytes[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
static constexpr uintptr_t patchAddr = 0x00492E3A;

// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------
bool userWantsEnabled = false;
static bool lastPatchState = false;

static LARGE_INTEGER g_lastCounter = {};
static bool g_hasCounter = false;

static float g_currentFov = 15000.0f;
static float g_previousSpeed = 0.0f;
static float g_filteredAcceleration = 0.0f;
static bool g_hasPreviousSpeed = false;

static bool g_previousShiftActive = false;
static float g_shiftPulseRemaining = 0.0f;
static int g_lastEngineLookupStatus = -1; // -1 unknown, 0 failed, 1 valid
static bool g_previousSpeedbreakerActive = false;
static bool g_speedbreakerTransitioningOut = false;

// Use QPC for this worker thread rather than repeatedly consuming the same game frame delta when the worker loops faster than the game simulation
static float GetElapsedSeconds()
{
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);

    if (!g_hasCounter)
    {
        g_lastCounter = now;
        g_hasCounter = true;
        return 1.0f / 60.0f;
    }

    const double elapsed =
        static_cast<double>(now.QuadPart - g_lastCounter.QuadPart) /
        static_cast<double>(g_qpcFrequency.QuadPart);
    g_lastCounter = now;

    if (!std::isfinite(elapsed) || elapsed <= 0.0)
        return 1.0f / 240.0f;

    // Clamp debugger pauses / scheduling hitches to prevent giant integration steps
    return static_cast<float>(elapsed > 0.05 ? 0.05 : elapsed);
}

// -----------------------------------------------------------------------------
// Patch and easing
// -----------------------------------------------------------------------------
static bool PatchBytes(bool enable)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(patchAddr), 7,
        PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    std::memcpy(reinterpret_cast<void*>(patchAddr),
        enable ? nopBytes : originalBytes, 7);

    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(patchAddr), 7,
        oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(),
        reinterpret_cast<const void*>(patchAddr), 7);
    return true;
}

static bool UpdatePatch(bool shouldEnable)
{
    if (shouldEnable != lastPatchState)
    {
        if (!PatchBytes(shouldEnable))
            return false;
        lastPatchState = shouldEnable;
    }
    return lastPatchState == shouldEnable;
}

static float EaseInOutExpo(float x)
{
    if (x == 0.0f) return 0.0f;
    if (x == 1.0f) return 1.0f;

    if (x < 0.5f)
        return powf(2.0f, 20.0f * x - 10.0f) / 2.0f;
    return (2.0f - powf(2.0f, -20.0f * x + 10.0f)) / 2.0f;
}

static float EaseInQuart(float x)
{
    return x * x * x * x;
}

static float EaseInOutCirc(float x)
{
    if (x < 0.5f)
        return (1.0f - sqrtf(1.0f - powf(2.0f * x, 2.0f))) / 2.0f;
    return (sqrtf(1.0f - powf(-2.0f * x + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

static float ApplyGraph(float t)
{
    t = ClampFloat(t, 0.0f, 1.0f);

    switch (cfg_graph_type)
    {
    case 0: return t;                                   // linear
    case 1: return t * t * t;                           // cubic in
    case 2: return 1.0f - powf(1.0f - t, 3.0f);         // cubic out
    case 3: return EaseInOutExpo(t);
    case 4: return EaseInQuart(t);
    case 5: return EaseInOutCirc(t);
    default: return t;
    }
}

// -----------------------------------------------------------------------------
// Main worker
// -----------------------------------------------------------------------------
DWORD WINAPI MainThread(void*)
{
    QueryPerformanceFrequency(&g_qpcFrequency);
    if (g_qpcFrequency.QuadPart <= 0)
        g_qpcFrequency.QuadPart = 1;

    LoadConfig();
    g_currentFov = static_cast<float>(cfg_initial_fov);
    OpenSpeedShakeLog();

    // Install the external translation-only shake at the final camera-matrix call
    if (cfg_speedShakeEnabled)
        InstallFinalCameraSetMatrixCallHook();

    while (true)
    {
        const float dt = GetElapsedSeconds();

        if (!IsReadableMemory(reinterpret_cast<const void*>(kGameStateAddress),
            sizeof(uint32_t)) ||
            !IsReadableMemory(reinterpret_cast<const void*>(kNisStateAddress),
                sizeof(uint8_t)))
        {
            Sleep(10);
            continue;
        }

        const bool inGameplay = (*GameState == 6);
        const bool inNIS = (*NisState != 0);

        if (inGameplay && (GetAsyncKeyState(cfg_toggleKey) & 1))
        {
            userWantsEnabled = !userWantsEnabled;
            if (cfg_showHudMsg && IsExecutableAddress(kHudShowMessageAddress))
            {
                Hud_ShowMessage(userWantsEnabled
                    ? const_cast<char*>("^Dynamic FOV enabled")
                    : const_cast<char*>("^Dynamic FOV disabled"));
            }
        }

        bool drivingCameraActive = true;
        bool drivingCameraQuerySucceeded = true;

        if (cfg_drivingCameraOnly)
        {
            drivingCameraQuerySucceeded = false;
            drivingCameraActive = false;

            // Query only during gameplay, while not in a NIS/cinematic sequence
            if (inGameplay && !inNIS)
                drivingCameraQuerySucceeded =
                QueryIsDrivingCamera(0, drivingCameraActive);

            if (!drivingCameraQuerySucceeded)
                drivingCameraActive = false;
        }

        const bool requestedActive =
            userWantsEnabled && inGameplay && !inNIS && drivingCameraActive;
        const bool patchReady = UpdatePatch(requestedActive);
        const bool effectActive = requestedActive && patchReady;

        if (effectActive &&
            IsReadableMemory(reinterpret_cast<const void*>(kSpeedAddress),
                sizeof(float)) &&
            IsReadableMemory(reinterpret_cast<const void*>(kFovAddress),
                sizeof(uint16_t)))
        {
            const float speedKmh = GetPlayerSpeedKmh();

            const float speed = ClampFloat(speedKmh, 0.0f, cfg_max_speed);
            const float speedT = speed / cfg_max_speed;
            const float eased = ApplyGraph(speedT);

            // Reserve headroom for acceleration, NOS, and shifting effects
            const float baseMaxFov =
                static_cast<float>(cfg_max_fov) - cfg_effect_headroom;
            const float baseFov = static_cast<float>(cfg_initial_fov) +
                (baseMaxFov - static_cast<float>(cfg_initial_fov)) * eased;

            float accelerationKick = 0.0f;
            if (cfg_accel_enabled)
            {
                if (g_hasPreviousSpeed && dt > 0.0001f)
                {
                    const float acceleration =
                        (speedKmh - g_previousSpeed) / dt;
                    const float normalizedAcceleration = ClampFloat(
                        acceleration / cfg_accel_reference, 0.0f, 1.0f);
                    const float accelTarget =
                        normalizedAcceleration * cfg_accel_gain;

                    g_filteredAcceleration = SmoothTowards(
                        g_filteredAcceleration, accelTarget,
                        cfg_accel_sensitivity, dt);
                    accelerationKick = g_filteredAcceleration;
                }
                else
                {
                    g_filteredAcceleration = 0.0f;
                }
            }
            else
            {
                g_filteredAcceleration = 0.0f;
            }

            g_previousSpeed = speedKmh;
            g_hasPreviousSpeed = true;

            // Resolve the current player's engine and validate all interface vtables
            PlayerEngineContext playerEngine = {};
            EngineSignals engineSignals = {};
            const bool engineLookupSucceeded =
                TryGetPlayerEngineContext(playerEngine);
            if (engineLookupSucceeded)
                engineSignals = ReadEngineSignals(playerEngine);

            // Optional one-time status messages help distinguish a failed pointer lookup from effects that are merely inactive in the current frame.
            const int lookupStatus = engineSignals.valid ? 1 : 0;
            if (cfg_debugEngineLookup &&
                lookupStatus != g_lastEngineLookupStatus &&
                IsExecutableAddress(kHudShowMessageAddress))
            {
                Hud_ShowMessage(lookupStatus
                    ? const_cast<char*>("^Dynamic FOV: player engine found")
                    : const_cast<char*>("^Dynamic FOV: engine lookup failed; engine effects skipped"));
            }
            g_lastEngineLookupStatus = lookupStatus;

            float nosKick = 0.0f;
            float shiftKick = 0.0f;
            float engineBrakeAdjustment = 0.0f;

            if (engineSignals.valid)
            {
                if (cfg_nos_enabled && engineSignals.nosEngaged)
                    nosKick = cfg_nos_gain;

                // Trigger a pulse only when a gear-change starts at/above the
                // configured in-game speed threshold. Keep tracking shift state
                // below the threshold so crossing it mid-shift does not trigger late.
                if (cfg_shift_enabled &&
                    speed >= cfg_shift_min_speed &&
                    engineSignals.shifting && !g_previousShiftActive)
                {
                    g_shiftPulseRemaining = cfg_shift_duration;
                }
                g_previousShiftActive = engineSignals.shifting;

                // A pulse is not allowed to continue below the speed threshold.
                // This avoids low-speed/near-stop shifts being affected by a pulse
                // that was triggered just before slowing down.
                if (speed < cfg_shift_min_speed)
                    g_shiftPulseRemaining = 0.0f;

                if (cfg_shift_enabled &&
                    speed >= cfg_shift_min_speed &&
                    g_shiftPulseRemaining > 0.0f)
                {
                    const float phase = ClampFloat(
                        g_shiftPulseRemaining / cfg_shift_duration, 0.0f, 1.0f);
                    // Quadratic falloff
                    shiftKick = cfg_shift_gain * phase * phase;
                    g_shiftPulseRemaining =
                        (g_shiftPulseRemaining > dt)
                        ? (g_shiftPulseRemaining - dt) : 0.0f;
                }

                if (cfg_engine_braking_enabled &&
                    engineSignals.engineBraking)
                {
                    engineBrakeAdjustment = cfg_engine_braking_gain;
                }
            }
            else
            {
                g_previousShiftActive = false;
                g_shiftPulseRemaining = 0.0f;
            }

            const bool speedbreakerActive =
                cfg_speedbreakerEnabled && IsSpeedbreakerActive();

            // Entering speedbreaker starts a dedicated zoom-in transition.
            // On exit, keep the slower exit rate until FOV nearly reaches the
            // current normal target, including if vehicle speed changes during exit.
            if (speedbreakerActive)
            {
                g_speedbreakerTransitioningOut = false;
            }
            else if (g_previousSpeedbreakerActive)
            {
                g_speedbreakerTransitioningOut = true;
            }

            // Speedbreaker has priority over speed and other transient effects.
            // Only the shift pulse may use the lower-than-initial headroom.
            const float rawNormalLowerFov =
                static_cast<float>(cfg_initial_fov) - cfg_effect_headroom_below;
            const float normalLowerFov =
                (rawNormalLowerFov > 0.0f) ? rawNormalLowerFov : 0.0f;

            // First resolve the ordinary driving target, including engine braking,
            // but keep it at or above initial_fov. This prevents engine braking
            // from pulling idle/low-speed FOV into the shift-only lower headroom.
            const float normalTargetWithoutShift = ClampFloat(
                baseFov + accelerationKick + nosKick + engineBrakeAdjustment,
                static_cast<float>(cfg_initial_fov),
                static_cast<float>(cfg_max_fov));

            // Apply shifting after the ordinary target is clamped, so a negative
            // shift pulse can dip below initial_fov by up to effect_headroom_below.
            const float targetFov = speedbreakerActive
                ? static_cast<float>(cfg_speedbreakerFov)
                : ClampFloat(
                    normalTargetWithoutShift + shiftKick,
                    normalLowerFov,
                    static_cast<float>(cfg_max_fov));

            float responseRate = 1.0f;
            if (speedbreakerActive)
            {
                // Direct per-second response rate; deliberately not multiplied
                // by the global fov_sensitivity, so this setting is predictable.
                responseRate = cfg_speedbreakerEnterSensitivity;
            }
            else if (g_speedbreakerTransitioningOut)
            {
                responseRate = cfg_speedbreakerExitSensitivity;
                if (fabsf(targetFov - g_currentFov) <= 25.0f)
                    g_speedbreakerTransitioningOut = false;
            }
            else
            {
                const float sensitivityScale = cfg_fov_sensitivity / 4.0f;
                responseRate = ((targetFov > g_currentFov)
                    ? cfg_expand_sensitivity
                    : cfg_recover_sensitivity) * sensitivityScale;
            }

            g_currentFov = SmoothTowards(
                g_currentFov, targetFov, responseRate, dt);

            // Keep room below initial_fov for negative effects and smooth recovery.
            // If speedbreaker targets an even lower value, permit that too.
            const float lowestAllowedFov =
                (cfg_speedbreakerEnabled &&
                    static_cast<float>(cfg_speedbreakerFov) < normalLowerFov)
                ? static_cast<float>(cfg_speedbreakerFov)
                : normalLowerFov;
            // Do not let an above-initial speedbreaker target cap the normal
            // driving FOV after speedbreaker exits.
            const float highestAllowedFov = static_cast<float>(cfg_max_fov);

            g_currentFov = ClampFloat(
                g_currentFov, lowestAllowedFov, highestAllowedFov);

            g_previousSpeedbreakerActive = speedbreakerActive;
            *fovAddr = static_cast<uint16_t>(g_currentFov);
        }
        else
        {
            g_hasPreviousSpeed = false;
            g_filteredAcceleration = 0.0f;
            g_previousShiftActive = false;
            g_shiftPulseRemaining = 0.0f;
            g_previousSpeedbreakerActive = false;
            g_speedbreakerTransitioningOut = false;
            g_currentFov = static_cast<float>(cfg_initial_fov);
        }

        Sleep(1);
    }
}
