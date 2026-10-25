#include "pch.h"
#include "CameraShake.h"
#include "GameAddresses.h"
#include "Config.h"
#include "Utils.h"
#include "PlayerEngine.h"

#include <windows.h>
#include <stdint.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// Camera shake state is updated only by the game's camera-update thread
static LARGE_INTEGER g_speedShakeLastCounter = {};
static bool g_speedShakeHasCounter = false;
static double g_speedShakeNoiseTime = 0.0;
static float g_speedShakeSmoothedIntensity = 0.0f;
// smoothed 0..1 blend to prevent abrupt changes when engine-braking toggles
static float g_speedShakeEngineBrakeBlend = 0.0f;
static DWORD g_speedShakeLastDebugTick = 0;
static bool g_speedShakeHasLogged = false;
static std::FILE* g_speedShakeLogFile = nullptr;

// Hook the final SetCameraMatrix call in CubicCameraMover::Update
static constexpr uintptr_t kFinalCameraSetMatrixCallSite = 0x00492F82;
static constexpr uintptr_t kSetCameraMatrixAddress = 0x004822F0;

struct CameraShakeMatrix4x4
{
    float m[16];
};
static_assert(sizeof(CameraShakeMatrix4x4) == 16 * sizeof(float),
    "Carbon camera matrix must be exactly 4x4 floats");

using SetCameraMatrixFn = void(__thiscall*)(void*, CameraShakeMatrix4x4*, float);

static float GetSpeedShakeDeltaSeconds()
{
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);

    if (!g_speedShakeHasCounter)
    {
        g_speedShakeLastCounter = now;
        g_speedShakeHasCounter = true;
        return 1.0f / 60.0f;
    }

    const double dt = static_cast<double>(
        now.QuadPart - g_speedShakeLastCounter.QuadPart) /
        static_cast<double>(g_qpcFrequency.QuadPart > 0
            ? g_qpcFrequency.QuadPart : 1);
    g_speedShakeLastCounter = now;

    if (!std::isfinite(dt) || dt <= 0.0)
        return 1.0f / 240.0f;
    return static_cast<float>(dt > 0.05 ? 0.05 : dt);
}

static uint32_t HashSpeedShakeNoise(int64_t lattice, uint32_t seed)
{
    uint32_t x = static_cast<uint32_t>(lattice) ^ seed;
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

static float SpeedShakeNoiseValue(int64_t lattice, uint32_t seed)
{
    const uint32_t value = HashSpeedShakeNoise(lattice, seed) & 0x00FFFFFFU;
    return (static_cast<float>(value) / 8388607.5f) - 1.0f;
}
static float SampleSpeedShakeNoise(double t, uint32_t seed)
{
    if (!std::isfinite(t))
        return 0.0f;

    const double floored = std::floor(t);
    if (floored < static_cast<double>((std::numeric_limits<int64_t>::min)()) ||
        floored > static_cast<double>((std::numeric_limits<int64_t>::max)() - 1))
        return 0.0f;

    const int64_t i0 = static_cast<int64_t>(floored);
    const float f = static_cast<float>(t - floored);
    // Cubic smoothstep keeps the noise continuous with a zero slope at each knot
    const float smooth = f * f * (3.0f - 2.0f * f);
    const float a = SpeedShakeNoiseValue(i0, seed);
    const float b = SpeedShakeNoiseValue(i0 + 1, seed);
    return a + (b - a) * smooth;
}

static void ApplyLocalCameraYawPitch(
    CameraShakeMatrix4x4* m,
    float yawRadians,
    float pitchRadians)
{
    if (!m ||
        !std::isfinite(yawRadians) ||
        !std::isfinite(pitchRadians))
        return;

    // D3DX view matrix basis columns: right, up, forward
    const float rx = m->m[0];
    const float ry = m->m[4];
    const float rz = m->m[8];

    const float ux = m->m[1];
    const float uy = m->m[5];
    const float uz = m->m[9];

    const float fx = m->m[2];
    const float fy = m->m[6];
    const float fz = m->m[10];

    const float tx = m->m[12];
    const float ty = m->m[13];
    const float tz = m->m[14];

    if (!std::isfinite(rx) || !std::isfinite(ry) ||
        !std::isfinite(rz) || !std::isfinite(ux) ||
        !std::isfinite(uy) || !std::isfinite(uz) ||
        !std::isfinite(fx) || !std::isfinite(fy) ||
        !std::isfinite(fz) || !std::isfinite(tx) ||
        !std::isfinite(ty) || !std::isfinite(tz))
        return;

    // Recover the camera's world position from the original view matrix
    const float eyeX = -(tx * rx + ty * ux + tz * fx);
    const float eyeY = -(tx * ry + ty * uy + tz * fy);
    const float eyeZ = -(tx * rz + ty * uz + tz * fz);

    const float cy = cosf(yawRadians);
    const float sy = sinf(yawRadians);
    const float cp = cosf(pitchRadians);
    const float sp = sinf(pitchRadians);

    // Local yaw: positive yaw turns the view to the right
    const float nrx = cy * rx - sy * fx;
    const float nry = cy * ry - sy * fy;
    const float nrz = cy * rz - sy * fz;

    const float yfx = sy * rx + cy * fx;
    const float yfy = sy * ry + cy * fy;
    const float yfz = sy * rz + cy * fz;

    // Local pitch: positive pitch looks upward
    const float nux = cp * ux - sp * yfx;
    const float nuy = cp * uy - sp * yfy;
    const float nuz = cp * uz - sp * yfz;

    const float nfx = sp * ux + cp * yfx;
    const float nfy = sp * uy + cp * yfy;
    const float nfz = sp * uz + cp * yfz;

    // write rotated basis back, camera world position stays unchanged
    m->m[0] = nrx;
    m->m[4] = nry;
    m->m[8] = nrz;

    m->m[1] = nux;
    m->m[5] = nuy;
    m->m[9] = nuz;

    m->m[2] = nfx;
    m->m[6] = nfy;
    m->m[10] = nfz;

    // Recalculate view translation from the unchanged world-space eye
    m->m[12] = -(nrx * eyeX + nry * eyeY + nrz * eyeZ);
    m->m[13] = -(nux * eyeX + nuy * eyeY + nuz * eyeZ);
    m->m[14] = -(nfx * eyeX + nfy * eyeY + nfz * eyeZ);
}

static void LogSpeedShakeDebug(float speedKmh, float targetIntensity,
    float smoothedIntensity, float frequencyHz,
    float engineBrakeBlend, float strengthMultiplier)
{
    if (!cfg_speedShakeDebug)
        return;

    const DWORD now = GetTickCount();
    if (g_speedShakeHasLogged && (now - g_speedShakeLastDebugTick) < 1000)
        return;

    g_speedShakeLastDebugTick = now;
    g_speedShakeHasLogged = true;

    char message[320] = {};
    std::snprintf(message, sizeof(message),
        "[CarbonDynamicFOV][SpeedShakeFinalMatrix] kmh=%.1f "
        "min_kmh=%.1f max_kmh=%.1f target=%.2f current=%.2f hz=%.2f "
        "engine_brake_blend=%.2f strength_mul=%.2f\n",
        speedKmh, cfg_speedShakeMinSpeed,
        cfg_speedShakeMaxSpeed, targetIntensity, smoothedIntensity,
        frequencyHz, engineBrakeBlend, strengthMultiplier);

    if (g_speedShakeLogFile)
    {
        std::fprintf(g_speedShakeLogFile,
            "kmh=%.1f min_kmh=%.1f max_kmh=%.1f "
            "target=%.2f current=%.2f hz=%.2f "
            "engine_brake_blend=%.2f strength_mul=%.2f\n",
            speedKmh, cfg_speedShakeMinSpeed,
            cfg_speedShakeMaxSpeed, targetIntensity, smoothedIntensity,
            frequencyHz, engineBrakeBlend, strengthMultiplier);
        std::fflush(g_speedShakeLogFile);
    }
    OutputDebugStringA(message);
}

void OpenSpeedShakeLog()
{
    if (!cfg_speedShakeDebug || g_speedShakeLogFile)
        return;

    if (fopen_s(&g_speedShakeLogFile, "CarbonDynamicFOV_SpeedShake.log", "w") != 0)
        g_speedShakeLogFile = nullptr;
    if (g_speedShakeLogFile)
    {
        std::fprintf(g_speedShakeLogFile,
            "NFS Carbon v1.4 external final-matrix translation shake. "
            "Only view-matrix translation is modified; orientation is preserved. "
            "Speed values are km/h (raw * %.4f, an estimate only).\n",
            kRawSpeedToKmh);
        std::fflush(g_speedShakeLogFile);
    }
}

static void ApplySpeedBasedCameraShake(CameraShakeMatrix4x4* cameraMatrix)
{
    const float dt = GetSpeedShakeDeltaSeconds();

    bool activeContext = cfg_speedShakeEnabled && cameraMatrix != nullptr;

    // This call site is specific to CubicCameraMover, but retain the gameplay,
    // NIS and driving-camera guards as a safety net.
    if (!IsReadableMemory(reinterpret_cast<const void*>(kGameStateAddress),
        sizeof(uint32_t)) || *GameState != 6 ||
        !IsReadableMemory(reinterpret_cast<const void*>(kNisStateAddress),
            sizeof(uint8_t)) || *NisState != 0)
        activeContext = false;

    if (activeContext && cfg_drivingCameraOnly)
    {
        bool isDriving = false;
        if (!QueryIsDrivingCamera(0, isDriving) || !isDriving)
            activeContext = false;
    }

    float speedKmh = 0.0f;
    if (IsReadableMemory(reinterpret_cast<const void*>(kSpeedAddress), sizeof(float)))
    {
        speedKmh = GetPlayerSpeedKmh();
    }
    else
    {
        activeContext = false;
    }

    float targetIntensity = 0.0f;
    if (activeContext && speedKmh >= cfg_speedShakeMinSpeed)
    {
        const float range = cfg_speedShakeMaxSpeed - cfg_speedShakeMinSpeed;
        targetIntensity = (range > 0.0001f)
            ? ClampFloat((speedKmh - cfg_speedShakeMinSpeed) / range, 0.0f, 1.0f)
            : 1.0f;
    }

    g_speedShakeSmoothedIntensity = SmoothTowards(
        g_speedShakeSmoothedIntensity, targetIntensity,
        cfg_speedShakeSmoothing, dt);

    const float intensity = ClampFloat(g_speedShakeSmoothedIntensity, 0.0f, 1.0f);

    // Query the same validated IEngineRacer engine-braking signal used by the
    // Dynamic FOV effects. This remains independent of the Dynamic FOV toggle.
    bool engineBraking = false;
    bool engineBrakeStateKnown = false;
    if (activeContext && cfg_speedShakeEngineBrakeModifierEnabled)
        engineBrakeStateKnown = QueryEngineBrakeStateForShake(engineBraking);

    // If the engine lookup is temporarily unavailable, ease back toward normal
    // shake rather than leaving the braking modifier stuck on indefinitely.
    const float engineBrakeTarget =
        (cfg_speedShakeEngineBrakeModifierEnabled &&
            engineBrakeStateKnown && engineBraking) ? 1.0f : 0.0f;

    g_speedShakeEngineBrakeBlend = SmoothTowards(
        g_speedShakeEngineBrakeBlend,
        engineBrakeTarget,
        cfg_speedShakeEngineBrakeSmoothing,
        dt);

    const float engineBrakeBlend = ClampFloat(
        g_speedShakeEngineBrakeBlend, 0.0f, 1.0f);

    // blend=0 -> normal shake; blend=1 -> configured engine-brake modifiers.
    const float strengthMultiplier = 1.0f +
        (cfg_speedShakeEngineBrakeStrengthMultiplier - 1.0f) *
        engineBrakeBlend;
    const float frequencyMultiplier = 1.0f +
        (cfg_speedShakeEngineBrakeFrequencyMultiplier - 1.0f) *
        engineBrakeBlend;

    const float baseFrequencyHz = cfg_speedShakeMinFrequency +
        (cfg_speedShakeMaxFrequency - cfg_speedShakeMinFrequency) * intensity;
    const float frequencyHz = baseFrequencyHz * frequencyMultiplier;

    LogSpeedShakeDebug(speedKmh, targetIntensity, intensity, frequencyHz,
        engineBrakeBlend, strengthMultiplier);

    if (!activeContext || intensity < 0.001f ||
        !IsReadableMemory(cameraMatrix, sizeof(CameraShakeMatrix4x4)))
        return;

    // Reject obviously invalid translation values before modifying the final view matrix.
    if (!std::isfinite(cameraMatrix->m[12]) ||
        !std::isfinite(cameraMatrix->m[13]) ||
        !std::isfinite(cameraMatrix->m[14]))
        return;

    // Advance noise in noise-space at the speed-controlled rate. This is fully
    // external to Carbon's CAMERA_NOISE/VLT terrain path.
    g_speedShakeNoiseTime += static_cast<double>(dt) * frequencyHz;
    if (!std::isfinite(g_speedShakeNoiseTime))
        g_speedShakeNoiseTime = 0.0;

    const double t = g_speedShakeNoiseTime;

    const float nx =
        0.75f * SampleSpeedShakeNoise(t, 0x1A2B3C4DU) +
        0.25f * SampleSpeedShakeNoise(
            t * 2.13 + 17.4, 0x55AA10EFU);

    const float ny =
        0.75f * SampleSpeedShakeNoise(
            t * 0.91 + 29.7, 0xC001D00DU) +
        0.25f * SampleSpeedShakeNoise(
            t * 1.83 + 3.1, 0xA5A5F00DU);

    constexpr float kDegreesToRadians =
        0.01745329251994329577f;

    const float effectiveIntensity = intensity * strengthMultiplier;

    const float yawRadians =
        nx * cfg_speedShakeYawDegrees *
        kDegreesToRadians * effectiveIntensity;

    const float pitchRadians =
        ny * cfg_speedShakePitchDegrees *
        kDegreesToRadians * effectiveIntensity;

    ApplyLocalCameraYawPitch(
        cameraMatrix, yawRadians, pitchRadians);
}

static void __fastcall CameraSetMatrixCallHook(
    void* cameraObject, void*, CameraShakeMatrix4x4* cameraMatrix, float dt)
{
    ApplySpeedBasedCameraShake(cameraMatrix);

    const auto original = reinterpret_cast<SetCameraMatrixFn>(kSetCameraMatrixAddress);
    original(cameraObject, cameraMatrix, dt);
}

bool InstallFinalCameraSetMatrixCallHook()
{
    if (!IsReadableMemory(reinterpret_cast<const void*>(kFinalCameraSetMatrixCallSite), 5) ||
        !IsExecutableAddress(kSetCameraMatrixAddress))
        return false;

    BYTE current[5] = {};
    std::memcpy(current, reinterpret_cast<const void*>(kFinalCameraSetMatrixCallSite), 5);
    if (current[0] != 0xE8)
    {
        OutputDebugStringA("[CarbonDynamicFOV][SpeedShakeFinalMatrix] hook skipped: final call-site is not CALL rel32\n");
        return false;
    }

    int32_t existingDisplacement = 0;
    std::memcpy(&existingDisplacement, current + 1, sizeof(existingDisplacement));
    const uintptr_t existingTarget = static_cast<uintptr_t>(
        static_cast<intptr_t>(kFinalCameraSetMatrixCallSite + 5) + existingDisplacement);
    const uintptr_t hookTarget = reinterpret_cast<uintptr_t>(&CameraSetMatrixCallHook);

    if (existingTarget == hookTarget)
        return true;
    if (existingTarget != kSetCameraMatrixAddress)
    {
        OutputDebugStringA("[CarbonDynamicFOV][SpeedShakeFinalMatrix] hook skipped: unexpected target at 0x00492F82; refusing to overwrite another mod\n");
        return false;
    }

    BYTE patch[5] = { 0xE8, 0, 0, 0, 0 };
    const uintptr_t afterCall = kFinalCameraSetMatrixCallSite + 5;
    const uint32_t newDisplacement = static_cast<uint32_t>(hookTarget - afterCall);
    std::memcpy(patch + 1, &newDisplacement, sizeof(newDisplacement));

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(kFinalCameraSetMatrixCallSite),
        sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    std::memcpy(reinterpret_cast<void*>(kFinalCameraSetMatrixCallSite), patch, sizeof(patch));
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(kFinalCameraSetMatrixCallSite),
        sizeof(patch), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(),
        reinterpret_cast<const void*>(kFinalCameraSetMatrixCallSite), sizeof(patch));

    OutputDebugStringA("[CarbonDynamicFOV][SpeedShakeFinalMatrix] hook installed at final camera call 0x00492F82\n");
    return true;
}
